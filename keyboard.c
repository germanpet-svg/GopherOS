// keyboard.c - Driver PS/2 (scancode set 1, layout US básico)
//
// Sigue la misma disciplina IRQ que el resto del kernel: el top-half SOLO
// lee el puerto, traduce el scancode (lookup O(1), sin asignar memoria) y
// lo mete en un ring buffer. Cualquier trabajo real (interpretar líneas,
// comandos) pasa en contexto de proceso (el shell), nunca en la ISR.

#include "types.h"
#include "hal.h"
#include "vga.h"
#include "keyboard.h"
#include "string.h"

#define KB_DATA_PORT 0x60
#define KB_EVENT_ID  2000
#define RING_SIZE    256

void irq_register(uint8_t irq, void (*top)(void), void (*bottom)(void), uint32_t event_id);
void proc_sleep_on(uint32_t event_id);

static volatile char ring[RING_SIZE];
static volatile uint32_t head = 0, tail = 0;

static bool shift_down = false;
static bool ctrl_down = false;

// Portapapeles interno del kernel: no hay mouse ni integracion con el
// host (QEMU/hardware real), asi que "copiar/pegar" ac es Ctrl+C/Ctrl+V
// clasico de consola: copia la linea que se esta editando ahora mismo,
// pega ese contenido en la linea actual. No es un portapapeles del
// sistema operativo host — vive solo mientras el kernel esta arriba.
#define CLIPBOARD_MAX 128
static char clipboard[CLIPBOARD_MAX];
static size_t clipboard_len = 0;

// Codigos de control que kb_readline() interpreta especial (nunca los
// produce una tecla normal: la tabla de traduccion solo genera
// imprimibles, tab, backspace o enter). Los valores 0x03/0x16 son los
// mismos que usa la terminal Unix de toda la vida para ETX/SYN
// (Ctrl+C/Ctrl+V), asi que el gesto se siente familiar.
#define KB_CTRL_C 0x03
#define KB_CTRL_V 0x16
#define KB_LEFT   0x11  // DC1 — flecha izquierda
#define KB_RIGHT  0x12  // DC2 — flecha derecha

// Scancode (set 1, "make code") -> caracter en minúscula / normal
static const char scancode_ascii[59] = {
    /*0x00*/ 0,  0 /*ESC*/, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    /*0x0F*/ '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    /*0x1D*/ 0 /*LCtrl*/, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    /*0x2A*/ 0 /*LShift*/, '\\','z','x','c','v','b','n','m',',','.','/',
    /*0x36*/ 0 /*RShift*/, '*', 0 /*LAlt*/, ' '
};

static const char scancode_ascii_shift[59] = {
    0, 0, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?',
    0, '*', 0, ' '
};

static void kb_push(char c) {
    uint32_t next = (head + 1) % RING_SIZE;
    if (next == tail) return; // buffer lleno: se descarta (política simple)
    ring[head] = c;
    head = next;
}

static bool extended_prefix = false; // ultimo byte fue 0xE0 (scancode "extendido": flechas, etc.)

static void kb_top_half(void) {
    uint8_t sc = inb(KB_DATA_PORT);

    if (sc == 0xE0) { extended_prefix = true; return; } // prefijo: el proximo byte es una tecla "extendida"

    if (extended_prefix) {
        extended_prefix = false;
        if (sc & 0x80) return;              // break code de una tecla extendida: la ignoramos
        if (sc == 0x4B) { kb_push(KB_LEFT); return; }  // flecha izquierda (make E0 4B)
        if (sc == 0x4D) { kb_push(KB_RIGHT); return; } // flecha derecha  (make E0 4D)
        return; // otras extendidas (arriba/abajo/home/end/etc.) todavia no soportadas
    }

    if (sc == 0x2A || sc == 0x36) { shift_down = true; return; }         // shift down
    if (sc == (0x2A | 0x80) || sc == (0x36 | 0x80)) { shift_down = false; return; } // shift up
    if (sc == 0x1D) { ctrl_down = true; return; }                        // LCtrl down
    if (sc == (0x1D | 0x80)) { ctrl_down = false; return; }               // LCtrl up
    if (sc & 0x80) return; // break code de otra tecla: lo ignoramos (sin repeat/estado)

    if (ctrl_down && sc == 0x2E) { kb_push(KB_CTRL_C); return; } // Ctrl+C ('c' = scancode 0x2E)
    if (ctrl_down && sc == 0x2F) { kb_push(KB_CTRL_V); return; } // Ctrl+V ('v' = scancode 0x2F)

    if (sc < sizeof(scancode_ascii)) {
        char c = shift_down ? scancode_ascii_shift[sc] : scancode_ascii[sc];
        if (c) kb_push(c);
    }
}

void keyboard_init(void) {
    head = tail = 0;
    shift_down = false;
    ctrl_down = false;
    extended_prefix = false;
    clipboard_len = 0;
    irq_register(1, kb_top_half, NULL, KB_EVENT_ID);
}

bool kb_haschar(void) {
    return head != tail;
}

char kb_getchar(void) {
    while (head == tail) {
        proc_sleep_on(KB_EVENT_ID);
    }
    char c = ring[tail];
    tail = (tail + 1) % RING_SIZE;
    return c;
}

