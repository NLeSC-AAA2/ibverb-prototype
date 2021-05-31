/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */

#ifndef LOOKUP_ADDR_H
#define LOOKUP_ADDR_H

#include <netinet/if_ether.h>
#include <stdint.h>

struct addr {
    char mac[ETH_ALEN];
    uint32_t ipv4;
    struct in6_addr ipv6;
};

void print_addr(struct addr *remote);

struct addr
lookup_remote_addr(char *interface, char *host, char *port);

struct addr
lookup_local_addr(char *interface);

struct addr
addr_from_strings(char *mac, char *ipv4, char *ib_gid);
#endif
