// kernel.c - Entry point de GopherOS
// Llamado desde boot/boot.s (Multiboot) en modo protegido de 32 bits.

#include "types.h"
#include "hal.h"
#include "typesafe.h"
#include "vga.h"
#include "string.h"
#include "kresults.h"

#include "filesystem.h"
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

    extern void fs_init(void);
    fs_init();

    // Deja disponibles los binarios ELF32 externos reales de userland/
    // (compilados APARTE del kernel — ver Makefile target 'userland') en
    // el fs de arranque, como pasaria con .COM/.EXE sueltos en el
    // directorio de un MS-DOS real. Cada uno se puede probar con
    // 'exec <archivo>' sin pasos manuales. Ver process.c:proc_load_elf().
    {
        extern const uint8_t hello_elf_blob[];
        extern const unsigned int hello_elf_blob_len;
        extern const uint8_t sysinfo_elf_blob[];
        extern const unsigned int sysinfo_elf_blob_len;
        extern const uint8_t counter_elf_blob[];
        extern const unsigned int counter_elf_blob_len;
        extern const uint8_t calc_elf_blob[];
        extern const unsigned int calc_elf_blob_len;
        extern const uint8_t stars_elf_blob[];
        extern const unsigned int stars_elf_blob_len;

        struct { const char* path; const uint8_t* data; unsigned int len; } progs[] = {
            { "/hello.elf",   hello_elf_blob,   hello_elf_blob_len },
            { "/sysinfo.elf", sysinfo_elf_blob, sysinfo_elf_blob_len },
            { "/counter.elf", counter_elf_blob, counter_elf_blob_len },
            { "/calc.elf",    calc_elf_blob,    calc_elf_blob_len },
            { "/stars.elf",   stars_elf_blob,   stars_elf_blob_len },
        };

        for (size_t i = 0; i < sizeof(progs) / sizeof(progs[0]); i++) {
            Region* prog_region = region_new(progs[i].len + 512);
            if (!prog_region) continue;
            fs_create_result_t pf = fs_create(progs[i].path, prog_region);
            if (pf.is_ok) {
                fs_append(pf.value, progs[i].data, progs[i].len, prog_region);
            }
        }
    }
    extern bool disk_init(void);
    bool have_disk = disk_init();
    vga_puts("[kernel] disco ATA: ");
    vga_puts(have_disk ? "detectado\n" : "no detectado (fs solo en RAM)\n");

    extern bool nic_init(void);
    extern void nic_get_mac(uint8_t mac[6]);
    bool have_nic = nic_init();
    vga_puts("[kernel] NIC (RTL8139): ");
    if (have_nic) {
        uint8_t mac[6];
        nic_get_mac(mac);
        vga_puts("detectada, MAC=");
        for (int i = 0; i < 6; i++) {
            char b[4];
            utoa(mac[i], b, 16);
            if (mac[i] < 16) vga_putc('0');
            vga_puts(b);
            if (i < 5) vga_putc(':');
        }
        vga_puts("\n");
    } else {
        vga_puts("no detectada\n");
    }
    if (have_nic) {
        extern void net_init(void);
        net_init();
    }

    // Los procesos ring3 todavia comparten el mismo binario del kernel (no
    // hay loader de programas separado), asi que su CODIGO vive en este
    // mismo .text/.rodata. Para que la CPU pueda siquiera *buscar* sus
    // instrucciones desde CPL3, este rango tiene que ser accesible — pero
    // SOLO codigo y constantes, nunca .data/.bss (ahi vive el estado real
    // del kernel: stacks, estructuras de procesos, etc, que sigue
    // protegido). Ver linker.ld para los simbolos _text_start/_text_end.
    extern uint8_t _text_start[];
    extern uint8_t _text_end[];
    paging_set_shared_code_range((uint32_t)_text_start, (uint32_t)(_text_end - _text_start));

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

    extern void statusbar_update(void);
    statusbar_update(); // pinta el pie de pagina desde el arranque, sin esperar el primer tick del timer

    scheduler_run(); // nunca retorna
}
