// shell.c - Shell interactivo por teclado sobre el scheduler cooperativo.
//
// Corre como un proceso más (creado con proc_create), exactamente igual que
// gopherd o cualquier otro. No hay nada "especial" en él a nivel de kernel.

#include "types.h"
#include "hal.h"
#include "string.h"
#include "vga.h"
#include "keyboard.h"
#include "typesafe.h"
#include "kresults.h"
#include "rtc.h"
#include "graphics.h"
#include "filesystem.h"
#include "disk.h"

extern volatile uint32_t jiffies;
extern uint32_t proc_current_pid(void);
extern void proc_exit(int code);
extern int proc_list(uint32_t* pids, char names[][16], int* states, int max);
extern proc_result_t proc_create(const char* name, void (*entry)(void));
extern proc_result_t proc_create_gxe(const char* name, const uint8_t* data, size_t size);
extern void gopherpy_demo_main(void);

static Region* shell_region = NULL; // para 'copy'/'load' (necesitan alocar espacio para datos)
static char cwd[128] = "/";

#define LINE_MAX 128
#define MAX_ARGS 8

static const char* proc_state_name(int s) {
    switch (s) {
        case 1: return "listo";
        case 2: return "corriendo";
        case 3: return "bloqueado";
        case 4: return "zombi";
        default: return "?";
    }
}

// Separa `line` en tokens (whitespace simple, sin comillas). Modifica `line`
// in-place (inserta '\0'), y llena argv con punteros dentro de line.
static int tokenize(char* line, char* argv[], int max_args) {
    int argc = 0;
    char* p = line;
    while (*p && argc < max_args) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) { *p = 0; p++; }
    }
    return argc;
}

// Combina cwd + un argumento (relativo o absoluto) en una ruta absoluta.
// Si arg ya empieza con '/', se usa tal cual.
static void join_path(const char* arg, char* out, size_t out_size) {
    if (arg[0] == '/') {
        strncpy(out, arg, out_size);
        return;
    }
    if (strcmp(cwd, "/") == 0) {
        out[0] = '/';
        strncpy(out + 1, arg, out_size - 1);
    } else {
        size_t cl = strlen(cwd);
        strncpy(out, cwd, out_size);
        if (cl < out_size - 1) { out[cl] = '/'; out[cl + 1] = 0; }
        strncpy(out + cl + 1, arg, out_size > cl + 1 ? out_size - cl - 1 : 0);
    }
}

static void cmd_help(void) {
    vga_puts(
        "Comandos disponibles (funcionan en estilo Unix y DOS por igual):\n"
        "  help                  - esta ayuda\n"
        "  echo <texto>          - imprime texto\n"
        "  ls | dir [ruta]       - lista un directorio\n"
        "  cat | type <archivo>  - muestra el contenido de un archivo\n"
        "  del | rm <archivo>    - borra un archivo\n"
        "  ren | mv <a> <b>      - renombra/mueve un archivo\n"
        "  copy | cp <a> <b>     - copia un archivo\n"
        "  cd <ruta>             - cambia de directorio\n"
        "  pwd                   - muestra el directorio actual\n"
        "  md | mkdir <nombre>   - crea un directorio\n"
        "  rd | rmdir <nombre>   - borra un directorio vacio\n"
        "  save                  - guarda el filesystem al disco (ATA)\n"
        "  load                  - carga el filesystem desde el disco\n"
        "  ps                    - lista los procesos activos\n"
        "  jiffies               - ticks del timer desde el arranque\n"
        "  clear | cls           - limpia la pantalla\n"
        "  date                  - fecha real (reloj de hardware CMOS/RTC)\n"
        "  time                  - hora real (reloj de hardware CMOS/RTC)\n"
        "  ver                   - version del kernel\n"
        "  vol                   - etiqueta del volumen\n"
        "  demo                  - prueba el modo grafico VGA 320x200x256\n"
        "  gopherpy              - corre el primer programa GopherPy (demo.py)\n"
        "  ring3demo             - corre un proceso real en ring3 (CPL3)\n"
        "  about                 - info del kernel\n"
        "  exit                  - termina el shell\n"
    );
}

static void cmd_ls(int argc, char* argv[]) {
    char path[128];
    const char* target = cwd;
    if (argc >= 2) {
        join_path(argv[1], path, sizeof(path));
        target = path;
    }

    char names[32][32];
    uint32_t sizes[32];
    bool is_dir[32];
    int n = fs_list(strcmp(target, "/") == 0 ? "" : target, names, sizes, is_dir, 32);
    if (n < 0) {
        vga_puts("dir: no existe o no es un directorio\n");
        return;
    }
    vga_puts(" Directorio de "); vga_puts(target); vga_puts("\n\n");
    uint32_t total = 0;
    int file_count = 0;
    for (int i = 0; i < n; i++) {
        vga_puts("  ");
        vga_puts(names[i]);
        if (is_dir[i]) {
            vga_puts("  <DIR>\n");
        } else {
            vga_puts("  ");
            vga_put_dec((int32_t)sizes[i]);
            vga_puts(" bytes\n");
            total += sizes[i];
            file_count++;
        }
    }
    if (n == 0) vga_puts("  (vacio)\n");
    vga_puts("\n");
    vga_put_dec(file_count);
    vga_puts(" archivo(s), ");
    vga_put_dec((int32_t)total);
    vga_puts(" bytes en total\n");
}

