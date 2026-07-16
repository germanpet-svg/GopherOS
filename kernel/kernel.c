// kernel.c - Entry point de GopherOS
// Llamado desde boot/boot.s (Multiboot) en modo protegido de 32 bits.

#include "types.h"
#include "hal.h"
#include "typesafe.h"
#include "vga.h"
#include "string.h"
#include "kresults.h"
#include "filesystem.h"
#include "disk.h"

extern void memory_init(void);
extern phys_addr_t page_alloc(size_t num_pages);
extern void syscall_init(void);
extern proc_result_t proc_create(const char* name, void (*entry)(void));
extern _Noreturn void scheduler_run(void);
extern void proc_yield(void);
extern const char* proc_current_name(void);
extern uint32_t proc_current_pid(void);
extern volatile uint32_t jiffies;

extern void serial_init(void);
extern void serial_puts(const char* s);
extern void serial_putc(char c);

_Noreturn void panic(const char* msg) {
    irq_disable();
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_puts("\n\n*** KERNEL PANIC ***\n");
    vga_puts(msg);
    vga_puts("\nSistema detenido.\n");
    for (;;) cpu_halt();
}

// ============================================================
// Proceso "gopherd" - demo del servidor gopher/init
// ============================================================
static Region* demo_region = NULL;

static void gopherd_main(void) {
    vga_set_color(VGA_LGREEN, VGA_BLACK);
    vga_puts("[gopherd] iniciado (pid=");
    vga_put_dec((int32_t)proc_current_pid());
    vga_puts(")\n");

    demo_region = region_new(8192);
    if (demo_region) {
        fs_create_result_t f = fs_create("/gopher/index.txt", demo_region);
        if (f.is_ok) {
            const char* contenido = "Bienvenido a GopherOS\n";
            fs_append(f.value, (const uint8_t*)contenido, strlen(contenido), demo_region);
            vga_puts("[gopherd] archivo /gopher/index.txt creado (");
            vga_put_dec((int32_t)strlen(contenido));
            vga_puts(" bytes)\n");
        }
    }

    for (int i = 0; i < 3; i++) {
        vga_puts("[gopherd] tick, cediendo CPU...\n");
        proc_yield();
    }

    vga_puts("[gopherd] terminando.\n");
    extern void proc_exit(int code);
    proc_exit(0);
}

// ============================================================
// Proceso "shell" - consola interactiva por teclado (ver shell.c)
// ============================================================
extern void shell_main(void);

// ============================================================
// Kernel entry - Llamado desde boot.s
// ============================================================
void kernel_main(void) {
    irq_disable();

    extern void serial_init(void);
    serial_init();

    vga_init();
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("GopherOS - kernel didactico con seguridad por diseno\n");
    vga_set_color(VGA_LGRAY, VGA_BLACK);
    vga_puts("(Option/Result, region allocator, IRQ top/bottom-half,\n");
    vga_puts(" scheduler cooperativo, 15 syscalls estables)\n\n");

    gdt_init();
    idt_init();
    paging_init();
    memory_init();
    timer_init(100);
    extern void keyboard_init(void);
    keyboard_init();
    syscall_init();

    fs_init();

    // Cargar el programa de usuario .gxe embebido en el binario del kernel
    // para que el shell pueda ejecutarlo con 'ring3demo'.
    {
        extern uint8_t _binary_build_ring3demo_gxe_start[];
        extern uint8_t _binary_build_ring3demo_gxe_end[];
        size_t gxe_size = (size_t)(_binary_build_ring3demo_gxe_end - _binary_build_ring3demo_gxe_start);
        Region* init_region = region_new(65536);
        if (init_region && gxe_size > 0) {
            fs_create_result_t fr = fs_create("/ring3demo.gxe", init_region);
            if (fr.is_ok) {
                fs_append(fr.value, _binary_build_ring3demo_gxe_start, gxe_size, init_region);
            }
        }
    }

    bool have_disk = disk_init();
    vga_puts("[kernel] disco ATA: ");
    vga_puts(have_disk ? "detectado\n" : "no detectado (fs solo en RAM)\n");

    // Si hay disco con datos validos, cargar el filesystem automaticamente.
    if (have_disk) {
        Region* kernel_region = region_new(65536);
        if (kernel_region) {
            gos_result_t lr = fs_load(kernel_region);
            if (lr != GOS_OK) {
                region_destroy(kernel_region);
            } else {
                vga_puts("[kernel] filesystem cargado desde disco (LBA 2048)\n");
            }
        }
    }

    irq_enable();

    proc_result_t p1 = proc_create("gopherd", gopherd_main);
    if (!p1.is_ok) {
        panic("Fallo al crear proceso gopherd");
    }

    proc_result_t p2 = proc_create("shell", shell_main);
    if (!p2.is_ok) {
        panic("Fallo al crear proceso shell");
    }

    vga_puts("[kernel] procesos creados, arrancando scheduler...\n\n");

    scheduler_run(); // nunca retorna
}
