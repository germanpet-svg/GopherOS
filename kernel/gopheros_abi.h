// gopheros_abi.h - La UNICA interfaz que un programa de GopherOS necesita.
//
// Filosofía: "todo lo interpreta el kernel". Un programa (GWBASIC portado,
// un juego, una utilidad) NO debe enlazar libc, ni una libreria grafica, ni
// nada — solo incluye este header de un solo archivo, sin dependencias, y
// llama a estas 15 funciones (mas los subcomandos de ioctl). Poner un solo
// punto en pantalla es UNA instruccion "int 0x80", no una cadena de
// abstracciones (libSDL -> X11/DRM -> framebuffer -> ...).
//
// Cero .c requerido: todo son "static inline" con un solo "int 0x80" cada
// uno, así que este header no deja símbolos que enlazar ni arrastra libc.
//
// Compatibilidad: hoy los "programas" corren como hilos cooperativos en
// modo kernel (creados con proc_create) y ya pueden usar int 0x80 sin
// problema (CPL0 puede invocar una puerta con DPL=3 sin restricciones).
// El día que haya procesos ring3 reales, este mismo header sigue sirviendo
// sin cambios: la ABI (numero de syscall en eax, argumentos en
// ebx/ecx/edx/esi/edi) es la frontera estable entre "programa" y "kernel".

#ifndef GOPHEROS_ABI_H
#define GOPHEROS_ABI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================
// Números de syscall (ABI v1.0 — estable, 15 y nunca más; ver syscall.c)
// ============================================================
#define GOS_SYS_EXIT     0
#define GOS_SYS_FORK     1
#define GOS_SYS_READ     2
#define GOS_SYS_WRITE    3
#define GOS_SYS_OPEN     4
#define GOS_SYS_CLOSE    5
#define GOS_SYS_SOCKET   6
#define GOS_SYS_SEND     7
#define GOS_SYS_RECV     8
#define GOS_SYS_MALLOC   9
#define GOS_SYS_FREE     10
#define GOS_SYS_IOCTL    11
#define GOS_SYS_MMAP     12
#define GOS_SYS_MUNMAP   13
#define GOS_SYS_YIELD    14

// Modos para gos_open(). Coinciden con los usados internamente por el kernel.
#define GOS_O_RDONLY 0
#define GOS_O_WRONLY 1
#define GOS_O_RDWR   2
#define GOS_O_CREAT  4
#define GOS_O_TRUNC  8

// Subcomandos de GOS_SYS_IOCTL — el mecanismo para crecer sin romper la
// regla de "15 syscalls". Cualquier dispositivo/servicio nuevo (video,
// RTC, y lo que siga: sonido, disco...) entra por acá, nunca como un
// numero de syscall nuevo.
#define GOS_IOCTL_VIDEO_SET_MODE  1  // arg1: 0=texto 80x25, 1=grafico 320x200x256
#define GOS_IOCTL_VIDEO_PUTPIXEL  2  // arg1=x, arg2=y, arg3=color (0-255, paleta VGA)
#define GOS_IOCTL_VIDEO_CLEAR     3  // arg1=color
#define GOS_IOCTL_RTC_READ        4  // arg1=puntero a gos_datetime_t
#define GOS_IOCTL_GETPID          5  // sin argumentos; retorna el pid

#define GOS_VIDEO_MODE_TEXT   0
#define GOS_VIDEO_MODE_VGA256 1

typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
} gos_datetime_t;

// ============================================================
// Trampolín genérico: un solo "int 0x80" para las 15 syscalls.
// Devuelve lo que el kernel puso en eax (0 = OK, o un codigo gos_result_t
// negativo-como-uint si algo fallo: ver GOS_E* en types.h del kernel).
// ============================================================
static inline uint32_t gos_syscall(uint32_t num, uint32_t a, uint32_t b,
                                    uint32_t c, uint32_t d) {
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d)
        : "memory"
    );
    return ret;
}

