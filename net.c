// net.c - Pila de red mínima: Ethernet + ARP + IPv4 + ICMP (ping).
//
// Todo por polling sobre rtl8139.c (nic_poll_recv/nic_send), sin IRQ
// todavía. net_poll() se llama repetidamente desde quien la necesite
// (comandos del shell, tcp.c, o un futuro proceso "netd").

#include "types.h"
#include "string.h"
#include "rtl8139.h"
#include "netdefs.h"
#include "net.h"

typedef struct __attribute__((packed)) {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sender_mac[6];
    uint32_t sender_ip;
    uint8_t target_mac[6];
    uint32_t target_ip;
} arp_packet_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} icmp_header_t;

#define MAX_FRAME 1600

static uint8_t my_mac[6];
static uint8_t gateway_mac[6];
static bool gateway_mac_known = false;

static uint32_t arp_wait_target_ip = 0;
static bool arp_wait_resolved = false;
static uint8_t arp_wait_mac[6];

static bool ping_waiting = false;
static uint16_t ping_wait_id = 0;
static uint16_t ping_wait_seq = 0;
static bool ping_reply_ok = false;

void net_get_my_mac(uint8_t mac[6]) { memcpy(mac, my_mac, 6); }

bool net_send_eth(const uint8_t dest_mac[6], uint16_t ethertype, const void* payload, size_t len) {
    static uint8_t frame[MAX_FRAME];
    if (len > MAX_FRAME - sizeof(eth_header_t)) return false;
    eth_header_t* eth = (eth_header_t*)frame;
    memcpy(eth->dest_mac, dest_mac, 6);
    memcpy(eth->src_mac, my_mac, 6);
    eth->ethertype = HTONS(ethertype);
    memcpy(frame + sizeof(eth_header_t), payload, len);
    return nic_send(frame, (uint16_t)(sizeof(eth_header_t) + len));
}

void net_ip_to_str(uint32_t ip, char* out) {
    extern size_t utoa(uint32_t val, char* buf, int base);
    uint8_t b0 = (uint8_t)(ip >> 24), b1 = (uint8_t)(ip >> 16), b2 = (uint8_t)(ip >> 8), b3 = (uint8_t)ip;
    char tmp[4];
    size_t pos = 0;
    size_t n;
    n = utoa(b0, tmp, 10); memcpy(out + pos, tmp, n); pos += n; out[pos++] = '.';
    n = utoa(b1, tmp, 10); memcpy(out + pos, tmp, n); pos += n; out[pos++] = '.';
    n = utoa(b2, tmp, 10); memcpy(out + pos, tmp, n); pos += n; out[pos++] = '.';
    n = utoa(b3, tmp, 10); memcpy(out + pos, tmp, n); pos += n;
    out[pos] = 0;
}

static void send_arp(uint16_t oper, const uint8_t dest_mac[6], uint32_t target_ip, const uint8_t target_mac[6]) {
    arp_packet_t arp;
    arp.htype = HTONS(1);
    arp.ptype = HTONS(0x0800);
    arp.hlen = 6;
    arp.plen = 4;
    arp.oper = HTONS(oper);
    memcpy(arp.sender_mac, my_mac, 6);
    arp.sender_ip = HTONL(NET_MY_IP);
    memcpy(arp.target_mac, target_mac, 6);
    arp.target_ip = HTONL(target_ip);
    net_send_eth(dest_mac, ETHERTYPE_ARP, &arp, sizeof(arp));
}

static void handle_arp(arp_packet_t* arp) {
    uint16_t oper = NTOHS(arp->oper);
    uint32_t sender_ip = NTOHL(arp->sender_ip);
    uint32_t target_ip = NTOHL(arp->target_ip);

    if (oper == 1 && target_ip == NET_MY_IP) {
        send_arp(2, arp->sender_mac, sender_ip, arp->sender_mac);
    } else if (oper == 2) {
        if (arp_wait_target_ip != 0 && sender_ip == arp_wait_target_ip) {
            memcpy(arp_wait_mac, arp->sender_mac, 6);
            arp_wait_resolved = true;
        }
        if (sender_ip == NET_GATEWAY_IP) {
            memcpy(gateway_mac, arp->sender_mac, 6);
            gateway_mac_known = true;
        }
    }
}

static void send_icmp_echo_reply(ip_header_t* ip_in, icmp_header_t* icmp_in, const uint8_t* data, size_t data_len, const uint8_t dest_mac[6]) {
    uint8_t buf[sizeof(ip_header_t) + sizeof(icmp_header_t) + 64];
    if (data_len > 64) data_len = 64;

    ip_header_t* ip = (ip_header_t*)buf;
    icmp_header_t* icmp = (icmp_header_t*)(buf + sizeof(ip_header_t));
    uint8_t* payload = buf + sizeof(ip_header_t) + sizeof(icmp_header_t);

    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = HTONS((uint16_t)(sizeof(ip_header_t) + sizeof(icmp_header_t) + data_len));
    ip->id = 0;
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTO_ICMP;
    ip->checksum = 0;
    ip->src_ip = HTONL(NET_MY_IP);
    ip->dst_ip = ip_in->src_ip;
    ip->checksum = HTONS(inet_checksum(ip, sizeof(ip_header_t)));

    icmp->type = 0;
    icmp->code = 0;
    icmp->id = icmp_in->id;
    icmp->seq = icmp_in->seq;
    memcpy(payload, data, data_len);
    icmp->checksum = 0;
    icmp->checksum = HTONS(inet_checksum(icmp, sizeof(icmp_header_t) + data_len));

    net_send_eth(dest_mac, ETHERTYPE_IP, buf, sizeof(ip_header_t) + sizeof(icmp_header_t) + data_len);
}

