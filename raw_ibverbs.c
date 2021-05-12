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
#include <net/ethernet.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/if_packet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

static uint32_t crc32table[] = {
0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

static uint32_t
crc32(uint32_t crc, const unsigned char *data, ssize_t size)
{
    crc = crc ^ 0xFFFFFFFF;

    for (int i = 0; i < size; i++) {
        crc = crc32table[(((int) crc) & 0xFF) ^ data[i]] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

static int packet_loop = 1;

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

    //uint8_t source[16];
    //uint8_t dest[16];
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

struct addr {
    char mac[ETH_ALEN];
    uint32_t ipv4;
};

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

uint16_t ipv4check(void *raw_data, int n)
{
    uint16_t *data = (uint16_t*) raw_data;
    n = 1 + ((n - 1) / 2);

    int32_t sum = 0;
    uint16_t answer;

    for (int i = 0; i < n; i++) {
        sum += data[i];
    }

    sum = (sum >> 16) + (sum & 0xffff);
    sum = sum + (sum >> 16);
    answer = (uint16_t) ~sum;

    return answer;
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
    if (argc == 4) {
        ifname = argv[3];
    } else if (argc != 3) {
        fprintf(stderr, "Incorrect number of arguments.\n");
        exit(EXIT_FAILURE);
    }

    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    struct addr local = lookup_local_addr(sock, ifname);
    printf("Local address:\n");
    print_addr(&local);

    struct addr remote = lookup_remote_addr(ifname, argv[1], NULL);
    printf("\nRemote address:\n");
    print_addr(&remote);
    printf("\n");

    struct sockaddr_ll device;
    memset(&device, 0, sizeof (device));
    if ((device.sll_ifindex = if_nametoindex(ifname)) == 0) {
        perror("if_nametoindex() failed to obtain interface index ");
        exit(EXIT_FAILURE);
    }

    device.sll_family = AF_PACKET;
    device.sll_halen = 6;
    memcpy(device.sll_addr, remote.mac, ETH_ALEN);

    ether_header->h_proto = htons(0x8915);
    memcpy(ether_header->h_dest, remote.mac, ETH_ALEN);
    memcpy(ether_header->h_source, local.mac, ETH_ALEN);

    memcpy(ether_buffer, ibdata, sizeof ibdata);

    ib_packet->bth.destination_qp = htonl(atoi(argv[2]) << 8);
    print_ib_packet(ib_packet);
    printf("\n");

    uint32_t *checksum = (uint32_t*) &ether_buffer[sizeof ibdata - 4];
    *checksum = ib_checksum(ib_packet);

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

    return 0;
}
