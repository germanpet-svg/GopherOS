// gopherpy_runtime.h - Puente entre el C generado por GopherPy y GopherOS.
//
// Todo el codigo que genera el traductor incluye este header. Como ya
// definimos la ABI completa en gopheros_abi.h (puros "static inline" con
// un "int 0x80" cada uno, cero dependencias), este archivo no necesita
// agregar nada mas — es literalmente un alias con nombre para que el
// generador de codigo no tenga que conocer la ruta real del header.

#ifndef GOPHERPY_RUNTIME_H
#define GOPHERPY_RUNTIME_H

#include "gopheros_abi.h"

#endif
