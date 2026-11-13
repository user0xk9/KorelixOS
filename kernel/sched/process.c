#include <stdint.h>
#include <stddef.h>
#include <korelix/sched.h>
#include <korelix/elf.h>
#include <korelix/mm.h>
#include <korelix/string.h>

#define USER_STACK_TOP   0x00007ffffffff000ULL   /* страница выше (до границы канонических адресов) не отображена */
#define USER_STACK_PAGES 8

static size_t slen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Запись в чужое адресное пространство через HHDM (сам процесс ещё не запущен) */
static int user_write(uint64_t *pml4, uint64_t va, const void *data, size_t len) {
    const uint8_t *src = data;
    while (len) {
        uint64_t phys = vmm_translate(pml4, va);
        if (!phys) return -1;
        size_t chunk = PAGE_SIZE - (va & (PAGE_SIZE - 1));
        if (chunk > len) chunk = len;
        memcpy(phys_to_virt(phys), src, chunk);
        va += chunk;
        src += chunk;
        len -= chunk;
    }
    return 0;
}

/* Раскладка у вершины стека: [строки аргументов][массив argv + NULL][фиктивный возврат].
 * Вход в программу: rdi = argc, rsi = argv, rsp % 16 == 8 (как после call). */
static int build_args(uint64_t *pml4, int argc, const char *const *argv,
                      uint64_t *sp_out, uint64_t *argv_out) {
    uint64_t ptrs[MAX_ARGS + 1];
    uint64_t sp = USER_STACK_TOP;

    for (int i = argc - 1; i >= 0; i--) {
        size_t len = slen(argv[i]) + 1;
        sp -= len;
        if (user_write(pml4, sp, argv[i], len)) return -1;
        ptrs[i] = sp;
    }
    ptrs[argc] = 0;

    sp = ALIGN_DOWN(sp, 16);
    sp -= (uint64_t)(argc + 1) * 8;
    uint64_t argv_va = sp;
    if (user_write(pml4, argv_va, ptrs, (size_t)(argc + 1) * 8)) return -1;

    sp -= 8;                              /* место под фиктивный адрес возврата (там нули) */
    if ((sp & 15) != 8) sp -= 8;

    *sp_out = sp;
    *argv_out = argv_va;
    return 0;
}

/* Создаёт процесс из ELF-образа в памяти. Возвращает pid или -1 (всё освобождается). */
long process_spawn(const char *name, const void *image, size_t size,
                   int argc, const char *const *argv, const struct spawn_env *env) {
    uint64_t entry, sp, argv_va;

    if (argc < 0 || argc > MAX_ARGS) return -1;
    size_t total = 0;
    for (int i = 0; i < argc; i++) {
        total += slen(argv[i]) + 1;
        if (total > MAX_ARG_TOTAL) return -1;
    }

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

    if (build_args(pml4, argc, argv, &sp, &argv_va))
        goto fail;

    long pid = task_create_user(name, pml4, entry, sp, (uint64_t)argc, argv_va, env);
    if (pid >= 0) return pid;

fail:
    vmm_destroy_address_space(pml4);
    return -1;
}