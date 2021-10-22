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
#include "raw_packet.h"

#define IB_PORT 1

static uint32_t max_mtu;
static int page_size, queue_size;
static int send_flags = IBV_SEND_SIGNALED;

static struct ibv_context *context;
static struct ibv_pd      *protection_domain;
struct ibv_cq             *completion_queue;
static struct ibv_qp      *queue_pair;

static void *header_buffer = NULL;
static void *buffer = NULL;

static struct ibv_sge *scatter_gather = NULL;
static struct ibv_recv_wr *recv_requests = NULL;
static struct ibv_send_wr *send_requests = NULL;

static struct ibv_mr *memory_region = NULL;
static struct ibv_mr *header_memory_region = NULL;
static struct ibv_ah *ah = NULL;

static void internal_rdma_cleanup();

// Allocate a page-alligned buffer and corresponding ibverbs memory region
static int allocate_buf(void **buf, struct ibv_mr **mr, size_t size)
{
    if (posix_memalign(buf, page_size, size)) {
        fprintf(stderr, "Couldn't allocate work buffer.\n");
        return 1;
    }

    memset(*buf, 0, size);

    *mr = ibv_reg_mr(protection_domain, *buf, size, IBV_ACCESS_LOCAL_WRITE);
    if (!*mr) {
        fprintf(stderr, "Couldn't register memory region.\n");
        goto clean_buf;
    }

    return 0;

  clean_buf:
    free(*buf);
    *buf= NULL;
    return 1;
}

