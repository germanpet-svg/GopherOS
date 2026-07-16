// filesystem.c - FS con directorios reales + persistencia opcional a disco.
//
// Modelo: árbol de nodos (archivo o directorio) en un array plano, cada uno
// apuntando a su padre por índice — igual de simple que el diseño flat
// original, pero ahora soporta jerarquía de verdad (mkdir/cd/ls por
// directorio). La raíz "/" es el nodo 0, creado en fs_init().
//
// Persistencia: fs_save()/fs_load() serializan todo el árbol a un formato
// propio simple (encabezado + tabla de nodos + datos concatenados) y lo
// escriben/leen del disco ATA sector por sector, en streaming — sin
// necesitar un buffer gigante en memoria para armar la imagen completa.

#include "filesystem.h"
#include "types.h"
#include "string.h"
#include "typesafe.h"
#include "kresults.h"
#include "disk.h"
#define MAX_FILES 256
#define MAX_FILE_SIZE (64 * 1024)
#define MAX_NAME 32
#define ROOT_IDX 0
#define FS_MAGIC "GOPHERFS"
#define FS_VERSION 1

extern volatile uint32_t jiffies;

static FileEntry files[MAX_FILES];
static SpinLock fs_lock;

void fs_init(void) {
    memset(files, 0, sizeof(files));
    files[ROOT_IDX].exists = true;
    files[ROOT_IDX].is_dir = true;
    files[ROOT_IDX].parent = -1;
    strncpy(files[ROOT_IDX].name, "/", MAX_NAME);
}

static int fs_alloc_node(void) {
    for (int i = 1; i < MAX_FILES; i++) { // 0 es la raiz, nunca se reusa
        if (!files[i].exists) return i;
    }
    return -1;
}

