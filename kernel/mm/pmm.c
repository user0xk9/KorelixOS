#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include <korelix/mm.h>
#include <korelix/log.h>
#include <korelix/string.h>
#include <korelix/spinlock.h>

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

uint64_t hhdm_base;

static uint8_t *bitmap;
static uint64_t total_pages;
static uint64_t free_pages;
static uint64_t search_hint;
static spinlock_t pmm_lock = SPINLOCK_INIT;

static inline void mark_used(uint64_t i) { bitmap[i / 8] |=  (uint8_t)(1u << (i % 8)); }
static inline void mark_free(uint64_t i) { bitmap[i / 8] &= (uint8_t)~(1u << (i % 8)); }
static inline int  is_used(uint64_t i)   { return bitmap[i / 8] & (1u << (i % 8)); }

static const char *type_name(uint64_t t) {
    static const char *names[] = {
        "usable", "reserved", "ACPI reclaimable", "ACPI NVS",
        "bad memory", "bootloader reclaimable", "kernel/modules", "framebuffer"
    };
    return t < 8 ? names[t] : "unknown";
}

void pmm_init(void) {
    if (!memmap_request.response || !hhdm_request.response)
        panic("Bootloader gave no memory map / HHDM");

    hhdm_base = hhdm_request.response->offset;
    struct limine_memmap_response *mm = memmap_request.response;

    /* 1. Печать карты памяти + поиск верхней границы usable-памяти */
    uint64_t top = 0;
    kprintf("Memory map:\n");
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        kprintf("  %lx - %lx  %s\n", e->base, e->base + e->length, type_name(e->type));
        if (e->type == LIMINE_MEMMAP_USABLE && e->base + e->length > top)
            top = e->base + e->length;
    }

    total_pages = top / PAGE_SIZE;
    uint64_t bitmap_size = ALIGN_UP((total_pages + 7) / 8, PAGE_SIZE);

    /* 2. Место под битмап: первый usable-регион выше 1 МБ, куда он влезает */
    uint64_t bitmap_phys = 0;
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->base >= 0x100000 && e->length >= bitmap_size) {
            bitmap_phys = e->base;
            break;
        }
    }
    if (!bitmap_phys) panic("No room for PMM bitmap");

    bitmap = phys_to_virt(bitmap_phys);

    /* 3. Сначала всё занято, потом освобождаем usable-регионы */
    memset(bitmap, 0xFF, bitmap_size);
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        for (uint64_t p = e->base / PAGE_SIZE; p < (e->base + e->length) / PAGE_SIZE; p++) {
            mark_free(p);
            free_pages++;
        }
    }

    /* 4. Резервируем страницы самого битмапа и страницу 0 (0 = «ошибка») */
    for (uint64_t p = bitmap_phys / PAGE_SIZE; p < (bitmap_phys + bitmap_size) / PAGE_SIZE; p++) {
        mark_used(p);
        free_pages--;
    }
    if (!is_used(0)) { mark_used(0); free_pages--; }

    kprintf("PMM: %lu MiB free of %lu MiB (bitmap %lu KiB)\n",
            free_pages * PAGE_SIZE / (1024 * 1024),
            total_pages * PAGE_SIZE / (1024 * 1024),
            bitmap_size / 1024);
}

uint64_t pmm_alloc(void) {
    uint64_t f = spin_lock_irqsave(&pmm_lock);
    uint64_t result = 0;

    for (uint64_t n = 0; n < total_pages; n++) {
        uint64_t i = (search_hint + n) % total_pages;
        if (i % 8 == 0 && bitmap[i / 8] == 0xFF) { n += 7; continue; }
        if (!is_used(i)) {
            mark_used(i);
            free_pages--;
            search_hint = i + 1;
            result = i * PAGE_SIZE;
            break;
        }
    }

    spin_unlock_irqrestore(&pmm_lock, f);
    return result;
}

void pmm_free(uint64_t phys) {
    uint64_t i = phys / PAGE_SIZE;
    if (phys % PAGE_SIZE || i == 0 || i >= total_pages)
        panic("pmm_free: bad address %lx", phys);

    uint64_t f = spin_lock_irqsave(&pmm_lock);
    if (!is_used(i))
        panic("pmm_free: double free of %lx", phys);
    mark_free(i);
    free_pages++;
    if (i < search_hint) search_hint = i;
    spin_unlock_irqrestore(&pmm_lock, f);
}
struct limine_memmap_response *boot_memmap(void) {
    return memmap_request.response;
}