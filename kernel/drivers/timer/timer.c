#include <stdint.h>
#include <korelix/apic.h>
#include <korelix/cpu.h>
#include <korelix/log.h>
#include <korelix/timer.h>
#include <korelix/sched.h>

static volatile uint64_t ticks;

static void timer_handler(struct regs *r) {
    (void)r;
    ticks++;
    sched_tick();       /* может переключить задачу */
}

uint64_t timer_ticks(void) { return ticks; }

void timer_init(uint32_t hz) {
    /* Калибровка: даём таймеру LAPIC считать 10 мс по эталону PIT */
    lapic_write(LAPIC_TIMER_DIV, 0x3);                      /* делитель 16 */
    lapic_write(LAPIC_LVT_TIMER, (1u << 16) | VEC_TIMER);   /* замаскирован, one-shot */
    lapic_write(LAPIC_TIMER_INIT, 0xFFFFFFFF);
    pit_wait_ms(10);
    uint32_t elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CUR);
    lapic_write(LAPIC_TIMER_INIT, 0);

    uint32_t per_ms = elapsed / 10;
    if (!per_ms) panic("LAPIC timer calibration failed");
    kprintf("LAPIC timer: %u ticks/ms\n", per_ms);

    irq_register(VEC_TIMER, timer_handler);
    lapic_write(LAPIC_LVT_TIMER, (1u << 17) | VEC_TIMER);   /* periodic */
    lapic_write(LAPIC_TIMER_INIT, (uint32_t)((uint64_t)per_ms * 1000 / hz));
}