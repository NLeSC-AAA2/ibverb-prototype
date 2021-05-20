/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

#include "raw_packet.h"

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

void
print_deth(struct ib_deth *hdr)
{
    printf("Queue key: 0x%x\n", ntohl(hdr->queue_key));
    printf("Source QP: 0x%x\n", ntohl(hdr->source_qp) >> 8);
}

void
print_ib_headers(struct ib_headers *hdr)
{
    print_grh(&hdr->grh);
    printf("\n");
    print_bth(&hdr->bth);
    printf("\n");
    print_deth(&hdr->deth);
}
