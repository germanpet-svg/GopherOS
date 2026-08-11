// counter.c - Tercer binario externo. A diferencia de hello.c/sysinfo.c
// (que corren de una sola pasada), este cede la CPU explicitamente entre
// cada paso (gos_yield) para poder correr intercalado con otros procesos
// ring3 — la prueba de que el scheduler cooperativo trata a un ELF
// externo exactamente igual que a gopherd/shell (que ya hacian esto).
#include "gopheros_abi.h"

__attribute__((noreturn, section(".text.start")))
void _start(void) {
    gos_print("[counter.elf] binario externo #3 - cuenta cediendo CPU entre pasos");
    for (int i = 1; i <= 5; i++) {
        gos_print_int(i);
        gos_yield();
    }
    gos_print("[counter.elf] listo, terminando.");
    gos_exit(0);
    for (;;) { }
}