// Busca un hijo directo de `parent` cuyo nombre sea `name`. -1 si no existe.
static int fs_find_child(int parent, const char* name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].exists && files[i].parent == parent && strcmp(files[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Resuelve una ruta completa ("/a/b/c") a un indice de nodo, o -1 si algun
// componente no existe. Ignora barras repetidas/vacias.
static int fs_resolve(const char* path) {
    if (path == NULL || path[0] == 0) return -1;
    int cur = ROOT_IDX;
    if (strcmp(path, "/") == 0) return ROOT_IDX;

    char component[MAX_NAME];
    size_t ci = 0;
    for (const char* p = path; ; p++) {
        if (*p == '/' || *p == 0) {
            if (ci > 0) {
                component[ci] = 0;
                int next = fs_find_child(cur, component);
                if (next < 0) return -1;
                cur = next;
                ci = 0;
            }
            if (*p == 0) break;
        } else if (ci < MAX_NAME - 1) {
            component[ci++] = *p;
        }
    }
    return cur;
}

// Separa `path` en (directorio padre, nombre de la hoja). Si create_dirs es
// true, crea cualquier directorio intermedio que falte (estilo "mkdir -p").
// Devuelve el indice del padre, o -1 si algo fallo.
static int fs_split_parent(const char* path, char* leaf_out, bool create_dirs) {
    if (path == NULL || path[0] == 0) return -1;

    int cur = ROOT_IDX;
    char component[MAX_NAME];
    size_t ci = 0;
    char last_component[MAX_NAME] = {0};
    bool have_pending = false;

    for (const char* p = path; ; p++) {
        if (*p == '/' || *p == 0) {
            if (ci > 0) {
                component[ci] = 0;
                if (have_pending) {
                    // el componente anterior era un directorio intermedio
                    int next = fs_find_child(cur, last_component);
                    if (next < 0) {
                        if (!create_dirs) return -1;
                        int slot = fs_alloc_node();
                        if (slot < 0) return -1;
                        files[slot].exists = true;
                        files[slot].is_dir = true;
                        files[slot].parent = cur;
                        strncpy(files[slot].name, last_component, MAX_NAME);
                        files[slot].ctime = files[slot].mtime = jiffies;
                        next = slot;
                    } else if (!files[next].is_dir) {
                        return -1; // hay un archivo con ese nombre en el medio
                    }
                    cur = next;
                }
                strncpy(last_component, component, MAX_NAME);
                have_pending = true;
                ci = 0;
            }
            if (*p == 0) break;
        } else if (ci < MAX_NAME - 1) {
            component[ci++] = *p;
        }
    }

    if (!have_pending) return -1; // path vacio o solo "/"
    strncpy(leaf_out, last_component, MAX_NAME);
    return cur;
}

fs_create_result_t fs_create(const char* path, Region* r) {
    (void)r;
    spin_lock(&fs_lock);

    char leaf[MAX_NAME];
    int parent = fs_split_parent(path, leaf, true);
    if (parent < 0) { spin_unlock(&fs_lock); fs_create_result_t res = Err(GOS_EINVAL); return res; }

    int existing = fs_find_child(parent, leaf);
    if (existing >= 0) {
        if (files[existing].is_dir) { spin_unlock(&fs_lock); fs_create_result_t res = Err(GOS_EINVAL); return res; }
        // sobreescribir archivo existente (trunca)
        files[existing].size = 0;
        files[existing].data = NULL;
        files[existing].mtime = jiffies;
        spin_unlock(&fs_lock);
        fs_create_result_t res = Ok(&files[existing]);
        return res;
    }

    int slot = fs_alloc_node();
    if (slot < 0) { spin_unlock(&fs_lock); fs_create_result_t res = Err(GOS_ENOSPC); return res; }

    strncpy(files[slot].name, leaf, MAX_NAME);
    files[slot].is_dir = false;
    files[slot].parent = parent;
    files[slot].size = 0;
    files[slot].data = NULL;
    files[slot].exists = true;
    files[slot].ctime = jiffies;
    files[slot].mtime = jiffies;

    spin_unlock(&fs_lock);
    fs_create_result_t res = Ok(&files[slot]);
    return res;
}

gos_result_t fs_mkdir(const char* path) {
    spin_lock(&fs_lock);
    char leaf[MAX_NAME];
    int parent = fs_split_parent(path, leaf, true);
    if (parent < 0) { spin_unlock(&fs_lock); return GOS_EINVAL; }

    if (fs_find_child(parent, leaf) >= 0) { spin_unlock(&fs_lock); return GOS_EINVAL; }

    int slot = fs_alloc_node();
    if (slot < 0) { spin_unlock(&fs_lock); return GOS_ENOSPC; }

    strncpy(files[slot].name, leaf, MAX_NAME);
    files[slot].is_dir = true;
    files[slot].parent = parent;
    files[slot].exists = true;
    files[slot].ctime = files[slot].mtime = jiffies;

    spin_unlock(&fs_lock);
    return GOS_OK;
}

gos_result_t fs_rmdir(const char* path) {
    spin_lock(&fs_lock);
    int idx = fs_resolve(path);
    if (idx < 0 || idx == ROOT_IDX || !files[idx].is_dir) { spin_unlock(&fs_lock); return GOS_EINVAL; }

    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].exists && files[i].parent == idx) {
            spin_unlock(&fs_lock);
            return GOS_EBUSY; // no esta vacio
        }
    }
    files[idx].exists = false;
    spin_unlock(&fs_lock);
    return GOS_OK;
}

fs_size_result_t fs_append(FileEntry* f, const uint8_t* data, size_t len, Region* r) {
    if (f == NULL || !f->exists || f->is_dir) { fs_size_result_t res = Err(GOS_EINVAL); return res; }
    if (f->size + len > MAX_FILE_SIZE) { fs_size_result_t res = Err(GOS_ENOSPC); return res; }

    uint8_t* new_data = (uint8_t*)region_alloc(r, f->size + len);
    if (new_data == NULL) { fs_size_result_t res = Err(GOS_ENOMEM); return res; }

    if (f->data != NULL) memcpy(new_data, f->data, f->size);
    memcpy(new_data + f->size, data, len);

    f->data = new_data;
    f->size += len;
    f->mtime = jiffies;

    fs_size_result_t res = Ok(len);
    return res;
}

fs_read_result_t fs_read(FileEntry* f, size_t offset, size_t len) {
    (void)len;
    if (f == NULL || !f->exists || f->is_dir) { fs_read_result_t res = Err(GOS_EINVAL); return res; }
    if (offset >= f->size) { fs_read_result_t res = Err(GOS_EINVAL); return res; }

    fs_read_result_t res = Ok(f->data + offset);
    return res;
}

FileEntry* fs_find(const char* path) {
    int idx = fs_resolve(path);
    if (idx < 0 || !files[idx].exists) return NULL;
    return &files[idx];
}

