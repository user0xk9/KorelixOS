#pragma once
#include <stdint.h>

/* Текстовая консоль на framebuffer (шрифт 8x16, UTF-8: ASCII + кириллица). */

void fb_init(void);                 /* найти framebuffer от загрузчика и включить консоль */
void fbcon_putc(char c);            /* байт потока; UTF-8 собирается внутри */
void fbcon_flush(void);             /* нарисовать курсор (вызывать после пачки символов) */
void fbcon_panic_mode(void);        /* дальнейший вывод красным */

/* Подключить буфер вручную (используется fb_init и тестами). 0 = успех. */
int fb_attach(void *addr, uint64_t width, uint64_t height, uint64_t pitch, uint16_t bpp,
              uint8_t red_size, uint8_t red_shift, uint8_t green_size, uint8_t green_shift,
              uint8_t blue_size, uint8_t blue_shift);

/* Размер текстовой сетки (0, если консоль не включена) */
uint32_t fbcon_cols(void);
uint32_t fbcon_rows(void);
