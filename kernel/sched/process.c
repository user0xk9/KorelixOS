#include <stdint.h>
#include <stddef.h>
#include <korelix/sched.h>
#include <korelix/elf.h>
#include <korelix/mm.h>
#include <korelix/string.h>

#define USER_STACK_TOP   0x00007ffffffff000ULL   /* страница выше (до границы канонических адресов) не отображена */
#define USER_STACK_PAGES 8

/* Создаёт процесс из ELF-образа в памяти. Возвращает pid или -1 (всё освобождается). */
long process_spawn(const char *name, const void *image, size_t size) {
    uint64_t entry;

    uint64_t *pml4 = vmm_create_address_space();
    if (!pml4) return -1;

    if (elf_load(pml4, image, size, &entry))
        goto fail;

    /* Стек пользователя: 32 КБ; страница под ним не отображена (защита от переполнения) */
    for (int i = 1; i <= USER_STACK_PAGES; i++) {
        uint64_t phys = pmm_alloc();
        if (!phys) goto fail;
        memset(phys_to_virt(phys), 0, PAGE_SIZE);
        if (vmm_map(pml4, USER_STACK_TOP - i * PAGE_SIZE, phys, PTE_USER | PTE_WRITE | PTE_NX)) {
            pmm_free(phys);
            goto fail;
        }
    }

    long pid = task_create_user(name, pml4, entry, USER_STACK_TOP - 8);
    if (pid >= 0) return pid;

fail:
    vmm_destroy_address_space(pml4);
    return -1;
}