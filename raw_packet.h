/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */
#ifndef RAW_PACKET_H
#define RAW_PACKET_H
#include <netinet/if_ether.h>

#ifdef __cplusplus
extern "C" {
#endif

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

struct __attribute__((__packed__)) ib_deth {
    uint32_t queue_key;

    uint32_t reserved: 8;
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
    unsigned char data[];
};

void print_grh(struct ib_grh *hdr);
void print_bth(struct ib_bth *hdr);
void print_deth(struct ib_deth *hdr);
void print_ib_headers(struct ib_headers *hdr);
#ifdef __cplusplus
}
#endif
#endif
