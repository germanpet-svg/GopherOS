#include "hal.h"
#include "statusbar.h"

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIT_FREQ  1193182u

volatile uint32_t jiffies = 0;

void irq_register(uint8_t irq, void (*top)(void), void (*bottom)(void), uint32_t event_id);

static void timer_top_half(void) {
    jiffies++;
}

static void timer_bottom_half(void) {
    statusbar_update(); // se auto-throttlea, ver statusbar.c
}

void timer_init(uint32_t hz) {
    uint32_t divisor = PIT_FREQ / hz;

    outb(PIT_CMD, 0x36); // canal 0, lobyte/hibyte, modo 3
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));

    irq_register(0, timer_top_half, timer_bottom_half, /*event_id=*/1000);
}
