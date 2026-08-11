// ring3_demo.c - Prueba real de CPL3: corre en ring3 de verdad (no es
// una simulación), usando SOLO la ABI pública (gopheros_abi.h) — ni una
// sola llamada a función interna del kernel, como debe ser un programa
// de "usuario" real.

#include "gopheros_abi.h"

void ring3_demo_main(void) {
    gos_print("[ring3_demo] hola desde CPL3 de verdad (no simulado)");
    gos_print("[ring3_demo] la syscall anterior ya probo que int 0x80 funciona en ring3");
    gos_print("[ring3_demo] ahora pruebo una instruccion privilegiada (cli)...");
    gos_print("[ring3_demo] si el aislamiento funciona, esto deberia fallar y el");
    gos_print("[ring3_demo] kernel deberia seguir vivo (no deberia colgarse todo).");

    __asm__ volatile ("cli"); // prohibido en CPL3: debe disparar #GP (int 13)

    // Si llegamos aca, el aislamiento NO esta funcionando.
    gos_print("[ring3_demo] ERROR: esto nunca deberia imprimirse");
    gos_exit(1);
}