static void cmd_cat(int argc, char* argv[]) {
    if (argc < 2) { vga_puts("uso: cat <archivo>\n"); return; }
    char path[128]; join_path(argv[1], path, sizeof(path));

    FileEntry* f = fs_find(path);
    if (!f) { vga_puts("cat: no existe '"); vga_puts(argv[1]); vga_puts("'\n"); return; }
    if (fs_is_dir(f)) { vga_puts("cat: '"); vga_puts(argv[1]); vga_puts("' es un directorio\n"); return; }

    uint32_t size = fs_size_of(f);
    fs_read_result_t r = fs_read(f, 0, size);
    if (!r.is_ok) { vga_puts("cat: error de lectura\n"); return; }
    for (uint32_t i = 0; i < size; i++) vga_putc((char)r.value[i]);
}

static void cmd_ps(void) {
    uint32_t pids[8];
    char names[8][16];
    int states[8];
    int n = proc_list(pids, names, states, 8);
    vga_puts("  PID  ESTADO      NOMBRE\n");
    for (int i = 0; i < n; i++) {
        vga_puts("  ");
        vga_put_dec((int32_t)pids[i]);
        vga_puts("    ");
        vga_puts(proc_state_name(states[i]));
        vga_puts("    ");
        vga_puts(names[i]);
        vga_puts("\n");
    }
}

static void cmd_about(void) {
    vga_puts(
        "GopherOS - kernel didactico con seguridad por diseno\n"
        "Option/Result, region allocator, IRQ top/bottom-half,\n"
        "scheduler cooperativo, 15 syscalls estables, driver PS/2,\n"
        "disco ATA PIO, filesystem con directorios reales.\n"
    );
}

static void cmd_ver(void) {
    vga_puts("GopherOS [Version 0.4] - kernel x86 didactico\n");
}

static void cmd_vol(void) {
    vga_puts(" El volumen no tiene etiqueta.\n");
    vga_puts(" El volumen es GOPHEROS\n");
}

static void cmd_del(int argc, char* argv[]) {
    if (argc < 2) { vga_puts("uso: del <archivo>\n"); return; }
    char path[128]; join_path(argv[1], path, sizeof(path));
    gos_result_t r = fs_delete(path);
    if (r != GOS_OK) { vga_puts("No se pudo borrar '"); vga_puts(argv[1]); vga_puts("'\n"); }
}

static void cmd_ren(int argc, char* argv[]) {
    if (argc < 3) { vga_puts("uso: ren <viejo> <nuevo>\n"); return; }
    char oldp[128], newp[128];
    join_path(argv[1], oldp, sizeof(oldp));
    join_path(argv[2], newp, sizeof(newp));
    gos_result_t r = fs_rename(oldp, newp);
    if (r != GOS_OK) { vga_puts("No se pudo renombrar '"); vga_puts(argv[1]); vga_puts("'\n"); }
}

static void cmd_copy(int argc, char* argv[]) {
    if (argc < 3) { vga_puts("uso: copy <origen> <destino>\n"); return; }
    char srcp[128], dstp[128];
    join_path(argv[1], srcp, sizeof(srcp));
    join_path(argv[2], dstp, sizeof(dstp));

    FileEntry* src = fs_find(srcp);
    if (!src) { vga_puts("copy: no existe '"); vga_puts(argv[1]); vga_puts("'\n"); return; }
    if (fs_is_dir(src)) { vga_puts("copy: '"); vga_puts(argv[1]); vga_puts("' es un directorio\n"); return; }

    uint32_t size = fs_size_of(src);
    fs_read_result_t rd = fs_read(src, 0, size);
    if (!rd.is_ok) { vga_puts("copy: error de lectura\n"); return; }

    fs_create_result_t created = fs_create(dstp, shell_region);
    if (!created.is_ok) { vga_puts("copy: no se pudo crear el destino\n"); return; }

    fs_size_result_t wr = fs_append(created.value, rd.value, size, shell_region);
    if (!wr.is_ok) { vga_puts("copy: sin espacio en la region del shell\n"); return; }

    vga_put_dec((int32_t)size);
    vga_puts(" byte(s) copiado(s)\n");
}

