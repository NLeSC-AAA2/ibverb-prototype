/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */

#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/if_packet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include "lookup_addr.h"

static int packet_loop = 1;

void stop_loop(int sig)
{
    (void) sig;
    packet_loop = 0;
}

static char ether_buffer[IP_MAXPACKET];
static struct ethhdr *ether_header = (struct ethhdr*) ether_buffer;
static struct iphdr *ip_header = (struct iphdr*) &ether_buffer[sizeof *ether_header];
static struct udphdr *udp_header = (struct udphdr*) &ether_buffer[sizeof *ether_header + sizeof *ip_header];
static char *udp_data = &ether_buffer[sizeof *ether_header + sizeof *ip_header + sizeof *udp_header];
static const size_t header_size = sizeof *ether_header + sizeof *ip_header + sizeof *udp_header;
static const size_t max_msg_size = IP_MAXPACKET - header_size;

// IPv4 header checksum implementation. Per RFC 791, the IPv4 header checksum
// is the ones' complement sum of all 16bit words in the header. Since C uses
// two's complement, we have to manually rejiggle any potential overvlow at the
// end.
uint16_t ipv4check(void *raw_data, int n)
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

    char *ifname = "eth4";
    if (argc == 4) {
        ifname = argv[3];
    } else if (argc != 3) {
        fprintf(stderr, "Usage: raw <UDP port> <destination IP> [<interface name>]\n");
        exit(EXIT_FAILURE);
    }

    struct addr local = lookup_local_addr(ifname);
    printf("Local address:\n");
    print_addr(&local);

    struct addr remote = lookup_remote_addr(ifname, argv[2], NULL);
    printf("\nRemote address:\n");
    print_addr(&remote);

    struct sockaddr_ll device;
    memset(&device, 0, sizeof (device));
    if ((device.sll_ifindex = if_nametoindex(ifname)) == 0) {
        perror("if_nametoindex() failed to obtain interface index ");
        exit(EXIT_FAILURE);
    }

    // Specify the local NIC to send from and the destination MAC
    device.sll_family = AF_PACKET;
    device.sll_halen = 6;
    memcpy(device.sll_addr, remote.mac, ETH_ALEN);

    ether_header->h_proto = htons(0x0800);
    memcpy(ether_header->h_dest, remote.mac, ETH_ALEN);
    memcpy(ether_header->h_source, local.mac, ETH_ALEN);

    ip_header->ihl = 5; // Header length
    ip_header->version = 4; // IPv4
    ip_header->tos = 0;
    ip_header->id = htonl (54321); // Arbitrarily chosen packet id.
    ip_header->frag_off = 0; // Don't fragment
    ip_header->ttl = 255;
    ip_header->protocol = IPPROTO_UDP;
    ip_header->daddr = remote.ipv4;
    ip_header->saddr = local.ipv4;

    // Use the commandline destination port
    udp_header->dest = htons(atoi(argv[1]));
    // Specify 4242 as source port
    udp_header->source = htons(4242);
    // UDP checksum is optional, so we skip it
    udp_header->check = 0;

    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    int count = 0;
    while (packet_loop) {
        int msg_size = snprintf(udp_data, max_msg_size, "Message: %d", count++);
        if (msg_size < 0 || (size_t) msg_size >= max_msg_size) {
            perror("Error creating message");
            exit(EXIT_FAILURE);
        }

        uint16_t total_length = header_size + msg_size;
        uint16_t ip_length = total_length - sizeof *ether_header;

        ip_header->tot_len = htons(ip_length);

        udp_header->len = htons(8 + msg_size);

        ip_header->check = 0;
        ip_header->check = ipv4check(ip_header, sizeof *ip_header);

        int result = sendto(sock, ether_buffer, total_length, 0, (struct sockaddr *) &device, sizeof (device));
        if (result == -1) {
            perror("Error sending message");
            exit(EXIT_FAILURE);
        }
        sleep(1);
    }

    return 0;
}
