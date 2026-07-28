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
#include "filesystem.h"
#include "memory.h"
#include "paging.h"
#include "gxe.h"

// Actualiza TSS.ESP0 usado por la CPU para syscalls/excepciones desde ring 3.
extern void tss_set_esp0(uint32_t esp0);

#define MAX_PROCESSES 5
#define STACK_SIZE    (16 * 1024)
#define PROC_MAX_FDS  8

#define PROC_O_RDONLY 0
#define PROC_O_WRONLY 1
#define PROC_O_RDWR   2
#define PROC_O_CREAT  4
#define PROC_O_TRUNC  8

typedef struct {
    FileEntry* file;
    uint32_t offset;
    bool writable;
} ProcFD;

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
    uint8_t* kstack_top;
    uint32_t page_dir;       // direccion fisica del page directory (CR3)
    char name[16];
    uint32_t jiffies_start;
    opt_u32_t blocked_on;
    bool is_ring3;
    void (*ring3_entry)(void);
    uint32_t ring3_user_stack;
    ProcFD fds[PROC_MAX_FDS];
} Process;

static Process processes[MAX_PROCESSES];
static Process* current = NULL;
static uint32_t next_pid = 1;
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
    p->kstack_top = stack_top;
    p->page_dir = paging_kernel_dir_phys();
    p->pid = next_pid++;
    p->state = PROC_READY;
    p->jiffies_start = jiffies;
    p->blocked_on = (opt_u32_t)None;
    p->is_ring3 = false;
    memset(p->fds, 0, sizeof(p->fds));
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

proc_result_t proc_create_gxe(const char* name, const uint8_t* data, size_t size) {
    if (size < sizeof(gxe_header_t)) {
        proc_result_t res = Err(GOS_EINVAL); return res;
    }

    gxe_header_t header;
    memcpy(&header, data, sizeof(header));
    if (header.magic != GXE_MAGIC || header.version != GXE_VERSION) {
        proc_result_t res = Err(GOS_EINVAL); return res;
    }

    size_t image_pages  = (header.image_size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t bss_pages    = (header.bss_size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t stack_size   = header.stack_size > PAGE_SIZE ? header.stack_size : (16 * 1024);
    size_t stack_pages  = (stack_size + PAGE_SIZE - 1) / PAGE_SIZE;

    // Imagen + BSS contiguos fisicamente para copiar/limpiar de una.
    phys_addr_t img_offset = page_alloc(image_pages + bss_pages);
    if (img_offset == 0) {
        proc_result_t res = Err(GOS_ENOMEM); return res;
    }
    uint32_t img_phys = (uint32_t)arena_base + img_offset;
    uint8_t* img_virt = (uint8_t*)phys_to_virt(img_offset);

    phys_addr_t stk_offset = page_alloc(stack_pages);
    if (stk_offset == 0) {
        page_free(img_offset, image_pages + bss_pages);
        proc_result_t res = Err(GOS_ENOMEM); return res;
    }
    uint32_t stk_phys = (uint32_t)arena_base + stk_offset;
    uint8_t* stk_virt = (uint8_t*)phys_to_virt(stk_offset);

    memcpy(img_virt, data + sizeof(gxe_header_t), header.image_size);
    memset(img_virt + header.image_size, 0, header.bss_size);
    memset(stk_virt, 0, stack_pages * PAGE_SIZE);

    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_UNUSED) { slot = i; break; }
    }
    if (slot < 0) {
        page_free(img_offset, image_pages + bss_pages);
        page_free(stk_offset, stack_pages);
        proc_result_t res = Err(GOS_EBUSY); return res;
    }

    Process* p = &processes[slot];
    size_t kstack_size = 4096;
    p->region = region_new(kstack_size + 8192);
    if (p->region == NULL) {
        page_free(img_offset, image_pages + bss_pages);
        page_free(stk_offset, stack_pages);
        proc_result_t res = Err(GOS_ENOMEM); return res;
    }

    uint8_t* kstack_top = (uint8_t*)region_alloc(p->region, kstack_size) + kstack_size;

    uint32_t* sp = (uint32_t*)kstack_top;
    sp -= 1; *sp = (uint32_t)ring3_trampoline;
    sp -= 1; *sp = 0;  // saved ebp
    sp -= 1; *sp = 0;  // eax
    sp -= 1; *sp = 0;  // ecx
    sp -= 1; *sp = 0;  // edx
    sp -= 1; *sp = 0;  // ebx
    sp -= 1; *sp = 0;  // esp dummy
    sp -= 1; *sp = 0;  // ebp
    sp -= 1; *sp = 0;  // esi
    sp -= 1; *sp = 0;  // edi

    uint32_t pd_phys = paging_create_user_dir();
    if (pd_phys == 0 ||
        !paging_map(pd_phys, GXE_USER_BASE, img_phys, image_pages + bss_pages,
                    PTE_RW | PTE_US) ||
        !paging_map(pd_phys, GXE_USER_BASE + GXE_USER_SIZE - stack_pages * PAGE_SIZE,
                    stk_phys, stack_pages, PTE_RW | PTE_US)) {
        if (pd_phys) {
            // Sin ruta de liberacion de page tables por ahora.
        }
        region_destroy(p->region);
        page_free(img_offset, image_pages + bss_pages);
        page_free(stk_offset, stack_pages);
        proc_result_t res = Err(GOS_ENOMEM); return res;
    }

    p->esp = (uint32_t)sp;
    p->kstack_top = kstack_top;
    p->page_dir = pd_phys;
    p->pid = next_pid++;
    p->state = PROC_READY;
    p->jiffies_start = jiffies;
    p->blocked_on = (opt_u32_t)None;
    p->is_ring3 = true;
    p->ring3_entry = (void (*)(void))(GXE_USER_BASE + header.entry_offset);
    p->ring3_user_stack = GXE_USER_BASE + GXE_USER_SIZE - 4;
    memset(p->fds, 0, sizeof(p->fds));
    strncpy(p->name, name, 16);

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
    current->state = PROC_ZOMBIE;
    extern void proc_yield(void);
    proc_yield();
    for (;;) cpu_halt(); // nunca debería llegar aquí
}

