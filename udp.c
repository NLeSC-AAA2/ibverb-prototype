/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */

#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int packet_loop = 1;

void stop_loop(int sig)
{
    (void) sig;
    packet_loop = 0;
}

static char udp_buffer[65535];

void
server_loop(int sock)
{
    ssize_t size;
    struct sockaddr src_addr;
    socklen_t addrlen = sizeof src_addr;

    char addr_str[INET6_ADDRSTRLEN];

    while (packet_loop) {
        size = recvfrom(sock, udp_buffer, sizeof udp_buffer, 0, &src_addr, &addrlen);
        if (size == -1) {
            perror("Error receiving packet");
            exit(EXIT_FAILURE);
        }

        udp_buffer[size] = '\0';

        switch (src_addr.sa_family) {
          case AF_INET: {
              struct sockaddr_in *addr_in = (struct sockaddr_in *) &src_addr;
              inet_ntop(AF_INET, &addr_in->sin_addr, addr_str, INET_ADDRSTRLEN);
              break;
          }
          case AF_INET6: {
              struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *) &src_addr;
              inet_ntop(AF_INET6, &addr_in6->sin6_addr, addr_str, INET6_ADDRSTRLEN);
              break;
          }
          default:
            break;
        }

        printf("%s: %s\n", addr_str, udp_buffer);
    }
}

void
client_loop(int sock, char *address, char *port)
{
    int msg_size, result;
    int count = 0;

    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST;

    result = getaddrinfo(address, port, &hints, &res);
    if (result != 0) {
        fprintf(stderr, "Error finding local adddress: %s\n",
                gai_strerror(result));
        exit(EXIT_FAILURE);
    }

    while (packet_loop) {
        msg_size = snprintf(udp_buffer, sizeof udp_buffer, "Message: %d", count++);
        if (msg_size < 0 || (size_t) msg_size >= sizeof udp_buffer) {
            perror("Error creating message");
            freeaddrinfo(res);
            exit(EXIT_FAILURE);
        }

        result = sendto(sock, udp_buffer, msg_size, 0, res->ai_addr, res->ai_addrlen);
        if (result != msg_size) {
            perror("Error sending message");
            freeaddrinfo(res);
            exit(EXIT_FAILURE);
        }
        sleep(1);
    }

    freeaddrinfo(res);
}

int main(int argc, char **argv)
{
    int result;

    struct sigaction handler;
    memset(&handler, 0, sizeof handler);
    handler.sa_handler = &stop_loop;

    if (sigaction(SIGINT, &handler, NULL)) {
        perror("Couldn't install signal handler");
        exit(EXIT_FAILURE);
    }

    char *local_port = "0";
    if (argc == 2) {
        local_port = argv[1];
    } else if (argc != 3) {
        fprintf(stderr, "Usage: udp <port number> [<destination IP>]\n");
        exit(EXIT_FAILURE);
    }

    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    result = getaddrinfo(NULL, local_port, &hints, &res);
    if (result != 0) {
        fprintf(stderr, "Error finding local adddress: %s\n", gai_strerror(result));
        exit(EXIT_FAILURE);
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    result = bind(sock, res->ai_addr, res->ai_addrlen);
    if (result == -1) {
        perror("Error binding socket");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(res);

    if (argc == 3) client_loop(sock, argv[2], argv[1]);
    else server_loop(sock);

    return 0;
}
