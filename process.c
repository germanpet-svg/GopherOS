// process.c - Scheduler cooperativo, máximo 5 procesos
//
// No hay timer interrupt que robe la CPU: el único mecanismo de scheduling
// es yield() explícito (o bloqueo en sleep_on). Esto elimina toda una clase
// de race conditions por preferir simplicidad sobre preemption real.

#include "types.h"
#include "hal.h"
#include "string.h"
#include "typesafe.h"
#include "vga.h"
#include "kresults.h"
#include "elf.h"

// MAX_PROCESSES=8: antes era 5. Los slots de un proceso terminado
// (PROC_ZOMBIE) no se reciclan todavia (no hay proc_reap()) — asi que con
// gopherd+shell ocupando 2 para siempre, 5 solo alcanzaba para 3
// ejecutables externos por sesion. Con userland/ creciendo (ver Makefile,
// USER_PROGS), 8 deja margen real. proc_list() ya cotiza esto con su
// parametro `max` (shell.c/statusbar.c usan buffers de 8), asi que no
// hace falta tocar nada mas.
#define MAX_PROCESSES 8
#define STACK_SIZE    (16 * 1024)

typedef enum {
    PROC_UNUSED,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE,
} ProcState;

typedef struct Process {
    uint32_t pid;
    ProcState state;
    Region* region;
    uint32_t esp;
    char name[16];
    uint32_t jiffies_start;
    opt_u32_t blocked_on;
    bool is_ring3;
    void (*ring3_entry)(void);
    uint32_t ring3_user_stack;
    uint32_t ring3_kernel_stack_top; // para tss_set_kernel_stack() al conmutar a este proceso
    int addr_space; // handle de paging_new_address_space(); -1 = espacio de kernel (procesos ring0)
} Process;

static Process processes[MAX_PROCESSES];
static Process* current = NULL;
static uint32_t next_pid = 1;

// Solo para pruebas de aislamiento ring3-a-ring3 (ver ring3_demo_mem.c y
// gopheros_abi.h/gos_debug_get_arg): un valor que el kernel setea ANTES de
// crear un proceso, para poder "pasarle" una direccion sin tener IPC real.
static uint32_t debug_test_arg = 0;
void debug_set_test_arg(uint32_t v) { debug_test_arg = v; }
uint32_t debug_get_test_arg(void) { return debug_test_arg; }
static uint32_t idle_esp = 0; // "contexto" del arranque del kernel

extern void context_switch(uint32_t* old_esp_store, uint32_t new_esp);
extern volatile uint32_t jiffies;
void proc_exit(int code);

// ============================================================
// Crear proceso - entry es una función void(void) que nunca debería
// retornar (si retorna, el proceso termina automáticamente).
// ============================================================
proc_result_t proc_create(const char* name, void (*entry)(void)) {
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_UNUSED) { slot = i; break; }
    }
    if (slot < 0) {
        proc_result_t res = Err(GOS_EBUSY);
        return res;
    }

    Process* p = &processes[slot];
    p->region = region_new(STACK_SIZE + 4096);
    if (p->region == NULL) {
        proc_result_t res = Err(GOS_ENOMEM);
        return res;
    }

    uint8_t* stack_top = (uint8_t*)region_alloc(p->region, STACK_SIZE) + STACK_SIZE;

    // Layout esperado por context_switch (ver isr.s), de mayor a menor dirección:
    // ... [ret_eip=entry][saved_ebp][eax][ecx][edx][ebx][esp_dummy][ebp][esi][edi] <- esp
    // Al hacer popa+pop ebp+ret, la CPU "regresa" directamente a `entry`.
    uint32_t* sp = (uint32_t*)stack_top;
    sp -= 1; *sp = (uint32_t)entry;   // "ret" saltará aquí como si fuera llamado
    sp -= 1; *sp = 0;                 // saved ebp
    sp -= 1; *sp = 0;                 // eax
    sp -= 1; *sp = 0;                 // ecx
    sp -= 1; *sp = 0;                 // edx
    sp -= 1; *sp = 0;                 // ebx
    sp -= 1; *sp = 0;                 // esp dummy
    sp -= 1; *sp = 0;                 // ebp
    sp -= 1; *sp = 0;                 // esi
    sp -= 1; *sp = 0;                 // edi

    p->esp = (uint32_t)sp;
    p->pid = next_pid++;
    p->state = PROC_READY;
    p->jiffies_start = jiffies;
    p->blocked_on = (opt_u32_t)None;
    p->is_ring3 = false;
    p->addr_space = -1;
    strncpy(p->name, name, 16);

    proc_result_t res = Ok(p);
    return res;
}

