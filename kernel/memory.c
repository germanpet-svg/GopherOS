// memory.c - Region-based allocation, nunca leaks
//
// Modelo simplificado: kernel identity-mapeado (sin paging real), toda la
// memoria física disponible se sirve desde un arena estático reservado en
// el binario del kernel (suficiente para una demo/OS didáctico).

#include "types.h"
#include "hal.h"
#include "string.h"
#include "typesafe.h"

#define ARENA_PAGES  2048                 // 8 MB de RAM gestionable
#define ARENA_BYTES  (ARENA_PAGES * PAGE_SIZE)

static uint8_t arena[ARENA_BYTES] __attribute__((aligned(4096)));
static uint8_t page_bitmap[ARENA_PAGES];  // 1 = usada
static size_t next_free_hint = 0;

void memory_init(void) {
    memset(page_bitmap, 0, sizeof(page_bitmap));
    // Reservamos la página 0: así page_alloc() nunca devuelve 0 como
    // dirección física válida, y podemos usar 0 como centinela de fallo
    // sin ambigüedad (offset 0 sería, si no, indistinguible de "sin memoria").
    page_bitmap[0] = 1;
    next_free_hint = 1;
}

// Devuelve "dirección física" (offset dentro del arena) o 0 si no hay espacio.
phys_addr_t page_alloc(size_t num_pages) {
    if (num_pages == 0) return 0;
    for (size_t start = next_free_hint; start + num_pages <= ARENA_PAGES; start++) {
        bool ok = true;
        for (size_t i = 0; i < num_pages; i++) {
            if (page_bitmap[start + i]) { ok = false; break; }
        }
        if (ok) {
            for (size_t i = 0; i < num_pages; i++) page_bitmap[start + i] = 1;
            next_free_hint = start + num_pages;
            return (phys_addr_t)(start * PAGE_SIZE);
        }
    }
    // No encontrado desde el hint: reintentar desde el inicio una vez.
    if (next_free_hint != 0) {
        next_free_hint = 0;
        return page_alloc(num_pages);
    }
    return 0;
}

void page_free(phys_addr_t addr, size_t num_pages) {
    size_t start = addr / PAGE_SIZE;
    if (start + num_pages > ARENA_PAGES) panic("page_free out of range");
    for (size_t i = 0; i < num_pages; i++) page_bitmap[start + i] = 0;
    if (start < next_free_hint) next_free_hint = start;
}

static inline void* phys_to_virt(phys_addr_t p) {
    return (void*)(arena + p);
}

// paging_init() ahora vive en paging.c (implementación real con PSE).

// ============================================================
// Pool allocator para objetos pequeños (slab-style)
// ============================================================
#define POOL_SIZE_16    0
#define POOL_SIZE_32    1
#define POOL_SIZE_64    2
#define POOL_SIZE_128   3
#define POOL_SIZE_256   4
#define POOL_SIZE_512   5
#define POOL_SIZE_1024  6
#define POOL_SIZE_2048  7
#define NUM_POOLS       8
#define POOL_ARENA_PAGES 4  // páginas físicas por pool bajo demanda

typedef struct {
    uint8_t* base;
    size_t slot_size;
    size_t num_slots;
    uint8_t* used_bitmap;
    SpinLock lock;
} Pool;

static Pool pools[NUM_POOLS] = {
    [POOL_SIZE_16]   = { .slot_size = 16 },
    [POOL_SIZE_32]   = { .slot_size = 32 },
    [POOL_SIZE_64]   = { .slot_size = 64 },
    [POOL_SIZE_128]  = { .slot_size = 128 },
    [POOL_SIZE_256]  = { .slot_size = 256 },
    [POOL_SIZE_512]  = { .slot_size = 512 },
    [POOL_SIZE_1024] = { .slot_size = 1024 },
    [POOL_SIZE_2048] = { .slot_size = 2048 },
};

void* kmalloc_bootstrap(size_t size);

