// paging.c - Memoria virtual real con espacio de direcciones POR PROCESO.
//
// Cada proceso ring3 tiene su propio directorio+tablas de página (copia
// completa del identity-map, todo supervisor-only al arrancar). Un
// proceso solo gana acceso a SU PROPIA región vía paging_grant_access() —
// como cada proceso tiene su PROPIA copia de las tablas, darle acceso a
// uno no le da acceso a ningún otro. Antes de este cambio, todos los
// procesos ring3 compartían las mismas tablas globales: el kernel estaba
// protegido, pero los procesos ring3 SÍ podían verse entre sí. Ya no.
//
// La lógica de índices/aislamiento de este archivo está validada aparte
// con AddressSanitizer/UBSan en un harness "hosted" (ver
// paging_test/paging_logic.c en el repo de pruebas) antes de integrarse
// acá — 5 casos, incluido aislamiento cruzado entre procesos y accesos
// fuera de rango, todos limpios.

#include "types.h"
#include "hal.h"

#define PG_BIT    (1u << 31)  // CR0.PG

#define PTE_PRESENT  (1u << 0)
#define PTE_RW       (1u << 1)
#define PTE_US       (1u << 2)  // 0 = solo supervisor (ring0), 1 = accesible desde ring3

#define IDENTITY_MAP_MB   32u
#define NUM_PAGE_TABLES    (IDENTITY_MAP_MB / 4)   // cada tabla cubre 4MB (1024 * 4KB)
#define PAGES_PER_TABLE    1024
#define PAGE_SIZE_4K       4096u
#define MAX_ADDR_SPACES    8   // uno por proceso ring3 (ver MAX_PROCESSES en process.c)

typedef struct {
    uint32_t directory[1024] __attribute__((aligned(4096)));
    uint32_t tables[NUM_PAGE_TABLES][PAGES_PER_TABLE] __attribute__((aligned(4096)));
    bool in_use;
} AddressSpace;

// El espacio de kernel es el "por defecto": todos los procesos ring0
// (cooperativos, sin CPL3) lo comparten, igual que antes de este cambio.
static AddressSpace kernel_space;
static AddressSpace proc_spaces[MAX_ADDR_SPACES];

// Rango de codigo compartido (.text/.rodata) que se le concede a TODO
// espacio de direcciones nuevo automaticamente — ver comentario en
// kernel.c sobre por que hace falta (no hay loader de programas separado
// todavia, el codigo de los procesos ring3 vive en el mismo binario).
static uint32_t shared_code_start = 0;
static uint32_t shared_code_size = 0;

static inline void write_cr3(uint32_t v) { __asm__ volatile ("mov %0, %%cr3" :: "r"(v) : "memory"); }
static inline uint32_t read_cr0(void) { uint32_t v; __asm__ volatile ("mov %%cr0, %0" : "=r"(v)); return v; }
static inline void write_cr0(uint32_t v) { __asm__ volatile ("mov %0, %%cr0" :: "r"(v) : "memory"); }

