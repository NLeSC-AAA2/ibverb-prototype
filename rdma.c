/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */

#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "rdma.h"

#define IB_PORT 1

static uint32_t max_mtu;
static int page_size;
static int send_flags = IBV_SEND_SIGNALED;

static struct ibv_context *context;
static struct ibv_pd      *protection_domain;
struct ibv_cq             *completion_queue;
static struct ibv_qp      *queue_pair;

static void *header_buf = NULL;
static void *buf = NULL;
static struct ibv_mr *mr = NULL;
static struct ibv_mr *header_mr = NULL;
static struct ibv_ah *ah = NULL;

static void internal_rdma_cleanup();

static int allocate_buf(void **buffer, struct ibv_mr **memory_region, size_t size)
{
    if (posix_memalign(buffer, page_size, size)) {
        fprintf(stderr, "Couldn't allocate work buf.\n");
        return 1;
    }

    memset(*buffer, 0x7b, size);

    *memory_region = ibv_reg_mr(protection_domain, *buffer, size, IBV_ACCESS_LOCAL_WRITE);
    if (!*memory_region) {
        fprintf(stderr, "Couldn't register memory region.\n");
        goto clean_buf;
    }

    return 0;

  clean_buf:
    free(*buffer);
    *buffer = NULL;
    return 1;
}

