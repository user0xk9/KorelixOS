#include <stdint.h>
#include <stddef.h>
#include <korelix/io.h>
#include <korelix/cpu.h>
#include <korelix/mm.h>
#include <korelix/log.h>
#include <korelix/heap.h>
#include <korelix/sched.h>
#include <korelix/string.h>
#include <korelix/vfs.h>
#include <korelix/console.h>
#include <korelix/syscall.h>
#include <korelix/errno.h>

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_FMASK  0xC0000084

uint64_t syscall_kernel_rsp;        /* обновляется планировщиком при каждом переключении */
extern void syscall_entry(void);

void syscall_init(void) {
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1);                  /* SCE: включить syscall/sysret */

    /* STAR[47:32] = 0x08: CS ядра (SS = 0x10).
     * STAR[63:48] = 0x10: sysret даёт SS = 0x10+8 = 0x18|3, CS = 0x10+16 = 0x20|3 */
    wrmsr(MSR_STAR, ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32));
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* при входе сбрасываем TF, IF, DF, AC */
    wrmsr(MSR_FMASK, (1ULL << 8) | (1ULL << 9) | (1ULL << 10) | (1ULL << 18));
}

/* ---------- безопасный доступ к памяти процесса ---------- */

static inline void user_access_begin(void) {
    if (smap_enabled) __asm__ volatile("stac" : : : "memory", "cc");
}
static inline void user_access_end(void) {
    if (smap_enabled) __asm__ volatile("clac" : : : "memory", "cc");
}

int copy_from_user(void *dst, uint64_t src, size_t n) {
    struct task *t = task_current();
    if (!t->pml4 || !vmm_user_ok(t->pml4, src, n, 0)) return -1;
    user_access_begin();
    memcpy(dst, (const void *)src, n);
    user_access_end();
    return 0;
}

int copy_to_user(uint64_t dst, const void *src, size_t n) {
    struct task *t = task_current();
    if (!t->pml4 || !vmm_user_ok(t->pml4, dst, n, 1)) return -1;
    user_access_begin();
    memcpy((void *)dst, src, n);
    user_access_end();
    return 0;
}

/* Копирует строку до нуля, не больше max байт (включая ноль). Длина или -errno. */
static long strncpy_from_user(char *dst, uint64_t src, size_t max) {
    struct task *t = task_current();
    if (!t->pml4) return -EFAULT;

    for (size_t i = 0; i < max; i++) {
        if (!vmm_user_ok(t->pml4, src + i, 1, 0)) return -EFAULT;
        user_access_begin();
        char c = *(volatile const char *)(src + i);
        user_access_end();
        dst[i] = c;
        if (!c) return (long)i;
    }
    return -ENAMETOOLONG;
}

/* Путь из памяти процесса в буфер VFS_PATH_MAX. 0 или -errno. */
static long get_path(uint64_t user, char *dst) {
    long n = strncpy_from_user(dst, user, VFS_PATH_MAX);
    return n < 0 ? n : 0;
}

/* ---------- сами вызовы ---------- */

#define MAX_IO 65536          /* предел одного read/write */

static struct file *fd_get(uint64_t fd) {
    if (fd >= MAX_FDS) return NULL;
    return task_current()->fds[fd];
}

/* fd 0..2 без файла = консоль */
static long sys_write(uint64_t fd, uint64_t buf, uint64_t len) {
    struct file *f = fd_get(fd);
    char tmp[512];
    uint64_t done = 0;

    if (!f) {
        if (fd != 1 && fd != 2) return -EBADF;
        if (len > 4096) len = 4096;
        while (done < len) {
            size_t chunk = len - done > 256 ? 256 : (size_t)(len - done);
            if (copy_from_user(tmp, buf + done, chunk))
                return done ? (long)done : -EFAULT;
            kwrite(tmp, chunk);
            done += chunk;
        }
        return (long)done;
    }

    if (len > MAX_IO) len = MAX_IO;
    while (done < len) {
        size_t chunk = len - done > sizeof(tmp) ? sizeof(tmp) : (size_t)(len - done);
        if (copy_from_user(tmp, buf + done, chunk))
            return done ? (long)done : -EFAULT;
        long r = vfs_write(f, tmp, chunk);
        if (r < 0) return done ? (long)done : r;
        done += (uint64_t)r;
        if ((size_t)r < chunk) break;
    }
    return (long)done;
}

