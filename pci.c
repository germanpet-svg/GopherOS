// pci.c - Acceso al espacio de configuración PCI (mecanismo #1, puertos
// 0xCF8/0xCFC) y enumeración simple de bus/slot/función para encontrar
// dispositivos por vendor/device ID (lo que necesita el driver de la NIC).

#include "types.h"
#include "hal.h"
#include "pci.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static uint32_t pci_make_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint32_t)0x80000000u
         | ((uint32_t)bus << 16)
         | ((uint32_t)slot << 11)
         | ((uint32_t)func << 8)
         | (offset & 0xFC);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = pci_make_address(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDRESS, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t addr = pci_make_address(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDRESS, addr);
    outl(PCI_CONFIG_DATA, value);
}

static uint16_t pci_vendor_id(uint8_t bus, uint8_t slot, uint8_t func) {
    return (uint16_t)(pci_config_read32(bus, slot, func, 0x00) & 0xFFFF);
}

bool pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* out) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            if (pci_vendor_id((uint8_t)bus, (uint8_t)slot, 0) == 0xFFFF) continue; // nada conectado

            for (uint32_t func = 0; func < 8; func++) {
                uint32_t reg0 = pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x00);
                uint16_t vid = (uint16_t)(reg0 & 0xFFFF);
                if (vid == 0xFFFF) continue;
                uint16_t did = (uint16_t)((reg0 >> 16) & 0xFFFF);

                if (vid == vendor_id && did == device_id) {
                    out->bus = (uint8_t)bus;
                    out->slot = (uint8_t)slot;
                    out->func = (uint8_t)func;
                    out->vendor_id = vid;
                    out->device_id = did;

                    uint32_t reg2 = pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x08);
                    out->class_code = (uint8_t)((reg2 >> 24) & 0xFF);
                    out->subclass   = (uint8_t)((reg2 >> 16) & 0xFF);
                    out->prog_if    = (uint8_t)((reg2 >> 8) & 0xFF);

                    for (int i = 0; i < 6; i++) {
                        uint32_t bar = pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, (uint8_t)(0x10 + i * 4));
                        // Bit0=1 -> BAR de I/O (limpiar bit0); Bit0=0 -> BAR de memoria (limpiar los 4 bits bajos)
                        out->bar[i] = (bar & 0x1) ? (bar & ~0x3u) : (bar & ~0xFu);
                    }

                    uint32_t reg15 = pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x3C);
                    out->irq_line = (uint8_t)(reg15 & 0xFF);

                    return true;
                }
            }
        }
    }
    return false;
}

void pci_enable_bus_mastering(const pci_device_t* dev) {
    uint32_t cmd = pci_config_read32(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= (1u << 2); // Bus Master Enable
    cmd |= (1u << 0); // I/O Space Enable (por si el BAR usado es de I/O)
    pci_config_write32(dev->bus, dev->slot, dev->func, 0x04, cmd);
}
