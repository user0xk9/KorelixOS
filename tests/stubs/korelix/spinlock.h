#pragma once
/* Замена kernel/include/korelix/spinlock.h для хостовых тестов (там cli/sti, в user mode нельзя).
 * Блокировка нерекурсивная, как настоящая: повторный захват или лишний unlock = падение теста. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { volatile int locked; } spinlock_t;
#define SPINLOCK_INIT { 0 }

static inline uint64_t spin_lock_irqsave(spinlock_t *l) {
    if (l->locked) { fprintf(stderr, "spinlock: recursive lock (deadlock in the kernel)\n"); abort(); }
    l->locked = 1;
    return 0;
}

static inline void spin_unlock_irqrestore(spinlock_t *l, uint64_t flags) {
    (void)flags;
    if (!l->locked) { fprintf(stderr, "spinlock: unlock of a free lock\n"); abort(); }
    l->locked = 0;
}
