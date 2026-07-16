// hal.h - Hardware Abstraction Layer (x86)
#ifndef HAL_H
#define HAL_H

#include "types.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static inline void cpu_halt(void) {
    __asm__ volatile ("hlt");
}

static inline void irq_disable(void) {
    __asm__ volatile ("cli");
}

static inline void irq_enable(void) {
    __asm__ volatile ("sti");
}

static inline uint32_t read_esp(void) {
    uint32_t v;
    __asm__ volatile ("mov %%esp, %0" : "=r"(v));
    return v;
}

void gdt_init(void);
void idt_init(void);
void paging_init(void);
void timer_init(uint32_t hz);
uint32_t read_cr2(void);

_Noreturn void panic(const char* msg);

#endif // HAL_H
