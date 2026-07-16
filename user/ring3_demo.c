// ring3_demo.c - Programa de usuario real para GopherOS.
//
// Corre en CPL3, mapeado en USER_BASE (0x40000000), usando solo la ABI
// publica de gopheros_abi.h.

#include "gopheros_abi.h"

int main(void) {
    gos_print("[ring3_demo] hola desde un .gxe real (CPL3)");
    gos_print("[ring3_demo] PID:");
    gos_print_int(gos_getpid());

    gos_print("[ring3_demo] pruebo syscall write... OK");

    // Crear y leer un archivo para probar open/read/write/close
    int fd = gos_open("/ring3_demo.txt",
                      GOS_O_WRONLY | GOS_O_CREAT | GOS_O_TRUNC);
    if (fd >= 0) {
        const char* msg = "Escrito desde ring3_demo.gxe";
        gos_write(fd, msg, gos_strlen(msg));
        gos_close(fd);

        fd = gos_open("/ring3_demo.txt", GOS_O_RDONLY);
        if (fd >= 0) {
            char buf[64];
            int n = gos_read(fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = 0;
                gos_print("[ring3_demo] leido:");
                gos_print(buf);
            }
            gos_close(fd);
        }
    } else {
        gos_print("[ring3_demo] no pudo crear /ring3_demo.txt");
    }

    gos_print("[ring3_demo] ahora pruebo una instruccion privilegiada (cli)...");
    __asm__ volatile ("cli");

    gos_print("[ring3_demo] ERROR: cli no causo fallo");
    gos_exit(1);
    return 0;
}
