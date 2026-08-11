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

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
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
void paging_set_user_access(uint32_t phys_start, uint32_t size, bool user_accessible);
int paging_new_address_space(void);
void paging_free_address_space(int handle);
uint32_t paging_address_space_cr3(int handle);
void paging_grant_access(int handle, uint32_t phys_start, uint32_t size, bool user_accessible);
void paging_switch_address_space(int handle);
void paging_set_shared_code_range(uint32_t start, uint32_t size);
bool paging_map_page(int handle, uint32_t virt, uint32_t phys, bool user, bool writable);
void timer_init(uint32_t hz);
uint32_t read_cr2(void);

_Noreturn void panic(const char* msg);

#endif // HAL_H