// ============================================================
// proc_create_ring3() - Crea un proceso que corre en CPL3 (ring 3).
//
// Primer corte: hay dos stacks (una chica de kernel, solo para el
// trampolín inicial + futuras interrupciones/syscalls vía la TSS; y la de
// usuario, donde corre `entry`). Ver paging.c: por ahora TODA la memoria
// identity-mapeada es accesible desde ring3 (sin aislacion fina todavia),
// asi que esto prueba la transicion de privilegio y el manejo de fallos,
// no aislacion de memoria real.
// ============================================================
static void ring3_trampoline(void) {
    extern void enter_ring3(uint32_t entry_eip, uint32_t user_esp);
    Process* p = current; // "current" ya apunta a este proceso (recien planificado)
    enter_ring3((uint32_t)p->ring3_entry, p->ring3_user_stack);
    for (;;) cpu_halt(); // enter_ring3 no deberia retornar jamas
}

proc_result_t proc_create_ring3(const char* name, void (*entry)(void), size_t user_stack_size) {
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_UNUSED) { slot = i; break; }
    }
    if (slot < 0) {
        proc_result_t res = Err(GOS_EBUSY);
        return res;
    }

    Process* p = &processes[slot];
    size_t kstack_size = 4096;
    p->region = region_new(kstack_size + user_stack_size + 8192);
    if (p->region == NULL) {
        proc_result_t res = Err(GOS_ENOMEM);
        return res;
    }

    uint8_t* kstack_top = (uint8_t*)region_alloc(p->region, kstack_size) + kstack_size;
    uint8_t* ustack_top = (uint8_t*)region_alloc(p->region, user_stack_size) + user_stack_size;

    uint32_t* sp = (uint32_t*)kstack_top;
    sp -= 1; *sp = (uint32_t)ring3_trampoline; // "ret" saltara aca (en CPL0 todavia)
    sp -= 1; *sp = 0;  // saved ebp
    sp -= 1; *sp = 0;  // eax
    sp -= 1; *sp = 0;  // ecx
    sp -= 1; *sp = 0;  // edx
    sp -= 1; *sp = 0;  // ebx
    sp -= 1; *sp = 0;  // esp dummy
    sp -= 1; *sp = 0;  // ebp
    sp -= 1; *sp = 0;  // esi
    sp -= 1; *sp = 0;  // edi

    p->esp = (uint32_t)sp;
    p->pid = next_pid++;
    p->state = PROC_READY;
    p->jiffies_start = jiffies;
    p->blocked_on = (opt_u32_t)None;
    p->is_ring3 = true;
    p->ring3_entry = entry;
    p->ring3_user_stack = (uint32_t)ustack_top;
    p->ring3_kernel_stack_top = (uint32_t)kstack_top;
    strncpy(p->name, name, 16);

    // Este proceso corre en CPL3 en SU PROPIO espacio de direcciones (no
    // el compartido): su región de memoria (stack de kernel para el
    // trampolín + stack de usuario) gana el bit US=1 solo AHI. Otro
    // proceso ring3, con su propio espacio, ni siquiera tiene esta región
    // mapeada como accesible — no es "permiso denegado", es que no existe
    // para el. Esto es lo que da aislamiento entre procesos, no solo
    // entre kernel y procesos (ver paging.c).
    extern phys_addr_t region_backing_phys(Region* r);
    extern size_t region_backing_size(Region* r);
    extern int paging_new_address_space(void);
    extern void paging_grant_access(int handle, uint32_t phys_start, uint32_t size, bool user_accessible);

    int space = paging_new_address_space();
    if (space < 0) {
        region_destroy(p->region);
        proc_result_t res = Err(GOS_ENOMEM);
        return res;
    }
    p->addr_space = space;
    paging_grant_access(space, region_backing_phys(p->region), (uint32_t)region_backing_size(p->region), true);

    proc_result_t res = Ok(p);
    return res;
}

