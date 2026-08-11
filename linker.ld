ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text BLOCK(4K) : ALIGN(4K)
    {
        _text_start = .;
        *(.multiboot)
        *(.text)
    }

    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata*)
        _text_end = .; /* fin de .text+.rodata: lo unico que ring3 puede
                           buscar-y-ejecutar (codigo y constantes), nunca
                           datos ni bss. */
    }

    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data)
    }

    .bss BLOCK(4K) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }

    .irq_funcs BLOCK(4K) : ALIGN(4K)
    {
        *(.irq_funcs)
    }
}
