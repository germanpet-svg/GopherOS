// ring3_victim.c - Proceso "victima" para probar aislamiento ring3-a-ring3.
// Escribe una marca conocida en SU PROPIA stack, imprime la direccion, y
// se queda vivo cediendo la CPU. Otro proceso (ring3_attacker.c) va a
// intentar escribirle esa direccion desde afuera.

#include "gopheros_abi.h"

void ring3_victim_main(void) {
    volatile uint32_t marker = 0xCAFEBABE; // variable LOCAL: vive en la stack de ESTE proceso

    gos_print("[victim] marca escrita: 0xCAFEBABE");
    gos_print("[victim] direccion de la marca (en MI stack, no en el kernel):");
    gos_print_hex((uint32_t)(uintptr_t)&marker);
    gos_print("[victim] me quedo vivo cediendo CPU. Ahora corre:");
    gos_print("[victim]   ring3attack <esa direccion>");

    for (;;) {
        gos_yield();
        if (marker != 0xCAFEBABE) {
            gos_print("[victim] ALERTA: mi marca cambio -- alguien la piso!");
            gos_print_hex(marker);
        }
    }
}
