#include <stdint.h>
#include <stddef.h>
#include <korelix/sched.h>
#include <korelix/spinlock.h>
#include <korelix/timer.h>
#include <korelix/heap.h>
#include <korelix/string.h>
#include <korelix/mm.h>
#include <korelix/cpu.h>
#include <korelix/log.h>

extern void switch_context(uint64_t *old_rsp, uint64_t new_rsp);
extern void enter_usermode(uint64_t rip, uint64_t rsp) __attribute__((noreturn));
extern uint64_t syscall_kernel_rsp;

static struct task boot_task;       /* поток, в котором запущен kmain */
static struct task *current;
static struct task *cursor;         /* последняя запущенная задача кольца */
static struct task *idle_task;
static struct task *zombies;
static uint64_t next_id = 1;
static volatile int sched_ready;
static uint64_t *active_pml4;       /* какое адресное пространство сейчас в CR3 */

/* ---------- кольцо задач (вызывать с выключенными прерываниями) ---------- */

static void ring_insert(struct task *t) {
    if (!cursor) {
        t->next = t->prev = t;
        cursor = t;
        return;
    }
    t->prev = cursor->prev;
    t->next = cursor;
    cursor->prev->next = t;
    cursor->prev = t;
}

static void ring_remove(struct task *t) {
    if (t->next == t) {
        cursor = NULL;
    } else {
        t->prev->next = t->next;
        t->next->prev = t->prev;
        if (cursor == t) cursor = t->prev;
    }
    t->next = t->prev = NULL;
}

/* Освобождаем завершённые задачи уже из контекста другой задачи:
 * на её собственном стеке и в её адресном пространстве это делать нельзя. */
static void reap(void) {
    while (zombies && zombies != current) {
        struct task *z = zombies;
        zombies = z->zombie_next;
        for (int i = 0; i < MAX_FDS; i++)
            if (z->fds[i]) kfree(z->fds[i]);
        if (z->pml4) vmm_destroy_address_space(z->pml4);
        kstack_free(z->stack);
        kfree(z);
    }
}

static struct task *pick_next(void) {
    uint64_t now = timer_ticks();

    if (cursor) {
        struct task *t = cursor;
        do {
            if (t->state == TASK_SLEEPING && t->wake_tick <= now)
                t->state = TASK_READY;
            t = t->next;
        } while (t != cursor);

        t = cursor->next;
        for (;;) {
            if (t->state == TASK_READY || t->state == TASK_RUNNING)
                return t;
            if (t == cursor) break;
            t = t->next;
        }
    }
    return idle_task;
}

/* Переключаем адресное пространство и стек для входов в ядро из ring 3 */
static void prepare_switch(struct task *next) {
    uint64_t *pml4 = next->pml4 ? next->pml4 : kernel_pml4;
    if (pml4 != active_pml4) {
        vmm_load(pml4);
        active_pml4 = pml4;
    }
    if (next->kstack_top) {
        tss_set_rsp0(next->kstack_top);          /* прерывания из ring 3 */
        syscall_kernel_rsp = next->kstack_top;   /* инструкция syscall */
    }
}

void schedule(void) {
    uint64_t f = irq_save();

    struct task *prev = current;
    struct task *next = pick_next();

    if (next != prev) {
        if (prev->state == TASK_RUNNING) prev->state = TASK_READY;
        next->state = TASK_RUNNING;
        if (next != idle_task) cursor = next;
        current = next;
        prepare_switch(next);
        fpu_save(prev->fpu);                     /* состояние FPU/SSE уходящей задачи */
        fpu_restore(next->fpu);                  /* и входящей */
        switch_context(&prev->rsp, next->rsp);
        /* сюда вернёмся, когда prev снова выберут */
    }
    reap();
    irq_restore(f);
}

void sched_tick(void) {
    if (sched_ready) schedule();
}

void task_yield(void) {
    schedule();
}

