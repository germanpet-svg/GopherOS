#ifndef KEYBOARD_H
#define KEYBOARD_H
#include "types.h"

void keyboard_init(void);
char kb_getchar(void);           // bloqueante: duerme el proceso hasta que haya una tecla
bool kb_haschar(void);           // no bloqueante
void kb_readline(char* buf, size_t maxlen); // con eco y soporte de backspace

#endif
