#include <stdint.h>
#include <stddef.h>
#include <cpuid.h>
#include <limine.h>
#include <korelix/mm.h>
#include <korelix/log.h>
#include <korelix/string.h>

#define HUGE_PAGE 0x200000ULL

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request exec_addr_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0
};

extern char __text_start[], __text_end[];
extern char __rodata_start[], __rodata_end[];
extern char __data_start[], __data_end[];

uint64_t *kernel_pml4;

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t v) {
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

/* Следующий уровень таблицы; при create=1 создаёт его, если нет. */
static uint64_t *next_level(uint64_t *table, uint64_t idx, int create, int user) {
    uint64_t e = table[idx];
    if (e & PTE_PRESENT) {
        if (e & PTE_HUGE) return NULL;               /* там большая страница */
        if (user && !(e & PTE_USER)) table[idx] |= PTE_USER;
        return phys_to_virt(e & PTE_ADDR_MASK);
    }
    if (!create) return NULL;

    uint64_t p = pmm_alloc();
    if (!p) return NULL;
    memset(phys_to_virt(p), 0, PAGE_SIZE);
    /* Промежуточные уровни разрешают всё, реальные права решает конечная запись */
    table[idx] = p | PTE_PRESENT | PTE_WRITE | (user ? PTE_USER : 0);
    return phys_to_virt(p);
}

int vmm_map(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    int user = (flags & PTE_USER) != 0;
    uint64_t *pdpt = next_level(pml4, (virt >> 39) & 0x1FF, 1, user);
    if (!pdpt) return -1;
    uint64_t *pd = next_level(pdpt, (virt >> 30) & 0x1FF, 1, user);
    if (!pd) return -1;
    uint64_t *pt = next_level(pd, (virt >> 21) & 0x1FF, 1, user);
    if (!pt) return -1;

    pt[(virt >> 12) & 0x1FF] = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

int vmm_map_2m(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    int user = (flags & PTE_USER) != 0;
    uint64_t *pdpt = next_level(pml4, (virt >> 39) & 0x1FF, 1, user);
    if (!pdpt) return -1;
    uint64_t *pd = next_level(pdpt, (virt >> 30) & 0x1FF, 1, user);
    if (!pd) return -1;

    uint64_t *e = &pd[(virt >> 21) & 0x1FF];
    uint64_t val = (phys & PTE_ADDR_MASK) | flags | PTE_HUGE | PTE_PRESENT;
    if ((*e & PTE_PRESENT) && *e != val) return -1;   /* конфликт */
    *e = val;
    return 0;
}

void vmm_unmap(uint64_t *pml4, uint64_t virt) {
    uint64_t *pdpt = next_level(pml4, (virt >> 39) & 0x1FF, 0, 0);
    if (!pdpt) return;
    uint64_t *pd = next_level(pdpt, (virt >> 30) & 0x1FF, 0, 0);
    if (!pd) return;
    uint64_t *pt = next_level(pd, (virt >> 21) & 0x1FF, 0, 0);
    if (!pt) return;

    pt[(virt >> 12) & 0x1FF] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

uint64_t vmm_translate(uint64_t *pml4, uint64_t virt) {
    uint64_t e = pml4[(virt >> 39) & 0x1FF];
    if (!(e & PTE_PRESENT)) return 0;

    uint64_t *pdpt = phys_to_virt(e & PTE_ADDR_MASK);
    e = pdpt[(virt >> 30) & 0x1FF];
    if (!(e & PTE_PRESENT)) return 0;
    if (e & PTE_HUGE) return (e & 0x000FFFFFC0000000ULL) | (virt & 0x3FFFFFFFULL);

    uint64_t *pd = phys_to_virt(e & PTE_ADDR_MASK);
    e = pd[(virt >> 21) & 0x1FF];
    if (!(e & PTE_PRESENT)) return 0;
    if (e & PTE_HUGE) return (e & 0x000FFFFFFFE00000ULL) | (virt & 0x1FFFFFULL);

    uint64_t *pt = phys_to_virt(e & PTE_ADDR_MASK);
    e = pt[(virt >> 12) & 0x1FF];
    if (!(e & PTE_PRESENT)) return 0;
    return (e & PTE_ADDR_MASK) | (virt & 0xFFF);
}

static void map_kernel_range(uint64_t start, uint64_t end, uint64_t flags,
                             uint64_t virt_base, uint64_t phys_base) {
    for (uint64_t v = ALIGN_DOWN(start, PAGE_SIZE); v < ALIGN_UP(end, PAGE_SIZE); v += PAGE_SIZE) {
        if (vmm_map(kernel_pml4, v, v - virt_base + phys_base, flags))
            panic("vmm: out of memory while mapping the kernel");
    }
}

void vmm_init(void) {
    /* NX обязателен: без него W^X невозможен */
    uint32_t a, b, c, d;
    if (!__get_cpuid(0x80000001, &a, &b, &c, &d) || !(d & (1u << 20)))
        panic("CPU does not support the NX bit");
    wrmsr(0xC0000080, rdmsr(0xC0000080) | (1ULL << 11));   /* EFER.NXE */

    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ULL << 16);                                   /* CR0.WP: ядро не пишет в read-only */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    if (!exec_addr_request.response)
        panic("Bootloader gave no kernel address");
    uint64_t kvirt = exec_addr_request.response->virtual_base;
    uint64_t kphys = exec_addr_request.response->physical_base;

    uint64_t root = pmm_alloc();
    if (!root) panic("vmm: no memory for PML4");
    memset(phys_to_virt(root), 0, PAGE_SIZE);
    kernel_pml4 = phys_to_virt(root);

    /* 1. HHDM: всё, что не reserved/bad, страницами по 2 МБ (данные, без исполнения) */
    struct limine_memmap_response *mm = boot_memmap();
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type == LIMINE_MEMMAP_RESERVED || e->type == LIMINE_MEMMAP_BAD_MEMORY)
            continue;
        uint64_t start = ALIGN_DOWN(e->base, HUGE_PAGE);
        uint64_t end   = ALIGN_UP(e->base + e->length, HUGE_PAGE);
        for (uint64_t p = start; p < end; p += HUGE_PAGE) {
            if (vmm_map_2m(kernel_pml4, hhdm_base + p, p, PTE_WRITE | PTE_NX))
                panic("vmm: HHDM mapping failed at %lx", p);
        }
    }

    /* 2. Ядро с правами по секциям (W^X) */
    map_kernel_range((uint64_t)__text_start,   (uint64_t)__text_end,   0,                    kvirt, kphys);
    map_kernel_range((uint64_t)__rodata_start, (uint64_t)__rodata_end, PTE_NX,               kvirt, kphys);
    map_kernel_range((uint64_t)__data_start,   (uint64_t)__data_end,   PTE_WRITE | PTE_NX,   kvirt, kphys);

        /* Заранее создаём ветви верхней половины PML4: адресные пространства процессов
     * копируют эти записи, и новых ветвей в ядре появляться уже не должно */
    for (int i = 256; i < 512; i++) {
        if (kernel_pml4[i] & PTE_PRESENT) continue;
        uint64_t p = pmm_alloc();
        if (!p) panic("vmm: out of memory for kernel PML4 entries");
        memset(phys_to_virt(p), 0, PAGE_SIZE);
        kernel_pml4[i] = p | PTE_PRESENT | PTE_WRITE;
    }

    /* 3. Переключаемся на свои таблицы */
    uint64_t root_phys = (uint64_t)kernel_pml4 - hhdm_base;
    __asm__ volatile("mov %0, %%cr3" : : "r"(root_phys) : "memory");
}
static uint64_t mmio_next = 0xffffe00000000000ULL;

