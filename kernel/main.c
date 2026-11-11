#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include <korelix/serial.h>
#include <korelix/log.h>
#include <korelix/cpu.h>
#include <korelix/mm.h>

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

static void halt(void) {
    for (;;) __asm__ volatile("hlt");
}

void kmain(void) {
    serial_init();
    kprintf("Hello from KorelixOS!\n");

    gdt_init();
    kprintf("GDT OK\n");
    idt_init();
    kprintf("IDT OK\n");

    pmm_init();

    vmm_init();
    kprintf("VMM: now on own page tables\n");

    /* тест VMM: отображаем свежую страницу по новому виртуальному адресу */
    uint64_t page = pmm_alloc();
    uint64_t test_va = 0xffffc00000000000ULL;
    vmm_map(kernel_pml4, test_va, page, PTE_WRITE | PTE_NX);
    *(volatile uint64_t *)test_va = 0x1234abcd;
    kprintf("virt %lx -> phys %lx (page was %lx)\n",
            test_va, vmm_translate(kernel_pml4, test_va), page);
    kprintf("same page via HHDM: %lx\n", *(volatile uint64_t *)phys_to_virt(page));

    vmm_unmap(kernel_pml4, test_va);
    pmm_free(page);
    kprintf("after unmap: translate = %lx (должно быть 0)\n",
            vmm_translate(kernel_pml4, test_va));

    if (fb_request.response && fb_request.response->framebuffer_count > 0) {
        struct limine_framebuffer *fb = fb_request.response->framebuffers[0];
        volatile uint32_t *px = fb->address;
        for (uint64_t y = 0; y < 100; y++)
            for (uint64_t x = 0; x < 100; x++)
                px[y * (fb->pitch / 4) + x] = 0x00FF8800;
        kprintf("Framebuffer OK\n");
    }

    
    kprintf("Boot complete, halting.\n");
    halt();
}