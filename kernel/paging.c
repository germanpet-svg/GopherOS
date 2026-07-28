// paging.c - Paginacion real (x86) con tablas de 4KB por proceso.
//
// El kernel se mapea con paginas de 4MB (PSE) en las primeras entradas del
// directorio, todo como supervisor (U/S=0). Cada proceso ring3 obtiene un
// directorio propio que copia esas entradas y anade una tabla de 4KB para el
// espacio de usuario en USER_BASE (0x40000000).

#include "types.h"
#include "hal.h"
#include "string.h"
#include "memory.h"
#include "paging.h"

#define PSE_BIT   (1u << 4)   // CR4.PSE
#define PG_BIT    (1u << 31)  // CR0.PG

#define IDENTITY_MAP_MB   32u   // debe cubrir kernel + arena (~9MB) con margen
#define IDENTITY_ENTRIES  (IDENTITY_MAP_MB / 4)

static uint32_t kernel_page_dir[1024] __attribute__((aligned(4096)));

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

static uint32_t virt_of_phys_addr(uint32_t paddr) {
    // El arena esta identity-mapeado; las direcciones dentro de el se pueden
    // acceder directamente. Todas las estructuras de paginacion se asignan
    // desde page_alloc() que devuelve un offset dentro del arena.
    return paddr;
}

void paging_init(void) {
    for (int i = 0; i < 1024; i++) kernel_page_dir[i] = 0;

    for (uint32_t i = 0; i < IDENTITY_ENTRIES; i++) {
        uint32_t phys_base = i * 0x400000u; // 4MB por entrada
        // Kernel mapeado como supervisor (sin PDE_US). Ring3 no puede
        // leer/escribir memoria del kernel, pero syscalls e IRQs siguen
        // funcionando porque el codigo del kernel corre en CPL0.
        kernel_page_dir[i] = phys_base | PDE_PRESENT | PDE_RW | PDE_PS;
    }

    write_cr4(read_cr4() | PSE_BIT);
    write_cr3((uint32_t)kernel_page_dir);
    write_cr0(read_cr0() | PG_BIT);
}

uint32_t paging_kernel_dir_phys(void) {
    return (uint32_t)kernel_page_dir;
}

uint32_t paging_create_user_dir(void) {
    phys_addr_t pd_offset = page_alloc(1);
    if (pd_offset == 0) return 0;

    uint32_t pd_phys = (uint32_t)arena_base + pd_offset;
    uint32_t* pd = (uint32_t*)virt_of_phys_addr(pd_phys);
    memset(pd, 0, 4096);

    // Copiar las entradas de kernel (identidad, supervisor) para que las
    // syscalls/interrupciones puedan volver al codigo/datos del kernel.
    for (uint32_t i = 0; i < IDENTITY_ENTRIES; i++) {
        pd[i] = kernel_page_dir[i];
    }

    return pd_phys;
}

bool paging_map(uint32_t pd_phys, uint32_t vaddr, uint32_t paddr,
                size_t num_pages, uint32_t flags) {
    if (vaddr & 0xFFFu) return false;

    uint32_t* pd = (uint32_t*)virt_of_phys_addr(pd_phys);

    for (size_t n = 0; n < num_pages; n++) {
        uint32_t va = vaddr + n * PAGE_SIZE;
        uint32_t pa = (paddr + n * PAGE_SIZE) & PAGE_MASK;

        uint32_t pd_idx = va / 0x400000u;
        uint32_t pt_idx = (va % 0x400000u) / PAGE_SIZE;

        if (!(pd[pd_idx] & PDE_PRESENT)) {
            phys_addr_t pt_offset = page_alloc(1);
            if (pt_offset == 0) return false;
            uint32_t pt_phys = (uint32_t)arena_base + pt_offset;
            uint32_t* pt = (uint32_t*)virt_of_phys_addr(pt_phys);
            memset(pt, 0, 4096);
            pd[pd_idx] = pt_phys | PDE_PRESENT | PDE_RW | PDE_US;
        }

        uint32_t pt_phys = pd[pd_idx] & PAGE_MASK;
        uint32_t* pt = (uint32_t*)virt_of_phys_addr(pt_phys);
        pt[pt_idx] = pa | flags | PTE_PRESENT;
    }

    return true;
}

void paging_switch_dir(uint32_t pd_phys) {
    write_cr3(pd_phys);
}
