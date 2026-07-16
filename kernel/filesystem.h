// filesystem.h - API publica del filesystem de GopherOS
#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "types.h"
#include "kresults.h"

typedef struct FileEntry {
    char name[32];
    bool is_dir;
    int parent;
    uint32_t size;
    uint32_t ctime;
    uint32_t mtime;
    uint8_t* data;
    bool exists;
} FileEntry;

void fs_init(void);

fs_create_result_t fs_create(const char* path, Region* r);
gos_result_t fs_mkdir(const char* path);
gos_result_t fs_rmdir(const char* path);
gos_result_t fs_delete(const char* path);
gos_result_t fs_rename(const char* old_path, const char* new_path);

fs_size_result_t fs_append(FileEntry* f, const uint8_t* data, size_t len, Region* r);
fs_read_result_t fs_read(FileEntry* f, size_t offset, size_t len);

FileEntry* fs_find(const char* path);
bool fs_is_dir(FileEntry* f);
uint32_t fs_size_of(FileEntry* f);
int fs_list(const char* path, char names[][32], uint32_t* sizes, bool* is_dir_out, int max);

gos_result_t fs_save(void);
gos_result_t fs_load(Region* r);

#endif // FILESYSTEM_H
