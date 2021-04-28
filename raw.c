#define _POSIX_C_SOURCE 200112L
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

static int packet_loop = 1;

void stop_loop(int sig)
{
    (void) sig;
    packet_loop = 0;
}

#define MAX_PACKET_SIZE 65535
static char ip_buffer[MAX_PACKET_SIZE + 1];
static struct iphdr *ip_header = (struct iphdr*) ip_buffer;
static struct udphdr *udp_header = (struct udphdr*) &ip_buffer[sizeof *ip_header];
static char *udp_data = &ip_buffer[sizeof *ip_header + sizeof *udp_header];
static const size_t header_size = sizeof *ip_header + sizeof *udp_header;
static const size_t max_msg_size = MAX_PACKET_SIZE - header_size;

uint16_t ipv4check(char *raw_data, int n)
{
    uint16_t *data = (uint16_t*) raw_data;
    n = 1 + ((n - 1) / 2);

    int32_t sum = 0;
    uint16_t answer;

    for (int i = 0; i < n; i++) {
        sum += data[i];
    }

    sum = (sum >> 16) + (sum & 0xffff);
    sum = sum + (sum >> 16);
    answer = (uint16_t) ~sum;

    return answer;
}

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

    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
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
        msg_size = snprintf(udp_data, max_msg_size, "Message: %d", count++);
        if (msg_size < 0 || (size_t) msg_size >= max_msg_size) {
            perror("Error creating message");
            freeaddrinfo(res);
            exit(EXIT_FAILURE);
        }

        uint16_t total_length = header_size + msg_size;
        ip_header->ihl = 5;
        ip_header->version = 4;
        ip_header->tos = 0;
        ip_header->tot_len = htons(total_length);
        ip_header->id = htonl (54321);
        ip_header->frag_off = 0;
        ip_header->ttl = 255;
        ip_header->protocol = IPPROTO_UDP;
        ip_header->check = 0;
        ip_header->saddr = inet_addr("10.141.0.104");
        ip_header->daddr = ((struct sockaddr_in*) res->ai_addr)->sin_addr.s_addr;

        udp_header->source = htons(4242);
        udp_header->dest = htons(atoi(argv[1]));
        udp_header->len = htons(8 + msg_size);
        udp_header->check = 0;

        ip_header->check = ipv4check(ip_buffer, total_length);

        result = sendto(sock, ip_buffer, total_length, 0, res->ai_addr, res->ai_addrlen);
        if (result == -1) {
            perror("Error sending message");
            freeaddrinfo(res);
            exit(EXIT_FAILURE);
        }
        sleep(1);
    }

    return 0;
}
