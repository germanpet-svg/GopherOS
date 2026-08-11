// syscall.c - 15 syscalls, nunca más, nunca menos
// ABI: eax = número de syscall, ebx/ecx/edx/esi/edi = argumentos
// Retorno en eax. Si necesitamos más funcionalidad: ioctl con subcomandos.

#include "types.h"
#include "descriptors.h"
#include "vga.h"
#include "string.h"
#include "rtc.h"

#define NUM_SYSCALLS 15

typedef uint32_t (*SyscallFn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

extern void proc_exit(int code);
extern void proc_yield(void);
extern uint32_t proc_current_pid(void);
extern void* kmalloc(size_t size);
extern void kfree(void* p);
extern int proc_current_addr_space(void);
extern void paging_grant_access(int handle, uint32_t phys_start, uint32_t size, bool user_accessible);

static uint32_t sys_exit(uint32_t code, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    proc_exit((int)code);
    return 0;
}

static uint32_t sys_fork(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)GOS_ENOSYS; // fork completo no implementado en esta demo
}

extern bool kb_haschar(void);
extern char kb_getchar(void);

static uint32_t sys_read(uint32_t fd, uint32_t buf, uint32_t count, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (fd != 0) return (uint32_t)GOS_EINVAL; // solo stdin
    char* dst = (char*)buf;
    uint32_t n = 0;
    while (n < count && kb_haschar()) {
        dst[n++] = kb_getchar();
    }
    return n;
}

static uint32_t sys_write(uint32_t fd, uint32_t buf, uint32_t count, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (fd != 1 && fd != 2) return (uint32_t)GOS_EINVAL; // solo stdout/stderr
    // vga_putc() ya espeja a COM1 internamente (ver vga.c) — no hace falta
    // (ni conviene) duplicarlo aca.
    const char* s = (const char*)buf;
    for (uint32_t i = 0; i < count; i++) vga_putc(s[i]);
    return count;
}

static uint32_t sys_open(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)GOS_ENOSYS;
}

static uint32_t sys_close(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return 0;
}

static uint32_t sys_socket(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)GOS_ENOSYS; // no hay NIC driver en esta demo
}

static uint32_t sys_send(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)GOS_ENOSYS;
}

static uint32_t sys_recv(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)GOS_ENOSYS;
}

static uint32_t sys_malloc(uint32_t size, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    void* ptr = kmalloc((size_t)size);
    if (ptr && size > 0) {
        // BUG real encontrado probando calc.elf: kmalloc() devuelve una
        // direccion del identity-map, que por defecto es supervisor-only
        // (US=0, ver fill_identity en paging.c) en TODO espacio de
        // direcciones nuevo. Sin este grant, un proceso ring3 que hace
        // malloc() y despues escribe en el puntero se cae con Page Fault
        // al primer acceso — exactamente lo que le paso a calc.elf antes
        // de este fix.
        //
        // LIMITACION CONOCIDA (no resuelta aca, documentada a proposito):
        // el permiso se otorga a nivel de PAGINA (4KB), y los "pools" de
        // kmalloc empaquetan varios slots chicos por pagina (ver
        // memory.c). Si un proceso ring3 obtiene acceso US=1 a una
        // pagina compartida del pool, tambien queda con acceso de
        // lectura/escritura a los OTROS slots de esa misma pagina —
        // incluyendo memoria que podria pertenecer a otro proceso o al
        // kernel. Un heap de usuario separado del heap interno del
        // kernel resolveria esto; no esta implementado todavia. Tampoco
        // se revoca el acceso en sys_free() (kfree no conoce el tamano
        // del bloque). Aceptable para esta demo, no para produccion.
        int space = proc_current_addr_space();
        if (space >= 0) {
            paging_grant_access(space, (uint32_t)(uintptr_t)ptr, (uint32_t)size, true);
        }
    }
    return (uint32_t)(uintptr_t)ptr;
}

static uint32_t sys_free(uint32_t ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    kfree((void*)ptr);
    return 0;
}

// Subcomandos de ioctl (ver gopheros_abi.h para la version "publica" de
// estas mismas constantes, pensada para programas fuera del kernel).
#define IOCTL_VIDEO_SET_MODE  1  // arg1 = modo (0=texto 80x25, 1=grafico 320x200)
#define IOCTL_VIDEO_PUTPIXEL  2  // arg1=x, arg2=y, arg3=color
#define IOCTL_VIDEO_CLEAR     3  // arg1 = color
#define IOCTL_RTC_READ        4  // arg1 = puntero a gos_datetime_t (6 bytes: y,mo,d,h,mi,s)
#define IOCTL_DEBUG_GET_ARG   5  // solo pruebas: devuelve debug_test_arg

extern void gfx_set_mode(int mode);
extern void gfx_putpixel(int x, int y, uint8_t color);
extern void gfx_clear(uint8_t color);
extern void rtc_read(rtc_datetime_t* out);

static uint32_t sys_ioctl(uint32_t request, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t e) {
    (void)e;
    switch (request) {
        case IOCTL_VIDEO_SET_MODE:
            gfx_set_mode((int)arg1);
            return 0;
        case IOCTL_VIDEO_PUTPIXEL:
            gfx_putpixel((int)arg1, (int)arg2, (uint8_t)arg3);
            return 0;
        case IOCTL_VIDEO_CLEAR:
            gfx_clear((uint8_t)arg1);
            return 0;
        case IOCTL_RTC_READ:
            if (arg1 == 0) return (uint32_t)GOS_EINVAL;
            rtc_read((rtc_datetime_t*)arg1);
            return 0;
        case IOCTL_DEBUG_GET_ARG: {
            extern uint32_t debug_get_test_arg(void);
            return debug_get_test_arg();
        }
        default:
            return (uint32_t)GOS_ENOSYS;
    }
}

static uint32_t sys_mmap(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)GOS_ENOSYS;
}

static uint32_t sys_munmap(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return 0;
}

static uint32_t sys_yield(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    proc_yield();
    return 0;
}

static uint32_t sys_getpid(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return proc_current_pid();
}

static SyscallFn syscall_table[NUM_SYSCALLS] = {
    [0]  = sys_exit,
    [1]  = sys_fork,
    [2]  = sys_read,
    [3]  = sys_write,
    [4]  = sys_open,
    [5]  = sys_close,
    [6]  = sys_socket,
    [7]  = sys_send,
    [8]  = sys_recv,
    [9]  = sys_malloc,
    [10] = sys_free,
    [11] = sys_ioctl,
    [12] = sys_mmap,
    [13] = sys_munmap,
    [14] = sys_yield,
};

void syscall_init(void) {
    // Nada que hacer: la tabla ya está lista; INT 0x80 fue instalado en idt_init().
}

// sys_getpid no forma parte del ABI original de 15 syscalls; se expone
// aparte vía ioctl-style helper para no romper la tabla estable v1.0.
uint32_t syscall_getpid_helper(void) { return sys_getpid(0,0,0,0,0); }

// Llamado desde isr_handler (idt.c) cuando int_no == 0x80
void syscall_dispatch(struct registers* r) {
    uint32_t num = r->eax;
    if (num >= NUM_SYSCALLS || syscall_table[num] == NULL) {
        r->eax = (uint32_t)GOS_EINVAL;
        return;
    }
    r->eax = syscall_table[num](r->ebx, r->ecx, r->edx, r->esi, r->edi);
}