static bool pool_ensure_backing(Pool* p) {
    if (p->base) return true;
    phys_addr_t phys = page_alloc(POOL_ARENA_PAGES);
    if (phys == 0) return false;
    p->base = (uint8_t*)phys_to_virt(phys);
    p->num_slots = (POOL_ARENA_PAGES * PAGE_SIZE) / p->slot_size;
    p->used_bitmap = (uint8_t*)kmalloc_bootstrap(p->num_slots);
    memset(p->used_bitmap, 0, p->num_slots);
    return true;
}

// Bootstrap allocator muy simple para metadata interna (bitmaps de pools).
// Usa su propio arena estático separado del de páginas.
#define BOOTSTRAP_BYTES (64 * 1024)
static uint8_t bootstrap_arena[BOOTSTRAP_BYTES];
static size_t bootstrap_used = 0;

void* kmalloc_bootstrap(size_t size) {
    size = ALIGN(size, 8);
    if (bootstrap_used + size > BOOTSTRAP_BYTES) panic("bootstrap arena exhausted");
    void* p = bootstrap_arena + bootstrap_used;
    bootstrap_used += size;
    return p;
}

void* pool_alloc(int pool_idx) {
    Pool* p = &pools[pool_idx];
    spin_lock(&p->lock);
    if (!pool_ensure_backing(p)) { spin_unlock(&p->lock); return NULL; }

    for (size_t i = 0; i < p->num_slots; i++) {
        if (!p->used_bitmap[i]) {
            p->used_bitmap[i] = 1;
            void* ptr = p->base + i * p->slot_size;
            memset(ptr, 0, p->slot_size);
            spin_unlock(&p->lock);
            return ptr;
        }
    }
    spin_unlock(&p->lock);
    return NULL; // pool lleno
}

void pool_free(void* ptr) {
    for (int pi = 0; pi < NUM_POOLS; pi++) {
        Pool* p = &pools[pi];
        if (!p->base) continue;
        uint8_t* base = p->base;
        size_t region_size = p->num_slots * p->slot_size;
        if ((uint8_t*)ptr >= base && (uint8_t*)ptr < base + region_size) {
            size_t idx = ((uint8_t*)ptr - base) / p->slot_size;
            p->used_bitmap[idx] = 0;
            return;
        }
    }
}

// ============================================================
// Region allocator - El default para todo
// ============================================================
struct Region {
    uint8_t* base;
    size_t used;
    size_t capacity;
    phys_addr_t backing_phys;
    size_t backing_pages;
    struct Region* parent;
};

Region* region_new(size_t capacity) {
    size_t total = capacity + sizeof(Region);
    size_t num_pages = ALIGN(total, PAGE_SIZE) / PAGE_SIZE;

    phys_addr_t phys = page_alloc(num_pages);
    if (phys == 0) return NULL;

    Region* r = (Region*)phys_to_virt(phys);
    r->base = (uint8_t*)r + sizeof(Region);
    r->used = 0;
    r->capacity = (num_pages * PAGE_SIZE) - sizeof(Region);
    r->backing_phys = phys;
    r->backing_pages = num_pages;
    r->parent = NULL;

    return r;
}

void* region_alloc(Region* r, size_t size) {
    if (r == NULL) panic("region_alloc with NULL region");

    size = ALIGN(size, 8);
    if (r->used + size > r->capacity) {
        return NULL; // política: nunca swap, el llamador decide qué hacer
    }

    void* p = r->base + r->used;
    r->used += size;
    memset(p, 0, size);
    return p;
}

void region_destroy(Region* r) {
    if (r == NULL) return;
    page_free(r->backing_phys, r->backing_pages);
}

// ============================================================
// kmalloc/kfree - SOLO para uso interno del kernel, nunca drivers
// ============================================================
void* kmalloc(size_t size) {
    int pool_idx = -1;
    for (int i = 0; i < NUM_POOLS; i++) {
        if (pools[i].slot_size >= size) { pool_idx = i; break; }
    }
    if (pool_idx < 0) return NULL; // demasiado grande: usar regiones
    return pool_alloc(pool_idx);
}

void kfree(void* p) {
    if (p == NULL) return;
    pool_free(p);
}
