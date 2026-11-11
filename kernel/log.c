#include <korelix/spinlock.h>
#include <stdint.h>
#include <korelix/log.h>
#include <korelix/serial.h>

static void kputc(char c) {
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
}

static void kputs(const char *s) {
    while (*s) kputc(*s++);
}

static void put_num(unsigned long long v, unsigned base, int neg) {
    char buf[32];
    int i = 0;
    if (v == 0) buf[i++] = '0';
    while (v) {
        buf[i++] = "0123456789abcdef"[v % base];
        v /= base;
    }
    if (neg) kputc('-');
    while (i--) kputc(buf[i]);
}

static void kvprintf_unlocked(const char *fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') { kputc(*fmt); continue; }
        fmt++;
        int lng = 0;
        while (*fmt == 'l' || *fmt == 'z') { lng++; fmt++; }
        if (!*fmt) return;

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            kputs(s ? s : "(null)");
            break;
        }
        case 'c':
            kputc((char)va_arg(ap, int));
            break;
        case 'd': {
            long long v = lng ? va_arg(ap, long long) : va_arg(ap, int);
            if (v < 0) put_num((unsigned long long)-v, 10, 1);
            else       put_num((unsigned long long)v, 10, 0);
            break;
        }
        case 'u': {
            unsigned long long v = lng ? va_arg(ap, unsigned long long)
                                       : va_arg(ap, unsigned int);
            put_num(v, 10, 0);
            break;
        }
        case 'x': {
            unsigned long long v = lng ? va_arg(ap, unsigned long long)
                                       : va_arg(ap, unsigned int);
            put_num(v, 16, 0);
            break;
        }
        case 'p':
            kputs("0x");
            put_num((uintptr_t)va_arg(ap, void *), 16, 0);
            break;
        case '%':
            kputc('%');
            break;
        default:
            kputc('%');
            kputc(*fmt);
        }
    }
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}
static spinlock_t log_lock = SPINLOCK_INIT;
static volatile int panic_mode;

/* В панике блокировки не берём: паника могла случиться внутри самого kprintf */
void log_enter_panic(void) { panic_mode = 1; }

void kvprintf(const char *fmt, va_list ap) {
    if (panic_mode) { kvprintf_unlocked(fmt, ap); return; }
    uint64_t f = spin_lock_irqsave(&log_lock);
    kvprintf_unlocked(fmt, ap);
    spin_unlock_irqrestore(&log_lock, f);
}