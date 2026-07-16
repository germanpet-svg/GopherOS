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

#define KB_DATA_PORT 0x60
#define KB_EVENT_ID  2000
#define RING_SIZE    256

void irq_register(uint8_t irq, void (*top)(void), void (*bottom)(void), uint32_t event_id);
void proc_sleep_on(uint32_t event_id);

static volatile char ring[RING_SIZE];
static volatile uint32_t head = 0, tail = 0;

static bool shift_down = false;

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

static void kb_top_half(void) {
    uint8_t sc = inb(KB_DATA_PORT);

    if (sc == 0x2A || sc == 0x36) { shift_down = true; return; }         // shift down
    if (sc == (0x2A | 0x80) || sc == (0x36 | 0x80)) { shift_down = false; return; } // shift up
    if (sc & 0x80) return; // break code de otra tecla: lo ignoramos (sin repeat/estado)

    if (sc < sizeof(scancode_ascii)) {
        char c = shift_down ? scancode_ascii_shift[sc] : scancode_ascii[sc];
        if (c) kb_push(c);
    }
}

void keyboard_init(void) {
    head = tail = 0;
    shift_down = false;
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

void kb_readline(char* buf, size_t maxlen) {
    size_t len = 0;
    for (;;) {
        char c = kb_getchar();
        if (c == '\n') {
            vga_putc('\n');
            break;
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                vga_backspace();
            }
        } else if (len + 1 < maxlen) {
            buf[len++] = c;
            vga_putc(c);
        }
        // si la línea ya está llena, ignoramos caracteres extra (pero seguimos
        // aceptando '\n'/'\b' arriba)
    }
    buf[len] = 0;
}
