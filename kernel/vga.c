#include "vga.h"
#include "string.h"
#include "hal.h"

extern void serial_putc(char c);

#define VGA_MEM   ((volatile uint16_t*)0xB8000)
#define VGA_COLS  80
#define VGA_ROWS  25

static int cur_row = 0, cur_col = 0;
static uint8_t cur_color = 0x0F; // blanco sobre negro

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static void vga_update_cursor(void) {
    uint16_t pos = cur_row * VGA_COLS + cur_col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_init(void) {
    cur_row = 0; cur_col = 0; cur_color = 0x0F;
    vga_clear();
}

void vga_clear(void) {
    for (int r = 0; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            VGA_MEM[r * VGA_COLS + c] = vga_entry(' ', cur_color);
    cur_row = 0; cur_col = 0;
    vga_update_cursor();
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    cur_color = (uint8_t)(fg | (bg << 4));
}

static void vga_scroll(void) {
    for (int r = 1; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            VGA_MEM[(r - 1) * VGA_COLS + c] = VGA_MEM[r * VGA_COLS + c];
    for (int c = 0; c < VGA_COLS; c++)
        VGA_MEM[(VGA_ROWS - 1) * VGA_COLS + c] = vga_entry(' ', cur_color);
    cur_row = VGA_ROWS - 1;
}

void vga_putc(char c) {
    serial_putc(c);
    if (c == '\n') {
        cur_col = 0; cur_row++;
    } else if (c == '\r') {
        cur_col = 0;
    } else {
        VGA_MEM[cur_row * VGA_COLS + cur_col] = vga_entry(c, cur_color);
        cur_col++;
        if (cur_col >= VGA_COLS) { cur_col = 0; cur_row++; }
    }
    if (cur_row >= VGA_ROWS) vga_scroll();
    vga_update_cursor();
}

void vga_puts(const char* s) {
    while (*s) vga_putc(*s++);
}

void vga_backspace(void) {
    if (cur_col > 0) {
        cur_col--;
    } else if (cur_row > 0) {
        cur_row--;
        cur_col = VGA_COLS - 1;
    } else {
        return;
    }
    VGA_MEM[cur_row * VGA_COLS + cur_col] = vga_entry(' ', cur_color);
    vga_update_cursor();
}

void vga_puts_at(const char* s, int row, int col, uint8_t color) {
    int r = row, c = col;
    while (*s) {
        VGA_MEM[r * VGA_COLS + c] = vga_entry(*s, color);
        c++;
        if (c >= VGA_COLS) { c = 0; r++; }
        s++;
    }
}

void vga_put_hex(uint32_t v) {
    char buf[16];
    vga_puts("0x");
    size_t len = utoa(v, buf, 16);
    vga_puts(buf);
    (void)len;
}

void vga_put_dec(int32_t v) {
    char buf[16];
    itoa(v, buf, 10);
    vga_puts(buf);
}
