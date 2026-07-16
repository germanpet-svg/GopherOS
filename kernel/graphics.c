// graphics.c - Modo gráfico VGA real (modo 13h, 320x200, 256 colores)
//
// No hay BIOS disponible después de Multiboot (el CPU ya está en modo
// protegido), así que cambiar de modo de video significa reprogramar a mano
// los registros del controlador VGA (Secuenciador, CRTC, Controlador Gráfico,
// Controlador de Atributos) — exactamente lo que hacía la BIOS al ejecutar
// INT 10h/AH=00h. Las tablas de registros de abajo son las estándar y
// ampliamente documentadas para 80x25 texto y modo 13h (320x200x256).
//
// El framebuffer de modo 13h queda mapeado en 0xA0000 (ya cubierto por
// nuestro identity-map de paging.c, que mapea los primeros 32MB).

#include "types.h"
#include "hal.h"
#include "graphics.h"

#define VGA_AC_INDEX   0x3C0
#define VGA_AC_WRITE   0x3C0
#define VGA_AC_READ    0x3C1
#define VGA_MISC_WRITE 0x3C2
#define VGA_SEQ_INDEX  0x3C4
#define VGA_SEQ_DATA   0x3C5
#define VGA_GC_INDEX   0x3CE
#define VGA_GC_DATA    0x3CF
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA  0x3D5
#define VGA_INSTAT_READ 0x3DA

#define FRAMEBUFFER_GRAPHICS ((volatile uint8_t*)0xA0000)

static int current_mode = GFX_MODE_TEXT_80x25;

// MISC, SEQ(5), CRTC(25), GC(9), AC(21) -- en ese orden, igual que
// programaría la BIOS real.
static const uint8_t regs_text_80x25[] = {
    0x67,
    0x03, 0x00, 0x03, 0x00, 0x07,  // SEQ4 (Memory Mode) = 0x07 para 80x25
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
    0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
    0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00,
    0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x0C, 0x00, 0x0F, 0x08, 0x00
};

static const uint8_t regs_mode13h[] = {
    0x63,
    0x03, 0x01, 0x0F, 0x00, 0x0E,
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
    0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F,
    0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x00, 0x0F, 0x00, 0x00
};

static void vga_write_registers(const uint8_t* regs) {
    // MISC
    outb(VGA_MISC_WRITE, *regs++);

    // SEQ (5 registros). La VGA requiere un pulso de reset (0x01 -> 0x03)
    // mientras se reprograman; escribimos seq1..seq4 con seq0 en reset y
    // el valor final de seq0 al final para levantar el reset.
    uint8_t seq0 = *regs++;
    outb(VGA_SEQ_INDEX, 0);
    outb(VGA_SEQ_DATA, 0x01);           // reset activo
    for (uint8_t i = 1; i < 5; i++) {
        outb(VGA_SEQ_INDEX, i);
        outb(VGA_SEQ_DATA, *regs++);
    }
    outb(VGA_SEQ_INDEX, 0);
    outb(VGA_SEQ_DATA, seq0);           // valor final del array (0x03)

    // Desbloquear registros CRTC 0-7 (bit 7 del registro 0x03 protege
    // ciertos campos; hay que apagarlo antes de reescribir todo el bloque).
    outb(VGA_CRTC_INDEX, 0x03);
    outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) | 0x80);
    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) & ~0x80);

    // CRTC (25 registros)
    uint8_t crtc[25];
    for (int i = 0; i < 25; i++) crtc[i] = regs[i];
    crtc[0x03] |= 0x80;   // mantener desbloqueado mientras escribimos
    crtc[0x11] &= ~0x80;
    for (uint8_t i = 0; i < 25; i++) {
        outb(VGA_CRTC_INDEX, i);
        outb(VGA_CRTC_DATA, crtc[i]);
    }
    regs += 25;

    // Graphics Controller (9 registros)
    for (uint8_t i = 0; i < 9; i++) {
        outb(VGA_GC_INDEX, i);
        outb(VGA_GC_DATA, *regs++);
    }

    // Attribute Controller (21 registros) - protocolo especial: hay que
    // leer el registro de estado de entrada antes de cada escritura al
    // puerto de índice, si no el hardware no sabe si esperamos índice o dato.
    for (uint8_t i = 0; i < 21; i++) {
        (void)inb(VGA_INSTAT_READ);
        outb(VGA_AC_INDEX, i);
        outb(VGA_AC_WRITE, *regs++);
    }

    // Bloquear la paleta de 16 colores y "desapagar" la pantalla
    (void)inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x20);
}

void gfx_set_mode(int mode) {
    if (mode == GFX_MODE_VGA_320x200) {
        vga_write_registers(regs_mode13h);
    } else {
        vga_write_registers(regs_text_80x25);
    }
    current_mode = mode;
}

void gfx_putpixel(int x, int y, uint8_t color) {
    if (current_mode != GFX_MODE_VGA_320x200) return;
    if (x < 0 || x >= 320 || y < 0 || y >= 200) return;
    FRAMEBUFFER_GRAPHICS[y * 320 + x] = color;
}

void gfx_clear(uint8_t color) {
    if (current_mode != GFX_MODE_VGA_320x200) return;
    for (int i = 0; i < 320 * 200; i++) FRAMEBUFFER_GRAPHICS[i] = color;
}

void gfx_hline(int x, int y, int w, uint8_t color) {
    for (int i = 0; i < w; i++) gfx_putpixel(x + i, y, color);
}

void gfx_rect(int x, int y, int w, int h, uint8_t color) {
    for (int j = 0; j < h; j++) gfx_hline(x, y + j, w, color);
}
