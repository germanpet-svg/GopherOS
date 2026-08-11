// hello.c - Primer programa EXTERNO real de GopherOS.
//
// Se compila y enlaza APARTE del kernel (ver Makefile, target userland),
// y se carga en runtime via proc_load_elf() + el comando 'exec' del shell.
// No linkea libc (no hay), no linkea nada del kernel: la unica frontera es
// gopheros_abi.h (int 0x80), igual que cualquier programa real de GopherOS.
//
// No hay crt0: _start es el entry point tal cual lo pone el ELF header,
// y enter_ring3() (isr.s) salta directo aca con la pila de usuario ya
// armada por proc_load_elf(). No hay argc/argv ni retorno posible: si
// _start "retornara", no hay a donde volver (no hay caller) — por eso
// termina explicitamente con GOS_SYS_EXIT.

#include "gopheros_abi.h"

static void gos_puts(const char* s) {
    uint32_t len = 0;
    while (s[len]) len++;
    gos_syscall(GOS_SYS_WRITE, 1, (uint32_t)s, len, 0);
}

__attribute__((noreturn, section(".text.start")))
void _start(void) {
    gos_puts("[hello.elf] soy un binario ELF32 externo, cargado por proc_load_elf()\n");
    gos_puts("[hello.elf] no vengo compilado dentro del kernel. terminando.\n");
    gos_syscall(GOS_SYS_EXIT, 0, 0, 0, 0);
    for (;;) { } // gos_syscall(EXIT) no deberia retornar; esto es solo por si acaso
}
