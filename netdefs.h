#ifndef NETDEFS_H
#define NETDEFS_H
#include "types.h"

#define ETHERTYPE_ARP 0x0806
#define ETHERTYPE_IP  0x0800
#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6

typedef struct __attribute__((packed)) {
    uint8_t dest_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
} eth_header_t;

typedef struct __attribute__((packed)) {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} ip_header_t;

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_offset; // bits altos: longitud del header en palabras de 32 bits
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} tcp_header_t;

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

static inline uint16_t net_bswap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint32_t net_bswap32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}
#define HTONS(x) net_bswap16(x)
#define HTONL(x) net_bswap32(x)
#define NTOHS(x) net_bswap16(x)
#define NTOHL(x) net_bswap32(x)

static inline uint16_t inet_checksum(const void* data, size_t len) {
    uint32_t sum = 0;
    const uint8_t* p = (const uint8_t*)data;
    while (len > 1) {
        sum += (uint16_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len == 1) sum += (uint16_t)(p[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

// Suma parcial (sin el complemento final) — usado para acumular el
// pseudo-header de TCP antes de sumar el segmento en si.
static inline uint32_t inet_checksum_partial(const void* data, size_t len, uint32_t sum) {
    const uint8_t* p = (const uint8_t*)data;
    while (len > 1) {
        sum += (uint16_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len == 1) sum += (uint16_t)(p[0] << 8);
    return sum;
}

static inline uint16_t inet_checksum_finish(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

// Funciones expuestas por net.c, usadas por tcp.c
void net_get_my_mac(uint8_t mac[6]);
bool net_send_eth(const uint8_t dest_mac[6], uint16_t ethertype, const void* payload, size_t len);
bool net_arp_resolve(uint32_t target_ip, uint8_t out_mac[6]);

#endif
