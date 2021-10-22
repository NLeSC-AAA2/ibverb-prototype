/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */

#ifndef RDMA_H
#define RDMA_H

#include <stdbool.h>

#if defined(__has_include)
#if __has_include(<infiniband/verbs.h>)
#include <infiniband/verbs.h>
#else
#include "verbs.h"
#endif
#endif

#include "constants.h"

// Struct for the allocated receive buffers, separate pointers for the header
// and payload parts, since we use separate buffers for these.
struct recv_buffer {
    struct ib_grh *header_buffer;
    char *data_buffer;
};

// Struct for the allocated send buffers
struct send_buffer {
    char *data_buffer;
};

// Pointer to the allocated completion queue
extern struct ibv_cq *completion_queue;

struct recv_buffer *
rdma_init_server
(char *dev_name, int completion_queue_size);

struct send_buffer *
rdma_init_client
( char *dev_name
, int completion_queue_size
, int lid
, union ibv_gid gid
, uint32_t qpn
);

void
rdma_cleanup();

int
post_sends(int start, int count);

int
post_recvs(int start, int count);
#endif
