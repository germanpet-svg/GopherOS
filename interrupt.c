// interrupt.c - IRQ handling sin races
//
// Regla de oro: ISR hace MÍNIMO, bottom-half hace el trabajo.
// top_half: <10 instrucciones equivalentes, nunca aloca memoria, nunca bloquea.
// bottom_half: corre en contexto de proceso (llamado por el scheduler), hace el trabajo real.

#include "types.h"
#include "hal.h"

void pic_unmask(uint8_t irq);

typedef struct {
    void (*top_half)(void);
    void (*bottom_half)(void);
    uint32_t event_id;
    volatile bool pending;
} IRQHandler;

static IRQHandler irq_table[16];

void irq_register(uint8_t irq, void (*top)(void), void (*bottom)(void), uint32_t event_id) {
    if (irq >= 16) panic("IRQ out of range");

    irq_table[irq].top_half = top;
    irq_table[irq].bottom_half = bottom;
    irq_table[irq].event_id = event_id;
    irq_table[irq].pending = false;

    pic_unmask(irq);
}

// Llamado desde irq_handler (idt.c) en contexto de ISR real.
void irq_dispatch(uint8_t irq) {
    if (irq >= 16) return;
    if (irq_table[irq].top_half) {
        irq_table[irq].top_half();
    }
    irq_table[irq].pending = true;
}

// Llamado desde el scheduler (contexto de proceso, nunca desde ISR).
void irq_run_bottom_halves(void) {
    extern void proc_wakeup(uint32_t event_id);
    for (int i = 0; i < 16; i++) {
        if (irq_table[i].pending) {
            irq_table[i].pending = false;
            if (irq_table[i].bottom_half) irq_table[i].bottom_half();
            proc_wakeup(irq_table[i].event_id);
        }
    }
}
