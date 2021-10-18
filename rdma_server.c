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
#include <time.h>

#include "rdma.h"

static int server_loop = 1;

void stop_loop(int sig)
{
    (void) sig;
    server_loop = 0;
}

int main(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    const int completion_queue_size = 50;

    int result = EXIT_SUCCESS;

    if (argc != 2) {
        fprintf(stderr, "Usage: rdma_server <IB driver>\n");
        return EXIT_FAILURE;
    }

    struct sigaction handler;
    memset(&handler, 0, sizeof handler);
    handler.sa_handler = &stop_loop;

    if (sigaction(SIGINT, &handler, NULL)) {
        fprintf(stderr, "Couldn't mask signals.\n");
        return EXIT_FAILURE;
    }

    rdma_init_server(argv[1], completion_queue_size);

    int outstanding = post_recvs(completion_queue_size);
    if (outstanding < completion_queue_size) {
        fprintf(stderr, "Couldn't post receive (%d)\n", outstanding);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    struct ibv_wc wc[10];
    while (server_loop) {
        int ne = ibv_poll_cq(completion_queue, 10, wc);
        if (ne < 0) {
            fprintf(stderr, "poll CQ failed %d\n", ne);
            result = EXIT_FAILURE;
            goto cleanup;
        } else if (ne == 0) {
            struct timespec sleep_time;
            sleep_time.tv_sec = 1;
            sleep_time.tv_nsec = 0;
            nanosleep(&sleep_time, NULL);
        } else {
            fprintf(stderr, "received %d messages\n", ne);
        }

        for (int i = 0; i < ne; ++i) {
            printf("Message for WR #%ld, size: %d bytes\n", wc[i].wr_id, wc[i].byte_len);
            if (wc[i].status != IBV_WC_SUCCESS) {
                fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
                        ibv_wc_status_str(wc[i].status),
                        wc[i].status, (int) wc[i].wr_id);
                result = EXIT_FAILURE;
                goto cleanup;
            }
        }

        outstanding -= ne;
        if (outstanding <= 1) {
            outstanding += post_recvs(completion_queue_size - outstanding);
            if (outstanding < completion_queue_size) {
                fprintf(stderr, "Couldn't post receive (%d)\n", outstanding);
                result = EXIT_FAILURE;
                goto cleanup;
            }
        }
    }

  cleanup:
    rdma_cleanup();

    return result;
}
