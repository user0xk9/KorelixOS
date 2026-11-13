#include <stddef.h>
#include <stdint.h>

/* Копирование через rep movsb/stosb: быстро на современных CPU (ERMS/FSRM) и
 * не превращается компилятором в рекурсивный вызов самого себя. */

void *memcpy(void *dest, const void *src, size_t n) {
    void *ret = dest;
    __asm__ volatile("rep movsb" : "+D"(dest), "+S"(src), "+c"(n) : : "memory");
    return ret;
}

void *memset(void *s, int c, size_t n) {
    void *ret = s;
    __asm__ volatile("rep stosb" : "+D"(s), "+c"(n) : "a"(c) : "memory");
    return ret;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    if (d == s || n == 0) return dest;
    if (d < s || d >= s + n)            /* вперёд безопасно */
        return memcpy(dest, src, n);

    /* перекрытие с d > s: копируем с конца (DF=1), обработчики прерываний делают cld */
    d += n - 1;
    s += n - 1;
    __asm__ volatile("std; rep movsb; cld" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return dest;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *p = a, *q = b;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != q[i]) return p[i] < q[i] ? -1 : 1;
    }
    return 0;
}
