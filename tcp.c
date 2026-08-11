// tcp.c - TCP deliberadamente mínimo: una conexión a la vez, sin
// retransmisión ni control de congestión — alcanza y sobra para servir
// algo simple como Gopher sobre una red local/QEMU. Nada de esto pretende
// ser una pila TCP "de produccion".

#include "types.h"
#include "string.h"
#include "netdefs.h"
#include "net.h"
#include "tcp.h"

typedef enum { TCP_ST_CLOSED, TCP_ST_LISTEN, TCP_ST_SYN_RCVD, TCP_ST_ESTABLISHED, TCP_ST_CLOSING } tcp_state_t;

static tcp_state_t state = TCP_ST_CLOSED;
static uint16_t listen_port = 0;
static tcp_conn_t* active_conn = NULL;

#define RECV_BUF_SIZE 4096
static uint8_t recv_buf[RECV_BUF_SIZE];
static int recv_buf_len = 0;
static bool got_fin = false;

static void send_tcp_segment(tcp_conn_t* conn, uint8_t flags, const uint8_t* data, size_t len) {
    uint8_t buf[1500];
    if (len > 1400) len = 1400;

    ip_header_t* ip = (ip_header_t*)buf;
    tcp_header_t* tcp = (tcp_header_t*)(buf + sizeof(ip_header_t));
    uint8_t* payload = buf + sizeof(ip_header_t) + sizeof(tcp_header_t);
    if (len > 0) memcpy(payload, data, len);

    tcp->src_port = HTONS(conn->local_port);
    tcp->dst_port = HTONS(conn->peer_port);
    tcp->seq = HTONL(conn->my_seq);
    tcp->ack = HTONL(conn->peer_seq);
    tcp->data_offset = (uint8_t)((sizeof(tcp_header_t) / 4) << 4);
    tcp->flags = flags;
    tcp->window = HTONS(8192);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    struct __attribute__((packed)) {
        uint32_t src, dst;
        uint8_t zero, proto;
        uint16_t tcp_len;
    } pseudo;
    pseudo.src = HTONL(NET_MY_IP);
    pseudo.dst = HTONL(conn->peer_ip);
    pseudo.zero = 0;
    pseudo.proto = IP_PROTO_TCP;
    pseudo.tcp_len = HTONS((uint16_t)(sizeof(tcp_header_t) + len));

    uint32_t sum = inet_checksum_partial(&pseudo, sizeof(pseudo), 0);
    sum = inet_checksum_partial(tcp, sizeof(tcp_header_t) + len, sum);
    tcp->checksum = HTONS(inet_checksum_finish(sum));

    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = HTONS((uint16_t)(sizeof(ip_header_t) + sizeof(tcp_header_t) + len));
    ip->id = 0;
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTO_TCP;
    ip->checksum = 0;
    ip->src_ip = HTONL(NET_MY_IP);
    ip->dst_ip = HTONL(conn->peer_ip);
    ip->checksum = HTONS(inet_checksum(ip, sizeof(ip_header_t)));

    net_send_eth(conn->peer_mac, ETHERTYPE_IP, buf, sizeof(ip_header_t) + sizeof(tcp_header_t) + len);

    if (flags & (TCP_SYN | TCP_FIN)) conn->my_seq += 1;
    else conn->my_seq += (uint32_t)len;
}

