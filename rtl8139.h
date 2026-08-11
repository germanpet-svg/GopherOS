#ifndef RTL8139_H
#define RTL8139_H
#include "types.h"

bool nic_init(void);                 // busca la NIC por PCI y la inicializa
bool nic_present(void);
void nic_get_mac(uint8_t mac[6]);

bool nic_send(const uint8_t* data, uint16_t len);

// No bloqueante: si hay un paquete esperando, lo copia a out_buf (hasta
// max_len bytes) y devuelve su tamaño real. Si no hay nada, devuelve 0.
uint16_t nic_poll_recv(uint8_t* out_buf, uint16_t max_len);

#endif
