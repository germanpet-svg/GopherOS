// multiboot.c - Carga archivos desde modulos Multiboot al filesystem de RAM.

#include "types.h"
#include "string.h"
#include "typesafe.h"
#include "filesystem.h"
#include "multiboot.h"

#define MB_FLAG_MODS (1u << 3)

static void ensure_dir(const char* path, Region* r) {
    (void)r;
    // Crea directorios intermedios simples (solo /demo de momento).
    const char* p = path + 1;
    const char* slash = NULL;
    for (const char* q = p; *q; q++) {
        if (*q == '/') { slash = q; break; }
    }
    if (slash) {
        char dir[128];
        size_t len = (size_t)(slash - path);
        if (len < sizeof(dir)) {
            memcpy(dir, path, len);
            dir[len] = 0;
            fs_mkdir(dir);
        }
    }
}

void multiboot_load_files(uint32_t mb_info, Region* r) {
    if (mb_info == 0) return;

    multiboot_info_t* info = (multiboot_info_t*)mb_info;
    if (!(info->flags & MB_FLAG_MODS) || info->mods_count == 0) return;

    multiboot_module_t* mods = (multiboot_module_t*)info->mods_addr;
    for (uint32_t i = 0; i < info->mods_count; i++) {
        const char* cmd = (const char*)mods[i].cmdline;
        if (cmd == NULL || cmd[0] == 0) continue;

        const char* path = cmd;
        if (path[0] != '/') continue;

        ensure_dir(path, r);
        fs_create_result_t fr = fs_create(path, r);
        if (!fr.is_ok) continue;

        size_t len = mods[i].mod_end - mods[i].mod_start;
        const uint8_t* data = (const uint8_t*)mods[i].mod_start;
        fs_append(fr.value, data, len, r);
    }
}
