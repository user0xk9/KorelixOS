#include <stdint.h>
#include <korelix/serial.h>

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00); // выключить прерывания
    outb(COM1 + 3, 0x80); // включить DLAB
    outb(COM1 + 0, 0x03); // скорость 38400
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); // 8 бит, без чётности, 1 стоп-бит
    outb(COM1 + 2, 0xC7); // FIFO
    outb(COM1 + 4, 0x0B);
}

void serial_putc(char c) {
    while (!(inb(COM1 + 5) & 0x20)) {}
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *s) {
    for (; *s; s++) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s);
    }
}