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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/if_packet.h>
#include <netinet/ip.h>

#include "crc32.h"
#include "lookup_addr.h"
#include "fpga_host.h"
#include "raw_packet.h"

static int packet_loop = 1;
static struct sockaddr_ll device = { 0 };

void stop_loop(int sig)
{
    (void) sig;
    packet_loop = 0;
}

static struct packet full_packet = { 0 };

static const uint32_t
checksum_size = sizeof(uint32_t);

static const uint32_t
ib_transport_header_size = sizeof (struct ib_bth) + sizeof (struct ib_deth);

static const size_t
total_header_size = sizeof (struct ethhdr) + sizeof (struct ib_headers);

// Implementation of the InfiniBand header checksumming. The checksum is the
// CRC32 of the Global Routing Header, Base Transport Header, and Datagram
// Extended Transport Header. Several fields (traffic class, flow label, hop
// limit, and reserved bit) are treated as all 1s, as are the 64 bits preceding
// the GRH.
uint32_t
ib_header_checksum(struct ib_headers *header)
{
    uint64_t ib_padding = ~0;
    struct ib_grh grh = header->grh;
    struct ib_bth bth = header->bth;

    grh.traffic_class_p1 = ~0;
    grh.traffic_class_p2 = ~0;
    grh.flow_label = ~0;
    grh.hop_limit = ~0;

    bth.reserved1 = ~0;

    uint32_t crc = 0;
    crc = crc32(crc, (const unsigned char*) &ib_padding, sizeof ib_padding);
    crc = crc32(crc, (const unsigned char*) &grh, sizeof grh);
    crc = crc32(crc, (const unsigned char*) &bth, sizeof bth);
    crc = crc32(crc, (const unsigned char*) &header->deth, sizeof header->deth);

    return crc;
}

// The checksum of the full InfiniBand packet is the CRC32 checksum of the
// headers and payload.
uint32_t
ib_checksum(struct packet *packet)
{
    size_t data_length = ntohs(packet->ib_header.grh.payload_length);
    data_length -= ib_transport_header_size + checksum_size;

    uint32_t crc = ib_header_checksum(&packet->ib_header);
    crc = crc32(crc, packet->data, data_length);

    unsigned char *raw = (unsigned char*) &crc;
    printf("%x\n", crc);
    printf("%x %x %x %x\n", raw[0], raw[1], raw[2], raw[3]);

    return crc;
}

// Initialise all the Ethernet, UDP, and InfiniBand headers that do not depend
// on the payload data.
uint32_t
init_invariant_headers
( struct packet *packet
, struct addr *src
, struct addr *dest
, uint32_t qp
)
{
    // The EtherType for ROCEv1
    packet->ether_header.h_proto = htons(0x8915);
    memcpy(packet->ether_header.h_dest, dest->mac, ETH_ALEN);
    memcpy(packet->ether_header.h_source, src->mac, ETH_ALEN);

    // Set the InfiniBand header IP version to IPv6
    packet->ib_header.grh.ip_version = 6;
    packet->ib_header.grh.next_header = 27;
    packet->ib_header.grh.hop_limit = 1;

    memcpy(packet->ib_header.grh.source, &src->ipv6, sizeof src->ipv6);
    memcpy(packet->ib_header.grh.dest, &dest->ipv6, sizeof dest->ipv6);

    // InfiniBand packet length is payload + headers + checksum
    uint32_t ib_length = MSG_SIZE + ib_transport_header_size + checksum_size;
    packet->ib_header.grh.payload_length = htons(ib_length);

    // Constant for Unreliable Datagram messages
    packet->ib_header.bth.opcode = 0x64; //UD - send only
    packet->ib_header.bth.sollicited_event = 0;
    packet->ib_header.bth.migration_request = 1;

    // Partition key is only used when there's a subnet manager
    packet->ib_header.bth.partition_key = htons(0xFFFF);
    // The destination queue pair specifed on the commandline
    packet->ib_header.bth.destination_qp = htonl(qp << 8);

    // Queue key 0x11111111 is a magic constant that delivers to any queue.
    packet->ib_header.deth.queue_key = htonl(0x11111111);
    // Arbitrarily chosen hardcoded source queue pair
    packet->ib_header.deth.source_qp = htonl(0x182 << 8);

    return ib_header_checksum(&packet->ib_header);
}

// Loop sending InfiniBand UD packets in a loop, updating the payload and
// headers at every iteration.
void
ib_host_send_loop(struct packet *packet, uint32_t header_crc)
{
    int result;

    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    int count = 0;
    while (packet_loop) {
        result = snprintf((char*) packet->data, MSG_SIZE, "Message: %d", count++);
        if (result < 0 || (size_t) result >= MSG_SIZE) {
            perror("Error creating message");
            exit(EXIT_FAILURE);
        }

        uint32_t length = MSG_SIZE + total_header_size + checksum_size;
        uint32_t *checksum = (uint32_t*) &packet->data[MSG_SIZE];
        *checksum = crc32(header_crc, packet->data, MSG_SIZE);

        print_ib_headers(&packet->ib_header);
        printf("\n");

        result = sendto(sock, packet, length, 0, (struct sockaddr *) &device, sizeof (device));
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

    uint32_t queue_pair;
    bool use_fpga = false;
    struct addr local = { 0 };
    struct addr remote = { 0 };

    if ((argc == 5 || argc == 6) && !strcmp("host", argv[1])) {
        char *ifname = argc == 6 ? argv[5] : "eth4";

        local = lookup_local_addr(ifname);
        remote = lookup_remote_addr(ifname, argv[2], argv[3]);
        queue_pair = atoi(argv[4]);

        if ((device.sll_ifindex = if_nametoindex(ifname)) == 0) {
            perror("if_nametoindex() failed to obtain interface index ");
            exit(EXIT_FAILURE);
        }

    } else if ((argc == 6 || argc == 9) && !strcmp("fpga", argv[1])) {
        use_fpga = true;
        if (argc == 6) {
            local = addr_from_strings("04:3f:72:d4:2f:7c", "10.149.5.104", "fe80::63f:72ff:fed4:2f7c");
            remote = addr_from_strings(argv[2], argv[3], argv[4]);
            queue_pair = atoi(argv[5]);
        } else {
            local = addr_from_strings(argv[2], argv[3], argv[4]);
            remote = addr_from_strings(argv[5], argv[6], argv[7]);
            queue_pair = atoi(argv[8]);
        }
    } else {
        fprintf(stderr, "Usage: raw_ibverbs host <dest IPv4> <dest IB GID> <IB QP> [<interface name>]\n");
        fprintf(stderr, "       raw_ibverbs fpga <dest MAC> <dest IPv4> <dest IB GID> <IB QP>\n");
        fprintf(stderr, "       raw_ibverbs fpga <src MAC> <src IPv4> <src IB GID> <dest MAC> <dest IPv4> <dest IB GID> <IB QP>\n");
        exit(EXIT_FAILURE);
    }

    printf("Local address:\n");
    print_addr(&local);

    printf("\nRemote address:\n");
    print_addr(&remote);
    printf("\n");

    device.sll_family = AF_PACKET;
    device.sll_halen = 6;
    memcpy(device.sll_addr, remote.mac, ETH_ALEN);

    uint32_t header_crc = init_invariant_headers(&full_packet, &local, &remote, queue_pair);

    if (use_fpga) ib_fpga_send_loop(&full_packet, header_crc);
    else ib_host_send_loop(&full_packet, header_crc);

    return 0;
}
