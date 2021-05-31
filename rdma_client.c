/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 *
 * Based on code by Topspin Communications, under the following license:
 *
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 *  - Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "rdma.h"

#define SEND_QUEUE_SIZE 1

static int client_loop = 1;

void stop_loop(int sig)
{
    (void) sig;
    client_loop = 0;
}

int main(int argc, char *argv[])
{
    int qpn;
    int lid;
    union ibv_gid gid;
    int result = EXIT_SUCCESS;

    if (argc != 5) {
        fprintf(stderr, "Usage: rdma_client <IB driver> <IB GID> <IB LID> <IB QP>\n");
        return EXIT_FAILURE;
    }

    lid = atoi(argv[3]);
    qpn = atoi(argv[4]);
    inet_pton(AF_INET6, argv[2], &gid);

    struct sigaction handler;
    memset(&handler, 0, sizeof handler);
    handler.sa_handler = &stop_loop;

    if (sigaction(SIGINT, &handler, NULL)) {
        fprintf(stderr, "Couldn't mask signals.\n");
        return EXIT_FAILURE;
    }

    rdma_init_client(argv[1], lid, gid);

    int outstanding = post_sends(qpn, SEND_QUEUE_SIZE);
    if (outstanding < SEND_QUEUE_SIZE) {
        fprintf(stderr, "Couldn't post receive (%d)\n", outstanding);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    struct ibv_wc wc[10];
    while (client_loop) {
        int ne = ibv_poll_cq(completion_queue, 10, wc);
        if (ne < 0) {
            fprintf(stderr, "poll CQ failed %d\n", ne);
            result = EXIT_FAILURE;
            goto cleanup;
        } else {
            fprintf(stderr, "sent %d messages\n", ne);
        }

        for (int i = 0; i < ne; ++i) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
                        ibv_wc_status_str(wc[i].status),
                        wc[i].status, (int) wc[i].wr_id);
                result = EXIT_FAILURE;
                goto cleanup;
            }
        }

        outstanding -= ne;
        if (outstanding <= SEND_QUEUE_SIZE) {
            outstanding += post_sends(qpn, SEND_QUEUE_SIZE - outstanding);
            if (outstanding < SEND_QUEUE_SIZE) {
                fprintf(stderr, "Couldn't post send (%d)\n", outstanding);
                result = EXIT_FAILURE;
                goto cleanup;
            }
        }
    }

  cleanup:
    rdma_cleanup();

    return result;
}
