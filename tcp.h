#ifndef TCP_H
#define TCP_H
#include "types.h"

typedef struct {
    uint32_t peer_ip;
    uint8_t peer_mac[6];
    uint16_t peer_port;
    uint16_t local_port;
    uint32_t my_seq;     // proximo byte que YO voy a enviar
    uint32_t peer_seq;   // proximo byte que espero del peer (ya recibido + 1)
    bool connected;
} tcp_conn_t;

// Bloqueante: espera hasta que llegue un SYN para `port`, completa el
// three-way handshake, y llena `conn`. Con timeout (devuelve false si no
// llega nada en un tiempo razonable).
bool tcp_accept(uint16_t port, tcp_conn_t* conn);

// Bloqueante con timeout: espera datos entrantes. Devuelve la cantidad de
// bytes copiados a `buf` (0 si el peer cerro la conexion o hubo timeout).
int tcp_recv(tcp_conn_t* conn, uint8_t* buf, int maxlen);

bool tcp_send(tcp_conn_t* conn, const uint8_t* data, int len);
void tcp_close(tcp_conn_t* conn);

#endif
