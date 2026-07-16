// typesafe.h - Tipos que previenen errores por diseño
#ifndef TYPESAFE_H
#define TYPESAFE_H

#include "types.h"
#include "hal.h"

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

// ============================================================
// Option<T> - Nunca un puntero nulo sin manejar
// ============================================================
#define Option(T) struct { T value; bool is_some; }

#define Some(v) { .value = (v), .is_some = true }
#define None    { .is_some = false }

#define unwrap(opt) \
    ({ __typeof__(opt) _o = (opt); \
       if (!_o.is_some) { panic("unwrap on None at " __FILE__ ":" STRINGIFY(__LINE__)); } \
       _o.value; })

#define unwrap_or(opt, def) \
    ((opt).is_some ? (opt).value : (def))

// ============================================================
// Result<T, E> - Errores explícitos, nunca silenciosos
// ============================================================
#define Result(T, E) struct { T value; E error; bool is_ok; }

#define Ok(v)  { .value = (v), .is_ok = true }
#define Err(e) { .error = (e), .is_ok = false }

// ============================================================
// NonNull<T> - Puntero que nunca es NULL por construcción
// ============================================================
typedef struct { void* ptr; } NonNull_void;

static inline NonNull_void NonNull_new(void* p) {
    if (p == NULL) panic("NonNull constructed with NULL");
    return (NonNull_void){ .ptr = p };
}

static inline void* NonNull_deref(NonNull_void n) {
    return n.ptr;
}

// ============================================================
// Region - Allocator por scope, nunca leak
// ============================================================
typedef struct Region Region;

Region* region_new(size_t capacity);
void* region_alloc(Region* r, size_t size);
void region_destroy(Region* r);

// ============================================================
// FixedArray<T, N> - Array con tamaño fijo en tiempo de compilación
// ============================================================
#define FixedArray(T, N) struct { T data[N]; size_t len; size_t cap; }

#define FixedArray_push(arr, item) \
    do { \
        if ((arr)->len >= (arr)->cap) panic("FixedArray overflow at " __FILE__ ":" STRINGIFY(__LINE__)); \
        (arr)->data[(arr)->len++] = (item); \
    } while (0)

// ============================================================
// SpinLock - En UP (single core) actúa como guardia de secciones críticas
// ============================================================
typedef struct {
    volatile bool locked;
    const char* file;
    int line;
} SpinLock;

#define spin_lock(l)   _spin_lock((l), __FILE__, __LINE__)
#define spin_unlock(l) _spin_unlock((l))

void _spin_lock(SpinLock* l, const char* file, int line);
void _spin_unlock(SpinLock* l);

// ============================================================
// IRQ-safe functions marker
// ============================================================
#define IRQ_FUNC __attribute__((section(".irq_funcs")))

#endif // TYPESAFE_H