// ============================================================
// Editor de linea con cursor movible (flechas izq/der). Antes, solo se
// podia escribir al final y borrar con backspace desde el final —
// insertar o corregir algo en el medio de lo ya tipeado era imposible
// sin borrar todo lo de despues primero.
//
// Modelo: guardamos (row0,col0) = donde arranca la linea en pantalla
// (justo despues del prompt), y un indice logico `cursor` dentro de
// `buf` (0..len). Para saber a que fila/columna de pantalla corresponde
// el indice `cursor`, contamos en "posicion lineal" (row0*80+col0+cursor)
// y deshacemos esa cuenta en fila/columna — funciona aunque la linea
// ocupe mas de una fila de pantalla (linea larga que da la vuelta).
//
// LIMITACION CONOCIDA (a proposito, no resuelta): si la linea es tan
// larga que llega a hacer scroll de la pantalla MIENTRAS se esta
// editando, (row0,col0) queda desactualizado (todo el contenido de
// arriba se corrio una fila) y el cursor visual se desalinea. No pasa en
// uso normal (comandos cortos), y arreglarlo bien requeriria que vga.c
// avise cuando hace scroll — lo dejamos para mas adelante si hace falta.
// ============================================================

// set_cursor_at_index() - Posiciona el cursor REAL de pantalla en el
// lugar que le corresponde al indice logico `idx` dentro de la linea que
// arranca en (row0,col0).
static void set_cursor_at_index(int row0, int col0, size_t idx) {
    int linear = row0 * VGA_SCREEN_COLS + col0 + (int)idx;
    vga_set_cursor(linear / VGA_SCREEN_COLS, linear % VGA_SCREEN_COLS);
}

// redraw_from() - Reimprime buf[from_idx..len) empezando en la posicion
// de pantalla que le corresponde a from_idx. Deja el cursor real al
// final de lo reimpreso (el caller lo reposiciona despues si hace falta).
static void redraw_from(int row0, int col0, const char* buf, size_t len, size_t from_idx) {
    set_cursor_at_index(row0, col0, from_idx);
    for (size_t i = from_idx; i < len; i++) vga_putc(buf[i]);
}

void kb_readline(char* buf, size_t maxlen) {
    size_t len = 0;
    size_t cursor = 0;
    int row0, col0;
    vga_get_cursor(&row0, &col0);

    for (;;) {
        char c = kb_getchar();
        if (c == '\n') {
            set_cursor_at_index(row0, col0, len); // el salto de linea va DESPUES de todo el contenido
            vga_putc('\n');
            break;
        } else if (c == '\b') {
            if (cursor > 0) {
                memmove(&buf[cursor - 1], &buf[cursor], len - cursor);
                cursor--; len--;
                redraw_from(row0, col0, buf, len, cursor);
                vga_putc(' '); // borra el caracter viejo que quedaba de mas al final
                set_cursor_at_index(row0, col0, cursor);
            }
        } else if (c == KB_LEFT) {
            if (cursor > 0) { cursor--; set_cursor_at_index(row0, col0, cursor); }
        } else if (c == KB_RIGHT) {
            if (cursor < len) { cursor++; set_cursor_at_index(row0, col0, cursor); }
        } else if (c == KB_CTRL_C) {
            // Copiar: guarda la linea completa tal cual esta ahora (no
            // hace falta Enter, ni que el cursor este al final).
            size_t n = len < CLIPBOARD_MAX - 1 ? len : CLIPBOARD_MAX - 1;
            memcpy(clipboard, buf, n);
            clipboard[n] = 0;
            clipboard_len = n;
            // Feedback inmediato en la barra de estado (ver statusbar.c):
            // se pisa solo con el proximo refresco periodico (~100ms),
            // asi que funciona como un "toast" sin tocar el area de texto.
            char msg[81] = " Copiado: ";
            size_t p = strlen(msg);
            for (size_t i = 0; i < n && p < sizeof(msg) - 1; i++) msg[p++] = clipboard[i];
            msg[p] = 0;
            vga_status_line(msg, (uint8_t)(VGA_BLACK | (VGA_LGREEN << 4)));
            set_cursor_at_index(row0, col0, cursor); // vga_status_line no toca el cursor, pero por las dudas
        } else if (c == KB_CTRL_V) {
            // Pegar: inserta el portapapeles en la posicion del cursor
            // (ya no solo al final).
            size_t n = clipboard_len;
            if (len + n >= maxlen) n = maxlen - 1 - len;
            if (n > 0) {
                size_t old_cursor = cursor;
                memmove(&buf[cursor + n], &buf[cursor], len - cursor);
                memcpy(&buf[cursor], clipboard, n);
                len += n; cursor += n;
                redraw_from(row0, col0, buf, len, old_cursor);
                set_cursor_at_index(row0, col0, cursor);
            }
        } else if (len + 1 < maxlen) {
            size_t old_cursor = cursor;
            memmove(&buf[cursor + 1], &buf[cursor], len - cursor);
            buf[cursor] = c;
            len++; cursor++;
            redraw_from(row0, col0, buf, len, old_cursor);
            set_cursor_at_index(row0, col0, cursor);
        }
        // si la línea ya está llena, ignoramos caracteres extra (pero seguimos
        // aceptando '\n'/'\b'/flechas/Ctrl+C/Ctrl+V arriba)
    }
    buf[len] = 0;
}
