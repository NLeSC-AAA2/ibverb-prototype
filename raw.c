/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */

#define _GNU_SOURCE
#include <netinet/ether.h>
#undef _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/if_packet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

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

struct addr {
    char mac[ETH_ALEN];
    uint32_t ipv4;
};

void
print_addr(struct addr *remote)
{
    struct in_addr addr_ipv4 = { 0 };
    addr_ipv4.s_addr = remote->ipv4;

    printf("IPv4: %s\n", inet_ntoa(addr_ipv4));

    struct ether_addr addr_eth = { 0 };
    memcpy(addr_eth.ether_addr_octet, remote->mac, ETH_ALEN);

    printf("MAC: %s\n", ether_ntoa(&addr_eth));
}

struct addr
lookup_remote_addr(char *interface, char *host, char *port)
{
    struct addr remote = { 0 };
    struct addrinfo hints = { 0 };
    struct addrinfo *res;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int result = getaddrinfo(host, port, &hints, &res);
    if (result != 0) {
        fprintf(stderr, "Error finding remote adddress: %s\n",
                gai_strerror(result));
        exit(EXIT_FAILURE);
    }

    struct arpreq req = { 0 };
    memcpy(&req.arp_pa, res->ai_addr, sizeof req.arp_pa);
    strncpy(req.arp_dev, interface, IFNAMSIZ);

    int sock;
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (ioctl(sock, SIOCGARP, &req) == -1) {
        perror("ioctl");
        exit(EXIT_FAILURE);
    }

    close(sock);

    memcpy(remote.mac, req.arp_ha.sa_data, ETH_ALEN);
    remote.ipv4 = ((struct sockaddr_in*) res->ai_addr)->sin_addr.s_addr;

    return remote;
}

struct addr
lookup_local_addr(int sock, char *interface)
{
    struct ifreq req = { 0 };
    struct addr local = { 0 };

    strncpy(req.ifr_name, interface, IFNAMSIZ);
    if (ioctl(sock, SIOCGIFHWADDR, &req) != 0) {
        perror("failed to lookup MAC address ");
        exit(EXIT_FAILURE);
    }
    memcpy(local.mac, req.ifr_addr.sa_data, ETH_ALEN);

    if (ioctl(sock, SIOCGIFADDR, &req) != 0) {
        perror("failed to lookup IP address ");
        exit(EXIT_FAILURE);
    }
    local.ipv4 = ((struct sockaddr_in*) &req.ifr_addr)->sin_addr.s_addr;

    return local;
}

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
        fprintf(stderr, "Incorrect number of arguments.\n");
        exit(EXIT_FAILURE);
    }

    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    struct addr local = lookup_local_addr(sock, ifname);
    printf("Local address:\n");
    print_addr(&local);

    struct addr remote = lookup_remote_addr(ifname, argv[2], argv[1]);
    printf("\nRemote address:\n");
    print_addr(&remote);

    struct sockaddr_ll device;
    memset(&device, 0, sizeof (device));
    if ((device.sll_ifindex = if_nametoindex(ifname)) == 0) {
        perror("if_nametoindex() failed to obtain interface index ");
        exit(EXIT_FAILURE);
    }

    device.sll_family = AF_PACKET;
    device.sll_halen = 6;
    memcpy(device.sll_addr, remote.mac, ETH_ALEN);

    ether_header->h_proto = htons(0x0800);
    memcpy(ether_header->h_dest, remote.mac, ETH_ALEN);
    memcpy(ether_header->h_source, local.mac, ETH_ALEN);

    ip_header->ihl = 5;
    ip_header->version = 4;
    ip_header->tos = 0;
    ip_header->id = htonl (54321);
    ip_header->frag_off = 0;
    ip_header->ttl = 255;
    ip_header->protocol = IPPROTO_UDP;
    ip_header->daddr = remote.ipv4;
    ip_header->saddr = local.ipv4;

    udp_header->dest = htons(atoi(argv[1]));
    udp_header->source = htons(4242);
    udp_header->check = 0;

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
