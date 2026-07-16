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

enum {
    VGA_BLACK = 0, VGA_BLUE, VGA_GREEN, VGA_CYAN, VGA_RED, VGA_MAGENTA,
    VGA_BROWN, VGA_LGRAY, VGA_DGRAY, VGA_LBLUE, VGA_LGREEN, VGA_LCYAN,
    VGA_LRED, VGA_LMAGENTA, VGA_YELLOW, VGA_WHITE
};

#endif
