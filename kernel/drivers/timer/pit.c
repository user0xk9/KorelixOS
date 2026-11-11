#include <korelix/io.h>
#include <korelix/timer.h>

#define PIT_HZ 1193182

/* Блокирующее ожидание на канале 2 PIT (только для калибровки) */
void pit_wait_ms(uint32_t ms) {
    uint16_t count = (uint16_t)((PIT_HZ * ms) / 1000);
    outb(0x61, inb(0x61) & 0xFC);          /* gate=0, динамик выкл */
    outb(0x43, 0xB0);                      /* канал 2, lo/hi, режим 0 */
    outb(0x42, count & 0xFF);
    outb(0x42, count >> 8);
    outb(0x61, (inb(0x61) & 0xFC) | 1);    /* gate=1: пошёл отсчёт */
    while (!(inb(0x61) & 0x20)) {}         /* ждём, пока OUT2 станет 1 */
}