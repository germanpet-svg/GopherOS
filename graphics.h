#ifndef GRAPHICS_H
#define GRAPHICS_H
#include "types.h"

#define GFX_MODE_TEXT_80x25   0
#define GFX_MODE_VGA_320x200  1  // modo 13h, 256 colores, paleta VGA estándar

void gfx_set_mode(int mode);
void gfx_putpixel(int x, int y, uint8_t color);
void gfx_clear(uint8_t color);
void gfx_hline(int x, int y, int w, uint8_t color);
void gfx_rect(int x, int y, int w, int h, uint8_t color);

#endif
