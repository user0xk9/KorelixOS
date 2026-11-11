#pragma once
#include <stdint.h>
#include <stddef.h>

#define KSTACK_PAGES 4
#define KSTACK_SIZE  (KSTACK_PAGES * 4096ULL)
#define MAX_FDS      16

struct file;

enum task_state { TASK_READY, TASK_RUNNING, TASK_SLEEPING, TASK_DEAD };

struct task {
    uint64_t rsp;                    /* сохранённый указатель стека */
    uint64_t id;
    char name[16];
    enum task_state state;
    uint64_t wake_tick;
    void *stack;
    uint64_t kstack_top;             /* вершина стека ядра (для TSS и syscall) */
    void (*entry)(void *);
    void *arg;
    uint64_t *pml4;                  /* адресное пространство; NULL = только ядро */
    uint64_t user_rip, user_rsp;     /* куда прыгнуть в ring 3 */
    struct file *fds[MAX_FDS];       /* открытые файлы (0..2 зарезервированы под консоль) */
    struct task *prev, *next;        /* кольцо планировщика */
    struct task *zombie_next;
    uint8_t fpu[512] __attribute__((aligned(16)));   /* состояние FPU/SSE */
};

void *kstack_alloc(void);
void  kstack_free(void *base);

void sched_init(void);
struct task *task_create(const char *name, void (*entry)(void *), void *arg);
long task_create_user(const char *name, uint64_t *pml4, uint64_t rip, uint64_t rsp);
long process_spawn(const char *name, const void *image, size_t size);
struct task *task_current(void);
void task_yield(void);
void task_sleep_ms(uint32_t ms);
void task_exit(void) __attribute__((noreturn));
void schedule(void);
void sched_tick(void);