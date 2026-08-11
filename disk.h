#ifndef DISK_H
#define DISK_H
#include "types.h"

// Driver ATA PIO (bus primario, master), LBA28, polling (sin IRQ14 todavia).
// Suficiente para un disco chico dedicado como el que usa GopherOS para
// persistir su filesystem (ver fs_save/fs_load en filesystem.c).

bool disk_init(void);                                  // detecta el disco, devuelve false si no hay
bool disk_read_sector(uint32_t lba, uint8_t* buf512);
bool disk_write_sector(uint32_t lba, const uint8_t* buf512);
bool disk_present(void);

#endif
