#include <stdint.h>
#include <stddef.h>
#include <korelix/sched.h>
#include <korelix/mm.h>
#include <korelix/spinlock.h>
#include <korelix/string.h>

static uint64_t next_stack_va = 0xffffe80000000000ULL;
static spinlock_t stack_lock = SPINLOCK_INIT;

/* Раскладка: [guard-страница, НЕ отображена][стек 16 КБ]. Виртуальные адреса
 * не переиспользуем (их хватает с запасом), физические страницы освобождаем. */
void *kstack_alloc(void) {
    uint64_t f = spin_lock_irqsave(&stack_lock);

    uint64_t base = next_stack_va + PAGE_SIZE;
    next_stack_va += (KSTACK_PAGES + 1) * PAGE_SIZE;

    for (int i = 0; i < KSTACK_PAGES; i++) {
        uint64_t phys = pmm_alloc();
        if (!phys || vmm_map(kernel_pml4, base + i * PAGE_SIZE, phys, PTE_WRITE | PTE_NX)) {
            if (phys) pmm_free(phys);
            for (int j = 0; j < i; j++) {                     /* откат */
                uint64_t pa = vmm_translate(kernel_pml4, base + j * PAGE_SIZE);
                vmm_unmap(kernel_pml4, base + j * PAGE_SIZE);
                pmm_free(pa);
            }
            spin_unlock_irqrestore(&stack_lock, f);
            return NULL;
        }
    }
    memset((void *)base, 0, KSTACK_SIZE);
    spin_unlock_irqrestore(&stack_lock, f);
    return (void *)base;
}

void kstack_free(void *stack) {
    uint64_t base = (uint64_t)stack;
    uint64_t f = spin_lock_irqsave(&stack_lock);
    for (int i = 0; i < KSTACK_PAGES; i++) {
        uint64_t pa = vmm_translate(kernel_pml4, base + i * PAGE_SIZE);
        vmm_unmap(kernel_pml4, base + i * PAGE_SIZE);
        if (pa) pmm_free(pa);
    }
    spin_unlock_irqrestore(&stack_lock, f);
}