#include <stdint.h>
#include <stddef.h>
#include <korelix/sched.h>
#include <korelix/spinlock.h>
#include <korelix/timer.h>
#include <korelix/heap.h>
#include <korelix/string.h>
#include <korelix/log.h>

extern void switch_context(uint64_t *old_rsp, uint64_t new_rsp);

static struct task boot_task;       /* поток, в котором запущен kmain */
static struct task *current;
static struct task *cursor;         /* последняя запущенная задача кольца */
static struct task *idle_task;      /* вне кольца: запускается, когда больше нечего */
static struct task *zombies;
static uint64_t next_id = 1;
static volatile int sched_ready;

/* ---------- кольцо задач (вызывать с выключенными прерываниями) ---------- */

static void ring_insert(struct task *t) {      /* в конец очереди: перед cursor */
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

/* Освобождаем завершённые задачи. Стек задачи нельзя освободить, пока на нём
 * исполняется она сама, поэтому делаем это уже из контекста другой задачи. */
static void reap(void) {
    while (zombies && zombies != current) {
        struct task *z = zombies;
        zombies = z->zombie_next;
        kstack_free(z->stack);
        kfree(z);
    }
}

static struct task *pick_next(void) {
    uint64_t now = timer_ticks();

    if (cursor) {
        struct task *t = cursor;
        do {                                            /* будим тех, чей срок вышел */
            if (t->state == TASK_SLEEPING && t->wake_tick <= now)
                t->state = TASK_READY;
            t = t->next;
        } while (t != cursor);

        t = cursor->next;                               /* round-robin: после последней */
        for (;;) {
            if (t->state == TASK_READY || t->state == TASK_RUNNING)
                return t;
            if (t == cursor) break;
            t = t->next;
        }
    }
    return idle_task;
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
    __asm__ volatile("sti");                     /* новая задача стартует с прерываниями */
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

    /* Готовим стек так, будто задача уже вызывала switch_context:
     * 6 регистров, затем адрес возврата = trampoline (выровнено под ABI). */
    uint64_t *sp = (uint64_t *)((uint64_t)t->stack + KSTACK_SIZE);
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

static void idle_entry(void *arg) {
    (void)arg;
    for (;;) __asm__ volatile("sti; hlt");
}

void sched_init(void) {
    memset(&boot_task, 0, sizeof(boot_task));
    set_name(&boot_task, "main");
    boot_task.state = TASK_RUNNING;             /* текущий поток становится задачей №0 */
    current = &boot_task;
    ring_insert(&boot_task);

    idle_task = make_task("idle", idle_entry, NULL);
    if (!idle_task) panic("sched: cannot create idle task");
    idle_task->id = next_id++;

    sched_ready = 1;
    kprintf("Scheduler: ready (idle task id=%lu)\n", idle_task->id);
}