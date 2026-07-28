// multiboot.h - Estructuras minimas de Multiboot 1 para leer modulos.

#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

typedef struct {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t reserved;
} multiboot_module_t;

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    // resto omitido para este kernel didactico
} multiboot_info_t;

void multiboot_load_files(uint32_t mb_info, Region* r);

#endif // MULTIBOOT_H
