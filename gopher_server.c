// gopher_server.c - Servidor Gopher (RFC 1436) minimo sobre nuestro TCP.
//
// Selector vacio -> menu del directorio raiz (listado del filesystem real).
// Selector = ruta de un archivo -> el contenido crudo de ese archivo.

#include "types.h"
#include "string.h"
#include "tcp.h"
#include "filesystem.h"
#include "kresults.h"

extern int fs_list(const char* path, char names[][32], uint32_t* sizes, bool* is_dir_out, int max);
extern FileEntry* fs_find(const char* path);
extern fs_read_result_t fs_read(FileEntry* f, size_t offset, size_t len);
extern uint32_t fs_size_of(FileEntry* f);
extern bool fs_is_dir(FileEntry* f);
extern void vga_puts(const char* s);

#define GOPHER_PORT 70

static void send_str(tcp_conn_t* conn, const char* s) {
    tcp_send(conn, (const uint8_t*)s, (int)strlen(s));
}

static void serve_menu(tcp_conn_t* conn, const char* path) {
    char names[32][32];
    uint32_t sizes[32];
    bool is_dir[32];
    int n = fs_list(path, names, sizes, is_dir, 32);
    if (n < 0) {
        send_str(conn, "3No existe ese directorio\terror\tlocalhost\t70\r\n.\r\n");
        return;
    }

    for (int i = 0; i < n; i++) {
        char line[160];
        char full_path[128];
        if (path == NULL || path[0] == 0) {
            full_path[0] = '/';
            strncpy(full_path + 1, names[i], sizeof(full_path) - 1);
        } else {
            size_t pl = strlen(path);
            strncpy(full_path, path, sizeof(full_path));
            full_path[pl] = '/';
            strncpy(full_path + pl + 1, names[i], sizeof(full_path) - pl - 1);
        }

        char type = is_dir[i] ? '1' : '0';
        // Formato Gopher: <tipo><nombre>\t<selector>\t<host>\t<puerto>\r\n
        size_t pos = 0;
        line[pos++] = type;
        size_t nlen = strlen(names[i]);
        memcpy(line + pos, names[i], nlen); pos += nlen;
        line[pos++] = '\t';
        size_t flen = strlen(full_path);
        memcpy(line + pos, full_path, flen); pos += flen;
        const char* suffix = "\tlocalhost\t70\r\n";
        size_t sflen = strlen(suffix);
        memcpy(line + pos, suffix, sflen); pos += sflen;
        line[pos] = 0;

        tcp_send(conn, (const uint8_t*)line, (int)pos);
    }

    send_str(conn, ".\r\n");
}

static void serve_file(tcp_conn_t* conn, FileEntry* f) {
    uint32_t size = fs_size_of(f);
    fs_read_result_t r = fs_read(f, 0, size);
    if (!r.is_ok) {
        send_str(conn, "No se pudo leer el archivo.\r\n.\r\n");
        return;
    }
    tcp_send(conn, r.value, (int)size);
    send_str(conn, "\r\n.\r\n");
}

// Maneja una conexion completa: lee el selector, responde, cierra.
static void handle_request(tcp_conn_t* conn) {
    uint8_t buf[256];
    int n = tcp_recv(conn, buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
    buf[n] = 0;

    // El selector viene terminado en CRLF (o LF) — lo recortamos.
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\r' || buf[i] == '\n') { buf[i] = 0; break; }
    }

    const char* selector = (const char*)buf;
    if (selector[0] == 0) {
        serve_menu(conn, "");
    } else {
        FileEntry* f = fs_find(selector);
        if (!f) {
            send_str(conn, "3No existe ese selector\terror\tlocalhost\t70\r\n.\r\n");
        } else if (fs_is_dir(f)) {
            serve_menu(conn, selector);
        } else {
            serve_file(conn, f);
        }
    }
}

void gopher_server_main(void) {
    vga_puts("[gopher] servidor escuchando en el puerto 70...\n");
    for (;;) {
        tcp_conn_t conn;
        if (!tcp_accept(GOPHER_PORT, &conn)) {
            continue; // timeout del accept: reintentar (deja pasar otros procesos)
        }
        vga_puts("[gopher] conexion aceptada, atendiendo pedido...\n");
        handle_request(&conn);
        tcp_close(&conn);
        vga_puts("[gopher] conexion cerrada.\n");
    }
}
