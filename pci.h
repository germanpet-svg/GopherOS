#ifndef PCI_H
#define PCI_H
#include "types.h"

typedef struct {
    uint8_t bus, slot, func;
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, prog_if;
    uint32_t bar[6];       // Base Address Registers, ya con los flags bajos limpios
    uint8_t irq_line;
} pci_device_t;

// Busca el primer dispositivo que matchee vendor/device. Devuelve true si lo encontro.
bool pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* out);

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

// Habilita Bus Mastering (necesario para que el dispositivo pueda hacer DMA)
void pci_enable_bus_mastering(const pci_device_t* dev);

#endif
