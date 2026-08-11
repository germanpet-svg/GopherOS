/* user.ld - Linker script para binarios de usuario de GopherOS.
 *
 * 0x01000000 (16MB) esta dentro del rango identity-mapeado de 32MB del
 * kernel (ver IDENTITY_MAP_MB en paging.c) pero bien lejos de donde carga
 * el kernel mismo (1MB via Multiboot) y de su arena estatica (~8MB) — no
 * hay overlap con la memoria fisica que el kernel ya usa para si mismo.
 * proc_load_elf() copia el contenido a paginas fisicas NUEVAS (via
 * page_alloc) y las remapea a esta direccion virtual solo dentro del
 * espacio de direcciones de este proceso, asi que el numero exacto no
 * tiene que coincidir con nada fisico real: es simplemente un rango
 * convencional para "aca vive el codigo de programas de usuario".
 */
ENTRY(_start)

SECTIONS
{
    . = 0x01000000;

    .text : {
        *(.text.start)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
