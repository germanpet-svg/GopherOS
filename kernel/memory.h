// memory.h - API publica del allocator de paginas.

#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

void memory_init(void);
phys_addr_t page_alloc(size_t num_pages);
void page_free(phys_addr_t addr, size_t num_pages);

// Convierte un offset dentro del arena (lo que devuelve page_alloc) a
// direccion virtual. El arena esta identity-mapeado, asi que la direccion
// virtual coincide con la fisica real (arena_base + offset).
void* phys_to_virt(phys_addr_t p);

// Direccion base del arena (para calcular direcciones fisicas reales).
extern uint8_t* arena_base;

#endif // MEMORY_H
