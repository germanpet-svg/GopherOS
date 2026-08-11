#ifndef NET_H
#define NET_H
#include "types.h"

// Configuracion de red estatica (sin DHCP todavia). Elegida para matchear
// el "user-mode networking" (SLIRP) de QEMU por defecto: gateway 10.0.2.2,
// guest en 10.0.2.15.
#define NET_MY_IP      ((uint32_t)((10u<<24)|(0u<<16)|(2u<<8)|15u))
#define NET_GATEWAY_IP ((uint32_t)((10u<<24)|(0u<<16)|(2u<<8)|2u))
#define NET_NETMASK    ((uint32_t)0xFFFFFF00u)

void net_init(void);
void net_poll(void); // procesa un paquete entrante si hay alguno (no bloqueante)

// Envia un ARP request para `target_ip` y espera (bloqueante, con timeout)
// la resolucion. Devuelve true si la consiguio, llenando `out_mac`.
bool net_arp_resolve(uint32_t target_ip, uint8_t out_mac[6]);

// Envia un ICMP echo request y espera la respuesta (bloqueante, con
// timeout). Devuelve true si llego el echo reply.
bool net_ping(uint32_t target_ip);

void net_ip_to_str(uint32_t ip, char* out); // "a.b.c.d"

#endif