// Common ibverbs initialisation shared by the client and server:
//
//   - Check for and open the specified IB device
//   - Create an ibverbs context for the device
//   - Find the IB_PORT to use
//   - Create a protection domain
//   - Create a completion queue
//   - Create Unreliable Datagram queue pair for the completion queue
//   - Configure the queue pair to use the found IB_PORT and transition it to
//     the RTR (Ready-to-Receive) state
static void rdma_init(char *dev_name, int completion_queue_size)
{
    page_size = sysconf(_SC_PAGESIZE);
    queue_size = completion_queue_size;

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
            .max_send_wr  = completion_queue_size,
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

// Server specific ibverbs initialisation. The result value is an array of
// "struct recv_buffer", we allocate one entry per (potential) completion queue
// element. These struct hold offsets into the header and data buffers used to
// receive ibverbs datagrams, splitting these buffers into 1 entry per incoming
// ibverbs datagram.
//
// Initialisation steps:
//   - Call the shared ibverbs initialisation
//   - Allocate a data buffer big enough to store the payload for a number of
//     messages equal to the completion queue size
//   - Allocate a header buffer big enough to store the header information for
//     a number of messages equal to the completion queue size
//   - Allocate a recv_buffer array, contains offsets into data and header
//     buffers to easily index the data "per datagram"
//   - Allocate an array for the sge (scatter-gather) configurations for each
//     of the recv_buffer entries
//   - Allocate an array of Receive Requests for each of the recv_buffer
//     entries
//   - Finally query and report the Local ID, queue pair number, and global ID
//     on stderr
struct recv_buffer *
rdma_init_server(char *dev_name, int completion_queue_size)
{
    struct recv_buffer *result = NULL;
    union ibv_gid gid;
    char gid_string[33];

    rdma_init(dev_name, completion_queue_size);

    if (allocate_buf(&buffer, &memory_region, completion_queue_size * MSG_SIZE)) {
        internal_rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    if (allocate_buf(&header_buffer, &header_memory_region, completion_queue_size * 40)) {
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    result = malloc(completion_queue_size * (sizeof *result));
    if (!result) {
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    scatter_gather = malloc(completion_queue_size * 2 * (sizeof *scatter_gather));
    if (!scatter_gather) {
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    recv_requests = malloc(completion_queue_size * (sizeof *recv_requests));
    if (!recv_requests) {
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    struct ib_grh *header_buffers = header_buffer;
    char *data_buffers = buffer;
    for (int i = 0; i < completion_queue_size; i++) {
        result[i].header_buffer = &header_buffers[i];
        result[i].data_buffer = &data_buffers[i * MSG_SIZE];

        scatter_gather[2 * i].addr = (uintptr_t) result[i].header_buffer;
        scatter_gather[2 * i].length = 40;
        scatter_gather[2 * i].lkey = header_memory_region->lkey;

        scatter_gather[(2 * i) + 1].addr = (uintptr_t) result[i].data_buffer;
        scatter_gather[(2 * i) + 1].length = MSG_SIZE;
        scatter_gather[(2 * i) + 1].lkey = memory_region->lkey;

        recv_requests[i].wr_id = i;
        if (i == completion_queue_size - 1) {
            recv_requests[i].next = &recv_requests[0];
        } else {
            recv_requests[i].next = &recv_requests[i+1];
        }
        recv_requests[i].sg_list = &scatter_gather[2*i];
        recv_requests[i].num_sge = 2;
    }

    if (ibv_query_gid(context, IB_PORT, 0, &gid)) {
        fprintf(stderr, "Could not get local gid for gid index 0\n");
        free(result);
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    struct ibv_port_attr port_attr;
    if (ibv_query_port(context, IB_PORT, &port_attr)) {
        fprintf(stderr, "Couldn't get port info\n");
        free(result);
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    if (!inet_ntop(AF_INET6, &gid, gid_string, sizeof gid_string)) {
        fprintf(stderr, "Couldn't get global id\n");
        free(result);
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "LID: %d\nQPN: %d\nGID: %s\n", port_attr.lid, queue_pair->qp_num, gid_string);

    return result;
}

// Client specific ibverbs initialisation. The result value is an array of
// "struct send_buffer", we allocate one entry per (potential) completion queue
// element. These struct hold offsets into the data buffer used to
// receive ibverbs datagrams, splitting it into 1 entry per outgoing ibverbs
// datagram.
//
// Initialisation steps:
//   - Call the shared ibverbs initialisation
//   - Allocate a data buffer big enough to store the payload for a number of
//     messages equal to the completion queue size
//   - Allocate a send_buffer array, contains an offsets into data buffer to
//     easily index the data "per datagram"
//   - Allocate an array for the sge (scatter-gather) configurations for each
//     of the send_buffer entries
//   - Allocate an array of Send Requests for each of the send_buffer entries
//   - Transition the queue pair from RTR (Ready-to-Receive) to RTS
//     (Ready-to-Send)
//   - Create an Address Handle to address for the server using its local ID,
//     global ID, and queue pair number
struct send_buffer*
rdma_init_client
( char *dev_name
, int completion_queue_size
, int lid
, union ibv_gid gid
, uint32_t qpn
)
{
    struct send_buffer *result = NULL;
    rdma_init(dev_name, completion_queue_size);

    if (allocate_buf(&buffer, &memory_region, completion_queue_size * MSG_SIZE)) {
        internal_rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    result = malloc(completion_queue_size * (sizeof *result));
    if (!result) {
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    scatter_gather = malloc(completion_queue_size * (sizeof *scatter_gather));
    if (!scatter_gather) {
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    send_requests = malloc(completion_queue_size * (sizeof *send_requests));
    if (!send_requests) {
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    struct ibv_qp_attr attr;
    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn   = 0;

    if (ibv_modify_qp(queue_pair, &attr, IBV_QP_STATE|IBV_QP_SQ_PSN)) {
        fprintf(stderr, "Failed to make queue pair ready to send.\n");
        free(result);
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
        free(result);
        rdma_cleanup();
        exit(EXIT_FAILURE);
    }

    char *data_buffers = buffer;
    for (int i = 0; i < completion_queue_size; i++) {
        result[i].data_buffer = &data_buffers[i * MSG_SIZE];

        scatter_gather[i].addr = (uintptr_t) result[i].data_buffer;
        scatter_gather[i].length = MSG_SIZE;
        scatter_gather[i].lkey = memory_region->lkey;

        send_requests[i].wr_id = i;
        if (i == completion_queue_size - 1) {
            send_requests[i].next = &send_requests[0];
        } else {
            send_requests[i].next = &send_requests[i+1];
        }
        send_requests[i].sg_list = &scatter_gather[i];
        send_requests[i].num_sge = 1;
        send_requests[i].opcode = IBV_WR_SEND;
        send_requests[i].send_flags = send_flags;
        send_requests[i].wr.ud.ah = ah;
        send_requests[i].wr.ud.remote_qpn = qpn;
        send_requests[i].wr.ud.remote_qkey = 0x11111111;
    }

    return result;
}

// Cleanup all the global allocations done during the initialisation
void rdma_cleanup()
{
    if (ibv_dereg_mr(memory_region)) {
        fprintf(stderr, "Couldn't destroy memory region.\n");
        exit(EXIT_FAILURE);
    }

    free(buffer);

    if (header_memory_region) {
        if (ibv_dereg_mr(header_memory_region)) {
            fprintf(stderr, "Couldn't destroy memory region.\n");
            exit(EXIT_FAILURE);
        }
    }

    if (header_buffer) free(header_buffer);

    if (scatter_gather) free(scatter_gather);
    if (recv_requests) free(recv_requests);
    if (send_requests) free(send_requests);

    internal_rdma_cleanup();
}

// Cleanup that's shared between rdma_cleanup() and the error handling in this
// file.
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

// Treat the allocated data buffer and Send Requests as a circular buffer from
// which we post requests to the the NIC. Starting from request at index
// 'start' and posting the next 'count' requests.
int post_sends(int start, int count)
{
    int return_value = 0;
    struct ibv_send_wr *bad_wr;

    size_t last_idx = (start + count - 1) % queue_size;
    void *old = send_requests[last_idx].next;

    send_requests[last_idx].next = NULL;

    int result = ibv_post_send(queue_pair, &send_requests[start], &bad_wr);
    if (result) {
        fprintf(stderr, "post send failed (%d) with errno: %d\n%s\n%s\n", result, errno, strerror(result), strerror(errno));
        return_value = -1;
    }

    send_requests[last_idx].next = old;

    return return_value;
}

// Treat the allocated data buffer and Receive Requests as a circular buffer
// from which we post requests to the the NIC. Starting from request at index
// 'start' and posting the next 'count' requests.
int post_recvs(int start, int count)
{
    int return_value = 0;
    struct ibv_recv_wr *bad_wr;

    size_t last_idx = (start + count - 1) % queue_size;
    void *old = recv_requests[last_idx].next;

    recv_requests[last_idx].next = NULL;

    int result = ibv_post_recv(queue_pair, &recv_requests[start], &bad_wr);
    if (result) {
        fprintf(stderr, "post receive failed (%d) with errno: %d\n", result, errno);
        return_value = -1;
    }

    recv_requests[last_idx].next = old;

    return return_value;
}
