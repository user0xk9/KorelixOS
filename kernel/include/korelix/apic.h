#pragma once
#include <stdint.h>

#define VEC_TIMER     0x20
#define VEC_KEYBOARD  0x21
#define VEC_SPURIOUS  0xFF

#define LAPIC_ID         0x20
#define LAPIC_TPR        0x80
#define LAPIC_EOI        0xB0
#define LAPIC_SVR        0xF0
#define LAPIC_LINT0      0x350
#define LAPIC_LVT_TIMER  0x320
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_CUR  0x390
#define LAPIC_TIMER_DIV  0x3E0

void     pic_disable(void);
void     lapic_init(void);
uint32_t lapic_read(uint32_t reg);
void     lapic_write(uint32_t reg, uint32_t val);
uint32_t lapic_id(void);
void     lapic_eoi(void);
void     ioapic_init(void);
void     ioapic_route_isa_irq(uint8_t irq, uint8_t vector);