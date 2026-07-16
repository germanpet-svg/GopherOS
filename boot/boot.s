; boot.s - Cabecera Multiboot 1 + punto de entrada
; GRUB (o QEMU con -kernel) carga este binario ELF directamente en modo
; protegido de 32 bits, sin necesidad de bootsector/stage2 propios.

[BITS 32]

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x00000003          ; align modules + mem info
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

section .bss
align 16
stack_bottom:
    resb 65536                       ; 64KB de stack inicial del kernel
stack_top:

section .text
global _start
extern kernel_main

_start:
    mov esp, stack_top
    xor ebp, ebp
    cli
    cmp eax, 0x2BADB002
    jne .no_mb
    push ebx
    jmp .call_main
.no_mb:
    push 0
.call_main:
    call kernel_main
.hang:
    hlt
    jmp .hang
