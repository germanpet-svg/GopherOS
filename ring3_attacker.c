// ring3_attacker.c - Intenta escribir en la direccion de memoria de OTRO
// proceso ring3 (la "victima"). Antes de este trabajo de espacios de
// direcciones por proceso, esto tenia exito (todos los procesos ring3
// compartian el mismo mapeo). Ahora deberia fallar con Page Fault.

#include "gopheros_abi.h"

void ring3_attacker_main(void) {
    uint32_t target = gos_debug_get_arg();

    gos_print("[attacker] direccion objetivo (memoria de otro proceso):");
    gos_print_hex(target);
    gos_print("[attacker] intentando escribir ahi...");
    gos_print("[attacker] si el aislamiento entre procesos funciona, esto debe fallar.");

    volatile uint32_t* ptr = (volatile uint32_t*)(uintptr_t)target;
    *ptr = 0x41544143; // "ATAC" -- si esto se escribe, el aislamiento fallo

    gos_print("[attacker] ERROR: la escritura tuvo exito, NO hay aislamiento entre procesos");
    gos_exit(1);
}