void task_sleep_ms(uint32_t ms) {
    uint64_t f = irq_save();
    current->wake_tick = timer_ticks() + ((uint64_t)ms * TIMER_HZ + 999) / 1000;
    current->state = TASK_SLEEPING;
    schedule();
    irq_restore(f);
}

void task_exit(void) {
    irq_save();                                  /* назад уже не вернёмся */
    if (current == &boot_task || current == idle_task)
        panic("task_exit: the main/idle thread must not exit");

    current->state = TASK_DEAD;
    ring_remove(current);
    current->zombie_next = zombies;
    zombies = current;
    schedule();
    panic("task_exit: dead task was rescheduled");
}

struct task *task_current(void) { return current; }

/* ---------- создание задач ---------- */

static void task_trampoline(void) {
    reap();
    __asm__ volatile("sti");
    current->entry(current->arg);
    task_exit();
}

static void set_name(struct task *t, const char *name) {
    size_t i = 0;
    while (name[i] && i < sizeof(t->name) - 1) { t->name[i] = name[i]; i++; }
    t->name[i] = 0;
}

static struct task *make_task(const char *name, void (*entry)(void *), void *arg) {
    struct task *t = kzalloc(sizeof(*t));
    if (!t) return NULL;

    t->stack = kstack_alloc();
    if (!t->stack) { kfree(t); return NULL; }

    set_name(t, name);
    t->entry = entry;
    t->arg = arg;
    t->state = TASK_READY;
    t->kstack_top = (uint64_t)t->stack + KSTACK_SIZE;
    memcpy(t->fpu, fpu_initial, sizeof(t->fpu));  /* чистое состояние FPU/SSE */

    /* Стек выглядит так, будто задача уже вызывала switch_context:
     * 6 регистров, затем адрес возврата = trampoline (выровнено под ABI). */
    uint64_t *sp = (uint64_t *)t->kstack_top;
    *--sp = 0;                                   /* фиктивный адрес возврата trampoline */
    *--sp = (uint64_t)task_trampoline;           /* сюда уйдёт ret */
    for (int i = 0; i < 6; i++) *--sp = 0;       /* rbp rbx r12 r13 r14 r15 */
    t->rsp = (uint64_t)sp;
    return t;
}

struct task *task_create(const char *name, void (*entry)(void *), void *arg) {
    struct task *t = make_task(name, entry, arg);
    if (!t) return NULL;

    uint64_t f = irq_save();
    t->id = next_id++;
    ring_insert(t);
    irq_restore(f);
    return t;
}

/* Поток ядра процесса: сразу прыгает в ring 3 */
static void user_entry(void *arg) {
    (void)arg;
    enter_usermode(current->user_rip, current->user_rsp);
}

/* Возвращает pid или -1. Указатель наружу не отдаём: после вставки в кольцо
 * задача может успеть завершиться и быть освобождена. */
long task_create_user(const char *name, uint64_t *pml4, uint64_t rip, uint64_t rsp) {
    struct task *t = make_task(name, user_entry, NULL);
    if (!t) return -1;

    t->pml4 = pml4;
    t->user_rip = rip;
    t->user_rsp = rsp;

    uint64_t f = irq_save();
    t->id = next_id++;
    long id = (long)t->id;
    ring_insert(t);
    irq_restore(f);
    return id;
}

static void idle_entry(void *arg) {
    (void)arg;
    for (;;) __asm__ volatile("sti; hlt");
}

void sched_init(void) {
    memset(&boot_task, 0, sizeof(boot_task));
    set_name(&boot_task, "main");
    boot_task.state = TASK_RUNNING;
    current = &boot_task;
    active_pml4 = kernel_pml4;
    ring_insert(&boot_task);

    idle_task = make_task("idle", idle_entry, NULL);
    if (!idle_task) panic("sched: cannot create idle task");
    idle_task->id = next_id++;

    sched_ready = 1;
    kprintf("Scheduler: ready (idle task id=%lu)\n", idle_task->id);
}