// ============================================================
// proc_load_elf() - Cargador de binarios ELF32 EXTERNOS (no compilados
// junto al kernel). A diferencia de proc_create_ring3(), el codigo de este
// proceso NO viene del rango .text/.rodata compartido del kernel
// (paging_set_shared_code_range en kernel.c) sino de paginas fisicas
// propias, copiadas desde `elf_data` y mapeadas SOLO en el espacio de
// direcciones de este proceso via paging_map_page(). Dos procesos que
// carguen el mismo ELF (o ELFs distintos linkeados a la misma direccion
// virtual) no comparten memoria: cada uno tiene su propia copia fisica.
//
// Es el primer paso real hacia el diseño de kernel-tri-mode-auto-adaptive.md
// (deteccion de EI_CLASS al "ejecutar" un binario) — hoy solo soporta
// ELFCLASS32/EM_386 porque es lo unico que este kernel corre, pero la
// validacion de cabecera ya queda en un solo lugar, listo para agregar
// un segundo camino (ELFCLASS64) el dia que el kernel mismo corra en
// long mode.
// ============================================================
proc_result_t proc_load_elf(const char* name, const uint8_t* elf_data, size_t elf_size, size_t user_stack_size) {
    if (elf_size < sizeof(Elf32_Ehdr)) {
        proc_result_t res = Err(GOS_EINVAL);
        return res;
    }
    const Elf32_Ehdr* eh = (const Elf32_Ehdr*)elf_data;
    if (eh->e_ident[EI_MAG0] != ELFMAG0 || eh->e_ident[EI_MAG1] != ELFMAG1 ||
        eh->e_ident[EI_MAG2] != ELFMAG2 || eh->e_ident[EI_MAG3] != ELFMAG3 ||
        eh->e_ident[EI_CLASS] != ELFCLASS32 || eh->e_machine != EM_386 || eh->e_type != ET_EXEC) {
        proc_result_t res = Err(GOS_EINVAL);
        return res;
    }
    if (eh->e_phoff == 0 || eh->e_phnum == 0 ||
        (uint64_t)eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > elf_size) {
        proc_result_t res = Err(GOS_EINVAL);
        return res;
    }

    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_UNUSED) { slot = i; break; }
    }
    if (slot < 0) {
        proc_result_t res = Err(GOS_EBUSY);
        return res;
    }
    Process* p = &processes[slot];

    extern int paging_new_address_space(void);
    extern void paging_free_address_space(int handle);
    extern bool paging_map_page(int handle, uint32_t virt, uint32_t phys, bool user, bool writable);
    extern void paging_grant_access(int handle, uint32_t phys_start, uint32_t size, bool user_accessible);
    extern void* page_alloc_mapped(size_t num_pages, phys_addr_t* out_phys);

    int space = paging_new_address_space();
    if (space < 0) {
        proc_result_t res = Err(GOS_ENOMEM);
        return res;
    }

    const Elf32_Phdr* phdrs = (const Elf32_Phdr*)(elf_data + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        const Elf32_Phdr* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;
        if ((uint64_t)ph->p_offset + ph->p_filesz > elf_size || ph->p_filesz > ph->p_memsz) {
            paging_free_address_space(space);
            proc_result_t res = Err(GOS_EINVAL);
            return res;
        }

        uint32_t vstart = ph->p_vaddr & ~(uint32_t)(PAGE_SIZE - 1);
        uint32_t vend = ALIGN(ph->p_vaddr + ph->p_memsz, PAGE_SIZE);
        uint32_t num_pages = (vend - vstart) / PAGE_SIZE;

        phys_addr_t phys = 0;
        void* backing = page_alloc_mapped(num_pages, &phys);
        if (!backing) {
            paging_free_address_space(space);
            proc_result_t res = Err(GOS_ENOMEM);
            return res;
        }
        memset(backing, 0, num_pages * PAGE_SIZE);

        uint32_t skip = ph->p_vaddr - vstart;
        memcpy((uint8_t*)backing + skip, elf_data + ph->p_offset, ph->p_filesz);

        bool writable = (ph->p_flags & PF_W) != 0;
        for (uint32_t pg = 0; pg < num_pages; pg++) {
            paging_map_page(space, vstart + pg * PAGE_SIZE, phys + pg * PAGE_SIZE, true, writable);
        }
    }

    size_t kstack_size = 4096;
    p->region = region_new(kstack_size + user_stack_size + 8192);
    if (p->region == NULL) {
        paging_free_address_space(space);
        proc_result_t res = Err(GOS_ENOMEM);
        return res;
    }

    uint8_t* kstack_top = (uint8_t*)region_alloc(p->region, kstack_size) + kstack_size;
    uint8_t* ustack_top = (uint8_t*)region_alloc(p->region, user_stack_size) + user_stack_size;

    uint32_t* sp = (uint32_t*)kstack_top;
    sp -= 1; *sp = (uint32_t)ring3_trampoline; // "ret" saltara aca (en CPL0 todavia)
    sp -= 1; *sp = 0;  // saved ebp
    sp -= 1; *sp = 0;  // eax
    sp -= 1; *sp = 0;  // ecx
    sp -= 1; *sp = 0;  // edx
    sp -= 1; *sp = 0;  // ebx
    sp -= 1; *sp = 0;  // esp dummy
    sp -= 1; *sp = 0;  // ebp
    sp -= 1; *sp = 0;  // esi
    sp -= 1; *sp = 0;  // edi

    p->esp = (uint32_t)sp;
    p->pid = next_pid++;
    p->state = PROC_READY;
    p->jiffies_start = jiffies;
    p->blocked_on = (opt_u32_t)None;
    p->is_ring3 = true;
    p->ring3_entry = (void (*)(void))(uintptr_t)eh->e_entry; // solo se usa como direccion de 32b, ver ring3_trampoline
    p->ring3_user_stack = (uint32_t)ustack_top;
    p->ring3_kernel_stack_top = (uint32_t)kstack_top;
    p->addr_space = space;
    strncpy(p->name, name, 16);

    extern phys_addr_t region_backing_phys(Region* r);
    extern size_t region_backing_size(Region* r);
    paging_grant_access(space, region_backing_phys(p->region), (uint32_t)region_backing_size(p->region), true);

    proc_result_t res = Ok(p);
    return res;
}

