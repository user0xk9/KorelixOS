#pragma once
#include <stdarg.h>
#include <stddef.h>

void kprintf(const char *fmt, ...);
void kvprintf(const char *fmt, va_list ap);
void kwrite(const char *buf, size_t n);
void log_enter_panic(void);
__attribute__((noreturn)) void panic(const char *fmt, ...);