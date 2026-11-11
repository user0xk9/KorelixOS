#pragma once
#include <stdint.h>

void     pit_wait_ms(uint32_t ms);   /* до ~50 мс, блокирующее */
void     timer_init(uint32_t hz);
uint64_t timer_ticks(void);