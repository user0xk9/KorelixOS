#pragma once
#include <stdarg.h>

void kprintf(const char *fmt, ...);
void kvprintf(const char *fmt, va_list ap);
__attribute__((noreturn)) void panic(const char *fmt, ...);
void log_enter_panic(void);