// sysinfo.c - Segundo binario EXTERNO real de GopherOS (ver hello.c para
// el primero). A proposito usa una syscall DISTINTA (GOS_IOCTL_RTC_READ,
// via gos_date()) para probar que el aislamiento entre procesos ring3 no
// depende de que todos hagan lo mismo: cada uno tiene su propio stack,
// su propia copia de codigo, y puede llamar a syscalls distintas sin
// pisarse.
//
// Se compila y enlaza aparte del kernel, igual que hello.c — ver Makefile
// (target 'userland').

#include "gopheros_abi.h"

static void print_field(const char* label, int32_t value) {
    gos_print(label);
    gos_print_int(value);
}

__attribute__((noreturn, section(".text.start")))
void _start(void) {
    gos_print("[sysinfo.elf] binario externo #2 - GopherOS");
    gos_print("[sysinfo.elf] leyendo el reloj real (RTC) via syscall...");

    gos_datetime_t dt;
    gos_date(&dt);

    print_field("  anio:  ", dt.year);
    print_field("  mes:   ", dt.month);
    print_field("  dia:   ", dt.day);
    print_field("  hora:  ", dt.hour);
    print_field("  min:   ", dt.minute);
    print_field("  seg:   ", dt.second);

    gos_print("[sysinfo.elf] listo, terminando.");
    gos_exit(0);
    for (;;) { }
}
