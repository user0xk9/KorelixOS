#include <korelix/io.h>
#include <korelix/apic.h>

/* Переносим PIC на неиспользуемые векторы 0x30-0x3F и маскируем все линии */
void pic_disable(void) {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x30); io_wait();   /* смещение мастера */
    outb(0xA1, 0x38); io_wait();   /* смещение слейва */
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}