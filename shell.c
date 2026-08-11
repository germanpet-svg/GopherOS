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

extern volatile uint32_t jiffies;
extern uint32_t proc_current_pid(void);
extern void proc_exit(int code);
extern int proc_list(uint32_t* pids, char names[][16], int* states, int max);
extern bool disk_present(void);
extern bool nic_present(void);
extern bool net_ping(uint32_t target_ip);
extern void net_ip_to_str(uint32_t ip, char* out);
extern proc_result_t proc_create(const char* name, void (*entry)(void));
extern proc_result_t proc_create_ring3(const char* name, void (*entry)(void), size_t user_stack_size);
extern void gopherpy_demo_main(void);
extern void ring3_demo_main(void);
extern void ring3_demo_mem_main(void);
extern void gopher_server_main(void);
extern void ring3_victim_main(void);
extern void ring3_attacker_main(void);
extern void debug_set_test_arg(uint32_t v);

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
        "  edit <archivo>        - editor de texto (:l :d :r :i para editar lineas)\n"
        "  run <archivo>         - ejecuta un archivo como script de comandos\n"
        "  ps                    - lista los procesos activos\n"
        "  kill <pid>            - saca un proceso de la tabla (libera el slot)\n"
        "  exec <archivo.elf>    - carga y corre un binario ELF32 externo\n"
        "  jiffies               - ticks del timer desde el arranque\n"
        "  clear | cls           - limpia la pantalla\n"
        "  date                  - fecha real (reloj de hardware CMOS/RTC)\n"
        "  time                  - hora real (reloj de hardware CMOS/RTC)\n"
        "  ver                   - version del kernel\n"
        "  vol                   - etiqueta del volumen\n"
        "  demo                  - prueba el modo grafico VGA 320x200x256\n"
        "  gopherpy              - corre el primer programa GopherPy (demo.py)\n"
        "  ring3demo             - corre un proceso real en ring3 (CPL3)\n"
        "  ring3mem              - prueba que ring3 NO puede tocar memoria del kernel\n"
        "  ping [ip]             - hace ping (ICMP echo) - sin ip, prueba el gateway\n"
        "  gopherserve           - levanta el servidor Gopher real en el puerto 70\n"
        "  ring3victim           - proceso ring3 que expone su direccion (para probar)\n"
        "  ring3attack <hex>     - otro proceso ring3 intenta escribir esa direccion\n"
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

static bool parse_ip(const char* s, uint32_t* out) {
    uint32_t parts[4] = {0, 0, 0, 0};
    int part = 0;
    const char* p = s;
    while (*p && part < 4) {
        if (*p >= '0' && *p <= '9') {
            parts[part] = parts[part] * 10 + (uint32_t)(*p - '0');
        } else if (*p == '.') {
            part++;
        } else {
            return false;
        }
        p++;
    }
    if (part != 3) return false;
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return true;
}

static bool parse_hex(const char* s, uint32_t* out) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (!*s) return false;
    uint32_t v = 0;
    for (; *s; s++) {
        char c = *s;
        uint32_t nibble;
        if (c >= '0' && c <= '9') nibble = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') nibble = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') nibble = (uint32_t)(c - 'A' + 10);
        else return false;
        v = (v << 4) | nibble;
    }
    *out = v;
    return true;
}

