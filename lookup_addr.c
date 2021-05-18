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
#include <linux/if_packet.h>

#include <ifaddrs.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lookup_addr.h"

void
print_addr(struct addr *addr)
{
    char ip[INET6_ADDRSTRLEN];

    struct in_addr addr_ipv4 = { 0 };
    addr_ipv4.s_addr = addr->ipv4;
    printf("IPv4: %s\n", inet_ntop(AF_INET, &addr_ipv4, ip, sizeof ip));
    printf("IPv6: %s\n", inet_ntop(AF_INET6, &addr->ipv6, ip, sizeof ip));

    struct ether_addr addr_eth = { 0 };
    memcpy(addr_eth.ether_addr_octet, addr->mac, ETH_ALEN);

    printf("MAC: %s\n", ether_ntoa(&addr_eth));
}

struct addr
lookup_remote_addr(char *interface, char *host, char *ipv6)
{
    struct addr remote = { 0 };
    struct addrinfo hints = { 0 };
    struct addrinfo *res;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int result = getaddrinfo(host, NULL, &hints, &res);
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

    if (ipv6) inet_pton(AF_INET6, ipv6, &remote.ipv6);

    return remote;
}

struct addr
lookup_local_addr(char *interface)
{
    struct addr local = { 0 };

    struct ifaddrs *if_addrs;
    struct ifaddrs *curr;

    if (getifaddrs(&if_addrs)) {
        perror("failed to enumerate NICs");
        exit(EXIT_FAILURE);
    }

    for (curr = if_addrs; curr != NULL; curr = curr->ifa_next) {
        if (strcmp(interface, curr->ifa_name)) {
            continue;
        }

        switch (curr->ifa_addr->sa_family) {
          case AF_INET: {
              struct sockaddr_in *ipv4 = (struct sockaddr_in*) curr->ifa_addr;
              local.ipv4 = ipv4->sin_addr.s_addr;
              break;
          }

          case AF_INET6: {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6*) curr->ifa_addr;
            memcpy(&local.ipv6, &ipv6->sin6_addr, sizeof ipv6->sin6_addr);
            break;
          }

          case AF_PACKET: {
            struct sockaddr_ll *mac = (struct sockaddr_ll*) curr->ifa_addr;
            memcpy(local.mac, &mac->sll_addr, ETH_ALEN);
            break;
          }

          default: {
            printf("Found unknown IF family type!\n");
            break;
          }
        }
    }

    freeifaddrs(if_addrs);

    return local;
}