bool fs_is_dir(FileEntry* f) {
    return f && f->exists && f->is_dir;
}

uint32_t fs_size_of(FileEntry* f) {
    return (f && f->exists && !f->is_dir) ? f->size : 0;
}

// Snapshot para 'ls'/'dir': lista los hijos directos de `path` (o de la
// raiz si path es NULL/vacio). is_dir_out puede ser NULL si no interesa.
int fs_list(const char* path, char names[][32], uint32_t* sizes, bool* is_dir_out, int max) {
    int dir_idx = ROOT_IDX;
    if (path != NULL && path[0] != 0) {
        int r = fs_resolve(path);
        if (r < 0 || !files[r].is_dir) return -1;
        dir_idx = r;
    }
    int n = 0;
    for (int i = 0; i < MAX_FILES && n < max; i++) {
        if (files[i].exists && files[i].parent == dir_idx) {
            strncpy(names[n], files[i].name, 32);
            sizes[n] = files[i].is_dir ? 0 : files[i].size;
            if (is_dir_out) is_dir_out[n] = files[i].is_dir;
            n++;
        }
    }
    return n;
}

gos_result_t fs_delete(const char* path) {
    spin_lock(&fs_lock);
    int idx = fs_resolve(path);
    if (idx < 0 || idx == ROOT_IDX) { spin_unlock(&fs_lock); return GOS_ENOENT; }
    if (files[idx].is_dir) { spin_unlock(&fs_lock); return GOS_EINVAL; } // usar rmdir

    // Nota: el espacio de datos vive en la Region del proceso que lo creo y
    // no se libera individualmente aqui (allocator "por region", no por
    // archivo) — solo se olvida la entrada. Limitacion conocida.
    files[idx].exists = false;
    files[idx].size = 0;
    files[idx].data = NULL;
    spin_unlock(&fs_lock);
    return GOS_OK;
}

gos_result_t fs_rename(const char* old_path, const char* new_path) {
    spin_lock(&fs_lock);
    int idx = fs_resolve(old_path);
    if (idx < 0 || idx == ROOT_IDX) { spin_unlock(&fs_lock); return GOS_ENOENT; }

    char leaf[MAX_NAME];
    int new_parent = fs_split_parent(new_path, leaf, true);
    if (new_parent < 0) { spin_unlock(&fs_lock); return GOS_EINVAL; }
    if (fs_find_child(new_parent, leaf) >= 0) { spin_unlock(&fs_lock); return GOS_EINVAL; }

    files[idx].parent = new_parent; // esto de paso permite "mover" el archivo
    strncpy(files[idx].name, leaf, MAX_NAME);
    files[idx].mtime = jiffies;
    spin_unlock(&fs_lock);
    return GOS_OK;
}

// ============================================================
// Persistencia a disco (ATA PIO) — streaming, sin buffer gigante.
// ============================================================
/*
 * Sector inicial donde guardar la imagen del filesystem.
 * Usamos LBA 2048 (1 MiB) para no pisar el MBR/boot sector del disco.
 */
#define FS_DISK_START_LBA 2048

typedef struct {
    uint8_t buf[512];
    size_t pos;
    uint32_t lba;
} FsIoStream;

static void io_write_flush(FsIoStream* s) {
    if (s->pos == 0) return;
    for (size_t i = s->pos; i < 512; i++) s->buf[i] = 0;
    disk_write_sector(s->lba, s->buf);
    s->lba++;
    s->pos = 0;
}

static void io_write_bytes(FsIoStream* s, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    while (len > 0) {
        size_t chunk = 512 - s->pos;
        if (chunk > len) chunk = len;
        memcpy(s->buf + s->pos, p, chunk);
        s->pos += chunk;
        p += chunk;
        len -= chunk;
        if (s->pos == 512) io_write_flush(s);
    }
}

static bool io_read_fill(FsIoStream* s) {
    if (!disk_read_sector(s->lba, s->buf)) return false;
    s->lba++;
    s->pos = 0;
    return true;
}

static bool io_read_bytes(FsIoStream* s, void* out, size_t len) {
    uint8_t* p = (uint8_t*)out;
    while (len > 0) {
        if (s->pos >= 512) {
            if (!io_read_fill(s)) return false;
        }
        size_t chunk = 512 - s->pos;
        if (chunk > len) chunk = len;
        memcpy(p, s->buf + s->pos, chunk);
        s->pos += chunk;
        p += chunk;
        len -= chunk;
    }
    return true;
}

