#include "descriptors.h"
#include "hal.h"

// TSS - Solo lo usamos para lo mínimo indispensable: que la CPU sepa a qué
// stack de kernel (SS0:ESP0) saltar cuando un proceso ring3 dispara una
// interrupción/excepción/syscall. No usamos task-switching real de x86
// (eso es lento y nadie lo usa hoy), solo este único campo.
struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

static struct gdt_entry gdt[6];
static struct gdt_ptr   gdtp;
static struct tss_entry tss;

// Cambia a que stack de kernel salta la CPU (SS0:ESP0) cuando el proceso
// ACTUAL entra a ring0 via syscall/excepcion. Cada proceso ring3 tiene su
// propio stack (ver process.c) — sin esto, dos procesos ring3 que entran
// al kernel en momentos distintos (uno suspendido, otro por una excepcion)
// pisan la pila compartida del otro. Nos mordio en la practica: un
// atacante fallando mientras la victima estaba suspendida corrompia el
// contexto guardado de la victima.
void tss_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}

// Stack de kernel dedicado para atender interrupciones/syscalls mientras
// corre un proceso ring3. En este primer corte es uno solo, compartido
// (correcto porque el scheduler es cooperativo: nunca hay dos procesos
// ring3 atendiendo una interrupcion al mismo tiempo). El dia que haga
// falta uno por proceso, esp0 se actualiza en cada context_switch.
#define TSS_KERNEL_STACK_SIZE 8192
static uint8_t tss_kernel_stack[TSS_KERNEL_STACK_SIZE] __attribute__((aligned(16)));

static void gdt_set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].base_low    = base & 0xFFFF;
    gdt[i].base_mid    = (base >> 16) & 0xFF;
    gdt[i].base_high   = (base >> 24) & 0xFF;
    gdt[i].limit_low   = limit & 0xFFFF;
    gdt[i].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].access      = access;
}

static void tss_init(void) {
    uint32_t base = (uint32_t)&tss;
    uint32_t limit = sizeof(tss) - 1;
    gdt_set_entry(5, base, limit, 0x89, 0x00); // TSS disponible, ring0

    for (size_t i = 0; i < sizeof(tss); i++) ((uint8_t*)&tss)[i] = 0;
    tss.ss0 = 0x10; // selector de datos de kernel
    tss.esp0 = (uint32_t)(tss_kernel_stack + TSS_KERNEL_STACK_SIZE);
    tss.iomap_base = sizeof(tss); // sin bitmap de I/O -> ring3 no puede usar in/out

    __asm__ volatile ("ltr %%ax" :: "a"((uint16_t)0x28)); // selector = indice 5 * 8
}

void gdt_init(void) {
    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base  = (uint32_t)&gdt;

    gdt_set_entry(0, 0, 0, 0, 0);                      // null
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);        // code ring0
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);        // data ring0
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);        // code ring3
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);        // data ring3

    gdt_flush((uint32_t)&gdtp);
    tss_init();
}
