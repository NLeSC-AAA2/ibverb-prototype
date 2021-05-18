/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */

#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/if_packet.h>
#include <netinet/ip.h>

#include "crc32.h"
#include "lookup_addr.h"

static int packet_loop = 1;
static struct sockaddr_ll device = { 0 };

void stop_loop(int sig)
{
    (void) sig;
    packet_loop = 0;
}

static char ether_buffer[IP_MAXPACKET];
unsigned char ibdata[] = {
#include "packet.data"
};

struct __attribute__((__packed__)) ib_grh {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    uint32_t traffic_class_p1: 4;
    uint32_t ip_version: 4;
    uint32_t traffic_class_p2: 4;
    uint32_t flow_label: 20;
#else
#error "Not implemented"
#endif

    uint16_t payload_length;

    uint8_t next_header;
    uint8_t hop_limit;

    uint8_t source[16];
    uint8_t dest[16];
};

void
print_grh(struct ib_grh *hdr)
{
    printf("IP Version: %d\n", hdr->ip_version);
    printf("Traffic class 1: %x\n", hdr->traffic_class_p1);
    printf("Traffic class 2: %x\n", hdr->traffic_class_p2);
    printf("Flow label: %x\n", hdr->flow_label);

    printf("Length: %d\n", ntohs(hdr->payload_length));

    printf("Next header: %d\n", hdr->next_header);
    printf("Hop limit: %d\n", hdr->hop_limit);

    char ip[INET6_ADDRSTRLEN];
    struct in6_addr addr_ipv6 = { 0 };

    memcpy(&addr_ipv6, hdr->source, sizeof hdr->source);
    printf("Source: %s\n", inet_ntop(AF_INET6, &addr_ipv6, ip, sizeof ip));

    memcpy(&addr_ipv6, hdr->dest, sizeof hdr->dest);
    printf("Dest: %s\n", inet_ntop(AF_INET6, &addr_ipv6, ip, sizeof ip));
}

struct __attribute__((__packed__)) ib_bth {
    uint8_t opcode;

#if __BYTE_ORDER == __LITTLE_ENDIAN
    uint8_t transport_hdr_version: 4;
    uint8_t pad_count: 2;
    uint8_t migration_request: 1;
    uint8_t sollicited_event: 1;
#else
#error "Not implemented"
#endif

    uint16_t partition_key;

#if __BYTE_ORDER == __LITTLE_ENDIAN
    uint32_t reserved1: 8;
    uint32_t destination_qp: 24;
#else
#error "Not implemented"
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN
    uint32_t acknowledge_req: 1;
    uint32_t reserved2: 7;
    uint32_t packet_sequence: 24;
#else
#error "Not implemented"
#endif
};

void
print_bth(struct ib_bth *hdr)
{
    printf("Opcode: 0x%x\n", hdr->opcode);

    printf("Sollicited: %d\n", hdr->sollicited_event);
    printf("Migration req: %d\n", hdr->migration_request);
    printf("Pad count: %d\n", hdr->pad_count);
    printf("Header version: %d\n", hdr->transport_hdr_version);

    printf("Partition key: %d\n", ntohs(hdr->partition_key));
    printf("Destination QP: 0x%x\n", ntohl(hdr->destination_qp) >> 8);

    printf("Acknowledge req: %d\n", hdr->acknowledge_req);
    printf("Packet sequence number: %d\n", hdr->packet_sequence);
}

struct __attribute__((__packed__)) ib_deth {
    uint32_t queue_key;

    uint32_t reserved: 8;
    uint32_t source_qp: 24;
};

void
print_deth(struct ib_deth *hdr)
{
    printf("Queue key: 0x%x\n", ntohl(hdr->queue_key));
    printf("Source QP: 0x%x\n", ntohl(hdr->source_qp) >> 8);
}

struct __attribute__((__packed__)) ib_packet {
    struct ib_grh grh;
    struct ib_bth bth;
    struct ib_deth deth;
};

void
print_ib_packet(struct ib_packet *packet)
{
    print_grh(&packet->grh);
    printf("\n");
    print_bth(&packet->bth);
    printf("\n");
    print_deth(&packet->deth);
}

static struct ethhdr *ether_header = (struct ethhdr*) ether_buffer;
static struct ib_packet *ib_packet = (struct ib_packet*) &ether_buffer[sizeof *ether_header];
//static char *raw_data = &ether_buffer[sizeof *ether_header + sizeof *ib_packet];

uint32_t
ib_checksum(struct ib_packet *packet)
{
    uint64_t ib_padding = ~0;
    struct ib_grh grh = packet->grh;
    struct ib_bth bth = packet->bth;

    size_t data_length = ntohs(packet->grh.payload_length) - sizeof bth - 4;

    grh.traffic_class_p1 = ~0;
    grh.traffic_class_p2 = ~0;
    grh.flow_label = ~0;
    grh.hop_limit = ~0;

    bth.reserved1 = ~0;

    uint32_t crc = 0;
    unsigned char *raw = (unsigned char*) &crc;
    crc = crc32(crc, (const unsigned char*) &ib_padding, sizeof ib_padding);
    crc = crc32(crc, (const unsigned char*) &grh, sizeof grh);
    crc = crc32(crc, (const unsigned char*) &bth, sizeof bth);
    crc = crc32(crc, (const unsigned char*) &packet->deth, data_length);

    printf("%x\n", crc);
    printf("%x %x %x %x\n", raw[0], raw[1], raw[2], raw[3]);

    return crc;
}

void ib_send_loop(struct addr *src, struct addr *dest, uint32_t qp)
{
    ether_header->h_proto = htons(0x8915);
    memcpy(ether_header->h_dest, dest->mac, ETH_ALEN);
    memcpy(ether_header->h_source, src->mac, ETH_ALEN);

    memcpy(ether_buffer, ibdata, sizeof ibdata);

    ib_packet->bth.destination_qp = htonl(qp << 8);
    print_ib_packet(ib_packet);
    printf("\n");

    uint32_t *checksum = (uint32_t*) &ether_buffer[sizeof ibdata - 4];
    *checksum = ib_checksum(ib_packet);

    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    //int count = 0;
    while (packet_loop) {
    /*
        int msg_size = snprintf(udp_data, max_msg_size, "Message: %d", count++);
        if (msg_size < 0 || (size_t) msg_size >= max_msg_size) {
            perror("Error creating message");
            exit(EXIT_FAILURE);
        }
        */

        uint16_t total_length = sizeof ibdata;
        int result = sendto(sock, ether_buffer, total_length, 0, (struct sockaddr *) &device, sizeof (device));
        if (result == -1) {
            perror("Error sending message");
            exit(EXIT_FAILURE);
        }
        sleep(1);
    }
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
    if (argc == 5) {
        ifname = argv[4];
    } else if (argc != 4) {
        fprintf(stderr, "Incorrect number of arguments.\n");
        exit(EXIT_FAILURE);
    }

    struct addr local = lookup_local_addr(ifname);
    printf("Local address:\n");
    print_addr(&local);

    struct addr remote = lookup_remote_addr(ifname, argv[1], argv[2]);
    printf("\nRemote address:\n");
    print_addr(&remote);
    printf("\n");

    if ((device.sll_ifindex = if_nametoindex(ifname)) == 0) {
        perror("if_nametoindex() failed to obtain interface index ");
        exit(EXIT_FAILURE);
    }

    device.sll_family = AF_PACKET;
    device.sll_halen = 6;
    memcpy(device.sll_addr, remote.mac, ETH_ALEN);

    ib_send_loop(&local, &remote, atoi(argv[3]));

    return 0;
}
