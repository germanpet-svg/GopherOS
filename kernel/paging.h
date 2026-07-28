// paging.h - Paginacion por proceso para GopherOS.

#ifndef PAGING_H
#define PAGING_H

#include "types.h"

#define PAGE_SIZE       4096u
#define PAGE_MASK       0xFFFFF000u
#define PDE_PRESENT     (1u << 0)
#define PDE_RW          (1u << 1)
#define PDE_US          (1u << 2)
#define PDE_PS          (1u << 7)   // 4 MB page (PDE)
#define PTE_PRESENT     (1u << 0)
#define PTE_RW          (1u << 1)
#define PTE_US          (1u << 2)

// Espacio virtual de usuario (4 MB a partir de 0x40000000)
#define USER_BASE       0x40000000u
#define USER_SIZE       0x00400000u
#define USER_PD_INDEX   (USER_BASE / 0x400000u)   // 256

void paging_init(void);
uint32_t paging_kernel_dir_phys(void);

// Crea un page directory nuevo con el kernel mapeado como supervisor y el
// espacio de usuario vacio; devuelve la direccion fisica real (para CR3).
uint32_t paging_create_user_dir(void);

// Mapea num_pages paginas fisicas (paddr) a vaddr en el page directory dado.
// paddr debe ser una direccion fisica real (no un offset del arena).
bool paging_map(uint32_t pd_phys, uint32_t vaddr, uint32_t paddr,
                size_t num_pages, uint32_t flags);

// Cambia el page directory activo (CR3).
void paging_switch_dir(uint32_t pd_phys);

#endif // PAGING_H
