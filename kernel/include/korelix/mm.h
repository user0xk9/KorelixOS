#pragma once
#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096ULL
#define ALIGN_UP(x, a)   (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))

extern uint64_t hhdm_base;

static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + hhdm_base);
}

void     pmm_init(void);
uint64_t pmm_alloc(void);           /* физический адрес страницы, 0 = нет памяти */
void     pmm_free(uint64_t phys);
uint64_t pmm_free_count(void);      /* в страницах */
uint64_t pmm_total_count(void);
#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITE    (1ULL << 1)
#define PTE_USER     (1ULL << 2)
#define PTE_PWT      (1ULL << 3)
#define PTE_PCD      (1ULL << 4)
#define PTE_HUGE     (1ULL << 7)
#define PTE_NX       (1ULL << 63)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

struct limine_memmap_response;
struct limine_memmap_response *boot_memmap(void);

extern uint64_t *kernel_pml4;   /* виртуальный адрес (через HHDM) корневой таблицы */

void     vmm_init(void);
int      vmm_map(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
int      vmm_map_2m(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
void     vmm_unmap(uint64_t *pml4, uint64_t virt);
uint64_t vmm_translate(uint64_t *pml4, uint64_t virt);   /* 0 = не отображено */
void *vmm_map_mmio(uint64_t phys, uint64_t size);   /* отображение без кэша */