static void cmd_cd(int argc, char* argv[]) {
    if (argc < 2) { vga_puts(cwd); vga_putc('\n'); return; }

    if (strcmp(argv[1], "..") == 0) {
        if (strcmp(cwd, "/") == 0) return; // ya en la raiz
        char* last_slash = cwd;
        for (char* p = cwd; *p; p++) if (*p == '/') last_slash = p;
        if (last_slash == cwd) cwd[1] = 0; // subimos hasta "/"
        else *last_slash = 0;
        return;
    }

    char path[128]; join_path(argv[1], path, sizeof(path));

    if (strcmp(path, "/") == 0) { strncpy(cwd, "/", sizeof(cwd)); return; }

    FileEntry* f = fs_find(path);
    if (!f || !fs_is_dir(f)) { vga_puts("cd: no existe el directorio '"); vga_puts(argv[1]); vga_puts("'\n"); return; }
    strncpy(cwd, path, sizeof(cwd));
}

static void cmd_pwd(void) {
    vga_puts(cwd);
    vga_putc('\n');
}

static void cmd_mkdir(int argc, char* argv[]) {
    if (argc < 2) { vga_puts("uso: mkdir <nombre>\n"); return; }
    char path[128]; join_path(argv[1], path, sizeof(path));
    gos_result_t r = fs_mkdir(path);
    if (r != GOS_OK) { vga_puts("mkdir: no se pudo crear '"); vga_puts(argv[1]); vga_puts("'\n"); }
}

static void cmd_rmdir(int argc, char* argv[]) {
    if (argc < 2) { vga_puts("uso: rmdir <nombre>\n"); return; }
    char path[128]; join_path(argv[1], path, sizeof(path));
    gos_result_t r = fs_rmdir(path);
    if (r == GOS_EBUSY) { vga_puts("rmdir: el directorio no esta vacio\n"); }
    else if (r != GOS_OK) { vga_puts("rmdir: no se pudo borrar '"); vga_puts(argv[1]); vga_puts("'\n"); }
}

static void cmd_save(void) {
    if (!disk_present()) { vga_puts("save: no hay disco ATA detectado\n"); return; }
    gos_result_t r = fs_save();
    vga_puts(r == GOS_OK ? "Filesystem guardado en el disco.\n" : "save: error al guardar\n");
}

static void cmd_load(void) {
    if (!disk_present()) { vga_puts("load: no hay disco ATA detectado\n"); return; }
    gos_result_t r = fs_load(shell_region);
    if (r == GOS_OK) {
        strncpy(cwd, "/", sizeof(cwd));
        vga_puts("Filesystem cargado desde el disco.\n");
    } else if (r == GOS_ENOENT) {
        vga_puts("load: el disco no tiene datos validos (probaste 'save' antes?)\n");
    } else {
        vga_puts("load: error al cargar\n");
    }
}

static const char* month_name(uint8_t m) {
    static const char* names[] = {"???","Ene","Feb","Mar","Abr","May","Jun",
                                   "Jul","Ago","Sep","Oct","Nov","Dic"};
    return (m >= 1 && m <= 12) ? names[m] : names[0];
}

static void put2(uint32_t v) {
    if (v < 10) vga_putc('0');
    vga_put_dec((int32_t)v);
}

static void cmd_date(void) {
    rtc_datetime_t dt;
    rtc_read(&dt);
    vga_puts("La fecha es: ");
    put2(dt.day); vga_putc('-'); vga_puts(month_name(dt.month)); vga_putc('-');
    vga_put_dec(dt.year);
    vga_puts(" (leido del reloj de hardware CMOS/RTC real)\n");
}

static void cmd_time(void) {
    rtc_datetime_t dt;
    rtc_read(&dt);
    vga_puts("La hora es: ");
    put2(dt.hour); vga_putc(':'); put2(dt.minute); vga_putc(':'); put2(dt.second);
    vga_puts(" (leido del reloj de hardware CMOS/RTC real)\n");
}

// Demo grafica: prueba end-to-end del modo VGA 320x200x256 real (no texto).
// Sirve como prueba de concepto para el dia que se porte algo tipo GWBASIC:
// SCREEN 1 / PSET son, literalmente, estas dos llamadas.
static void cmd_demo(void) {
    vga_puts("Cambiando a modo grafico VGA 320x200x256 (modo 13h)...\n");
    vga_puts("Presiona cualquier tecla para volver al modo texto.\n");

    gfx_set_mode(GFX_MODE_VGA_320x200);
    gfx_clear(1); // azul oscuro (paleta VGA por defecto)

    gfx_rect(20, 20, 80, 50, 4);   // rectangulo rojo
    gfx_rect(120, 20, 80, 50, 2);  // rectangulo verde
    for (int i = 0; i < 100; i++) {
        gfx_putpixel(200 + i, 120 + (i / 2), 14); // linea diagonal amarilla
    }
    for (int y = 150; y < 190; y++) {
        gfx_hline(20, y, 260, (uint8_t)(y % 16)); // barras de color (paleta completa)
    }

    kb_getchar(); // bloquea (via proc_sleep_on) hasta la proxima tecla

    gfx_set_mode(GFX_MODE_TEXT_80x25);
    vga_clear();
    vga_puts("De vuelta en modo texto.\n");
}

