#pragma once

typedef unsigned long u64;
typedef long i64;
typedef unsigned int u32;

#define SYS_EXIT     0
#define SYS_WRITE    1
#define SYS_GETPID   2
#define SYS_SLEEP_MS 3
#define SYS_OPEN     4
#define SYS_READ     5
#define SYS_CLOSE    6
#define SYS_READDIR  7
#define SYS_SPAWN    8

/* Запись каталога (ровно 64 байта; type: 1 = файл, 2 = каталог) */
struct user_dirent {
    u32 type;
    u32 size;
    char name[56];
};

static inline i64 syscall3(i64 n, i64 a, i64 b, i64 c) {
    i64 ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline __attribute__((noreturn)) void sys_exit(i64 code) {
    syscall3(SYS_EXIT, code, 0, 0);
    for (;;) {}
}
static inline i64 sys_write(int fd, const void *buf, u64 len) { return syscall3(SYS_WRITE, fd, (i64)buf, (i64)len); }
static inline i64 sys_getpid(void)                            { return syscall3(SYS_GETPID, 0, 0, 0); }
static inline i64 sys_sleep_ms(u64 ms)                        { return syscall3(SYS_SLEEP_MS, (i64)ms, 0, 0); }
static inline i64 sys_open(const char *path)                  { return syscall3(SYS_OPEN, (i64)path, 0, 0); }
static inline i64 sys_read(int fd, void *buf, u64 len)        { return syscall3(SYS_READ, fd, (i64)buf, (i64)len); }
static inline i64 sys_close(int fd)                           { return syscall3(SYS_CLOSE, fd, 0, 0); }
static inline i64 sys_readdir(int fd, u64 i, struct user_dirent *d) { return syscall3(SYS_READDIR, fd, (i64)i, (i64)d); }
static inline i64 sys_spawn(const char *path)                 { return syscall3(SYS_SPAWN, (i64)path, 0, 0); }

static inline u64 u_strlen(const char *s) {
    u64 n = 0;
    while (s[n]) n++;
    return n;
}

static inline void u_puts(const char *s) {
    sys_write(1, s, u_strlen(s));
}

/* Сборка строки в буфере: каждый append возвращает новый конец */
static inline char *u_append(char *p, const char *s) {
    while (*s) *p++ = *s++;
    return p;
}

static inline char *u_append_num(char *p, u64 v) {
    char tmp[24];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) *p++ = tmp[--n];
    return p;
}