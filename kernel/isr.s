; isr.s - GDT/IDT flush, stubs de ISR/IRQ, cambio de contexto
[BITS 32]

section .text

; ------------------------------------------------------------
global gdt_flush
gdt_flush:
    mov eax, [esp+4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush
.flush:
    ret

; ------------------------------------------------------------
global idt_flush
idt_flush:
    mov eax, [esp+4]
    lidt [eax]
    ret

; ------------------------------------------------------------
extern isr_handler
extern irq_handler

%macro ISR_NOERR 1
global isr%1
isr%1:
    cli
    push dword 0        ; err_code falso
    push dword %1        ; int_no
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    cli
    push dword %1        ; int_no (err_code ya lo puso la CPU)
    jmp isr_common_stub
%endmacro

%macro IRQ_STUB 2
global irq%1
irq%1:
    cli
    push dword 0
    push dword %2
    jmp irq_common_stub
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_NOERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

IRQ_STUB 0, 32
IRQ_STUB 1, 33
IRQ_STUB 2, 34
IRQ_STUB 3, 35
IRQ_STUB 4, 36
IRQ_STUB 5, 37
IRQ_STUB 6, 38
IRQ_STUB 7, 39
IRQ_STUB 8, 40
IRQ_STUB 9, 41
IRQ_STUB 10, 42
IRQ_STUB 11, 43
IRQ_STUB 12, 44
IRQ_STUB 13, 45
IRQ_STUB 14, 46
IRQ_STUB 15, 47

; INT 0x80 - syscall gate (sin código de error)
global isr128
isr128:
    cli
    push dword 0
    push dword 0x80
    jmp isr_common_stub

isr_common_stub:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call isr_handler
    add esp, 4

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8   ; int_no + err_code
    sti
    iret

irq_common_stub:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call irq_handler
    add esp, 4

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8
    sti
    iret

; ------------------------------------------------------------
; void context_switch(uint32_t* old_esp_store, uint32_t new_esp)
; Guarda el contexto actual en *old_esp_store y salta al nuevo esp
; ------------------------------------------------------------
global context_switch
context_switch:
    push ebp
    mov ebp, esp
    pusha
    mov eax, [ebp+8]      ; old_esp_store
    mov [eax], esp
    mov eax, [ebp+12]     ; new_esp
    mov esp, eax
    popa
    pop ebp
    ret

; ------------------------------------------------------------
; void enter_ring3(uint32_t entry_eip, uint32_t user_esp)
; Salta a ring3 armando a mano el frame que espera IRET.
; Nunca vuelve (si el proceso ring3 quiere terminar, usa la syscall exit).
; ------------------------------------------------------------
global enter_ring3
enter_ring3:
    mov eax, [esp+4]   ; entry_eip
    mov ecx, [esp+8]   ; user_esp

    mov dx, 0x23        ; selector de datos ring3 (indice 4 * 8 | RPL 3)
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx

    push dword 0x23      ; SS  (ring3 data)
    push ecx             ; ESP (stack de usuario)
    pushfd
    or dword [esp], 0x200 ; asegurar IF=1 en el contexto nuevo
    push dword 0x1B      ; CS  (ring3 code, indice 3 * 8 | RPL 3)
    push eax             ; EIP (entry point)
    iret

; Tabla de punteros a los stubs (usada desde C)
section .data
global isr_stub_table
isr_stub_table:
    dd isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    dd isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    dd isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    dd isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31

global irq_stub_table
irq_stub_table:
    dd irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
    dd irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15
