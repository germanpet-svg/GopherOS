#ifndef DESCRIPTORS_H
#define DESCRIPTORS_H
#include "types.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct registers {
    uint32_t ds;                                           // empujado a mano (selector ds original)
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;  // pusha
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;                  // empujado por CPU
};

extern void gdt_flush(uint32_t);
extern void idt_flush(uint32_t);

#endif
