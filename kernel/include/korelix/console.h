#pragma once
#include <stddef.h>

/* Блокирующее чтение строки (с эхо и Backspace). 0 = конец ввода (Ctrl-D). */
long console_read(char *buf, size_t len);