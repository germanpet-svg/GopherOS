#include "descriptors.h"
#include "hal.h"
#include "vga.h"

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

extern void* isr_stub_table[32];
extern void* irq_stub_table[16];

static void idt_set_gate(int n, uint32_t handler, uint16_t sel, uint8_t flags) {
    idt[n].offset_low  = handler & 0xFFFF;
    idt[n].offset_high = (handler >> 16) & 0xFFFF;
    idt[n].selector    = sel;
    idt[n].zero        = 0;
    idt[n].type_attr   = flags;
}

// ============================================================
// PIC 8259 remap: IRQ0-7 -> INT 32-39, IRQ8-15 -> INT 40-47
// ============================================================
#define PIC1 0x20
#define PIC2 0xA0
#define PIC1_DATA 0x21
#define PIC2_DATA 0xA1

static void pic_remap(void) {
    uint8_t m1 = inb(PIC1_DATA), m2 = inb(PIC2_DATA);

    outb(PIC1, 0x11); io_wait();
    outb(PIC2, 0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait(); // offset 32
    outb(PIC2_DATA, 0x28); io_wait(); // offset 40
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    outb(PIC1_DATA, m1);
    outb(PIC2_DATA, m2);
}

void pic_mask(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = irq < 8 ? irq : irq - 8;
    outb(port, inb(port) | (1 << bit));
}

void pic_unmask(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = irq < 8 ? irq : irq - 8;
    outb(port, inb(port) & ~(1 << bit));
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2, 0x20);
    outb(PIC1, 0x20);
}

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;

    for (int i = 0; i < 256; i++) idt_set_gate(i, 0, 0, 0);

    for (int i = 0; i < 32; i++)
        idt_set_gate(i, (uint32_t)isr_stub_table[i], 0x08, 0x8E);

    pic_remap();
    // Enmascarar todo hasta que cada driver registre su IRQ
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    for (int i = 0; i < 16; i++)
        idt_set_gate(32 + i, (uint32_t)irq_stub_table[i], 0x08, 0x8E);

    // Syscall gate (INT 0x80), accesible desde ring3 (DPL=3 -> 0xEE)
    extern void isr128(void);
    idt_set_gate(0x80, (uint32_t)isr128, 0x08, 0xEE);

    idt_flush((uint32_t)&idtp);
}

static const char* exception_names[] = {
    "Division By Zero","Debug","NMI","Breakpoint","Overflow","Bound Range",
    "Invalid Opcode","Device Not Available","Double Fault","Coproc Overrun",
    "Invalid TSS","Segment Not Present","Stack Fault","GPF","Page Fault",
    "Reserved","x87 FP","Alignment Check","Machine Check","SIMD FP"
};

// Handler C llamado desde isr_common_stub (assembly)
void isr_handler(struct registers* r) {
    if (r->int_no == 0x80) {
        extern void syscall_dispatch(struct registers* r);
        syscall_dispatch(r);
        return;
    }

    bool from_ring3 = (r->cs & 3) == 3;

    if (from_ring3) {
        // Aislamiento de fallos: si el codigo que fallo corria en CPL3,
        // no apagamos todo el kernel — matamos solo ese proceso.
        //
        // Truco: en vez de manejarlo "aca adentro" (donde seguimos en medio
        // de la pila de la ISR y las interrupciones siguen deshabilitadas),
        // reescribimos a donde va a "volver" el IRET normal de
        // isr_common_stub: en vez de la direccion que fallo, apuntamos a
        // kernel_reap_current_process(), en CPL0 (mismo privilegio que la
        // ISR, asi que IRET no toca SS/ESP, solo EIP/CS/EFLAGS). Recien ahi,
        // ya con IF restaurado y fuera de la ISR, es seguro llamar a
        // proc_exit()/proc_yield() sin dejar interrupciones apagadas para
        // siempre ni abandonar el stack de la ISR a mitad de camino.
        extern _Noreturn void kernel_reap_current_process(void);
        extern const char* proc_current_name(void);

        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_puts("\n[ring3] '"); vga_puts(proc_current_name()); vga_puts("' fallo: ");
        if (r->int_no < 20) vga_puts(exception_names[r->int_no]);
        vga_puts(" (int="); vga_put_dec((int32_t)r->int_no); vga_puts(")\n");

        r->eip = (uint32_t)kernel_reap_current_process;
        r->cs = 0x08; // kernel code, ring0 (mismo privilegio -> IRET no cambia stack)
        return;
    }

    vga_set_color(VGA_WHITE, VGA_RED);
    vga_puts("\n*** KERNEL PANIC: CPU EXCEPTION ***\n");
    if (r->int_no < 20) vga_puts(exception_names[r->int_no]);
    vga_puts("  int=");
    vga_put_dec((int32_t)r->int_no);
    vga_puts(" err=");
    vga_put_hex(r->err_code);
    vga_puts("\n");

    if (r->int_no == 14) { // Page Fault: el error code y CR2 dicen exactamente qué pasó
        uint32_t fault_addr = read_cr2();
        vga_puts("  direccion fallida (CR2) = ");
        vga_put_hex(fault_addr);
        vga_puts("\n  causa: ");
        vga_puts((r->err_code & 0x1) ? "violacion de proteccion" : "pagina no presente");
        vga_puts(", acceso de ");
        vga_puts((r->err_code & 0x2) ? "escritura" : "lectura");
        vga_puts(", modo ");
        vga_puts((r->err_code & 0x4) ? "usuario" : "supervisor");
        vga_puts("\n");
    }

    vga_puts("  eip=");
    vga_put_hex(r->eip);
    vga_puts("\n");
    for (;;) cpu_halt();
}

// Handler C llamado desde irq_common_stub (assembly)
void irq_handler(struct registers* r) {
    extern void irq_dispatch(uint8_t irq);
    uint8_t irq = (uint8_t)(r->int_no - 32);
    irq_dispatch(irq);
    pic_eoi(irq);
}
