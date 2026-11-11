#include <stdint.h>
#include <stddef.h>
#include <korelix/heap.h>
#include <korelix/mm.h>
#include <korelix/log.h>
#include <korelix/string.h>
#include <korelix/spinlock.h>

#define HEAP_START  0xffffd00000000000ULL
#define HEAP_MAX    (HEAP_START + (1ULL << 30))   /* максимум 1 ГБ */
#define GROW_MIN    (16 * PAGE_SIZE)              /* растём минимум на 64 КБ */
#define MIN_SPLIT   16
#define BLOCK_MAGIC 0xC0DEB10C

/* Заголовок = 32 байта, значит данные всегда выровнены по 16 */
struct block {
    uint64_t size;            /* размер данных (без заголовка) */
    struct block *prev, *next;
    uint32_t magic;
    uint32_t free;
};

static struct block *head, *tail;
static uint64_t heap_end = HEAP_START;
static spinlock_t heap_lock = SPINLOCK_INIT;

/* Отображаем новые страницы в конце кучи */
static int grow(size_t bytes) {
    bytes = ALIGN_UP(bytes, PAGE_SIZE);
    if (heap_end + bytes > HEAP_MAX) return -1;

    for (size_t off = 0; off < bytes; off += PAGE_SIZE) {
        uint64_t phys = pmm_alloc();
        if (!phys || vmm_map(kernel_pml4, heap_end + off, phys, PTE_WRITE | PTE_NX)) {
            if (phys) pmm_free(phys);
            for (size_t o = 0; o < off; o += PAGE_SIZE) {   /* откат */
                uint64_t pa = vmm_translate(kernel_pml4, heap_end + o);
                vmm_unmap(kernel_pml4, heap_end + o);
                pmm_free(pa);
            }
            return -1;
        }
        memset((void *)(heap_end + off), 0, PAGE_SIZE);
    }

    if (tail && tail->free) {
        tail->size += bytes;                  /* сливаем с последним свободным */
    } else {
        struct block *b = (struct block *)heap_end;
        b->size  = bytes - sizeof(struct block);
        b->prev  = tail;
        b->next  = NULL;
        b->magic = BLOCK_MAGIC;
        b->free  = 1;
        if (tail) tail->next = b; else head = b;
        tail = b;
    }
    heap_end += bytes;
    return 0;
}

void heap_init(void) {
    head = tail = NULL;
    heap_end = HEAP_START;
    if (grow(GROW_MIN))
        panic("heap: cannot allocate initial memory");
}

/* Берём блок b под size байт; остаток отрезаем в новый свободный блок */
static void *take(struct block *b, size_t size) {
    if (b->size >= size + sizeof(struct block) + MIN_SPLIT) {
        struct block *n = (struct block *)((uint8_t *)(b + 1) + size);
        n->size  = b->size - size - sizeof(struct block);
        n->magic = BLOCK_MAGIC;
        n->free  = 1;
        n->prev  = b;
        n->next  = b->next;
        if (b->next) b->next->prev = n; else tail = n;
        b->next = n;
        b->size = size;
    }
    b->free = 0;
    return b + 1;
}

static void *kmalloc_locked(size_t size) {
    if (size == 0 || size > HEAP_MAX - HEAP_START - sizeof(struct block))
        return NULL;
    size = ALIGN_UP(size, 16);

    for (struct block *b = head; b; b = b->next)
        if (b->free && b->size >= size)
            return take(b, size);

    /* места нет: растём. Новый/расширенный свободный блок всегда становится tail */
    size_t need = size + sizeof(struct block);
    if (grow(need < GROW_MIN ? GROW_MIN : need))
        return NULL;
    return take(tail, size);
}

void *kzalloc(size_t size) {
    void *p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

static void kfree_locked(void *ptr) {
    if (!ptr) return;

    uint64_t a = (uint64_t)ptr;
    if (a < HEAP_START + sizeof(struct block) || a >= heap_end || (a & 15))
        panic("kfree: bad pointer %p", ptr);

    struct block *b = (struct block *)ptr - 1;
    if (b->magic != BLOCK_MAGIC)
        panic("kfree: corrupted header or bad pointer %p", ptr);
    if (b->free)
        panic("kfree: double free of %p", ptr);

    memset(ptr, 0xDD, b->size);   /* яд: ловит use-after-free, не оставляет данных */
    b->free = 1;

    /* слияние со следующим */
    if (b->next && b->next->free) {
        struct block *n = b->next;
        b->size += sizeof(struct block) + n->size;
        b->next = n->next;
        if (n->next) n->next->prev = b; else tail = b;
        n->magic = 0;
    }
    /* слияние с предыдущим */
    if (b->prev && b->prev->free) {
        struct block *p = b->prev;
        p->size += sizeof(struct block) + b->size;
        p->next = b->next;
        if (b->next) b->next->prev = p; else tail = p;
        b->magic = 0;
    }
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) { kfree(ptr); return NULL; }

    struct block *b = (struct block *)ptr - 1;
    if (b->magic != BLOCK_MAGIC || b->free)
        panic("krealloc: bad pointer %p", ptr);
    if (b->size >= ALIGN_UP(size, 16)) return ptr;

    void *n = kmalloc(size);
    if (!n) return NULL;
    memcpy(n, ptr, b->size);
    kfree(ptr);
    return n;
}

void heap_print(void) {
    uint64_t used = 0, freeb = 0, blocks = 0;
    for (struct block *b = head; b; b = b->next) {
        blocks++;
        if (b->free) freeb += b->size; else used += b->size;
    }
    kprintf("heap: %lu KiB mapped, used %lu B, free %lu B, %lu blocks\n",
            (heap_end - HEAP_START) / 1024, used, freeb, blocks);
}
void *kmalloc(size_t size) {
    uint64_t f = spin_lock_irqsave(&heap_lock);
    void *p = kmalloc_locked(size);
    spin_unlock_irqrestore(&heap_lock, f);
    return p;
}

void kfree(void *ptr) {
    uint64_t f = spin_lock_irqsave(&heap_lock);
    kfree_locked(ptr);
    spin_unlock_irqrestore(&heap_lock, f);
}