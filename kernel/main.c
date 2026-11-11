#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include <korelix/serial.h>
#include <korelix/log.h>
#include <korelix/cpu.h>
#include <korelix/mm.h>
#include <korelix/heap.h>
#include <korelix/string.h>
#include <korelix/acpi.h>
#include <korelix/apic.h>
#include <korelix/timer.h>
#include <korelix/keyboard.h>

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

    /* тест кучи */
    heap_init();
    heap_print();

    void *p1 = kmalloc(100);
    void *p2 = kmalloc(5000);
    void *p3 = kmalloc(32);
    kprintf("kmalloc: p1=%p p2=%p p3=%p\n", p1, p2, p3);
    memset(p1, 0xAB, 100);

    kfree(p2);
    void *p4 = kmalloc(4000);   /* должен занять место p2 */
    kprintf("reuse: p4=%p (p2 was %p)\n", p4, p2);

    kfree(p1);
    kfree(p3);
    kfree(p4);
    heap_print();               /* всё слилось: 1 блок, used 0 */

    void *big = kmalloc(1024 * 1024);   /* больше начальной кучи: она вырастет */
    kprintf("big: %p\n", big);
    kfree(big);
    heap_print();

    if (fb_request.response && fb_request.response->framebuffer_count > 0) {
        struct limine_framebuffer *fb = fb_request.response->framebuffers[0];
        volatile uint32_t *px = fb->address;
        for (uint64_t y = 0; y < 100; y++)
            for (uint64_t x = 0; x < 100; x++)
                px[y * (fb->pitch / 4) + x] = 0x00FF8800;
        kprintf("Framebuffer OK\n");
    }

    /* Тесты исключений: раскомментируй по одному, ядро после них паникует */
    // __asm__ volatile("ud2");                          /* #UD */
    // *(volatile int *)0 = 1;                           /* #PF, cr2=0 */
    // *(volatile uint8_t *)(uintptr_t)kmain = 0;        /* #PF, error=3: W^X */
    // void *x = kmalloc(16); kfree(x); kfree(x);        /* panic: double free */

    /* прерывания, таймер, клавиатура */
    pic_disable();
    acpi_init();
    lapic_init();
    ioapic_init();
    timer_init(100);        /* 100 тиков в секунду */
    kbd_init();

    kprintf("Interrupts on. Click the QEMU window and type.\n");
    __asm__ volatile("sti");

    uint64_t last_sec = 0;
    for (;;) {
        int c = kbd_getc();
        if (c >= 0) kprintf("%c", c);

        uint64_t sec = timer_ticks() / 100;
        if (sec != last_sec) {
            last_sec = sec;
            kprintf("[uptime %lu s]\n", sec);
        }
        __asm__ volatile("hlt");        /* спим до следующего прерывания */
    }
}