static long sys_read(uint64_t fd, uint64_t buf, uint64_t len) {
    struct file *f = fd_get(fd);
    char tmp[512];

    if (!f) {
        if (fd != 0) return -EBADF;                 /* stdin = консоль: строка целиком */
        size_t n = len > 256 ? 256 : (size_t)len;
        long r = console_read(tmp, n);
        if (r > 0 && copy_to_user(buf, tmp, (size_t)r)) return -EFAULT;
        return r;
    }

    if (len > MAX_IO) len = MAX_IO;
    uint64_t done = 0;
    while (done < len) {
        size_t chunk = len - done > sizeof(tmp) ? sizeof(tmp) : (size_t)(len - done);
        long r = vfs_read(f, tmp, chunk);
        if (r < 0) return done ? (long)done : r;
        if (r == 0) break;                          /* конец файла */
        if (copy_to_user(buf + done, tmp, (size_t)r))
            return done ? (long)done : -EFAULT;
        done += (uint64_t)r;
        if ((size_t)r < chunk) break;
    }
    return (long)done;
}

static long sys_open(uint64_t path_user, uint64_t flags) {
    char path[VFS_PATH_MAX];
    long e = get_path(path_user, path);
    if (e) return e;

    struct task *t = task_current();
    int fd = 3;                                     /* 0..2 не выдаются: это stdin/stdout/stderr */
    while (fd < MAX_FDS && t->fds[fd]) fd++;
    if (fd == MAX_FDS) return -EMFILE;

    struct file *f;
    int r = vfs_open_at(t->cwd, path, (int)flags, &f);
    if (r) return r;
    t->fds[fd] = f;
    return fd;
}

static long sys_close(uint64_t fd) {
    struct file *f = fd_get(fd);
    if (!f) return -EBADF;
    task_current()->fds[fd] = NULL;
    file_put(f);
    return 0;
}

/* 1 = запись получена, 0 = больше нет, -errno = ошибка */
static long sys_readdir(uint64_t fd, uint64_t index, uint64_t ubuf) {
    struct file *f = fd_get(fd);
    if (!f) return -EBADF;

    struct user_dirent d;
    int r = vfs_readdir(f, index, &d);
    if (r <= 0) return r;
    if (copy_to_user(ubuf, &d, sizeof(d))) return -EFAULT;
    return 1;
}

static long sys_lseek(uint64_t fd, uint64_t off, uint64_t whence) {
    struct file *f = fd_get(fd);
    if (!f) return -EBADF;
    return vfs_lseek(f, (int64_t)off, (int)whence);
}

static long sys_stat(uint64_t path_user, uint64_t ubuf) {
    char path[VFS_PATH_MAX];
    long e = get_path(path_user, path);
    if (e) return e;

    struct user_stat st;
    int r = vfs_stat_at(task_current()->cwd, path, &st);
    if (r) return r;
    if (copy_to_user(ubuf, &st, sizeof(st))) return -EFAULT;
    return 0;
}

static long sys_mkdir(uint64_t path_user) {
    char path[VFS_PATH_MAX];
    long e = get_path(path_user, path);
    return e ? e : vfs_mkdir_at(task_current()->cwd, path);
}

static long sys_unlink(uint64_t path_user) {
    char path[VFS_PATH_MAX];
    long e = get_path(path_user, path);
    return e ? e : vfs_unlink_at(task_current()->cwd, path);
}

static long sys_rmdir(uint64_t path_user) {
    char path[VFS_PATH_MAX];
    long e = get_path(path_user, path);
    return e ? e : vfs_rmdir_at(task_current()->cwd, path);
}

static long sys_rename(uint64_t from_user, uint64_t to_user) {
    char from[VFS_PATH_MAX], to[VFS_PATH_MAX];
    long e = get_path(from_user, from);
    if (e) return e;
    e = get_path(to_user, to);
    return e ? e : vfs_rename_at(task_current()->cwd, from, to);
}

static long sys_chdir(uint64_t path_user) {
    char path[VFS_PATH_MAX];
    long e = get_path(path_user, path);
    if (e) return e;

    struct task *t = task_current();
    struct vnode *n;
    int r = vfs_chdir_at(t->cwd, path, &n);       /* n уже с взятой ссылкой */
    if (r) return r;
    struct vnode *old = t->cwd;
    t->cwd = n;
    if (old) vfs_node_put(old);
    return 0;
}

static long sys_getcwd(uint64_t ubuf, uint64_t size) {
    struct vnode *c = task_current()->cwd;
    if (!c) c = vfs_lookup("/");
    char tmp[VFS_PATH_MAX];
    long r = vfs_path(c, tmp, sizeof(tmp));
    if (r < 0) return r;
    if ((uint64_t)r + 1 > size) return -ERANGE;
    if (copy_to_user(ubuf, tmp, (size_t)r + 1)) return -EFAULT;
    return r;
}

static long sys_fsinfo(uint64_t ubuf) {
    struct user_fsinfo info;
    vfs_stats(&info);
    return copy_to_user(ubuf, &info, sizeof(info)) ? -EFAULT : 0;
}

/* Запуск программы: путь + NULL-терминированный массив argv (может быть 0).
 * in_fd/out_fd: fd родителя, ставший stdin/stdout ребёнка, или -1 = унаследовать. Возвращает pid. */
