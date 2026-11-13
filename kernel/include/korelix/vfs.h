#pragma once
#include <stdint.h>
#include <stddef.h>

#define VFS_FILE      1
#define VFS_DIR       2
#define VFS_NAME_MAX  56
#define VFS_PATH_MAX  256
#define VFS_MAX_FILE  (8u * 1024 * 1024)      /* предел размера файла */

/* Флаги open (значения как в Linux) */
#define O_RDONLY   0
#define O_WRONLY   1
#define O_RDWR     2
#define O_ACCMODE  3
#define O_CREAT    0x40
#define O_EXCL     0x80
#define O_TRUNC    0x200
#define O_APPEND   0x400

#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

struct vnode {
    char name[VFS_NAME_MAX];
    int type;
    uint64_t id;                     /* уникальный номер узла */
    uint8_t *data;                   /* содержимое файла */
    size_t size;
    size_t cap;                      /* размер выделенного буфера (если owned) */
    int owned;                       /* 1: data в куче; 0: лежит в initramfs, при записи копируется */
    uint32_t refs;                   /* открытые файлы + рабочие каталоги процессов */
    int unlinked;                    /* удалён из дерева, освободится после последнего close */
    struct vnode *parent, *children, *next;
};

/* Открытый файл (может быть общим у нескольких процессов после spawn) */
struct file {
    struct vnode *node;
    uint64_t offset;
    int flags;
    uint32_t refs;
};

/* Запись каталога для SYS_READDIR (ровно 64 байта) */
struct user_dirent {
    uint32_t type;
    uint32_t size;
    char name[56];
};

/* Ответ SYS_STAT (24 байта) */
struct user_stat {
    uint32_t type;
    uint32_t _pad;
    uint64_t size;
    uint64_t id;
};

/* Ответ SYS_FSINFO (32 байта) */
struct user_fsinfo {
    uint64_t files, dirs, bytes, max_file;
};

void vfs_init(void);

/* Для initramfs: создаёт узел по пути (недостающие каталоги по дороге), данные не копируются */
struct vnode *vfs_install(const char *path, int type, const void *data, size_t size);

/* Поиск без взятия ссылки: только для кода ядра при старте и для тестов */
struct vnode *vfs_lookup(const char *path);
struct vnode *vfs_child(struct vnode *dir, size_t index);

/* Операции над путями. cwd == NULL: относительные пути считаются от корня.
 * Результат 0 или -errno (см. errno.h). */
int  vfs_open_at(struct vnode *cwd, const char *path, int flags, struct file **out);
int  vfs_mkdir_at(struct vnode *cwd, const char *path);
int  vfs_unlink_at(struct vnode *cwd, const char *path);
int  vfs_rmdir_at(struct vnode *cwd, const char *path);
int  vfs_rename_at(struct vnode *cwd, const char *from, const char *to);
int  vfs_stat_at(struct vnode *cwd, const char *path, struct user_stat *st);
int  vfs_chdir_at(struct vnode *cwd, const char *path, struct vnode **out);   /* out с взятой ссылкой */
int  vfs_read_whole(struct vnode *cwd, const char *path, void **buf, size_t *size);  /* kfree(*buf) */

/* Операции над открытым файлом */
long vfs_read(struct file *f, void *buf, size_t len);
long vfs_write(struct file *f, const void *buf, size_t len);
long vfs_lseek(struct file *f, int64_t off, int whence);
int  vfs_readdir(struct file *f, size_t index, struct user_dirent *d);   /* 1 есть, 0 конец, -errno */

/* Счётчики ссылок */
void file_get(struct file *f);
void file_put(struct file *f);
void vfs_node_get(struct vnode *n);
void vfs_node_put(struct vnode *n);

long vfs_path(struct vnode *n, char *buf, size_t size);     /* абсолютный путь узла */
void vfs_stats(struct user_fsinfo *info);

int  initramfs_mount(const void *tar, size_t size);
