// types.h - Tipos base de GopherOS
#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint32_t phys_addr_t;
typedef uint32_t virt_addr_t;

typedef enum {
    GOS_OK       = 0,
    GOS_EINVAL   = 1,
    GOS_ENOMEM   = 2,
    GOS_ENOSPC   = 3,
    GOS_EBUSY    = 4,
    GOS_ENOSYS   = 5,
    GOS_ENOENT   = 6,
} gos_result_t;

#define KERNEL_BASE   0x00000000u   // Kernel identity-mapeado (sin higher-half por simplicidad)
#define PAGE_SIZE     4096u
#define ALIGN(x, a)   (((x) + ((a) - 1)) & ~((a) - 1))

#endif // TYPES_H
