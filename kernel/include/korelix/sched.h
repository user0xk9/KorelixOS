#pragma once
#include <stdint.h>
#include <stddef.h>

#define KSTACK_PAGES  4
#define KSTACK_SIZE   (KSTACK_PAGES * 4096ULL)
#define MAX_FDS       16
#define MAX_ARGS      16
#define MAX_ARG_TOTAL 1024

struct file;
struct vnode;

/* Что ребёнок наследует от родителя. Ссылки на cwd и файлы уже взяты вызывающим:
 * при успехе их забирает процесс, при ошибке вызывающий отпускает их сам. */
struct spawn_env {
    struct vnode *cwd;               /* NULL = корень */
    struct file  *fds[3];            /* stdin, stdout, stderr; NULL = консоль */
};

enum task_state { TASK_READY, TASK_RUNNING, TASK_SLEEPING, TASK_BLOCKED, TASK_DEAD };

struct task {
    uint64_t rsp;                    /* сохранённый указатель стека */
    uint64_t id;
    char name[16];
    enum task_state state;
    uint64_t wake_tick;
    uint64_t wait_pid;               /* для TASK_BLOCKED: завершения какого процесса ждём */
    void *stack;
    uint64_t kstack_top;             /* вершина стека ядра (для TSS и syscall) */
    void (*entry)(void *);
    void *arg;
    uint64_t *pml4;                  /* адресное пространство; NULL = только ядро */
    uint64_t user_rip, user_rsp;     /* куда прыгнуть в ring 3 */
    uint64_t user_arg1, user_arg2;   /* rdi, rsi при входе в ring 3 (argc, argv) */
    struct file *fds[MAX_FDS];       /* открытые файлы (0..2: NULL = консоль) */
    struct vnode *cwd;               /* рабочий каталог (NULL = корень) */
    struct task *prev, *next;        /* кольцо планировщика */
    struct task *zombie_next;
    uint8_t fpu[512] __attribute__((aligned(16)));   /* состояние FPU/SSE */
};

void *kstack_alloc(void);
void  kstack_free(void *base);

void sched_init(void);
struct task *task_create(const char *name, void (*entry)(void *), void *arg);
long task_create_user(const char *name, uint64_t *pml4, uint64_t rip, uint64_t rsp,
                      uint64_t arg1, uint64_t arg2, const struct spawn_env *env);
long process_spawn(const char *name, const void *image, size_t size,
                   int argc, const char *const *argv, const struct spawn_env *env);
struct task *task_current(void);
void task_yield(void);
void task_sleep_ms(uint32_t ms);
long task_wait(uint64_t pid, long *status);
void task_exit_status(long status) __attribute__((noreturn));
void task_exit(void) __attribute__((noreturn));
void schedule(void);
void sched_tick(void);