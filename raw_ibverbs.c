/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */

#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <assert.h>
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
    char data[];
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

static char
ether_buffer[IP_MAXPACKET] = { 0 };

static struct ethhdr *
ether_header = (struct ethhdr*) ether_buffer;

static struct ib_packet *
ib_packet = (struct ib_packet*) &ether_buffer[sizeof *ether_header];

static const uint32_t
checksum_size = sizeof(uint32_t);

static const uint32_t
ib_transport_header_size = sizeof ib_packet->bth + sizeof ib_packet->deth;

static const size_t
total_header_size = sizeof *ether_header + sizeof *ib_packet;

static const size_t
msg_size = 1024;

uint32_t
ib_header_checksum(struct ib_packet *packet)
{
    uint64_t ib_padding = ~0;
    struct ib_grh grh = packet->grh;
    struct ib_bth bth = packet->bth;

    grh.traffic_class_p1 = ~0;
    grh.traffic_class_p2 = ~0;
    grh.flow_label = ~0;
    grh.hop_limit = ~0;

    bth.reserved1 = ~0;

    uint32_t crc = 0;
    crc = crc32(crc, (const unsigned char*) &ib_padding, sizeof ib_padding);
    crc = crc32(crc, (const unsigned char*) &grh, sizeof grh);
    crc = crc32(crc, (const unsigned char*) &bth, sizeof bth);
    crc = crc32(crc, (const unsigned char*) &packet->deth, sizeof packet->deth);

    return crc;
}

uint32_t
ib_checksum(struct ib_packet *packet)
{
    size_t data_length = ntohs(packet->grh.payload_length);
    data_length -= ib_transport_header_size + checksum_size;

    uint32_t crc = ib_header_checksum(packet);
    crc = crc32(crc, (const unsigned char*) packet->data, data_length);

    unsigned char *raw = (unsigned char*) &crc;
    printf("%x\n", crc);
    printf("%x %x %x %x\n", raw[0], raw[1], raw[2], raw[3]);

    return crc;
}

uint32_t
init_invariant_ib_headers
( struct addr *src
, struct addr *dest
, uint32_t qp
)
{
    ether_header->h_proto = htons(0x8915);
    memcpy(ether_header->h_dest, dest->mac, ETH_ALEN);
    memcpy(ether_header->h_source, src->mac, ETH_ALEN);

    ib_packet->grh.ip_version = 6;
    ib_packet->grh.next_header = 27;
    ib_packet->grh.hop_limit = 1;

    memcpy(ib_packet->grh.source, &src->ipv6, sizeof src->ipv6);
    memcpy(ib_packet->grh.dest, &dest->ipv6, sizeof dest->ipv6);

    uint32_t ib_length = msg_size + ib_transport_header_size + checksum_size;
    ib_packet->grh.payload_length = htons(ib_length);

    ib_packet->bth.opcode = 0x64; //UD - send only
    ib_packet->bth.sollicited_event = 0;
    ib_packet->bth.migration_request = 1;

    ib_packet->bth.partition_key = htons(0xFFFF);
    ib_packet->bth.destination_qp = htonl(qp << 8);

    ib_packet->deth.queue_key = htonl(0x11111111);
    ib_packet->deth.source_qp = htonl(0x182 << 8);

    return ib_header_checksum(ib_packet);
}

void ib_host_send_loop(uint32_t header_crc, uint32_t msg_size)
{
    int result;

    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    int count = 0;
    while (packet_loop) {
        result = snprintf(ib_packet->data, msg_size, "Message: %d", count++);
        if (result < 0 || (size_t) result >= msg_size) {
            perror("Error creating message");
            exit(EXIT_FAILURE);
        }

        uint32_t length = total_header_size + msg_size + checksum_size;
        uint32_t *checksum = (uint32_t*) &ib_packet->data[msg_size];
        *checksum = crc32(header_crc, (unsigned char*) ib_packet->data, msg_size);

        print_ib_packet(ib_packet);
        printf("\n");

        result = sendto(sock, ether_buffer, length, 0, (struct sockaddr *) &device, sizeof (device));
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

    assert(sizeof ether_buffer - total_header_size - checksum_size > msg_size + checksum_size);

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

    uint32_t header_crc = init_invariant_ib_headers(&local, &remote, atoi(argv[3]));

    ib_host_send_loop(header_crc, msg_size);

    return 0;
}