// Llamado (indirectamente, ver idt.c) cuando un proceso ring3 provoca una
// excepcion de CPU. Para cuando esto se ejecuta, la CPU ya volvio a CPL0
// de forma normal (iret con el mismo privilegio), asi que es seguro llamar
// a las funciones normales del scheduler ac
_Noreturn void kernel_reap_current_process(void) {
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("[ring3] proceso terminado por fallo, el resto del sistema sigue vivo.\n");
    proc_exit(-1);
    for (;;) cpu_halt(); // no deberia llegar aca
}

void proc_exit(int code) {
    (void)code;
    if (current == NULL) return;
    if (current->is_ring3) {
        extern void paging_free_address_space(int handle);
        paging_free_address_space(current->addr_space);
        current->addr_space = -1;
    }
    current->state = PROC_ZOMBIE;
    extern void proc_yield(void);
    proc_yield();
    for (;;) cpu_halt(); // nunca debería llegar aquí
}

const char* proc_current_name(void) {
    return current ? current->name : "kernel";
}

// ============================================================
// proc_kill() / proc_reap() - "Sacar" un proceso de la tabla, a pedido
// (comando 'kill' del shell). Antes de esto, un proceso que terminaba
// (PROC_ZOMBIE) se quedaba en la tabla PARA SIEMPRE: el slot nunca
// volvia a PROC_UNUSED (ver el comentario historico en MAX_PROCESSES
// arriba), y su Region (stack de kernel + stack de usuario) tampoco se
// liberaba — un leak real de memoria fisica ademas de slots de proceso.
// region_destroy()/page_free() ya existian en memory.c pero nadie los
// llamaba desde este archivo.
//
// proc_kill(pid): funciona sobre CUALQUIER estado salvo UNUSED.
//   - Si esta READY o BLOCKED (no es el que esta corriendo ahora mismo:
//     en un scheduler cooperativo de un solo core, "current" es siempre
//     quien esta ejecutando esta misma llamada, asi que nunca se mata a
//     si mismo por esta via): lo fuerza a ZOMBIE, liberando su espacio
//     de direcciones si era ring3 — mismo camino que proc_exit(), pero
//     sin que el proceso haya pedido salir el mismo.
//   - Si ya esta ZOMBIE: reapea directo (libera Region, slot -> UNUSED).
//   - Si es el proceso RUNNING actual (kill de si mismo, ej. 'kill' del
//     propio pid del shell): rechazado, devuelve false — no hay forma
//     segura de "matarse a uno mismo" desde dentro de la propia llamada
//     sin dejar al scheduler sin stack valido.
// ============================================================
bool proc_kill(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        Process* p = &processes[i];
        if (p->state == PROC_UNUSED || p->pid != pid) continue;

        if (p->state == PROC_RUNNING) return false; // no te podes matar a vos mismo asi

        if (p->state == PROC_READY || p->state == PROC_BLOCKED) {
            if (p->is_ring3 && p->addr_space >= 0) {
                extern void paging_free_address_space(int handle);
                paging_free_address_space(p->addr_space);
                p->addr_space = -1;
            }
            p->state = PROC_ZOMBIE;
        }

        // A esta altura state == PROC_ZOMBIE (ya sea porque llego asi, o
        // porque lo acabamos de forzar arriba): reapear de una.
        extern void region_destroy(Region* r);
        region_destroy(p->region);
        p->region = NULL;
        p->state = PROC_UNUSED;
        return true;
    }
    return false; // no existe ese pid
}

