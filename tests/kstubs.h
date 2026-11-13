#pragma once
/* Заглушки ядра для хостовых тестов (подключать ровно в один .c файл теста) */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

void *kmalloc(size_t n) { return malloc(n); }
void *kzalloc(size_t n) { return calloc(1, n); }
void  kfree(void *p)    { free(p); }
void  kprintf(const char *fmt, ...) { (void)fmt; }
__attribute__((noreturn)) void panic(const char *fmt, ...) {
    (void)fmt;
    fprintf(stderr, "panic()\n");
    abort();
}