// ============================================================
// Wrappers con nombre — esto es TODO lo que un programa necesita conocer.
// ============================================================
static inline void gos_exit(int code) {
    gos_syscall(GOS_SYS_EXIT, (uint32_t)code, 0, 0, 0);
}

static inline uint32_t gos_write(int fd, const void* buf, size_t len) {
    return gos_syscall(GOS_SYS_WRITE, (uint32_t)fd, (uint32_t)(uintptr_t)buf, (uint32_t)len, 0);
}

static inline uint32_t gos_read(int fd, void* buf, size_t len) {
    return gos_syscall(GOS_SYS_READ, (uint32_t)fd, (uint32_t)(uintptr_t)buf, (uint32_t)len, 0);
}

static inline void gos_yield(void) {
    gos_syscall(GOS_SYS_YIELD, 0, 0, 0, 0);
}

// open() y close() usan descriptores de archivos por proceso.
// Devuelven >= 0 (fd) en exito, o un codigo de error negativo si fallan.
static inline int gos_open(const char* path, int flags) {
    return (int)gos_syscall(GOS_SYS_OPEN, (uint32_t)(uintptr_t)path, (uint32_t)flags, 0, 0);
}

static inline int gos_close(int fd) {
    return (int)gos_syscall(GOS_SYS_CLOSE, (uint32_t)fd, 0, 0, 0);
}

static inline int gos_getpid(void) {
    return (int)gos_syscall(GOS_SYS_IOCTL, GOS_IOCTL_GETPID, 0, 0, 0);
}

static inline void* gos_malloc(size_t size) {
    return (void*)(uintptr_t)gos_syscall(GOS_SYS_MALLOC, (uint32_t)size, 0, 0, 0);
}

static inline void gos_free(void* ptr) {
    gos_syscall(GOS_SYS_FREE, (uint32_t)(uintptr_t)ptr, 0, 0, 0);
}

// --- "poner un punto en pantalla", de punta a punta, en dos líneas: ---
static inline void gos_screen_mode(int mode) {
    gos_syscall(GOS_SYS_IOCTL, GOS_IOCTL_VIDEO_SET_MODE, (uint32_t)mode, 0, 0);
}

static inline void gos_pset(int x, int y, uint8_t color) {
    gos_syscall(GOS_SYS_IOCTL, GOS_IOCTL_VIDEO_PUTPIXEL, (uint32_t)x, (uint32_t)y, color);
}

static inline void gos_cls_graphics(uint8_t color) {
    gos_syscall(GOS_SYS_IOCTL, GOS_IOCTL_VIDEO_CLEAR, color, 0, 0);
}

static inline void gos_date(gos_datetime_t* out) {
    gos_syscall(GOS_SYS_IOCTL, GOS_IOCTL_RTC_READ, (uint32_t)(uintptr_t)out, 0, 0);
}

// --- helpers de texto mínimos, para no necesitar ni siquiera printf ---
static inline size_t gos_strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

// print() al estilo Python: agrega salto de linea automatico.
static inline void gos_print(const char* s) {
    gos_write(1, s, gos_strlen(s));
    gos_write(1, "\n", 1);
}

// Imprime un entero con signo sin depender de ninguna libreria (itoa a mano,
// coherente con el espiritu de "cero dependencias" de este header).
static inline void gos_print_int(int32_t val) {
    char digits[12];
    int n = 0;
    bool neg = val < 0;
    uint32_t v = neg ? (uint32_t)(-(int64_t)val) : (uint32_t)val;

    if (v == 0) digits[n++] = '0';
    while (v > 0) {
        digits[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    if (neg) digits[n++] = '-';

    char out[13];
    for (int i = 0; i < n; i++) out[i] = digits[n - 1 - i];
    out[n] = '\n';
    gos_write(1, out, (size_t)(n + 1));
}

#endif // GOPHEROS_ABI_H
