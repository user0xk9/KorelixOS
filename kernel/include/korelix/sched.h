#pragma once
#include <stdint.h>

#define KSTACK_PAGES 4
#define KSTACK_SIZE  (KSTACK_PAGES * 4096ULL)

enum task_state { TASK_READY, TASK_RUNNING, TASK_SLEEPING, TASK_DEAD };

struct task {
    uint64_t rsp;                    /* сохранённый указатель стека */
    uint64_t id;
    char name[16];
    enum task_state state;
    uint64_t wake_tick;
    void *stack;
    void (*entry)(void *);
    void *arg;
    struct task *prev, *next;        /* кольцо планировщика */
    struct task *zombie_next;        /* список завершённых, ждущих освобождения */
};

void *kstack_alloc(void);
void  kstack_free(void *base);

void sched_init(void);
struct task *task_create(const char *name, void (*entry)(void *), void *arg);
struct task *task_current(void);
void task_yield(void);
void task_sleep_ms(uint32_t ms);
void task_exit(void) __attribute__((noreturn));
void schedule(void);
void sched_tick(void);