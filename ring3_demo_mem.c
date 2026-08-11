// ring3_demo_mem.c - Prueba de aislamiento de MEMORIA (no solo de
// instrucciones privilegiadas). Antes de este paso, un proceso ring3 podia
// leer/escribir CUALQUIER direccion identity-mapeada, incluida la memoria
// del kernel. Ahora, solo su propia region (stack/codigo) es accesible.

#include "gopheros_abi.h"

void ring3_demo_mem_main(void) {
    gos_print("[ring3_mem] intentando escribir en 0x0010b000 (.bss del kernel)...");
    gos_print("[ring3_mem] (.text/.rodata SI son accesibles a proposito: ahi vive");
    gos_print("[ring3_mem] el codigo de los procesos, no hay loader separado todavia)");
    gos_print("[ring3_mem] pero .data/.bss (el ESTADO real del kernel) debe seguir");
    gos_print("[ring3_mem] protegido. Si el aislamiento funciona, esto debe fallar.");

    volatile uint32_t* kernel_data_addr = (volatile uint32_t*)0x0010b000;
    *kernel_data_addr = 0xDEADBEEF; // debe disparar #PF (pagina supervisor-only)

    // Si llegamos aca, la escritura tuvo exito: el aislamiento NO funciona.
    gos_print("[ring3_mem] ERROR: la escritura tuvo exito, NO hay aislamiento real");
    gos_exit(1);
}
