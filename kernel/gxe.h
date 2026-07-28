// gxe.h - Formato ejecutable propio de GopherOS (Gopher eXecutable).
//
// Header de 32 bytes seguido de la imagen plana (text+data). El BSS se
// encuentra despues de la imagen y se inicializa a cero por el loader.
// El programa se carga en USER_BASE (0x40000000) por convencion.

#ifndef GXE_H
#define GXE_H

#include <stdint.h>

#define GXE_MAGIC   0x45584700u   // "GXE\0" little-endian
#define GXE_VERSION 1u

#define GXE_USER_BASE 0x40000000u
#define GXE_USER_SIZE 0x00400000u // 4 MB de espacio de usuario

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_offset;   // offset desde USER_BASE donde empieza la ejecucion
    uint32_t image_size;     // bytes de text+data que vienen en el archivo
    uint32_t bss_size;       // bytes adicionales de BSS a poner en cero
    uint32_t stack_size;     // minimo de stack de usuario (el loader redondea a pagina)
} gxe_header_t;

#endif // GXE_H
