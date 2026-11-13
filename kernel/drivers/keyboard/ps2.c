#include <stdint.h>
#include <korelix/io.h>
#include <korelix/apic.h>
#include <korelix/cpu.h>
#include <korelix/keyboard.h>

/* Раскладка US, scancode set 1 (индекс = скан-код нажатия) */
static const char lower[] =
    "\0\033" "1234567890-=\b\t" "qwertyuiop[]\n\0" "asdfghjkl;'`\0\\" "zxcvbnm,./\0*\0 ";
static const char upper[] =
    "\0\033" "!@#$%^&*()_+\b\t" "QWERTYUIOP{}\n\0" "ASDFGHJKL:\"~\0|" "ZXCVBNM<>?\0*\0 ";

static volatile uint8_t buf[256];
static volatile uint8_t head, tail;     /* uint8_t: индексы сами заворачиваются по 256 */
static int shift, caps, extended;

static void push(char c) {
    uint8_t next = head + 1;
    if (next == tail) return;           /* буфер полон: символ теряем */
    buf[head] = (uint8_t)c;
    head = next;
}

int kbd_getc(void) {
    if (head == tail) return -1;
    uint8_t c = buf[tail];
    tail++;
    return c;
}

static void kbd_handler(struct regs *r) {
    (void)r;
    uint8_t sc = inb(0x60);

    if (sc == 0xE0) { extended = 1; return; }
    if (extended)   { extended = 0; return; }   /* стрелки, Del и т.п. пока игнорируем */

    int release = sc & 0x80;
    sc &= 0x7F;

    if (sc == 0x2A || sc == 0x36) { shift = !release; return; }
    if (release) return;
    if (sc == 0x3A) { caps = !caps; return; }
    if (sc >= sizeof(lower) - 1) return;   /* -1: завершающий NUL строки не клавиша */

    char c = shift ? upper[sc] : lower[sc];
    if (caps && lower[sc] >= 'a' && lower[sc] <= 'z')
        c = shift ? lower[sc] : upper[sc];
    if (c) push(c);
}

void kbd_init(void) {
    while (inb(0x64) & 1) inb(0x60);            /* вычистить буфер контроллера */
    irq_register(VEC_KEYBOARD, kbd_handler);
    ioapic_route_isa_irq(1, VEC_KEYBOARD);
}