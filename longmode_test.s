; longmode_test.s - Prototipo AISLADO: modo protegido 32b -> long mode 64b.
;
; Objetivo (paso 1 del roadmap de 64 bits): probar, SIN tocar el kernel
; real de GopherOS, que la secuencia completa de transicion a long mode
; funciona en este entorno (QEMU) antes de meterla en gdt.c/paging.c/
; boot.s de verdad. Si esto falla, es mucho mas barato depurarlo aca que
; en medio del kernel de 32 bits que ya funciona.
;
; Secuencia (la real, la que exige la arquitectura x86-64, no un atajo):
;   1. GRUB/Multiboot nos deja en modo protegido de 32 bits (igual que el
;      kernel real).
;   2. Verificar por CPUID que el CPU soporta long mode (bit LM).
;   3. Armar tablas de paginacion de 4 niveles (PML4->PDPT->PD), usando
;      paginas de 2MB en el ultimo nivel para no necesitar una 4ta tabla
;      (PT) todavia — identity-map del primer 1GB, suficiente para probar.
;   4. Activar PAE (CR4), cargar CR3, prender EFER.LME (MSR), prender
;      paging (CR0.PG). Esto nos deja en "compatibility submode": paging
;      de 64 bits activo pero corriendo con un code segment de 32 bits.
;   5. Cargar una GDT con un descriptor de codigo de 64 bits (bit L) y
;      hacer un far jump para recargar CS -> ahi si estamos en long mode
;      de verdad.
;   6. Prueba definitiva: ejecutar una instruccion que SOLO es valida en
;      64 bits (mov de un inmediato de 64 bits a un registro de 64 bits
;      via prefijo REX.W) e imprimir el resultado por serial. Si esto
;      corriera, el CPU seguiria en modo de 32 bits — no hay forma de
;      "fingir" este resultado desde compat mode.

[BITS 32]

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x00000003
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

section .bss
align 4096
pml4:      resb 4096
pdpt:      resb 4096
pd:        resb 4096
align 16
stack32_bottom: resb 16384
stack32_top:

section .rodata
msg_start:      db "[longmode_test] modo protegido 32b, arrancando...", 10, 0
msg_cpuid_ok:   db "[longmode_test] CPUID reporta soporte de long mode (LM=1)", 10, 0
msg_cpuid_bad:  db "[longmode_test] FALLO: este CPU no soporta long mode. Abortando.", 10, 0
msg_paging_ok:  db "[longmode_test] PAE + CR3 + EFER.LME + CR0.PG activados", 10, 0
msg_jumping:    db "[longmode_test] far jump a segmento de codigo de 64 bits...", 10, 0
msg_lm64_hdr:   db "[longmode_test] ESTO SE IMPRIME DESDE CODIGO REAL DE 64 BITS.", 10, 0
msg_rax_hdr:    db "[longmode_test] mov rax, 0x1234567890ABCDEF ; rax = ", 0
msg_ok:         db 10, "[longmode_test] OK: long mode confirmado end-to-end.", 10, 0

section .text
global _start
_start:
    mov esp, stack32_top
    xor ebp, ebp
    cli

    call serial_init32
    mov esi, msg_start
    call serial_puts32

    ; --- 1. Verificar soporte de long mode via CPUID ---
    call check_longmode
    test eax, eax
    jnz .cpuid_ok
    mov esi, msg_cpuid_bad
    call serial_puts32
    jmp halt32
.cpuid_ok:
    mov esi, msg_cpuid_ok
    call serial_puts32

    ; --- 2. Armar PML4 -> PDPT -> PD (paginas de 2MB, identity map 1GB) ---
    call setup_page_tables

    ; --- 3. PAE, CR3, EFER.LME, CR0.PG ---
    mov eax, cr4
    or eax, 0x20            ; CR4.PAE
    mov cr4, eax

    mov eax, pml4
    mov cr3, eax

    mov ecx, 0xC0000080      ; MSR EFER
    rdmsr
    or eax, 0x100             ; EFER.LME
    wrmsr

    mov eax, cr0
    or eax, 0x80000000        ; CR0.PG
    mov cr0, eax

    mov esi, msg_paging_ok
    call serial_puts32

    ; --- 4. GDT de 64 bits + far jump ---
    mov esi, msg_jumping
    call serial_puts32

    lgdt [gdt64.pointer]
    jmp gdt64.code:long_mode_start

halt32:
    hlt
    jmp halt32

; ============================================================
; check_longmode() -> eax=1 si CPUID extendido reporta LM (bit 29 de EDX
; en la funcion 0x80000001), eax=0 si no.
; ============================================================
check_longmode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no
    mov eax, 0x80000001
    cpuid
    test edx, (1 << 29)
    jz .no
    mov eax, 1
    ret
