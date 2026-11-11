#pragma once
#include <stdint.h>
#include <stddef.h>

#define VFS_FILE     1
#define VFS_DIR      2
#define VFS_NAME_MAX 56

struct vnode {
    char name[VFS_NAME_MAX];
    int type;
    const uint8_t *data;             /* содержимое файла (лежит в модуле Limine) */
    size_t size;
    struct vnode *parent, *children, *next;
};

/* Открытый файл процесса */
struct file {
    struct vnode *node;
    uint64_t offset;
};

/* То, что видит пользователь через SYS_READDIR (ровно 64 байта) */
struct user_dirent {
    uint32_t type;
    uint32_t size;
    char name[56];
};

void vfs_init(void);
struct vnode *vfs_lookup(const char *path);
struct vnode *vfs_create(const char *path, int type, const void *data, size_t size);
struct vnode *vfs_child(struct vnode *dir, size_t index);
int initramfs_mount(const void *tar, size_t size);