void *vmm_map_mmio(uint64_t phys, uint64_t size) {
    uint64_t off   = phys & (PAGE_SIZE - 1);
    uint64_t pages = ALIGN_UP(size + off, PAGE_SIZE) / PAGE_SIZE;
    uint64_t virt  = mmio_next;

    for (uint64_t i = 0; i < pages; i++) {
        if (vmm_map(kernel_pml4, virt + i * PAGE_SIZE, (phys - off) + i * PAGE_SIZE,
                    PTE_WRITE | PTE_NX | PTE_PCD | PTE_PWT))
            panic("vmm_map_mmio: out of memory");
    }
    mmio_next += pages * PAGE_SIZE;
    return (void *)(virt + off);
}
void vmm_load(uint64_t *pml4) {
    uint64_t phys = (uint64_t)pml4 - hhdm_base;
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys) : "memory");
}

/* Новое адресное пространство: нижняя половина пустая, верхняя (ядро) общая */
uint64_t *vmm_create_address_space(void) {
    uint64_t phys = pmm_alloc();
    if (!phys) return NULL;
    uint64_t *pml4 = phys_to_virt(phys);
    memset(pml4, 0, PAGE_SIZE);
    for (int i = 256; i < 512; i++)
        pml4[i] = kernel_pml4[i];
    return pml4;
}

/* Освобождает все страницы и таблицы нижней половины. Не вызывать для активного CR3. */
static void free_user_table(uint64_t *table, int level) {
    for (int i = 0; i < 512; i++) {
        uint64_t e = table[i];
        if (!(e & PTE_PRESENT)) continue;
        uint64_t phys = e & PTE_ADDR_MASK;
        if (level == 1)
            pmm_free(phys);                                  /* страница данных процесса */
        else if (!(e & PTE_HUGE))
            free_user_table(phys_to_virt(phys), level - 1);
    }
    pmm_free((uint64_t)table - hhdm_base);
}

void vmm_destroy_address_space(uint64_t *pml4) {
    for (int i = 0; i < 256; i++) {
        uint64_t e = pml4[i];
        if (e & PTE_PRESENT)
            free_user_table(phys_to_virt(e & PTE_ADDR_MASK), 3);
    }
    pmm_free((uint64_t)pml4 - hhdm_base);
}

static uint64_t *leaf_entry(uint64_t *pml4, uint64_t virt) {
    uint64_t *pdpt = next_level(pml4, (virt >> 39) & 0x1FF, 0, 0);
    if (!pdpt) return NULL;
    uint64_t *pd = next_level(pdpt, (virt >> 30) & 0x1FF, 0, 0);
    if (!pd) return NULL;
    uint64_t *pt = next_level(pd, (virt >> 21) & 0x1FF, 0, 0);
    if (!pt) return NULL;
    return &pt[(virt >> 12) & 0x1FF];
}

/* Можно ли безопасно читать (write=0) или писать (write=1) [addr, addr+len) из ядра */
int vmm_user_ok(uint64_t *pml4, uint64_t addr, uint64_t len, int write) {
    if (len == 0) return 1;
    if (addr >= USER_LIMIT || len > USER_LIMIT - addr) return 0;

    for (uint64_t p = ALIGN_DOWN(addr, PAGE_SIZE); p < addr + len; p += PAGE_SIZE) {
        uint64_t *e = leaf_entry(pml4, p);
        if (!e) return 0;
        if ((*e & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) return 0;
        if (write && !(*e & PTE_WRITE)) return 0;
    }
    return 1;
}