static long sys_spawn(uint64_t path_user, uint64_t argv_user, uint64_t in_fd, uint64_t out_fd) {
    char path[VFS_PATH_MAX];
    long e = get_path(path_user, path);
    if (e) return e;

    struct task *t = task_current();
    void *img;
    size_t img_size;
    e = vfs_read_whole(t->cwd, path, &img, &img_size);   /* копия: файл могут менять во время загрузки */
    if (e) return e;

    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/') base = p + 1;

    char argbuf[MAX_ARG_TOTAL];
    const char *argv[MAX_ARGS + 1];
    int argc = 0;
    size_t used = 0;

    if (argv_user) {
        for (;;) {
            if (argc >= MAX_ARGS) { e = -E2BIG; goto out; }
            uint64_t uptr;
            if (copy_from_user(&uptr, argv_user + (uint64_t)argc * 8, 8)) { e = -EFAULT; goto out; }
            if (!uptr) break;

            long len = strncpy_from_user(argbuf + used, uptr, sizeof(argbuf) - used);
            if (len < 0) { e = len == -ENAMETOOLONG ? -E2BIG : len; goto out; }
            argv[argc++] = argbuf + used;
            used += (size_t)len + 1;
        }
    }
    if (argc == 0) { argv[0] = base; argc = 1; }
    argv[argc] = NULL;

    struct spawn_env env = { .cwd = t->cwd };
    for (int i = 0; i < 3; i++) env.fds[i] = t->fds[i];
    if ((int64_t)in_fd != -1) {
        if (!(env.fds[0] = fd_get(in_fd))) { e = -EBADF; goto out; }
    }
    if ((int64_t)out_fd != -1) {
        if (!(env.fds[1] = fd_get(out_fd))) { e = -EBADF; goto out; }
    }

    if (env.cwd) vfs_node_get(env.cwd);                  /* ссылки для ребёнка */
    for (int i = 0; i < 3; i++)
        if (env.fds[i]) file_get(env.fds[i]);

    long pid = process_spawn(base, img, img_size, argc, argv, &env);
    if (pid < 0) {                                       /* ребёнок не создан: отпускаем ссылки */
        if (env.cwd) vfs_node_put(env.cwd);
        for (int i = 0; i < 3; i++)
            if (env.fds[i]) file_put(env.fds[i]);
        e = -ENOEXEC;
    } else {
        e = pid;
    }
out:
    kfree(img);
    return e;
}

static long sys_wait(uint64_t pid, uint64_t status_user) {
    long status;
    if (task_wait(pid, &status)) return -1;
    if (status_user && copy_to_user(status_user, &status, sizeof(status))) return -EFAULT;
    return 0;
}

void syscall_dispatch(struct syscall_frame *f) {
    __asm__ volatile("sti");            /* дальше системный вызов можно вытеснять */

    /* sysret с неканоническим rip приводит к #GP в режиме ядра на чужом стеке: не допускаем */
    if (f->rip >= USER_LIMIT) {
        kprintf("[kernel] bad user rip, killing process\n");
        task_exit_status(-1);
    }

    long ret = -1;
    switch (f->rax) {
    case SYS_EXIT:     task_exit_status((long)f->rdi);
    case SYS_WRITE:    ret = sys_write(f->rdi, f->rsi, f->rdx); break;
    case SYS_GETPID:   ret = (long)task_current()->id; break;
    case SYS_SLEEP_MS: task_sleep_ms((uint32_t)f->rdi); ret = 0; break;
    case SYS_OPEN:     ret = sys_open(f->rdi, f->rsi); break;
    case SYS_READ:     ret = sys_read(f->rdi, f->rsi, f->rdx); break;
    case SYS_CLOSE:    ret = sys_close(f->rdi); break;
    case SYS_READDIR:  ret = sys_readdir(f->rdi, f->rsi, f->rdx); break;
    case SYS_SPAWN:    ret = sys_spawn(f->rdi, f->rsi, f->rdx, f->r10); break;
    case SYS_WAIT:     ret = sys_wait(f->rdi, f->rsi); break;
    case SYS_MKDIR:    ret = sys_mkdir(f->rdi); break;
    case SYS_UNLINK:   ret = sys_unlink(f->rdi); break;
    case SYS_RMDIR:    ret = sys_rmdir(f->rdi); break;
    case SYS_RENAME:   ret = sys_rename(f->rdi, f->rsi); break;
    case SYS_CHDIR:    ret = sys_chdir(f->rdi); break;
    case SYS_GETCWD:   ret = sys_getcwd(f->rdi, f->rsi); break;
    case SYS_LSEEK:    ret = sys_lseek(f->rdi, f->rsi, f->rdx); break;
    case SYS_STAT:     ret = sys_stat(f->rdi, f->rsi); break;
    case SYS_FSINFO:   ret = sys_fsinfo(f->rdi); break;
    default:           break;           /* неизвестный номер: -1 */
    }
    f->rax = (uint64_t)ret;
}