static void cmd_ping(int argc, char* argv[]) {
    if (!nic_present()) { vga_puts("ping: no hay tarjeta de red detectada\n"); return; }

    uint32_t target = 0x0A000202; // 10.0.2.2 (gateway QEMU user-net por defecto)
    if (argc >= 2) {
        if (!parse_ip(argv[1], &target)) { vga_puts("ping: direccion invalida\n"); return; }
    }

    char ipstr[16];
    net_ip_to_str(target, ipstr);
    vga_puts("PING "); vga_puts(ipstr); vga_puts(": ");

    bool ok = net_ping(target);
    vga_puts(ok ? "respuesta recibida\n" : "sin respuesta (timeout)\n");
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

#define EDIT_MAX_LINES 200
#define EDIT_LINE_LEN 120
static char edit_lines[EDIT_MAX_LINES][EDIT_LINE_LEN];
static int edit_n_lines = 0;
static char edit_path[128];
static bool editing = false;

static void edit_print_banner(void) {
    vga_puts("--- editor de GopherOS ---\n");
    vga_puts("Escribi texto para agregarlo al final. Comandos (con ':'):\n");
    vga_puts("  :l            lista las lineas actuales, numeradas\n");
    vga_puts("  :d N          borra la linea N\n");
    vga_puts("  :r N <texto>  reemplaza la linea N\n");
    vga_puts("  :i N <texto>  inserta una linea nueva ANTES de la N\n");
    vga_puts("  :help         muestra esta ayuda de nuevo\n");
    vga_puts("  .             (un punto solo) GUARDA y sale\n");
    vga_puts("  :q            sale SIN guardar\n\n");
}

static void edit_list_lines(void) {
    if (edit_n_lines == 0) { vga_puts("(sin lineas todavia)\n"); return; }
    for (int i = 0; i < edit_n_lines; i++) {
        vga_put_dec(i + 1); vga_puts(": "); vga_puts(edit_lines[i]); vga_putc('\n');
    }
}

// Parsea un numero de linea (1-based) al inicio de `s`, devuelve el indice
// 0-based en `*out_idx` y un puntero al resto de la cadena (sin espacios
// iniciales) en `*out_rest`. Devuelve false si no hay un numero valido.
static bool edit_parse_line_num(const char* s, int* out_idx, const char** out_rest) {
    while (*s == ' ') s++;
    if (*s < '0' || *s > '9') return false;
    int n = 0;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
    while (*s == ' ') s++;
    *out_idx = n - 1;
    *out_rest = s;
    return n >= 1;
}

static void cmd_edit(int argc, char* argv[]) {
    if (argc < 2) { vga_puts("uso: edit <archivo>\n"); return; }
    join_path(argv[1], edit_path, sizeof(edit_path));

    edit_n_lines = 0;
    FileEntry* existing = fs_find(edit_path);
    if (existing && !fs_is_dir(existing)) {
        uint32_t size = fs_size_of(existing);
        fs_read_result_t r = fs_read(existing, 0, size);
        if (r.is_ok) {
            uint32_t i = 0;
            while (i < size && edit_n_lines < EDIT_MAX_LINES) {
                int col = 0;
                while (i < size && r.value[i] != '\n' && col < EDIT_LINE_LEN - 1) {
                    edit_lines[edit_n_lines][col++] = (char)r.value[i++];
                }
                edit_lines[edit_n_lines][col] = 0;
                if (i < size && r.value[i] == '\n') i++;
                edit_n_lines++;
            }
        }
    }

    edit_print_banner();
    if (edit_n_lines > 0) {
        vga_puts("--- contenido actual ---\n");
        edit_list_lines();
        vga_puts("--- fin ---\n\n");
    }

    editing = true; // a partir de aca, el loop de shell_main manda cada linea
                     // a edit_process_line() en vez del dispatcher normal --
                     // NUNCA se llama kb_readline() desde una funcion anidada.
}

// Llamado desde shell_main() por cada linea tipeada MIENTRAS editing==true.
static void edit_process_line(const char* buf) {
    if (strcmp(buf, ".") == 0) {
        editing = false;

        fs_create_result_t created = fs_create(edit_path, shell_region);
        if (!created.is_ok) { vga_puts("edit: no se pudo crear el archivo\n"); return; }

        for (int i = 0; i < edit_n_lines; i++) {
            size_t len = strlen(edit_lines[i]);
            fs_append(created.value, (const uint8_t*)edit_lines[i], len, shell_region);
            fs_append(created.value, (const uint8_t*)"\n", 1, shell_region);
        }

        vga_puts("Guardado (");
        vga_put_dec(edit_n_lines);
        vga_puts(" lineas)\n");
        return;
    }

    if (strcmp(buf, ":q") == 0) {
        editing = false;
        vga_puts("Edicion cancelada (no se guardo).\n");
        return;
    }

    if (strcmp(buf, ":help") == 0) {
        edit_print_banner();
        return;
    }

    if (strcmp(buf, ":l") == 0) {
        edit_list_lines();
        return;
    }

    if (buf[0] == ':' && buf[1] == 'd') {
        int idx; const char* rest;
        if (!edit_parse_line_num(buf + 2, &idx, &rest) || idx < 0 || idx >= edit_n_lines) {
            vga_puts("uso: :d N   (N entre 1 y "); vga_put_dec(edit_n_lines); vga_puts(")\n");
            return;
        }
        for (int i = idx; i < edit_n_lines - 1; i++) {
            strncpy(edit_lines[i], edit_lines[i + 1], EDIT_LINE_LEN);
        }
        edit_n_lines--;
        vga_puts("Linea "); vga_put_dec(idx + 1); vga_puts(" borrada.\n");
        return;
    }

    if (buf[0] == ':' && buf[1] == 'r') {
        int idx; const char* rest;
        if (!edit_parse_line_num(buf + 2, &idx, &rest) || idx < 0 || idx >= edit_n_lines) {
            vga_puts("uso: :r N <texto nuevo>\n");
            return;
        }
        strncpy(edit_lines[idx], rest, EDIT_LINE_LEN);
        vga_puts("Linea "); vga_put_dec(idx + 1); vga_puts(" reemplazada.\n");
        return;
    }

    if (buf[0] == ':' && buf[1] == 'i') {
        int idx; const char* rest;
        if (!edit_parse_line_num(buf + 2, &idx, &rest) || idx < 0 || idx > edit_n_lines) {
            vga_puts("uso: :i N <texto nuevo>   (inserta ANTES de la linea N)\n");
            return;
        }
        if (edit_n_lines >= EDIT_MAX_LINES) {
            vga_puts("(limite de lineas alcanzado)\n");
            return;
        }
        for (int i = edit_n_lines; i > idx; i--) {
            strncpy(edit_lines[i], edit_lines[i - 1], EDIT_LINE_LEN);
        }
        strncpy(edit_lines[idx], rest, EDIT_LINE_LEN);
        edit_n_lines++;
        vga_puts("Linea insertada en la posicion "); vga_put_dec(idx + 1); vga_puts(".\n");
        return;
    }

    if (edit_n_lines < EDIT_MAX_LINES) {
        strncpy(edit_lines[edit_n_lines], buf, EDIT_LINE_LEN);
        edit_n_lines++;
    } else {
        vga_puts("(limite de lineas alcanzado, se ignora esta linea)\n");
    }
}

static void execute_line(char* line);

// cmd_exec() - Carga un ELF32 real desde el filesystem y lo corre como
// proceso ring3 con paginas propias (no compartidas). A diferencia de
// 'run' (que interpreta un archivo de texto como script de comandos del
// shell), esto ejecuta codigo maquina real via proc_load_elf().
static void cmd_exec(int argc, char* argv[]) {
    if (argc < 2) { vga_puts("uso: exec <archivo.elf>\n"); return; }
    char path[128]; join_path(argv[1], path, sizeof(path));

    FileEntry* f = fs_find(path);
    if (!f || fs_is_dir(f)) { vga_puts("exec: no existe '"); vga_puts(argv[1]); vga_puts("'\n"); return; }

    uint32_t size = fs_size_of(f);
    fs_read_result_t r = fs_read(f, 0, size);
    if (!r.is_ok) { vga_puts("exec: error de lectura\n"); return; }

    extern proc_result_t proc_load_elf(const char* name, const uint8_t* elf_data, size_t elf_size, size_t user_stack_size);
    proc_result_t p = proc_load_elf(argv[1], r.value, size, 8192);
    if (!p.is_ok) {
        vga_puts("exec: fallo al cargar ELF (codigo ");
        vga_put_dec((int32_t)p.error);
        vga_puts(")\n");
        return;
    }
    vga_puts("exec: '"); vga_puts(argv[1]); vga_puts("' cargado y listo para correr.\n");
}

// cmd_kill() - Saca un proceso de la tabla por PID: si esta corriendo o
// listo, lo fuerza a terminar (liberando su espacio de direcciones si
// era ring3); si ya es zombi, libera su Region y devuelve el slot para
// que 'exec'/'proc_create' lo puedan reusar. Antes de esto no habia
// forma de recuperar un slot ocupado por un proceso ya terminado.
static void cmd_kill(int argc, char* argv[]) {
    if (argc < 2) { vga_puts("uso: kill <pid>\n"); return; }

    uint32_t pid = 0;
    for (const char* c = argv[1]; *c; c++) {
        if (*c < '0' || *c > '9') { vga_puts("kill: pid invalido '"); vga_puts(argv[1]); vga_puts("'\n"); return; }
        pid = pid * 10 + (uint32_t)(*c - '0');
    }

    extern bool proc_kill(uint32_t pid);
    if (proc_kill(pid)) {
        vga_puts("kill: proceso "); vga_put_dec((int32_t)pid); vga_puts(" removido, slot liberado.\n");
    } else {
        vga_puts("kill: no se pudo (no existe ese pid, o es el proceso que esta corriendo ahora mismo).\n");
    }
}

static void cmd_run(int argc, char* argv[]) {
    if (argc < 2) { vga_puts("uso: run <archivo>\n"); return; }
    char path[128]; join_path(argv[1], path, sizeof(path));

    FileEntry* f = fs_find(path);
    if (!f || fs_is_dir(f)) { vga_puts("run: no existe '"); vga_puts(argv[1]); vga_puts("'\n"); return; }

    uint32_t size = fs_size_of(f);
    fs_read_result_t r = fs_read(f, 0, size);
    if (!r.is_ok) { vga_puts("run: error de lectura\n"); return; }

    char line[LINE_MAX];
    uint32_t i = 0;
    while (i < size) {
        int col = 0;
        while (i < size && r.value[i] != '\n' && col < LINE_MAX - 1) {
            line[col++] = (char)r.value[i++];
        }
        line[col] = 0;
        if (i < size && r.value[i] == '\n') i++;

        char* p = line;
        while (*p == ' ') p++;
        if (*p == 0 || *p == '#') continue; // saltar lineas vacias y comentarios

        vga_set_color(VGA_LGRAY, VGA_BLACK);
        vga_puts("+ "); vga_puts(p); vga_putc('\n');
        vga_set_color(VGA_WHITE, VGA_BLACK);

        execute_line(p);
    }
}

static void execute_line(char* line) {
    char* argv[MAX_ARGS];
    int argc = tokenize(line, argv, MAX_ARGS);
    if (argc == 0) return;

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
    } else if (strcmp(cmd, "edit") == 0) {
        cmd_edit(argc, argv);
    } else if (strcmp(cmd, "run") == 0) {
        cmd_run(argc, argv);
    } else if (strcmp(cmd, "exec") == 0) {
        cmd_exec(argc, argv);
    } else if (strcmp(cmd, "kill") == 0) {
        cmd_kill(argc, argv);
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
        proc_result_t p = proc_create_ring3("ring3_demo", ring3_demo_main, 8192);
        if (!p.is_ok) vga_puts("ring3demo: no se pudo crear el proceso\n");
        else vga_puts("[shell] ring3_demo lanzado en CPL3 de verdad...\n");
    } else if (strcmp(cmd, "ring3mem") == 0) {
        proc_result_t p = proc_create_ring3("ring3_mem", ring3_demo_mem_main, 8192);
        if (!p.is_ok) vga_puts("ring3mem: no se pudo crear el proceso\n");
        else vga_puts("[shell] ring3_demo_mem lanzado (prueba de aislamiento de memoria)...\n");
    } else if (strcmp(cmd, "ping") == 0) {
        cmd_ping(argc, argv);
    } else if (strcmp(cmd, "gopherserve") == 0) {
        if (!nic_present()) { vga_puts("gopherserve: no hay tarjeta de red detectada\n"); }
        else {
            proc_result_t p = proc_create("gopher_srv", gopher_server_main);
            if (!p.is_ok) vga_puts("gopherserve: no se pudo crear el proceso\n");
            else vga_puts("[shell] servidor gopher lanzado en el puerto 70...\n");
        }
    } else if (strcmp(cmd, "ring3victim") == 0) {
        proc_result_t p = proc_create_ring3("ring3_victim", ring3_victim_main, 8192);
        if (!p.is_ok) vga_puts("ring3victim: no se pudo crear el proceso\n");
        else vga_puts("[shell] ring3_victim lanzado, cediendo turno...\n");
    } else if (strcmp(cmd, "ring3attack") == 0) {
        if (argc < 2) { vga_puts("uso: ring3attack <direccion hex, ej. 0x15d018>\n"); }
        else {
            uint32_t target;
            if (!parse_hex(argv[1], &target)) { vga_puts("ring3attack: direccion invalida\n"); }
            else {
                debug_set_test_arg(target);
                proc_result_t p = proc_create_ring3("ring3_atk", ring3_attacker_main, 8192);
                if (!p.is_ok) vga_puts("ring3attack: no se pudo crear el proceso\n");
                else vga_puts("[shell] ring3_attacker lanzado, cediendo turno...\n");
            }
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

void shell_main(void) {
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("\n[shell] listo (pid=");
    vga_put_dec((int32_t)proc_current_pid());
    vga_puts("). Escribe 'help' para ver los comandos.\n\n");

    shell_region = region_new(65536);

    char line[LINE_MAX];

    for (;;) {
        if (editing) {
            vga_set_color(VGA_YELLOW, VGA_BLACK);
            vga_put_dec(edit_n_lines + 1);
        } else {
            vga_set_color(VGA_LGREEN, VGA_BLACK);
            vga_puts("gopheros:");
            vga_puts(cwd);
        }
        vga_puts("> ");
        vga_set_color(VGA_WHITE, VGA_BLACK);

        kb_readline(line, LINE_MAX);

        if (editing) {
            edit_process_line(line);
        } else {
            execute_line(line);
        }
    }
}