uint32_t proc_current_pid(void) {
    return current ? current->pid : 0;
}

// proc_current_addr_space() - Handle de paging para el proceso que esta
// corriendo AHORA (o -1 si es un hilo de kernel puro, ring0, como
// gopherd/shell). Lo usa syscall.c para saber a que espacio de
// direcciones hay que otorgarle acceso US=1 cuando un proceso ring3 pide
// memoria dinamica (ver sys_malloc) — sin esto, kmalloc() devuelve una
// direccion que existe en el identity-map pero con bit US=0 (supervisor-
// only por defecto, ver fill_identity en paging.c), y el proceso ring3
// se cae con Page Fault al primer acceso.
int proc_current_addr_space(void) {
    return current ? current->addr_space : -1;
}

// Snapshot de los procesos para 'ps'. state: 0=libre 1=listo 2=corriendo
// 3=bloqueado 4=zombi. Devuelve cuántas entradas escribió.
int proc_list(uint32_t* pids, char names[][16], int* states, int max) {
    int n = 0;
    for (int i = 0; i < MAX_PROCESSES && n < max; i++) {
        if (processes[i].state == PROC_UNUSED) continue;
        pids[n] = processes[i].pid;
        strncpy(names[n], processes[i].name, 16);
        states[n] = (int)processes[i].state;
        n++;
    }
    return n;
}

