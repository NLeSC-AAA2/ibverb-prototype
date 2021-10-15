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

extern struct ibv_cq *completion_queue;

void
rdma_init_server
(char *dev_name, int completion_queue_size);

void
rdma_init_client
(char *dev_name, int completion_queue_size, int lid, union ibv_gid gid);

void
rdma_cleanup();

int
post_sends(uint32_t qpn, int n);

int
post_recvs(int n);
#endif
