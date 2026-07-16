// paging.c - Memoria virtual real (x86, páginas de 4MB vía PSE)
//
// Diseño: el kernel asume identity-mapping (virt == phys, ver KERNEL_BASE en
// types.h), así que no necesitamos tablas de página de 4KB por proceso todavía.
// Lo que SÍ ganamos activando paging de verdad:
//   - CR2 nos dice la dirección exacta que causó un fallo (útil para el BSOD).
//   - El bit US=0 (supervisor) protege todo el kernel de escritura desde ring3
//     el día que haya procesos de usuario reales.
//   - Es la base necesaria para el día que se quiera paginar por proceso.
//
// Usamos páginas de 4MB (PSE) en vez de 4KB porque para un identity map
// simple es mucho menos código (8 entradas de directorio cubren 32MB) y
// el mecanismo real de CR0/CR3/CR4 es idéntico al de paginación "seria".

#include "types.h"
#include "hal.h"

#define PSE_BIT   (1u << 4)   // CR4.PSE
#define PG_BIT    (1u << 31)  // CR0.PG

#define PDE_PRESENT  (1u << 0)
#define PDE_RW       (1u << 1)
#define PDE_US       (1u << 2)  // 0 = solo supervisor (ring0)
#define PDE_PS       (1u << 7)  // 1 = página de 4MB

#define IDENTITY_MAP_MB   32u   // debe cubrir kernel + arena (~9MB) con margen
#define ENTRIES_NEEDED    (IDENTITY_MAP_MB / 4)

static uint32_t page_directory[1024] __attribute__((aligned(4096)));

static inline void write_cr3(uint32_t v) { __asm__ volatile ("mov %0, %%cr3" :: "r"(v) : "memory"); }
static inline uint32_t read_cr4(void) { uint32_t v; __asm__ volatile ("mov %%cr4, %0" : "=r"(v)); return v; }
static inline void write_cr4(uint32_t v) { __asm__ volatile ("mov %0, %%cr4" :: "r"(v) : "memory"); }
static inline uint32_t read_cr0(void) { uint32_t v; __asm__ volatile ("mov %%cr0, %0" : "=r"(v)); return v; }
static inline void write_cr0(uint32_t v) { __asm__ volatile ("mov %0, %%cr0" :: "r"(v) : "memory"); }

uint32_t read_cr2(void) {
    uint32_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

void paging_init(void) {
    for (int i = 0; i < 1024; i++) page_directory[i] = 0; // no presente

    for (uint32_t i = 0; i < ENTRIES_NEEDED; i++) {
        uint32_t phys_base = i * 0x400000u; // 4MB por entrada
        // NOTA: PDE_US habilitado en TODAS las paginas identity-mapeadas.
        // Esto es una simplificacion deliberada del primer corte de ring3:
        // alcanza para probar que la transicion CPL0<->CPL3 y el manejo de
        // fallos funcionan de verdad, pero NO hay aislacion de memoria real
        // todavia (un proceso ring3 puede leer/escribir memoria del kernel
        // o de otros procesos). Aislacion de verdad requiere paginas de 4KB
        // con permisos por region, que es el siguiente paso pendiente.
        page_directory[i] = phys_base | PDE_PRESENT | PDE_RW | PDE_PS | PDE_US;
    }

    write_cr4(read_cr4() | PSE_BIT);
    write_cr3((uint32_t)page_directory);
    write_cr0(read_cr0() | PG_BIT);
}
