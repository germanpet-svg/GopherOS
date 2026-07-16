// kresults.h - Instancias con nombre de Result()/Option() compartidas entre
// varias unidades de compilación. En C, dos expansiones de una macro que
// genera "struct { ... }" (sin tag) en archivos .c distintos producen tipos
// anónimos NO compatibles entre sí a nivel de declaración, aunque su layout
// binario sea idéntico. Para poder declarar una función en un .c y
// (correctamente) usarla via extern en otro, necesitamos un typedef con
// nombre compartido a través de un header común: este archivo.
#ifndef KRESULTS_H
#define KRESULTS_H

#include "typesafe.h"

typedef struct Process Process;
typedef struct FileEntry FileEntry;

typedef Result(Process*, gos_result_t)   proc_result_t;
typedef Result(FileEntry*, gos_result_t) fs_create_result_t;
typedef Result(size_t, gos_result_t)     fs_size_result_t;
typedef Result(const uint8_t*, gos_result_t) fs_read_result_t;
typedef Option(uint32_t) opt_u32_t;

#endif // KRESULTS_H
