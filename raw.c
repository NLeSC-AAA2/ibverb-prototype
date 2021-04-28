#define _POSIX_C_SOURCE 200112L
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/udp.h>

static int packet_loop = 1;

void stop_loop(int sig)
{
    (void) sig;
    packet_loop = 0;
}

static char udp_buffer[65535];
static struct udphdr *udp_header = (struct udphdr*) udp_buffer;
static char *udp_data = &udp_buffer[sizeof *udp_header];

int main(int argc, char **argv)
{
    struct sigaction handler;
    memset(&handler, 0, sizeof handler);
    handler.sa_handler = &stop_loop;

    if (sigaction(SIGINT, &handler, NULL)) {
        perror("Couldn't install signal handler");
        exit(EXIT_FAILURE);
    }

    if (argc != 3) {
        fprintf(stderr, "Incorrect number of arguments.\n");
        exit(EXIT_FAILURE);
    }

    int sock = socket(PF_INET, SOCK_RAW, IPPROTO_UDP);
    if (sock == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    int msg_size, result;
    int count = 0;

    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST;

    result = getaddrinfo(argv[2], argv[1], &hints, &res);
    if (result != 0) {
        fprintf(stderr, "Error finding local adddress: %s\n",
                gai_strerror(result));
        exit(EXIT_FAILURE);
    }


    while (packet_loop) {
        msg_size = snprintf(udp_data, sizeof udp_buffer, "Message: %d", count++);
        if (msg_size < 0 || (size_t) msg_size >= sizeof udp_buffer) {
            perror("Error creating message");
            freeaddrinfo(res);
            exit(EXIT_FAILURE);
        }

        udp_header->source = htons(4242);
        udp_header->dest = htons(atoi(argv[1]));
        udp_header->len = htons(8 + msg_size);
        udp_header->check = 0;

        result = sendto(sock, udp_buffer, 8 + msg_size, 0, res->ai_addr, res->ai_addrlen);
        if (result == -1) {
            perror("Error sending message");
            freeaddrinfo(res);
            exit(EXIT_FAILURE);
        }
        sleep(1);
    }

    return 0;
}
