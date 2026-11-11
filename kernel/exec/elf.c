#include <stdint.h>
#include <stddef.h>
#include <korelix/elf.h>
#include <korelix/mm.h>
#include <korelix/string.h>
#include <korelix/log.h>

struct elf64_ehdr {
    uint8_t  ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
} __attribute__((packed));

#define PT_LOAD 1
#define PF_X 1
#define PF_W 2

/* Образ приходит извне, поэтому проверяем всё: на каждое поле есть проверка границ. */
int elf_load(uint64_t *pml4, const void *image, size_t size, uint64_t *entry_out) {
    const uint8_t *img = image;

    if (size < sizeof(struct elf64_ehdr)) return -1;
    const struct elf64_ehdr *eh = image;

    if (memcmp(eh->ident, "\177ELF", 4)) return -1;
    if (eh->ident[4] != 2 || eh->ident[5] != 1) return -1;       /* 64 бита, little-endian */
    if (eh->type != 2 || eh->machine != 62) return -1;            /* ET_EXEC, x86-64 */
    if (eh->phentsize != sizeof(struct elf64_phdr)) return -1;
    if (eh->phnum == 0 || eh->phnum > 32) return -1;
    if (eh->phoff > size || (uint64_t)eh->phnum * eh->phentsize > size - eh->phoff) return -1;
    if (eh->entry < 0x10000 || eh->entry >= USER_LIMIT) return -1;

    for (int i = 0; i < eh->phnum; i++) {
        const struct elf64_phdr *ph =
            (const struct elf64_phdr *)(img + eh->phoff + (uint64_t)i * sizeof(*ph));

        if (ph->type != PT_LOAD || ph->memsz == 0) continue;
        if (ph->filesz > ph->memsz) return -1;
        if (ph->offset > size || ph->filesz > size - ph->offset) return -1;
        if (ph->vaddr < 0x10000 || ph->vaddr >= USER_LIMIT) return -1;   /* нижние 64 КБ не отдаём */
        if (ph->memsz > USER_LIMIT - ph->vaddr) return -1;

        if ((ph->flags & PF_W) && (ph->flags & PF_X)) {
            kprintf("elf: refusing writable+executable segment\n");
            return -1;
        }

        uint64_t flags = PTE_USER;
        if (ph->flags & PF_W)    flags |= PTE_WRITE;
        if (!(ph->flags & PF_X)) flags |= PTE_NX;

        uint64_t start = ALIGN_DOWN(ph->vaddr, PAGE_SIZE);
        uint64_t end   = ALIGN_UP(ph->vaddr + ph->memsz, PAGE_SIZE);

        for (uint64_t va = start; va < end; va += PAGE_SIZE) {
            if (vmm_translate(pml4, va)) return -1;      /* сегменты пересекаются */

            uint64_t phys = pmm_alloc();
            if (!phys) return -1;
            uint8_t *page = phys_to_virt(phys);
            memset(page, 0, PAGE_SIZE);                  /* bss и хвосты страниц = нули */

            /* часть файла, попадающая на эту страницу */
            uint64_t lo = va > ph->vaddr ? va : ph->vaddr;
            uint64_t file_end = ph->vaddr + ph->filesz;
            uint64_t hi = va + PAGE_SIZE < file_end ? va + PAGE_SIZE : file_end;
            if (lo < hi)
                memcpy(page + (lo - va), img + ph->offset + (lo - ph->vaddr), hi - lo);

            if (vmm_map(pml4, va, phys, flags)) {
                pmm_free(phys);
                return -1;
            }
        }
    }

    *entry_out = eh->entry;
    return 0;
}