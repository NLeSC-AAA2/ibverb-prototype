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

static int client_loop = 1;

void stop_loop(int sig)
{
    (void) sig;
    client_loop = 0;
}

int main(int argc, char *argv[])
{
    const int completion_queue_size = 20;
    struct send_buffer *buffers = NULL;

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

    // ibverbs initialisation and allocate a circular buffer to write from
    buffers = rdma_init_client(argv[1], completion_queue_size, lid, gid, qpn);

    // initialise the memory in each send buffer
    int count = 0;
    for (int i = 0; i < completion_queue_size; i++) {
        memset(buffers[i].data_buffer, count++, MSG_SIZE);
    }

    // Fill completion queue with Send Requests for each buffer
    if (post_sends(0, completion_queue_size)) {
        fprintf(stderr, "Couldn't post sends\n");
        result = EXIT_FAILURE;
        goto cleanup;
    }

    struct ibv_wc wc[10];
    while (client_loop) {
        // Poll for completed Send Requests
        int ne = ibv_poll_cq(completion_queue, 10, wc);
        if (ne < 0) {
            fprintf(stderr, "poll CQ failed %d\n", ne);
            result = EXIT_FAILURE;
            goto cleanup;
        } else {
            fprintf(stderr, "sent %d messages\n", ne);
        }

        // Check the result status for each completed Send Request
        for (int i = 0; i < ne; ++i) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
                        ibv_wc_status_str(wc[i].status),
                        wc[i].status, (int) wc[i].wr_id);
                result = EXIT_FAILURE;
                goto cleanup;
            }

            // Change buffer contents
            memset(buffers[wc[i].wr_id].data_buffer, count++, MSG_SIZE);
        }

        // If there were completed requests, requeue the Send Requests.
        if (ne > 0) {
            if (post_sends(wc[0].wr_id, ne)) {
                fprintf(stderr, "Couldn't post sends\n");
                result = EXIT_FAILURE;
                goto cleanup;
            }
        }
        sleep(1);
    }

  cleanup:
    free(buffers);
    rdma_cleanup();

    return result;
}
