#ifndef VGA_H
#define VGA_H
#include "types.h"

void vga_init(void);
void vga_clear(void);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_putc(char c);
void vga_backspace(void);
void vga_puts(const char* s);
void vga_puts_at(const char* s, int row, int col, uint8_t color);
void vga_put_hex(uint32_t v);
void vga_put_dec(int32_t v);

// vga_status_line() - Pinta el texto directamente en la ultima fila de la
// pantalla (reservada, no hace scroll, no mueve el cursor del shell). El
// texto se trunca/rellena con espacios a 80 columnas. Usado por
// statusbar.c para el pie de pagina estilo GW-BASIC.
void vga_status_line(const char* text, uint8_t color);

// vga_get_cursor()/vga_set_cursor() - ver vga.c. Usado por keyboard.c
// para el editor de linea con cursor movible (flechas izq/der).
void vga_get_cursor(int* row, int* col);
void vga_set_cursor(int row, int col);

// Ancho de pantalla en columnas. Debe coincidir con VGA_COLS (vga.c,
// privado a ese archivo) — se expone aca porque keyboard.c necesita este
// numero para calcular a que fila/columna cae un indice logico dentro de
// una linea que puede ocupar mas de una fila de pantalla.
#define VGA_SCREEN_COLS 80

enum {
    VGA_BLACK = 0, VGA_BLUE, VGA_GREEN, VGA_CYAN, VGA_RED, VGA_MAGENTA,
    VGA_BROWN, VGA_LGRAY, VGA_DGRAY, VGA_LBLUE, VGA_LGREEN, VGA_LCYAN,
    VGA_LRED, VGA_LMAGENTA, VGA_YELLOW, VGA_WHITE
};

#endif