void shell_main(void) {
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("\n[shell] listo (pid=");
    vga_put_dec((int32_t)proc_current_pid());
    vga_puts("). Escribe 'help' para ver los comandos.\n\n");

    shell_region = region_new(65536);

    char line[LINE_MAX];
    char* argv[MAX_ARGS];

    for (;;) {
        vga_set_color(VGA_LGREEN, VGA_BLACK);
        vga_puts("gopheros:");
        vga_puts(cwd);
        vga_puts("> ");
        vga_set_color(VGA_WHITE, VGA_BLACK);

        kb_readline(line, LINE_MAX);
        int argc = tokenize(line, argv, MAX_ARGS);
        if (argc == 0) continue;

        const char* cmd = argv[0];

        if (strcmp(cmd, "help") == 0) {
            cmd_help();
        } else if (strcmp(cmd, "echo") == 0) {
            for (int i = 1; i < argc; i++) {
                vga_puts(argv[i]);
                if (i + 1 < argc) vga_putc(' ');
            }
            vga_putc('\n');
        } else if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0) {
            cmd_ls(argc, argv);
        } else if (strcmp(cmd, "cat") == 0 || strcmp(cmd, "type") == 0) {
            cmd_cat(argc, argv);
        } else if (strcmp(cmd, "del") == 0 || strcmp(cmd, "rm") == 0) {
            cmd_del(argc, argv);
        } else if (strcmp(cmd, "ren") == 0 || strcmp(cmd, "mv") == 0) {
            cmd_ren(argc, argv);
        } else if (strcmp(cmd, "copy") == 0 || strcmp(cmd, "cp") == 0) {
            cmd_copy(argc, argv);
        } else if (strcmp(cmd, "cd") == 0) {
            cmd_cd(argc, argv);
        } else if (strcmp(cmd, "pwd") == 0) {
            cmd_pwd();
        } else if (strcmp(cmd, "md") == 0 || strcmp(cmd, "mkdir") == 0) {
            cmd_mkdir(argc, argv);
        } else if (strcmp(cmd, "rd") == 0 || strcmp(cmd, "rmdir") == 0) {
            cmd_rmdir(argc, argv);
        } else if (strcmp(cmd, "save") == 0) {
            cmd_save();
        } else if (strcmp(cmd, "load") == 0) {
            cmd_load();
        } else if (strcmp(cmd, "ps") == 0) {
            cmd_ps();
        } else if (strcmp(cmd, "jiffies") == 0) {
            vga_put_dec((int32_t)jiffies);
            vga_putc('\n');
        } else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
            vga_clear();
        } else if (strcmp(cmd, "date") == 0) {
            cmd_date();
        } else if (strcmp(cmd, "time") == 0) {
            cmd_time();
        } else if (strcmp(cmd, "ver") == 0) {
            cmd_ver();
        } else if (strcmp(cmd, "vol") == 0) {
            cmd_vol();
        } else if (strcmp(cmd, "demo") == 0) {
            cmd_demo();
        } else if (strcmp(cmd, "gopherpy") == 0) {
            proc_result_t p = proc_create("gopherpy_demo", gopherpy_demo_main);
            if (!p.is_ok) vga_puts("gopherpy: no se pudo crear el proceso\n");
            else vga_puts("[shell] gopherpy_demo lanzado, cediendo turno...\n");
        } else if (strcmp(cmd, "ring3demo") == 0) {
            FileEntry* f = fs_find("/ring3demo.gxe");
            if (f == NULL || f->is_dir) f = fs_find("/demo/ring3demo.gxe");
            if (f == NULL || f->is_dir) {
                vga_puts("ring3demo: /ring3demo.gxe no encontrado\n");
            } else {
                proc_result_t p = proc_create_gxe("ring3_demo", f->data, f->size);
                if (!p.is_ok) vga_puts("ring3demo: no se pudo crear el proceso\n");
                else vga_puts("[shell] ring3_demo.gxe lanzado en CPL3 real...\n");
            }
        } else if (strcmp(cmd, "about") == 0) {
            cmd_about();
        } else if (strcmp(cmd, "exit") == 0) {
            vga_puts("[shell] hasta luego.\n");
            proc_exit(0);
        } else {
            vga_puts(argv[0]);
            vga_puts(": comando no encontrado (prueba 'help')\n");
        }
    }
}
