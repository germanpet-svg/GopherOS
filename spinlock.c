#include "typesafe.h"
#include "hal.h"

// En un solo core, el único "concurrente" real es una IRQ.
// spin_lock deshabilita interrupciones para simular sección crítica atómica,
// respetando la regla del diseño: "si hay spinlock, no hay IRQ".
void _spin_lock(SpinLock* l, const char* file, int line) {
    irq_disable();
    if (l->locked) {
        panic("SpinLock re-entrante detectado");
    }
    l->locked = true;
    l->file = file;
    l->line = line;
}

void _spin_unlock(SpinLock* l) {
    l->locked = false;
    irq_enable();
}
