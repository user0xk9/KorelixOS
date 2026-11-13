#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include <korelix/serial.h>
#include <korelix/fb.h>
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
    fb_init();                      /* вывод на экран: всё, что ниже, видно и в serial, и на экране */
    kprintf("Hello from KorelixOS!\n");

    gdt_init();
    kprintf("GDT OK\n");
    idt_init();
    kprintf("IDT OK\n");

    pmm_init();
    vmm_init();
    kprintf("VMM: now on own page tables\n");
    heap_init();

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

    void *init_img;
    size_t init_size;
    if (vfs_read_whole(NULL, "/bin/init", &init_img, &init_size))
        panic("/bin/init not found in initramfs");
    const char *init_argv[] = { "init" };
    if (process_spawn("init", init_img, init_size, 1, init_argv, NULL) < 0)
        panic("cannot start /bin/init");
    kfree(init_img);

    kprintf("Interrupts on.\n");
    __asm__ volatile("sti");

    /* Главный поток больше не читает клавиатуру (это делает консоль): просто спит */
    for (;;) __asm__ volatile("hlt");
}