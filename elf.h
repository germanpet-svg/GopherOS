// elf.h - Estructuras ELF32 minimas (subset) para el loader de programas
// externos. No es un formato inventado: es el ELF32 estandar (ver
// referencias en kernel-tri-mode-auto-adaptive.md, seccion 0.2 — EI_CLASS
// solo tiene ELFCLASS32/ELFCLASS64 oficialmente, aca implementamos el
// primero).
#ifndef ELF_H
#define ELF_H

#include "types.h"

#define EI_NIDENT 16

// e_ident[]
#define EI_MAG0    0
#define EI_MAG1    1
#define EI_MAG2    2
#define EI_MAG3    3
#define EI_CLASS   4
#define EI_DATA    5

#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS32 1
#define ELFDATA2LSB 1

#define ET_EXEC 2
#define EM_386  3

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;      // direccion virtual de entrada
    uint32_t e_phoff;      // offset (en el archivo) de la tabla de program headers
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

#define PT_LOAD 1

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;   // offset en el archivo
    uint32_t p_vaddr;    // direccion virtual donde debe quedar mapeado
    uint32_t p_paddr;    // no lo usamos (identity-map en este kernel)
    uint32_t p_filesz;   // bytes a copiar desde el archivo
    uint32_t p_memsz;    // tamanio en memoria (>= filesz; el resto es BSS, va a cero)
    uint32_t p_flags;    // PF_R/PF_W/PF_X
    uint32_t p_align;
} Elf32_Phdr;

#endif // ELF_H
