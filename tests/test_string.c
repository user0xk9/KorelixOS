/* Проверка kernel/lib/string.c на хосте (имена переименованы ключами -D в Makefile). */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "check.h"

void *k_memcpy(void *, const void *, size_t);
void *k_memset(void *, int, size_t);
void *k_memmove(void *, const void *, size_t);
int   k_memcmp(const void *, const void *, size_t);

#define N 256

int main(void) {
    uint8_t a[N], ref[N];

    /* memset: границы не трогает */
    memset(a, 0x11, N);
    k_memset(a + 10, 0xAB, 100);
    for (int i = 0; i < N; i++) CHECK(a[i] == ((i >= 10 && i < 110) ? 0xAB : 0x11));

    /* memcpy без перекрытия */
    for (int i = 0; i < N; i++) a[i] = (uint8_t)i;
    uint8_t b[N];
    memset(b, 0, N);
    k_memcpy(b + 3, a + 5, 200);
    for (int i = 0; i < N; i++) CHECK(b[i] == ((i >= 3 && i < 203) ? (uint8_t)(i + 2) : 0));

    /* memmove: все варианты перекрытия против эталона (libc memmove) */
    for (size_t dst = 0; dst < 40; dst++)
        for (size_t src = 0; src < 40; src++)
            for (size_t n = 0; n < 100; n += 7) {
                for (int i = 0; i < N; i++) a[i] = ref[i] = (uint8_t)(i * 7 + 1);
                memmove(ref + dst, ref + src, n);
                void *r = k_memmove(a + dst, a + src, n);
                CHECK(r == a + dst);
                CHECK(memcmp(a, ref, N) == 0);
            }

    /* memcmp */
    CHECK(k_memcmp("abc", "abc", 3) == 0);
    CHECK(k_memcmp("abc", "abd", 3) < 0);
    CHECK(k_memcmp("abd", "abc", 3) > 0);
    CHECK(k_memcmp("abc", "abd", 2) == 0);
    CHECK(k_memcmp("", "", 0) == 0);

    puts("test_string: OK");
    return 0;
}
