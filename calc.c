// calc.c - Cuarto binario externo. Prueba GOS_SYS_MALLOC/GOS_SYS_FREE
// (memoria dinamica real, pedida al kernel desde ring3) calculando
// factoriales en un buffer reservado en runtime, no en el stack.
#include "gopheros_abi.h"

__attribute__((noreturn, section(".text.start")))
void _start(void) {
    gos_print("[calc.elf] binario externo #4 - malloc/free real desde ring3");

    int32_t* buf = (int32_t*)gos_malloc(6 * sizeof(int32_t));
    if (!buf) {
        gos_print("[calc.elf] malloc fallo");
        gos_exit(1);
    }

    buf[0] = 1;
    for (int i = 1; i <= 5; i++) buf[i] = buf[i - 1] * i;

    gos_print("[calc.elf] factoriales 0!..5!:");
    for (int i = 0; i <= 5; i++) gos_print_int(buf[i]);

    gos_free(buf);
    gos_print("[calc.elf] buffer liberado, terminando.");
    gos_exit(0);
    for (;;) { }
}
