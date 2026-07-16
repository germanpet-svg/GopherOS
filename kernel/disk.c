// disk.c - Driver ATA PIO (bus primario, master), LBA28, por polling.
//
// Sin DMA ni IRQ14 todavia: cada operacion bloquea el proceso que la pide
// hasta que el controlador dice "listo" (polling del registro de status).
// Es la forma mas simple y confiable de hablar con un disco IDE/SATA en
// modo de compatibilidad, y es exactamente lo que hacia el DOS real por
// debajo de INT 13h en el hardware de la epoca.

#include "types.h"
#include "hal.h"
#include "disk.h"

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01
#define ATA_SR_DF   0x20

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY   0xEC

static bool have_disk = false;

static inline void ata_400ns_delay(void) {
    // Leer el puerto de status alternativo (0x3F6) 4 veces es el truco
    // clasico para esperar ~400ns tras seleccionar el drive.
    for (int i = 0; i < 4; i++) inb(0x3F6);
}

static bool ata_wait_not_busy(void) {
    // Timeout generoso: en hardware/QEMU real esto tarda microsegundos.
    for (uint32_t i = 0; i < 100000u; i++) {
        uint8_t status = inb(ATA_STATUS);
        if (!(status & ATA_SR_BSY)) return true;
    }
    return false;
}

static bool ata_wait_drq(void) {
    for (uint32_t i = 0; i < 100000u; i++) {
        uint8_t status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR) return false;
        if (status & ATA_SR_DF) return false;
        if (status & ATA_SR_DRQ) return true;
    }
    return false;
}

bool disk_present(void) {
    return have_disk;
}

bool disk_init(void) {
    // IDENTIFY DEVICE: si no hay disco conectado, el status queda en 0
    // o el comando nunca setea DRQ — asi detectamos su ausencia sin
    // colgarnos esperando algo que nunca va a pasar.
    outb(ATA_DRIVE_HEAD, 0xA0); // master, LBA
    ata_400ns_delay();
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_STATUS);
    if (status == 0) {
        have_disk = false;
        return false;
    }

    if (!ata_wait_not_busy()) { have_disk = false; return false; }

    // Si LBA_MID/LBA_HIGH no son 0, probablemente es un dispositivo
    // ATAPI (como el CD-ROM), no un disco duro — lo ignoramos aca.
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HIGH) != 0) {
        have_disk = false;
        return false;
    }

    if (!ata_wait_drq()) { have_disk = false; return false; }

    // Descartar los 256 words de IDENTIFY (no necesitamos el detalle).
    for (int i = 0; i < 256; i++) (void)inw(ATA_DATA);

    have_disk = true;
    return true;
}

bool disk_read_sector(uint32_t lba, uint8_t* buf512) {
    if (!have_disk) return false;
    if (!ata_wait_not_busy()) return false;

    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_400ns_delay();
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_READ_PIO);

    if (!ata_wait_not_busy()) return false;
    if (!ata_wait_drq()) return false;

    uint16_t* buf16 = (uint16_t*)buf512;
    for (int i = 0; i < 256; i++) buf16[i] = inw(ATA_DATA);

    return true;
}

bool disk_write_sector(uint32_t lba, const uint8_t* buf512) {
    if (!have_disk) return false;
    if (!ata_wait_not_busy()) return false;

    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_400ns_delay();
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_WRITE_PIO);

    if (!ata_wait_not_busy()) return false;
    if (!ata_wait_drq()) return false;

    const uint16_t* buf16 = (const uint16_t*)buf512;
    for (int i = 0; i < 256; i++) outw(ATA_DATA, buf16[i]);

    outb(ATA_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_not_busy();

    return true;
}