typedef struct {
    char name[MAX_NAME];
    uint8_t is_dir;
    uint8_t _pad[3];
    int32_t parent;
    uint32_t size;
    uint32_t ctime;
    uint32_t mtime;
} FsOnDiskRecord;

gos_result_t fs_save(void) {
    if (!disk_present()) return GOS_ENOSYS;

    uint32_t num_nodes = 0;
    uint32_t data_bytes = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].exists) {
            num_nodes++;
            if (!files[i].is_dir) data_bytes += files[i].size;
        }
    }

    FsIoStream s;
    s.pos = 0;
    s.lba = FS_DISK_START_LBA;

    char magic[8];
    memcpy(magic, FS_MAGIC, 8);
    io_write_bytes(&s, magic, 8);
    uint32_t version = FS_VERSION;
    io_write_bytes(&s, &version, 4);
    io_write_bytes(&s, &num_nodes, 4);
    io_write_bytes(&s, &data_bytes, 4);

    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].exists) continue;
        FsOnDiskRecord rec;
        memset(&rec, 0, sizeof(rec));
        strncpy(rec.name, files[i].name, MAX_NAME);
        rec.is_dir = files[i].is_dir ? 1 : 0;
        rec.parent = files[i].parent;
        rec.size = files[i].is_dir ? 0 : files[i].size;
        rec.ctime = files[i].ctime;
        rec.mtime = files[i].mtime;
        // Guardamos tambien el indice original para reconstruir el arbol tal cual.
        io_write_bytes(&s, &i, 4);
        io_write_bytes(&s, &rec, sizeof(rec));
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].exists && !files[i].is_dir && files[i].size > 0) {
            io_write_bytes(&s, files[i].data, files[i].size);
        }
    }

    io_write_flush(&s);
    return GOS_OK;
}

gos_result_t fs_load(Region* r) {
    if (!disk_present()) return GOS_ENOSYS;
    if (r == NULL) return GOS_EINVAL;

    FsIoStream s;
    s.pos = 512; // fuerza a leer el primer sector en el primer io_read_bytes
    s.lba = FS_DISK_START_LBA;

    char magic[8];
    if (!io_read_bytes(&s, magic, 8)) return GOS_ENOENT;
    if (memcmp(magic, FS_MAGIC, 8) != 0) return GOS_EINVAL;

    uint32_t version, num_nodes, data_bytes;
    if (!io_read_bytes(&s, &version, 4)) return GOS_EINVAL;
    if (!io_read_bytes(&s, &num_nodes, 4)) return GOS_EINVAL;
    if (!io_read_bytes(&s, &data_bytes, 4)) return GOS_EINVAL;
    if (version != FS_VERSION) return GOS_EINVAL;
    if (num_nodes > MAX_FILES) return GOS_EINVAL;

    // Reinicia el arbol en memoria antes de reconstruirlo desde disco.
    fs_init();

    for (uint32_t k = 0; k < num_nodes; k++) {
        int32_t idx;
        FsOnDiskRecord rec;
        if (!io_read_bytes(&s, &idx, 4)) return GOS_EINVAL;
        if (!io_read_bytes(&s, &rec, sizeof(rec))) return GOS_EINVAL;
        if (idx < 0 || idx >= MAX_FILES) return GOS_EINVAL;
        if (idx == ROOT_IDX) continue; // la raiz ya esta creada por fs_init()

        strncpy(files[idx].name, rec.name, MAX_NAME);
        files[idx].is_dir = rec.is_dir != 0;
        files[idx].parent = rec.parent;
        files[idx].size = rec.size;
        files[idx].ctime = rec.ctime;
        files[idx].mtime = rec.mtime;
        files[idx].exists = true;
        files[idx].data = NULL;
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].exists && !files[i].is_dir && files[i].size > 0) {
            uint8_t* data = (uint8_t*)region_alloc(r, files[i].size);
            if (data == NULL) return GOS_ENOMEM;
            if (!io_read_bytes(&s, data, files[i].size)) return GOS_EINVAL;
            files[i].data = data;
        }
    }

    return GOS_OK;
}
