// stars.c - Quinto binario externo. Prueba la syscall de video grafico
// (GOS_IOCTL_VIDEO_*) desde un proceso ring3 real: cambia a modo grafico
// 320x200x256, dibuja un patron, y vuelve a modo texto antes de salir
// (para no dejar la pantalla en modo grafico si el shell sigue arriba).
#include "gopheros_abi.h"

__attribute__((noreturn, section(".text.start")))
void _start(void) {
    gos_print("[stars.elf] binario externo #5 - modo grafico real via syscall");

    gos_screen_mode(GOS_VIDEO_MODE_VGA256);
    gos_cls_graphics(0); // negro

    // patron simple: "estrellas" en diagonal + un cuadrado en el centro
    for (int i = 0; i < 200; i += 2) {
        gos_pset(i % 320, i, 15); // blanco
    }
    for (int y = 80; y < 120; y++) {
        for (int x = 140; x < 180; x++) {
            gos_pset(x, y, 4); // rojo
        }
    }

    gos_yield(); // le da un instante a que quede visible antes de volver a texto
    gos_screen_mode(GOS_VIDEO_MODE_TEXT);

    gos_print("[stars.elf] patron dibujado, vuelto a modo texto, terminando.");
    gos_exit(0);
    for (;;) { }
}