static void handle_ip(eth_header_t* eth, ip_header_t* ip, size_t frame_len) {
    if (NTOHL(ip->dst_ip) != NET_MY_IP) return;

    size_t ip_header_len = (size_t)(ip->version_ihl & 0x0F) * 4;

    if (ip->protocol == IP_PROTO_TCP) {
        extern void tcp_handle_segment(eth_header_t* eth, ip_header_t* ip, size_t frame_len);
        tcp_handle_segment(eth, ip, frame_len);
        return;
    }

    if (ip->protocol != IP_PROTO_ICMP) return;
    if (frame_len < sizeof(eth_header_t) + ip_header_len + sizeof(icmp_header_t)) return;

    icmp_header_t* icmp = (icmp_header_t*)((uint8_t*)ip + ip_header_len);
    size_t icmp_total = NTOHS(ip->total_len) - ip_header_len;
    size_t data_len = icmp_total > sizeof(icmp_header_t) ? icmp_total - sizeof(icmp_header_t) : 0;
    uint8_t* data = (uint8_t*)icmp + sizeof(icmp_header_t);

    if (icmp->type == 8) {
        send_icmp_echo_reply(ip, icmp, data, data_len, eth->src_mac);
    } else if (icmp->type == 0) {
        if (ping_waiting && NTOHS(icmp->id) == ping_wait_id && NTOHS(icmp->seq) == ping_wait_seq) {
            ping_reply_ok = true;
        }
    }
}

void net_init(void) {
    extern void nic_get_mac(uint8_t mac[6]);
    nic_get_mac(my_mac);
    gateway_mac_known = false;
    arp_wait_target_ip = 0;
    ping_waiting = false;
}

void net_poll(void) {
    static uint8_t frame[MAX_FRAME];
    uint16_t len = nic_poll_recv(frame, MAX_FRAME);
    if (len < sizeof(eth_header_t)) return;

    eth_header_t* eth = (eth_header_t*)frame;
    uint16_t ethertype = NTOHS(eth->ethertype);

    if (ethertype == ETHERTYPE_ARP && len >= sizeof(eth_header_t) + sizeof(arp_packet_t)) {
        handle_arp((arp_packet_t*)(frame + sizeof(eth_header_t)));
    } else if (ethertype == ETHERTYPE_IP && len >= sizeof(eth_header_t) + sizeof(ip_header_t)) {
        handle_ip(eth, (ip_header_t*)(frame + sizeof(eth_header_t)), len);
    }
}

bool net_arp_resolve(uint32_t target_ip, uint8_t out_mac[6]) {
    extern void proc_yield(void);
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t zero_mac[6] = {0, 0, 0, 0, 0, 0};

    if (target_ip == NET_GATEWAY_IP && gateway_mac_known) {
        memcpy(out_mac, gateway_mac, 6);
        return true;
    }

    arp_wait_target_ip = target_ip;
    arp_wait_resolved = false;

    send_arp(1, broadcast, target_ip, zero_mac);

    for (int i = 0; i < 200000; i++) {
        net_poll();
        if (arp_wait_resolved) {
            memcpy(out_mac, arp_wait_mac, 6);
            arp_wait_target_ip = 0;
            return true;
        }
        if ((i % 1000) == 0) proc_yield();
    }
    arp_wait_target_ip = 0;
    return false;
}

bool net_ping(uint32_t target_ip) {
    uint8_t dest_mac[6];
    if (target_ip == NET_MY_IP) return false;

    uint32_t via = ((target_ip & NET_NETMASK) == (NET_MY_IP & NET_NETMASK)) ? target_ip : NET_GATEWAY_IP;
    if (!net_arp_resolve(via, dest_mac)) return false;

    uint8_t buf[sizeof(ip_header_t) + sizeof(icmp_header_t) + 4];
    ip_header_t* ip = (ip_header_t*)buf;
    icmp_header_t* icmp = (icmp_header_t*)(buf + sizeof(ip_header_t));
    uint8_t* payload = buf + sizeof(ip_header_t) + sizeof(icmp_header_t);

    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = HTONS((uint16_t)(sizeof(ip_header_t) + sizeof(icmp_header_t) + 4));
    ip->id = 0;
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTO_ICMP;
    ip->checksum = 0;
    ip->src_ip = HTONL(NET_MY_IP);
    ip->dst_ip = HTONL(target_ip);
    ip->checksum = HTONS(inet_checksum(ip, sizeof(ip_header_t)));

    ping_wait_id = 0x1234;
    ping_wait_seq++;
    icmp->type = 8;
    icmp->code = 0;
    icmp->id = HTONS(ping_wait_id);
    icmp->seq = HTONS(ping_wait_seq);
    payload[0] = 'g'; payload[1] = 'o'; payload[2] = 'p'; payload[3] = 'h';
    icmp->checksum = 0;
    icmp->checksum = HTONS(inet_checksum(icmp, sizeof(icmp_header_t) + 4));

    ping_reply_ok = false;
    ping_waiting = true;
    net_send_eth(dest_mac, ETHERTYPE_IP, buf, sizeof(buf));

    extern void proc_yield(void);
    for (int i = 0; i < 300000; i++) {
        net_poll();
        if (ping_reply_ok) { ping_waiting = false; return true; }
        if ((i % 1000) == 0) proc_yield();
    }
    ping_waiting = false;
    return false;
}
