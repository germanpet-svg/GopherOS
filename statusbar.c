// statusbar.c - Pie de pagina persistente estilo GW-BASIC.
//
// GW-BASIC siempre dejaba la fila 25 de la pantalla con la barra de
// F-keys ("F1=LIST F2=RUN F3=LOAD..."), sin importar que estuvieras
// editando o corriendo un programa: esa fila nunca hacia scroll ni se
// pisaba con la salida normal. Reproducimos la misma idea aca: vga.c
// reserva la ultima fila (VGA_STATUS_ROW, ver vga.c) fuera del area de
// scroll, y esta funcion la repinta con info en vivo del kernel.

#include "types.h"
#include "vga.h"
#include "string.h"

extern volatile uint32_t jiffies;
extern const char* proc_current_name(void);
extern int proc_list(uint32_t* pids, char names[][16], int* states, int max);
extern void memory_get_stats(uint32_t* used_pages, uint32_t* total_pages);

static uint32_t last_drawn = 0;
static bool drawn_once = false;

// statusbar_update() - Se llama seguido (ver timer.c, bottom-half del
// timer), pero se auto-throttlea: solo redibuja de verdad ~10 veces por
// segundo (timer corre a 100Hz), asi que llamarla mas seguido no cuesta
// nada extra ni genera parpadeo perceptible.
void statusbar_update(void) {
    if (drawn_once && (jiffies - last_drawn) < 10) return;
    last_drawn = jiffies;
    drawn_once = true;

    uint32_t used = 0, total = 0;
    memory_get_stats(&used, &total);

    uint32_t pids[8];
    char names[8][16];
    int states[8];
    int n = proc_list(pids, names, states, 8);
    (void)pids; (void)names; (void)states;

    char line[81];
    size_t pos = 0;
    char num[16];

#define SB_APPEND(s) do { \
        const char* _p = (s); \
        while (*_p && pos < sizeof(line) - 1) line[pos++] = *_p++; \
    } while (0)

    SB_APPEND(" F1=Help F2=ps F3=exec | ");
    SB_APPEND(proc_current_name());
    SB_APPEND(" | procs ");
    itoa(n, num, 10); SB_APPEND(num);
    SB_APPEND("/5 | mem ");
    itoa((int32_t)used, num, 10); SB_APPEND(num);
    SB_APPEND("/");
    itoa((int32_t)total, num, 10); SB_APPEND(num);
    SB_APPEND("pg | t+");
    itoa((int32_t)(jiffies / 100), num, 10); SB_APPEND(num);
    SB_APPEND("s | GopherOS");

#undef SB_APPEND

    line[pos] = '\0';
    // Negro sobre cyan: el look clasico de las barras de estado DOS/BASIC.
    vga_status_line(line, (uint8_t)(VGA_BLACK | (VGA_CYAN << 4)));
}
