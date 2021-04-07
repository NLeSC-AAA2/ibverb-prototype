#include <arpa/inet.h>
#include <malloc.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

    int result = EXIT_SUCCESS;

    if (argc != 2) {
        fprintf(stderr, "Insufficient arguments\n");
        return EXIT_FAILURE;
    }

    struct sigaction handler;
    memset(&handler, 0, sizeof handler);
    handler.sa_handler = &stop_loop;

    if (sigaction(SIGINT, &handler, NULL)) {
        fprintf(stderr, "Couldn't mask signals.\n");
        return EXIT_FAILURE;
    }

    rdma_init_server(argv[1]);

    int outstanding = post_recvs(RECV_QUEUE_SIZE);
    if (outstanding < RECV_QUEUE_SIZE) {
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
            usleep(1000);
        } else {
            fprintf(stderr, "received %d messages\n", ne);
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
        if (outstanding <= 1) {
            outstanding += post_recvs(RECV_QUEUE_SIZE - outstanding);
            if (outstanding < RECV_QUEUE_SIZE) {
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
