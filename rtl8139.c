// rtl8139.c - Driver para la tarjeta de red RTL8139 (la que emula QEMU por
// defecto con "-device rtl8139"). Polling, sin usar IRQ todavia — igual
// que el driver de disco, es la forma mas simple y confiable de empezar.
//
// Solo maneja tramas Ethernet crudas (enviar/recibir bytes). ARP/IP/TCP
// viven en capas separadas (net_*.c) que usan esto como transporte.

#include "types.h"
#include "hal.h"
#include "pci.h"
#include "rtl8139.h"

#define RTL_VENDOR_ID 0x10EC
#define RTL_DEVICE_ID 0x8139

// Offsets de registros (todos relativos a io_base, el BAR0 de I/O)
#define REG_IDR0     0x00 // MAC address (6 bytes)
#define REG_TSD0     0x10 // Transmit Status of Descriptor 0-3 (4 bytes c/u)
#define REG_TSAD0    0x20 // Transmit Start Address of Descriptor 0-3
#define REG_RBSTART  0x30 // Receive Buffer Start
#define REG_CAPR     0x38 // Current Address of Packet Read
#define REG_IMR      0x3C // Interrupt Mask
#define REG_ISR      0x3E // Interrupt Status
#define REG_TCR      0x40 // Transmit Config
#define REG_RCR      0x44 // Receive Config
#define REG_CONFIG1  0x52
#define REG_CMD      0x37 // Command register (RE/TE/RST)

#define CMD_RESET    0x10
#define CMD_RX_EN    0x08
#define CMD_TX_EN    0x04

#define ISR_ROK      0x01 // Receive OK
#define ISR_TOK      0x04 // Transmit OK

#define RX_BUF_SIZE  (8192 + 16 + 1500) // +16 header +1500 overflow (WRAP)
#define TX_BUF_SIZE  1536
#define NUM_TX_DESC  4

static uint16_t io_base = 0;
static bool present = false;
static uint8_t mac_addr[6];

static uint8_t rx_buffer[RX_BUF_SIZE] __attribute__((aligned(4)));
static uint32_t rx_offset = 0;

static uint8_t tx_buffers[NUM_TX_DESC][TX_BUF_SIZE] __attribute__((aligned(4)));
static int tx_cur = 0;

bool nic_present(void) { return present; }

void nic_get_mac(uint8_t mac[6]) {
    for (int i = 0; i < 6; i++) mac[i] = mac_addr[i];
}

bool nic_init(void) {
    pci_device_t dev;
    if (!pci_find_device(RTL_VENDOR_ID, RTL_DEVICE_ID, &dev)) {
        present = false;
        return false;
    }

    pci_enable_bus_mastering(&dev);

    // BAR0 es el de I/O para el RTL8139 (ya viene con los bits de flag
    // limpios por pci_find_device).
    io_base = (uint16_t)dev.bar[0];

    outb(io_base + REG_CONFIG1, 0x00); // encender el dispositivo

    outb(io_base + REG_CMD, CMD_RESET);
    for (volatile int i = 0; i < 1000000 && (inb(io_base + REG_CMD) & CMD_RESET); i++) { }

    // Direccion MAC asignada por QEMU, la leemos de IDR0-IDR5
    for (int i = 0; i < 6; i++) mac_addr[i] = inb(io_base + REG_IDR0 + i);

    rx_offset = 0;
    for (size_t i = 0; i < sizeof(rx_buffer); i++) rx_buffer[i] = 0;
    outl(io_base + REG_RBSTART, (uint32_t)rx_buffer);

    outw(io_base + REG_IMR, ISR_ROK | ISR_TOK);

    // RCR: aceptar paquetes para nuestra MAC (APM), broadcast (AB),
    // multicast (AM), y activar WRAP (bit7) para simplificar el manejo
    // del buffer circular (a costa de los 1500 bytes extra de relleno).
    outl(io_base + REG_RCR, 0x0F | (1u << 7));

    // TCR: configuracion por defecto razonable (deja el IFG/versionID en 0)
    outl(io_base + REG_TCR, 0x03000000);

    outb(io_base + REG_CMD, CMD_RX_EN | CMD_TX_EN);

    tx_cur = 0;
    present = true;
    return true;
}

bool nic_send(const uint8_t* data, uint16_t len) {
    if (!present) return false;
    if (len > TX_BUF_SIZE) return false;

    uint8_t* buf = tx_buffers[tx_cur];
    for (uint16_t i = 0; i < len; i++) buf[i] = data[i];
    // Ethernet exige un minimo de 60 bytes de payload (sin el CRC, que pone el hardware)
    uint16_t padded_len = len < 60 ? 60 : len;
    for (uint16_t i = len; i < padded_len; i++) buf[i] = 0;

    outl(io_base + REG_TSAD0 + tx_cur * 4, (uint32_t)buf);
    outl(io_base + REG_TSD0 + tx_cur * 4, padded_len & 0x1FFF);

    // Esperar a que termine de transmitir (polling, sin IRQ todavia).
    for (volatile int i = 0; i < 2000000; i++) {
        if (inl(io_base + REG_TSD0 + tx_cur * 4) & 0x8000) break; // TOK
    }

    tx_cur = (tx_cur + 1) % NUM_TX_DESC;
    return true;
}

uint16_t nic_poll_recv(uint8_t* out_buf, uint16_t max_len) {
    if (!present) return 0;

    uint16_t isr = inw(io_base + REG_ISR);
    if (!(isr & ISR_ROK)) {
        if (isr) outw(io_base + REG_ISR, isr); // limpiar otros bits (ej. TOK) igual
        return 0;
    }

    uint16_t status = *(uint16_t*)(rx_buffer + rx_offset);
    uint16_t plen = *(uint16_t*)(rx_buffer + rx_offset + 2); // incluye 4 bytes de CRC

    if (!(status & 0x01)) { // ROK del propio paquete (bit0 del status)
        outw(io_base + REG_ISR, ISR_ROK);
        return 0;
    }

    uint16_t data_len = (plen >= 4) ? (uint16_t)(plen - 4) : 0; // sin el CRC
    uint16_t copy_len = data_len > max_len ? max_len : data_len;

    // Con WRAP activado reservamos +1500 bytes extra, asi que un paquete
    // individual (max ~1514 bytes) nunca necesita "dar la vuelta" al
    // buffer a mitad de copia — copiar linealmente es seguro aca.
    uint8_t* pkt = rx_buffer + rx_offset + 4;
    for (uint16_t i = 0; i < copy_len; i++) out_buf[i] = pkt[i];

    rx_offset = (rx_offset + plen + 4 + 3) & ~3u;
    if (rx_offset >= 8192) rx_offset -= 8192;
    outw(io_base + REG_CAPR, (uint16_t)(rx_offset - 16));

    outw(io_base + REG_ISR, ISR_ROK);

    return copy_len;
}