.no:
    xor eax, eax
    ret

; ============================================================
; setup_page_tables() - PML4[0]->PDPT, PDPT[0]->PD, PD[0..511] = paginas
; de 2MB identity-map (0..1GB). PRESENT|WRITABLE en todos los niveles;
; PS (bit7) en las entradas de PD para que sean paginas grandes de 2MB
; y no haga falta una 4ta tabla (PT) para este prototipo.
; ============================================================
setup_page_tables:
    ; limpiar las 3 tablas (4096*3 bytes = 3072 dwords)
    mov edi, pml4
    xor eax, eax
    mov ecx, 4096 * 3 / 4
    rep stosd

    mov eax, pdpt
    or eax, 0x3               ; PRESENT|WRITABLE
    mov [pml4], eax

    mov eax, pd
    or eax, 0x3
    mov [pdpt], eax

    mov edi, pd
    mov eax, 0x83              ; PRESENT|WRITABLE|PS(2MB), base=0
    mov ecx, 512
.fill_pd:
    mov [edi], eax
    add eax, 0x200000           ; siguiente pagina de 2MB
    add edi, 8
    loop .fill_pd
    ret

; ============================================================
; serial32: init + putc + puts (16550 estandar, mismo init que
; kernel/serial.c del kernel real — ver ahi la explicacion de cada byte)
; ============================================================
COM1 equ 0x3F8

serial_init32:
    mov dx, COM1 + 1
    mov al, 0x00
    out dx, al
    mov dx, COM1 + 3
    mov al, 0x80
    out dx, al
    mov dx, COM1 + 0
    mov al, 0x03
    out dx, al
    mov dx, COM1 + 1
    mov al, 0x00
    out dx, al
    mov dx, COM1 + 3
    mov al, 0x03
    out dx, al
    mov dx, COM1 + 2
    mov al, 0xC7
    out dx, al
    mov dx, COM1 + 4
    mov al, 0x0B
    out dx, al
    ret

serial_putc32:
.wait:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x20
    jz .wait
    mov dx, COM1
    mov al, [esp + 4]
    out dx, al
    ret

; imprime string null-terminated apuntado por ESI (32b)
serial_puts32:
    push eax
.loop:
    mov al, [esi]
    test al, al
    jz .done
    push eax
    call serial_putc32
    add esp, 4
    inc esi
    jmp .loop
.done:
    pop eax
    ret

; ============================================================
; GDT de 64 bits - la codificacion minima estandar (ver OSDev "Setting
; Up Long Mode"): null descriptor + un descriptor de codigo con bit L
; (long mode) prendido. No hace falta descriptor de datos: en 64 bits
; la segmentacion esta "aplanada" y los selectores de datos/stack se
; pueden dejar en null sin que el CPU falle por eso.
; ============================================================
align 16
gdt64:
    dq 0
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)  ; ejecutable, S=1, present, L=1
.pointer:
    dw $ - gdt64 - 1
    dd gdt64

[BITS 64]
long_mode_start:
    ; Selectores de datos en null: valido en 64 bits (modelo flat, el
    ; CPU no usa base/limite de estos para direccionar memoria).
    xor ax, ax
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov eax, msg_lm64_hdr
    call serial_puts64

    ; --- Prueba definitiva: instruccion que SOLO existe en 64 bits ---
    mov eax, msg_rax_hdr
    call serial_puts64

    mov rax, 0x1234567890ABCDEF   ; mov de inmediato de 64 bits -> imposible en 32b/compat
    call print_hex64_rax

    mov eax, msg_ok
    call serial_puts64

halt64:
    hlt
    jmp halt64

serial_putc64:
.wait:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x20
    jz .wait
    mov dx, COM1
    mov al, dil
    out dx, al
    ret

; imprime string null-terminated apuntado por RAX
serial_puts64:
    mov rsi, rax
.loop:
    mov dil, [rsi]
    test dil, dil
    jz .done
    call serial_putc64
    inc rsi
    jmp .loop
.done:
    ret

; imprime RAX en hex (16 digitos) por serial
print_hex64_rax:
    mov rbx, rax
    mov rcx, 16
.next_nibble:
    rol rbx, 4
    mov dil, bl
    and dil, 0x0F
    cmp dil, 10
    jb .digit
    add dil, 'A' - 10 - '0'
.digit:
    add dil, '0'
    push rcx
    push rbx
    call serial_putc64
    pop rbx
    pop rcx
    loop .next_nibble
    mov dil, 10
    call serial_putc64
    ret