// ============================================================
// yield() - Cede la CPU voluntariamente al siguiente READY.
// Si nadie está READY pero hay procesos BLOCKED (esperando IRQ,
// como el teclado), entra en espera de bajo consumo (sti;hlt) hasta
// la próxima interrupción y reintenta — nunca "apaga" el sistema
// mientras haya algo por lo que valga la pena seguir esperando.
// ============================================================
void proc_yield(void) {
    extern void irq_run_bottom_halves(void);

    for (;;) {
        irq_run_bottom_halves();

        int start_idx = current ? (int)(current - processes) : -1;

        for (int i = 1; i <= MAX_PROCESSES; i++) {
            int idx = (start_idx + i) % MAX_PROCESSES;
            if (processes[idx].state == PROC_READY) {
                Process* prev = current;
                current = &processes[idx];
                current->state = PROC_RUNNING;
                if (prev && prev != current && prev->state == PROC_RUNNING) prev->state = PROC_READY;

                extern void paging_switch_address_space(int handle);
                paging_switch_address_space(current->addr_space);
                if (current->is_ring3) {
                    extern void tss_set_kernel_stack(uint32_t esp0);
                    tss_set_kernel_stack(current->ring3_kernel_stack_top);
                }

                uint32_t* old_store = prev ? &prev->esp : &idle_esp;
                context_switch(old_store, current->esp);
                return;
            }
        }

        // Nadie más READY: si el actual sigue vivo (no se bloqueó), sigue corriendo.
        if (current && current->state == PROC_RUNNING) return;

        // ¿Queda algo vivo (bloqueado, esperando teclado/timer/etc)?
        bool any_alive = false;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].state == PROC_BLOCKED || processes[i].state == PROC_READY) {
                any_alive = true;
                break;
            }
        }
        if (!any_alive) {
            vga_puts("\n[scheduler] No hay procesos listos. Sistema detenido.\n");
            for (;;) cpu_halt();
        }

        // Nada que hacer ahora mismo: dormir la CPU hasta la próxima IRQ
        // (sti+hlt es atómico en x86: no se puede perder la interrupción
        // entre habilitar y dormir) y reintentar desde el principio.
        __asm__ volatile ("sti; hlt");
    }
}

void proc_sleep_on(uint32_t event_id) {
    if (current == NULL) return;
    current->state = PROC_BLOCKED;
    current->blocked_on = (opt_u32_t) Some(event_id);
    proc_yield();
}

void proc_wakeup(uint32_t event_id) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_BLOCKED &&
            processes[i].blocked_on.is_some &&
            processes[i].blocked_on.value == event_id) {
            processes[i].state = PROC_READY;
            processes[i].blocked_on = (opt_u32_t)None;
        }
    }
}

// ============================================================
// scheduler_run() - Arranca el scheduling, nunca retorna
// ============================================================
_Noreturn void scheduler_run(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_READY) {
            current = &processes[i];
            current->state = PROC_RUNNING;
            extern void paging_switch_address_space(int handle);
            paging_switch_address_space(current->addr_space);
            if (current->is_ring3) {
                extern void tss_set_kernel_stack(uint32_t esp0);
                tss_set_kernel_stack(current->ring3_kernel_stack_top);
            }
            context_switch(&idle_esp, current->esp);
            break;
        }
    }
    // Si algún proceso hace yield() y vuelve aquí (no debería en este diseño),
    // mantenemos la CPU ocupada de forma segura.
    for (;;) {
        proc_yield();
        cpu_halt();
    }
}
