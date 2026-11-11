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
#include <korelix/syscall.h>

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

/* Копирует строку до нуля, не больше max байт (включая ноль). Длина или -1. */
static long strncpy_from_user(char *dst, uint64_t src, size_t max) {
    struct task *t = task_current();
    if (!t->pml4) return -1;

    for (size_t i = 0; i < max; i++) {
        if (!vmm_user_ok(t->pml4, src + i, 1, 0)) return -1;
        user_access_begin();
        char c = *(volatile const char *)(src + i);
        user_access_end();
        dst[i] = c;
        if (!c) return (long)i;
    }
    return -1;                                  /* слишком длинная строка */
}

/* ---------- сами вызовы ---------- */

static __attribute__((noreturn)) void sys_exit(long code) {
    struct task *t = task_current();
    kprintf("[kernel] process %lu (%s) exited with code %ld\n", t->id, t->name, code);
    task_exit();
}

static long sys_write(uint64_t fd, uint64_t buf, uint64_t len) {
    if (fd != 1 && fd != 2) return -1;
    if (len > 4096) len = 4096;

    char tmp[256];
    uint64_t done = 0;
    while (done < len) {
        size_t chunk = len - done > sizeof(tmp) ? sizeof(tmp) : (size_t)(len - done);
        if (copy_from_user(tmp, buf + done, chunk))
            return done ? (long)done : -1;
        kwrite(tmp, chunk);
        done += chunk;
    }
    return (long)done;
}

static struct file *fd_get(uint64_t fd) {
    if (fd >= MAX_FDS) return NULL;
    return task_current()->fds[fd];
}

static long sys_open(uint64_t path_user) {
    char path[256];
    if (strncpy_from_user(path, path_user, sizeof(path)) < 0) return -1;

    struct vnode *n = vfs_lookup(path);
    if (!n) return -1;

    struct task *t = task_current();
    for (int fd = 3; fd < MAX_FDS; fd++) {      /* 0..2 зарезервированы под консоль */
        if (!t->fds[fd]) {
            struct file *f = kzalloc(sizeof(*f));
            if (!f) return -1;
            f->node = n;
            t->fds[fd] = f;
            return fd;
        }
    }
    return -1;
}

static long sys_read(uint64_t fd, uint64_t buf, uint64_t len) {
    struct file *f = fd_get(fd);
    if (!f || f->node->type != VFS_FILE) return -1;
    if (f->offset >= f->node->size) return 0;   /* конец файла */

    uint64_t avail = f->node->size - f->offset;
    if (len > avail) len = avail;
    if (copy_to_user(buf, f->node->data + f->offset, len)) return -1;
    f->offset += len;
    return (long)len;
}

static long sys_close(uint64_t fd) {
    struct file *f = fd_get(fd);
    if (!f) return -1;
    kfree(f);
    task_current()->fds[fd] = NULL;
    return 0;
}

/* 1 = запись получена, 0 = больше нет, -1 = ошибка */
static long sys_readdir(uint64_t fd, uint64_t index, uint64_t ubuf) {
    struct file *f = fd_get(fd);
    if (!f || f->node->type != VFS_DIR) return -1;

    struct vnode *c = vfs_child(f->node, index);
    if (!c) return 0;

    struct user_dirent d;
    memset(&d, 0, sizeof(d));
    d.type = (uint32_t)c->type;
    d.size = (uint32_t)c->size;
    for (size_t i = 0; i < sizeof(d.name) - 1 && c->name[i]; i++)
        d.name[i] = c->name[i];

    if (copy_to_user(ubuf, &d, sizeof(d))) return -1;
    return 1;
}

/* Запуск программы по пути. Возвращает pid или -1. (Позже появятся fork/exec.) */
static long sys_spawn(uint64_t path_user) {
    char path[256];
    if (strncpy_from_user(path, path_user, sizeof(path)) < 0) return -1;

    struct vnode *n = vfs_lookup(path);
    if (!n || n->type != VFS_FILE) return -1;
    return process_spawn(n->name, n->data, n->size);
}

void syscall_dispatch(struct syscall_frame *f) {
    __asm__ volatile("sti");            /* дальше системный вызов можно вытеснять */

    /* sysret с неканоническим rip приводит к #GP в режиме ядра на чужом стеке: не допускаем */
    if (f->rip >= USER_LIMIT) {
        kprintf("[kernel] bad user rip, killing process\n");
        task_exit();
    }

    long ret = -1;
    switch (f->rax) {
    case SYS_EXIT:     sys_exit((long)f->rdi);
    case SYS_WRITE:    ret = sys_write(f->rdi, f->rsi, f->rdx); break;
    case SYS_GETPID:   ret = (long)task_current()->id; break;
    case SYS_SLEEP_MS: task_sleep_ms((uint32_t)f->rdi); ret = 0; break;
    case SYS_OPEN:     ret = sys_open(f->rdi); break;
    case SYS_READ:     ret = sys_read(f->rdi, f->rsi, f->rdx); break;
    case SYS_CLOSE:    ret = sys_close(f->rdi); break;
    case SYS_READDIR:  ret = sys_readdir(f->rdi, f->rsi, f->rdx); break;
    case SYS_SPAWN:    ret = sys_spawn(f->rdi); break;
    default:           break;           /* неизвестный номер: -1 */
    }
    f->rax = (uint64_t)ret;
}