uint32_t read_cr2(void) {
    uint32_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

static inline void invlpg(uint32_t addr) {
    __asm__ volatile ("invlpg (%0)" :: "r"(addr) : "memory");
}

static void fill_identity(AddressSpace* sp) {
    for (uint32_t pt = 0; pt < NUM_PAGE_TABLES; pt++) {
        for (uint32_t i = 0; i < PAGES_PER_TABLE; i++) {
            uint32_t phys = pt * 0x400000u + i * PAGE_SIZE_4K;
            sp->tables[pt][i] = phys | PTE_PRESENT | PTE_RW; // US=0: supervisor-only por defecto
        }
        // El directorio queda "permisivo" (US=1): el control real de
        // acceso pasa por el bit US de cada PTE individual, no del PDE.
        sp->directory[pt] = ((uint32_t)&sp->tables[pt]) | PTE_PRESENT | PTE_RW | PTE_US;
    }
    for (int i = NUM_PAGE_TABLES; i < 1024; i++) sp->directory[i] = 0;
}

static AddressSpace* space_for(int handle) {
    if (handle < 0 || handle >= MAX_ADDR_SPACES) return &kernel_space;
    return &proc_spaces[handle];
}

static void grant_in(AddressSpace* sp, uint32_t phys_start, uint32_t size, bool user) {
    uint32_t start_page = phys_start / PAGE_SIZE_4K;
    uint32_t end_page = (phys_start + size + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K;
    for (uint32_t p = start_page; p < end_page; p++) {
        uint32_t pt_idx = p / PAGES_PER_TABLE;
        uint32_t pte_idx = p % PAGES_PER_TABLE;
        if (pt_idx >= NUM_PAGE_TABLES) break; // fuera del rango identity-mapeado

        if (user) sp->tables[pt_idx][pte_idx] |= PTE_US;
        else sp->tables[pt_idx][pte_idx] &= ~(uint32_t)PTE_US;

        invlpg(p * PAGE_SIZE_4K);
    }
}

void paging_init(void) {
    fill_identity(&kernel_space);
    for (int i = 0; i < MAX_ADDR_SPACES; i++) proc_spaces[i].in_use = false;

    write_cr3((uint32_t)&kernel_space.directory);
    write_cr0(read_cr0() | PG_BIT);
}

// Registra el rango .text/.rodata compartido — se aplica automaticamente
// a todo espacio de direcciones que se cree de ahora en mas.
void paging_set_shared_code_range(uint32_t start, uint32_t size) {
    shared_code_start = start;
    shared_code_size = size;
    grant_in(&kernel_space, start, size, true);
}

// Crea un espacio de direcciones nuevo (copia completa del identity-map,
// todo supervisor-only salvo el rango de codigo compartido). Devuelve un
// handle >= 0, o -1 si no hay slots libres.
int paging_new_address_space(void) {
    for (int i = 0; i < MAX_ADDR_SPACES; i++) {
        if (!proc_spaces[i].in_use) {
            fill_identity(&proc_spaces[i]);
            proc_spaces[i].in_use = true;
            if (shared_code_size > 0) {
                grant_in(&proc_spaces[i], shared_code_start, shared_code_size, true);
            }
            return i;
        }
    }
    return -1;
}

void paging_free_address_space(int handle) {
    if (handle < 0 || handle >= MAX_ADDR_SPACES) return;
    proc_spaces[handle].in_use = false;
}

uint32_t paging_address_space_cr3(int handle) {
    return (uint32_t)&space_for(handle)->directory;
}

// Da (o quita) acceso desde ring3 a [phys_start, phys_start+size) SOLO
// dentro del espacio de direcciones `handle` (-1 = el compartido de
// kernel). Como cada proceso ring3 tiene su PROPIA copia de las tablas,
// esto no afecta a ningun otro proceso.
void paging_grant_access(int handle, uint32_t phys_start, uint32_t size, bool user_accessible) {
    grant_in(space_for(handle), phys_start, size, user_accessible);
}

void paging_switch_address_space(int handle) {
    write_cr3(paging_address_space_cr3(handle));
}

// ============================================================
// paging_map_page() - A diferencia de grant_in() (que solo prende/apaga
// el bit US sobre el identity-map fijo), esto REDIRIGE una direccion
// virtual a un frame fisico distinto SOLO dentro del espacio `handle`.
// Es lo que permite que el codigo de un programa cargado desde ELF viva
// en paginas propias (no identity, no compartidas con el kernel ni con
// otros procesos) aunque dos procesos usen la MISMA direccion virtual de
// carga — cada uno tiene su propia copia de `tables[][]`, asi que apuntan
// a frames fisicos distintos sin saberlo el uno del otro.
//
// Restriccion actual: `virt` debe caer dentro del rango identity-mapeado
// (< IDENTITY_MAP_MB), porque las tablas de pagina de este diseño solo
// cubren ese rango. Suficiente para un kernel didactico; un diseño de
// produccion reservaria un rango virtual aparte para "espacio de usuario".
// ============================================================
bool paging_map_page(int handle, uint32_t virt, uint32_t phys, bool user, bool writable) {
    AddressSpace* sp = space_for(handle);
    uint32_t page = virt / PAGE_SIZE_4K;
    uint32_t pt_idx = page / PAGES_PER_TABLE;
    uint32_t pte_idx = page % PAGES_PER_TABLE;
    if (pt_idx >= NUM_PAGE_TABLES) return false; // fuera del rango que este kernel puede mapear

    uint32_t flags = PTE_PRESENT | (writable ? PTE_RW : 0) | (user ? PTE_US : 0);
    sp->tables[pt_idx][pte_idx] = (phys & ~0xFFFu) | flags;
    invlpg(virt);
    return true;
}

// Compatibilidad con el codigo existente que todavia llama a la version
// vieja (sin handle): opera sobre el espacio de kernel compartido.
void paging_set_user_access(uint32_t phys_start, uint32_t size, bool user_accessible) {
    paging_grant_access(-1, phys_start, size, user_accessible);
}
