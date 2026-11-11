#pragma once
#include <stdint.h>

typedef struct { volatile int locked; } spinlock_t;
#define SPINLOCK_INIT { 0 }

/* Запретить прерывания, вернуть прежнее состояние флагов */
static inline uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags) {
    if (flags & (1ULL << 9))            /* IF был включён */
        __asm__ volatile("sti" : : : "memory");
}

static inline uint64_t spin_lock_irqsave(spinlock_t *l) {
    uint64_t flags = irq_save();
    while (__sync_lock_test_and_set(&l->locked, 1))
        __asm__ volatile("pause");
    return flags;
}

static inline void spin_unlock_irqrestore(spinlock_t *l, uint64_t flags) {
    __sync_lock_release(&l->locked);
    irq_restore(flags);
}