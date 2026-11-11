#include <korelix/log.h>

__attribute__((noreturn)) void panic(const char *fmt, ...) {
    __asm__ volatile("cli");
        log_enter_panic();
    kprintf("\n=== KERNEL PANIC ===\n");

    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);

    kprintf("\nSystem halted.\n");
    for (;;) __asm__ volatile("hlt");
}