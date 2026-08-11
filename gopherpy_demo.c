    #include "gopherpy_runtime.h"

    void gopherpy_demo_main(void) {
    gos_print("=== GopherPy - primer programa de GopherOS ===");
    gos_print("Compilado a C nativo, cero librerias, un solo int 0x80 por syscall");
    int x = 10;
    if ((x > 5)) {
        gos_print("x es grande");
    }
    else {
        gos_print("x es chico");
    }
    gos_print("Contando con un for real (range):");
    for (int i = 0; i < 5; i++) {
        gos_print_int(i);
    }
    gos_print("Probando el modo grafico VGA 320x200x256...");
    gos_screen_mode(1);
    gos_cls_graphics(1);
    gos_pset(160, 100, 14);
    gos_pset(161, 100, 14);
    gos_pset(160, 101, 14);
    gos_pset(161, 101, 14);
    gos_yield();
    gos_screen_mode(0);
    gos_print("Listo. GopherPy funcionando de punta a punta.");
    gos_exit(0);
    }