const char* proc_current_name(void) {
    return current ? current->name : "kernel";
}

uint32_t proc_current_pid(void) {
    return current ? current->pid : 0;
}

Region* proc_current_region(void) {
    return current ? current->region : NULL;
}

int proc_open_file(const char* path, int flags) {
    if (current == NULL || path == NULL) return -(int)GOS_EINVAL;

    int fd = -1;
    for (int i = 3; i < PROC_MAX_FDS; i++) {
        if (current->fds[i].file == NULL) { fd = i; break; }
    }
    if (fd < 0) return -(int)GOS_EBUSY;

    bool writable = (flags & PROC_O_WRONLY) || (flags & PROC_O_RDWR) ||
                    (flags & PROC_O_CREAT) || (flags & PROC_O_TRUNC);
    bool create = (flags & PROC_O_CREAT) != 0;
    bool trunc  = (flags & PROC_O_TRUNC) != 0;

    FileEntry* f = fs_find(path);
    if (f == NULL) {
        if (!create) return -(int)GOS_ENOENT;
        fs_create_result_t cr = fs_create(path, current->region);
        if (!cr.is_ok) return -(int)cr.error;
        f = cr.value;
    } else if (f->is_dir) {
        return -(int)GOS_EINVAL;
    } else if (trunc && writable) {
        f->size = 0;
        f->data = NULL;
        f->mtime = jiffies;
    }

    current->fds[fd].file = f;
    current->fds[fd].offset = 0;
    current->fds[fd].writable = writable;
    return fd;
}

int proc_close_file(int fd) {
    if (current == NULL) return -(int)GOS_EINVAL;
    if (fd < 0 || fd >= PROC_MAX_FDS) return -(int)GOS_EINVAL;
    if (current->fds[fd].file == NULL) return -(int)GOS_EINVAL;
    current->fds[fd].file = NULL;
    current->fds[fd].offset = 0;
    current->fds[fd].writable = false;
    return 0;
}

int proc_read_file(int fd, void* buf, size_t count) {
    if (current == NULL || buf == NULL) return -(int)GOS_EINVAL;
    if (fd < 0 || fd >= PROC_MAX_FDS) return -(int)GOS_EINVAL;
    FileEntry* f = current->fds[fd].file;
    if (f == NULL) return -(int)GOS_EINVAL;

    fs_read_result_t r = fs_read(f, current->fds[fd].offset, count);
    if (!r.is_ok) return -(int)r.error;

    uint32_t size = fs_size_of(f);
    uint32_t remain = size > current->fds[fd].offset ? size - current->fds[fd].offset : 0;
    uint32_t to_copy = (uint32_t)count < remain ? (uint32_t)count : remain;
    if (to_copy > 0) memcpy(buf, r.value, to_copy);
    current->fds[fd].offset += to_copy;
    return (int)to_copy;
}

int proc_write_file(int fd, const void* buf, size_t count) {
    if (current == NULL || buf == NULL) return -(int)GOS_EINVAL;
    if (fd < 0 || fd >= PROC_MAX_FDS) return -(int)GOS_EINVAL;
    FileEntry* f = current->fds[fd].file;
    if (f == NULL || !current->fds[fd].writable) return -(int)GOS_EINVAL;

    fs_size_result_t r = fs_append(f, (const uint8_t*)buf, count, current->region);
    if (!r.is_ok) return -(int)r.error;
    current->fds[fd].offset = fs_size_of(f);
    return (int)r.value;
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

                tss_set_esp0((uint32_t)current->kstack_top);

                // Si el proceso que despertamos somos nosotros mismos no hace falta
                // cambiar de contexto; volvemos directo a proc_sleep_on()/kb_getchar/etc.
                if (prev == current) {
                    return;
                }

                // Activar el espacio de direcciones del proceso que va a correr.
                paging_switch_dir(current->page_dir);

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
            tss_set_esp0((uint32_t)current->kstack_top);
            paging_switch_dir(current->page_dir);
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