static void rdma_init(char *dev_name, int completion_queue_size)
{
    page_size = sysconf(_SC_PAGESIZE);

    int num_devs;
    struct ibv_device *ib_dev = NULL;
    struct ibv_device **dev_list = ibv_get_device_list(&num_devs);
    if (!dev_list) {
        fprintf(stderr, "No IB devices found.\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; dev_list[i]; ++i) {
        if (!strcmp(ibv_get_device_name(dev_list[i]), dev_name)) {
            ib_dev = dev_list[i];
            break;
        }
    }

    if (!ib_dev) {
        fprintf(stderr, "No device with name: %s\n", dev_name);
        ibv_free_device_list(dev_list);
        exit(EXIT_FAILURE);
    }

    context = ibv_open_device(ib_dev);
    ibv_free_device_list(dev_list);
    if (!context) {
        fprintf(stderr, "Failed to open device: %s\n", ibv_get_device_name(ib_dev));
        goto clean_context;
    }

    struct ibv_port_attr port_info;
    if (ibv_query_port(context, IB_PORT, &port_info)) {
        fprintf(stderr, "Failed to query port info.\n");
        goto clean_device;
    }

    max_mtu = 1 << (port_info.active_mtu + 7);

    protection_domain = ibv_alloc_pd(context);
    if (!protection_domain) {
        fprintf(stderr, "Failed to allocate protection domain.\n");
        goto clean_context;
    }

    completion_queue = ibv_create_cq(context, completion_queue_size, NULL, NULL, 0);
    if (!completion_queue) {
        fprintf(stderr, "Failed to create completion queue.\n");
        goto clean_protection_domain;
    }

    struct ibv_qp_init_attr init_attr = {
        .send_cq = completion_queue,
        .recv_cq = completion_queue,
        .cap     = {
            .max_send_wr  = 1,
            .max_recv_wr  = completion_queue_size,
            .max_send_sge = 1,
            .max_recv_sge = 2
        },
        .qp_type = IBV_QPT_UD,
    };

    queue_pair = ibv_create_qp(protection_domain, &init_attr);
    if (!queue_pair)  {
        fprintf(stderr, "Couldn't create queue pair.\n");
        goto clean_completion_queue;
    }

    struct ibv_qp_attr attr;
    ibv_query_qp(queue_pair, &attr, IBV_QP_CAP, &init_attr);
    if (init_attr.cap.max_inline_data >= max_mtu) {
        send_flags |= IBV_SEND_INLINE;
    }

    attr.qp_state   = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num   = IB_PORT;
    attr.qkey       = 0x11111111;

    if (ibv_modify_qp(queue_pair, &attr, IBV_QP_STATE|IBV_QP_PKEY_INDEX|IBV_QP_PORT|IBV_QP_QKEY)) {
        fprintf(stderr, "Failed to initialise queue pair.\n");
        goto clean_queue_pair;
    }

    attr.qp_state = IBV_QPS_RTR;
    if (ibv_modify_qp(queue_pair, &attr, IBV_QP_STATE)) {
        fprintf(stderr, "Failed to make queue pair ready to receive.\n");
        goto clean_queue_pair;
    }

    return;

  clean_queue_pair:
    ibv_destroy_qp(queue_pair);

  clean_completion_queue:
    ibv_destroy_cq(completion_queue);

  clean_protection_domain:
    ibv_dealloc_pd(protection_domain);

  clean_device:
    ibv_close_device(context);

  clean_context:
    fprintf(stderr, "Failed to initialise RDMA context.\n");
    exit(EXIT_FAILURE);
}

void rdma_init_server(char *dev_name, int completion_queue_size)
{
    union ibv_gid gid;
    char gid_string[33];

    rdma_init(dev_name, completion_queue_size);

    if (allocate_buf(&buf, &mr, MSG_SIZE)) internal_rdma_cleanup();
    if (allocate_buf(&header_buf, &header_mr, 40)) rdma_cleanup();

    if (ibv_query_gid(context, IB_PORT, 0, &gid)) {
        fprintf(stderr, "Could not get local gid for gid index 0\n");
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    struct ibv_port_attr port_attr;
    if (ibv_query_port(context, IB_PORT, &port_attr)) {
        fprintf(stderr, "Couldn't get port info\n");
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    if (!inet_ntop(AF_INET6, &gid, gid_string, sizeof gid_string)) {
        fprintf(stderr, "Couldn't get global id\n");
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "LID: %d\nQPN: %d\nGID: %s\n", port_attr.lid, queue_pair->qp_num, gid_string);
}

void
rdma_init_client
(char *dev_name, int completion_queue_size, int lid, union ibv_gid gid)
{
    rdma_init(dev_name, completion_queue_size);

    if (allocate_buf(&buf, &mr, MSG_SIZE)) internal_rdma_cleanup();

    struct ibv_qp_attr attr;
    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn   = 0;

    if (ibv_modify_qp(queue_pair, &attr, IBV_QP_STATE|IBV_QP_SQ_PSN)) {
        fprintf(stderr, "Failed to make queue pair ready to send.\n");
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    struct ibv_ah_attr ah_attr;
    memset(&ah_attr, 0, sizeof(ah_attr));
    ah_attr.dlid          = lid;
    ah_attr.sl            = 0; // ??
    ah_attr.src_path_bits = 0;
    ah_attr.port_num      = IB_PORT;
    ah_attr.is_global = 1;
    ah_attr.grh.hop_limit = 1;
    ah_attr.grh.dgid = gid;
    ah_attr.grh.sgid_index = 0; // ??

    errno = 0;
    ah = ibv_create_ah(protection_domain, &ah_attr);
    if (!ah) {
        char *msg = errno != 0 ? strerror(errno) : "";
        fprintf(stderr, "Failed to create AH: %s\n", msg);
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }
}

void rdma_cleanup()
{
    if (ibv_dereg_mr(mr)) {
        fprintf(stderr, "Couldn't destroy memory region.\n");
        exit(EXIT_FAILURE);
    }

    free(buf);

    if (header_mr) {
        if (ibv_dereg_mr(header_mr)) {
            fprintf(stderr, "Couldn't destroy memory region.\n");
            exit(EXIT_FAILURE);
        }
    }

    if (header_buf) free(header_buf);

    internal_rdma_cleanup();
}

static void internal_rdma_cleanup()
{
    if (ah) {
        if (ibv_destroy_ah(ah)) {
            fprintf(stderr, "Couldn't destroy AH.\n");
            exit(EXIT_FAILURE);
        }
    }

    if (ibv_destroy_qp(queue_pair)) {
        fprintf(stderr, "Couldn't destroy queue pair.\n");
        exit(EXIT_FAILURE);
    }

    if (ibv_destroy_cq(completion_queue)) {
        fprintf(stderr, "Couldn't destroy completion queue.\n");
        exit(EXIT_FAILURE);
    }

    if (ibv_dealloc_pd(protection_domain)) {
        fprintf(stderr, "Couldn't deallocate protection domain.\n");
        exit(EXIT_FAILURE);
    }

    if (ibv_close_device(context)) {
        fprintf(stderr, "Couldn't release context\n");
        exit(EXIT_FAILURE);
    }
}

int post_sends(uint32_t qpn, int n)
{
    struct ibv_sge list = {
        .addr   = (uintptr_t) buf,
        .length = MSG_SIZE,
        .lkey   = mr->lkey
    };
    struct ibv_send_wr wr = {
        .wr_id      = 0,
        .sg_list    = &list,
        .num_sge    = 1,
        .opcode     = IBV_WR_SEND,
        .send_flags = send_flags,
        .wr         = {
                .ud = {
                    .ah          = ah,
                    .remote_qpn  = qpn,
                    .remote_qkey = 0x11111111
                    }
        }
    };
    struct ibv_send_wr *bad_wr;

    int i;
    for (i = 0; i < n; ++i) {
        if (ibv_post_send(queue_pair, &wr, &bad_wr))
            break;
    }

    return i;
}

int post_recvs(int n)
{
    struct ibv_sge list[2];

    list[0].addr = (uintptr_t) header_buf;
    list[0].length = 40;
    list[0].lkey = header_mr->lkey;

    list[1].addr = (uintptr_t) buf;
    list[1].length = MSG_SIZE;
    list[1].lkey = mr->lkey;

    struct ibv_recv_wr wr = {
        .wr_id = 0,
        .next = NULL,
        .sg_list = list,
        .num_sge = 2
    };

    struct ibv_recv_wr *bad_wr;

    int i;
    for (i = 0; i < n; ++i) {
        int result = ibv_post_recv(queue_pair, &wr, &bad_wr);
        if (result) {
            fprintf(stderr, "post receive #%d failed (%d) with errno: %d\n", i, result, errno);

            break;
        }
    }

    return i;
}