// Llamado desde net.c cuando llega un segmento TCP (protocolo IP = 6).
void tcp_handle_segment(eth_header_t* eth, ip_header_t* ip, size_t frame_len) {
    size_t ip_hlen = (size_t)(ip->version_ihl & 0x0F) * 4;
    if (frame_len < sizeof(eth_header_t) + ip_hlen + sizeof(tcp_header_t)) return;

    tcp_header_t* tcp = (tcp_header_t*)((uint8_t*)ip + ip_hlen);
    size_t tcp_hlen = (size_t)((tcp->data_offset >> 4) & 0x0F) * 4;
    size_t ip_total = NTOHS(ip->total_len);
    size_t tcp_seg_len = ip_total > ip_hlen ? ip_total - ip_hlen : 0;
    size_t data_len = tcp_seg_len > tcp_hlen ? tcp_seg_len - tcp_hlen : 0;
    uint8_t* data = (uint8_t*)tcp + tcp_hlen;

    uint16_t dst_port = NTOHS(tcp->dst_port);
    uint16_t src_port = NTOHS(tcp->src_port);
    uint32_t seg_seq = NTOHL(tcp->seq);

    if (state == TCP_ST_LISTEN && (tcp->flags & TCP_SYN) && dst_port == listen_port) {
        active_conn->peer_ip = NTOHL(ip->src_ip);
        memcpy(active_conn->peer_mac, eth->src_mac, 6);
        active_conn->peer_port = src_port;
        active_conn->local_port = dst_port;
        active_conn->peer_seq = seg_seq + 1;
        active_conn->my_seq = 0x1000;
        state = TCP_ST_SYN_RCVD;
        send_tcp_segment(active_conn, TCP_SYN | TCP_ACK, NULL, 0);
        return;
    }

    if (!active_conn || dst_port != active_conn->local_port || src_port != active_conn->peer_port) return;

    if (state == TCP_ST_SYN_RCVD) {
        if (tcp->flags & TCP_ACK) {
            state = TCP_ST_ESTABLISHED;
            active_conn->connected = true;
        }
        return;
    }

    if (state == TCP_ST_ESTABLISHED) {
        if (tcp->flags & TCP_FIN) {
            active_conn->peer_seq = seg_seq + 1;
            got_fin = true;
            send_tcp_segment(active_conn, TCP_ACK, NULL, 0);
            return;
        }
        if (data_len > 0 && seg_seq == active_conn->peer_seq) {
            size_t copy = data_len;
            if ((size_t)recv_buf_len + copy > RECV_BUF_SIZE) copy = RECV_BUF_SIZE - (size_t)recv_buf_len;
            memcpy(recv_buf + recv_buf_len, data, copy);
            recv_buf_len += (int)copy;
            active_conn->peer_seq += (uint32_t)data_len;
            send_tcp_segment(active_conn, TCP_ACK, NULL, 0);
        }
        return;
    }

    if (state == TCP_ST_CLOSING) {
        if (tcp->flags & TCP_FIN) {
            active_conn->peer_seq = seg_seq + 1;
            send_tcp_segment(active_conn, TCP_ACK, NULL, 0);
        }
        if (tcp->flags & TCP_ACK) {
            state = TCP_ST_CLOSED;
        }
        return;
    }
}

bool tcp_accept(uint16_t port, tcp_conn_t* conn) {
    extern void net_poll(void);
    extern void proc_yield(void);

    memset(conn, 0, sizeof(*conn));
    active_conn = conn;
    listen_port = port;
    state = TCP_ST_LISTEN;
    recv_buf_len = 0;
    got_fin = false;

    for (int i = 0; i < 3000000; i++) {
        net_poll();
        if (state == TCP_ST_ESTABLISHED) return true;
        if ((i % 1000) == 0) proc_yield();
    }
    state = TCP_ST_CLOSED;
    return false;
}

int tcp_recv(tcp_conn_t* conn, uint8_t* buf, int maxlen) {
    extern void net_poll(void);
    extern void proc_yield(void);
    (void)conn;

    for (int i = 0; i < 500000; i++) {
        net_poll();
        if (recv_buf_len > 0) {
            int n = recv_buf_len > maxlen ? maxlen : recv_buf_len;
            memcpy(buf, recv_buf, (size_t)n);
            if (recv_buf_len > n) memmove(recv_buf, recv_buf + n, (size_t)(recv_buf_len - n));
            recv_buf_len -= n;
            return n;
        }
        if (got_fin) return 0;
        if ((i % 1000) == 0) proc_yield();
    }
    return 0;
}

bool tcp_send(tcp_conn_t* conn, const uint8_t* data, int len) {
    int sent = 0;
    while (sent < len) {
        int chunk = (len - sent) > 1400 ? 1400 : (len - sent);
        send_tcp_segment(conn, TCP_ACK | TCP_PSH, data + sent, (size_t)chunk);
        sent += chunk;
    }
    return true;
}

void tcp_close(tcp_conn_t* conn) {
    extern void net_poll(void);
    extern void proc_yield(void);

    send_tcp_segment(conn, TCP_FIN | TCP_ACK, NULL, 0);
    state = TCP_ST_CLOSING;

    for (int i = 0; i < 300000; i++) {
        net_poll();
        if (state == TCP_ST_CLOSED) break;
        if ((i % 1000) == 0) proc_yield();
    }
    state = TCP_ST_CLOSED;
    conn->connected = false;
}
