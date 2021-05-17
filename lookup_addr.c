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
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lookup_addr.h"

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
