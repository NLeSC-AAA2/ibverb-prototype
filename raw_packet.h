/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */
#ifndef RAW_PACKET_H
#define RAW_PACKET_H
#include <netinet/if_ether.h>
#include "constants.h"

#ifdef __cplusplus
extern "C" {
#endif

// Struct representing the InfiniBand Global Routing Header. The traffic_class
// field is split into two parts, as the bit indexing of wire protocols does
// not line up with the bit indexing in C.
struct __attribute__((__packed__)) ib_grh {
    // For our prototype the traffic class and flow label are always 0
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

// Struct representing the InfiniBand Base Transport Header.
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

// Struct representing the InfiniBand Datagram Extended Transport Header
struct __attribute__((__packed__)) ib_deth {
    // The receiving queue this datagram addresses
    uint32_t queue_key;

    uint32_t reserved: 8;
    // The source queue pair
    uint32_t source_qp: 24;
};

struct __attribute__((__packed__)) ib_headers {
    struct ib_grh grh;
    struct ib_bth bth;
    struct ib_deth deth;
};

struct __attribute__((__packed__)) packet {
    struct ethhdr ether_header;
    struct ib_headers ib_header;
    unsigned char data[MSG_SIZE];
};

void print_grh(struct ib_grh *hdr);
void print_bth(struct ib_bth *hdr);
void print_deth(struct ib_deth *hdr);
void print_ib_headers(struct ib_headers *hdr);
#ifdef __cplusplus
}
#endif
#endif
