/* linker.ld - prototipo aislado, no comparte nada con kernel/linker.ld */
ENTRY(_start)

SECTIONS
{
    . = 0x100000; /* 1MB, igual que el kernel real via Multiboot */

    .text : {
        *(.multiboot)
        *(.text*)
    }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) *(COMMON) }
}
