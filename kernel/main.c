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
#include <korelix/sched.h>
#include <korelix/syscall.h>
#include <korelix/vfs.h>

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
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
    heap_init();

    if (fb_request.response && fb_request.response->framebuffer_count > 0) {
        struct limine_framebuffer *fb = fb_request.response->framebuffers[0];
        volatile uint32_t *px = fb->address;
        for (uint64_t y = 0; y < 100; y++)
            for (uint64_t x = 0; x < 100; x++)
                px[y * (fb->pitch / 4) + x] = 0x00FF8800;
        kprintf("Framebuffer OK\n");
    }

    /* прерывания, таймер, клавиатура */
    pic_disable();
    acpi_init();
    lapic_init();
    ioapic_init();
    timer_init(TIMER_HZ);
    kbd_init();

    /* userspace */
    fpu_init();
    cpu_enable_protections();
    syscall_init();
    sched_init();

    /* корневая файловая система из модуля initrd.tar */
    if (!module_request.response || module_request.response->module_count == 0)
        panic("No initrd module from the bootloader");
    struct limine_file *rd = module_request.response->modules[0];

    vfs_init();
    int nfiles = initramfs_mount(rd->address, rd->size);
    if (nfiles < 0) panic("initramfs: bad archive");
    kprintf("initramfs: %d files (%lu bytes)\n", nfiles, (uint64_t)rd->size);

    struct vnode *init = vfs_lookup("/bin/init");
    if (!init || init->type != VFS_FILE) panic("/bin/init not found in initramfs");
    if (process_spawn("init", init->data, init->size) < 0)
        panic("cannot start /bin/init");

    kprintf("Interrupts on. Click the QEMU window and type.\n");
    __asm__ volatile("sti");

    uint64_t last_sec = 0;
    for (;;) {
        int c = kbd_getc();
        if (c >= 0) kprintf("%c", c);

        uint64_t sec = timer_ticks() / TIMER_HZ;
        if (sec != last_sec) {
            last_sec = sec;
            kprintf("[uptime %lu s]\n", sec);
        }
        __asm__ volatile("hlt");
    }
}