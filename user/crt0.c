// crt0.c - Startup de programas de usuario GopherOS.

#include "gopheros_abi.h"

extern char __bss_start[];
extern char __bss_end[];

extern int main(void);

void _start(void) {
    for (char* p = __bss_start; p < __bss_end; p++) *p = 0;
    int rc = main();
    gos_exit(rc);
}
