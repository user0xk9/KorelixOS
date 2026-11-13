#pragma once

typedef unsigned long u64;
typedef long i64;
typedef unsigned int u32;

/* Номера вызовов и коды ошибок = kernel/include/korelix/syscall.h и errno.h */
#define SYS_EXIT     0
#define SYS_WRITE    1
#define SYS_GETPID   2
#define SYS_SLEEP_MS 3
#define SYS_OPEN     4
#define SYS_READ     5
#define SYS_CLOSE    6
#define SYS_READDIR  7
#define SYS_SPAWN    8
#define SYS_WAIT     9
#define SYS_MKDIR    10
#define SYS_UNLINK   11
#define SYS_RMDIR    12
#define SYS_RENAME   13
#define SYS_CHDIR    14
#define SYS_GETCWD   15
#define SYS_LSEEK    16
#define SYS_STAT     17
#define SYS_FSINFO   18

#define EPERM         1
#define ENOENT        2
#define E2BIG         7
#define ENOEXEC       8
#define EBADF         9
#define ENOMEM       12
#define EFAULT       14
#define EBUSY        16
#define EEXIST       17
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define EMFILE       24
#define ENOSPC       28
#define ERANGE       34
#define ENAMETOOLONG 36
#define ENOTEMPTY    39

#define O_RDONLY   0
#define O_WRONLY   1
#define O_RDWR     2
#define O_CREAT    0x40
#define O_EXCL     0x80
#define O_TRUNC    0x200
#define O_APPEND   0x400

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define T_FILE 1
#define T_DIR  2

/* Запись каталога (64 байта) */
struct user_dirent {
    u32 type;
    u32 size;
    char name[56];
};

struct user_stat {
    u32 type;
    u32 _pad;
    u64 size;
    u64 id;
};

struct user_fsinfo {
    u64 files, dirs, bytes, max_file;
};

#ifdef ULIB_HOST                       /* сборка для хостовых тестов: вызовы эмулирует tests/host_sys.c */
i64 host_syscall(i64 n, i64 a, i64 b, i64 c, i64 d);
static inline i64 syscall4(i64 n, i64 a, i64 b, i64 c, i64 d) { return host_syscall(n, a, b, c, d); }
#else
static inline i64 syscall4(i64 n, i64 a, i64 b, i64 c, i64 d) {
    register i64 r10 __asm__("r10") = d;
    i64 ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10)
                     : "rcx", "r11", "memory");
    return ret;
}
#endif
static inline i64 syscall3(i64 n, i64 a, i64 b, i64 c) { return syscall4(n, a, b, c, 0); }

static inline __attribute__((noreturn)) void sys_exit(i64 code) {
    syscall3(SYS_EXIT, code, 0, 0);
    for (;;) {}
}
static inline i64 sys_write(int fd, const void *buf, u64 len) { return syscall3(SYS_WRITE, fd, (i64)buf, (i64)len); }
static inline i64 sys_getpid(void)                            { return syscall3(SYS_GETPID, 0, 0, 0); }
static inline i64 sys_sleep_ms(u64 ms)                        { return syscall3(SYS_SLEEP_MS, (i64)ms, 0, 0); }
static inline i64 sys_open(const char *path, int flags)       { return syscall3(SYS_OPEN, (i64)path, flags, 0); }
static inline i64 sys_read(int fd, void *buf, u64 len)        { return syscall3(SYS_READ, fd, (i64)buf, (i64)len); }
static inline i64 sys_close(int fd)                           { return syscall3(SYS_CLOSE, fd, 0, 0); }
static inline i64 sys_readdir(int fd, u64 i, struct user_dirent *d) { return syscall3(SYS_READDIR, fd, (i64)i, (i64)d); }
/* in_fd/out_fd: свой fd, который станет stdin/stdout ребёнка; -1 = унаследовать */
static inline i64 sys_spawn(const char *path, char **argv, int in_fd, int out_fd) {
    return syscall4(SYS_SPAWN, (i64)path, (i64)argv, in_fd, out_fd);
}
static inline i64 sys_wait(i64 pid, i64 *status)              { return syscall3(SYS_WAIT, pid, (i64)status, 0); }
static inline i64 sys_mkdir(const char *path)                 { return syscall3(SYS_MKDIR, (i64)path, 0, 0); }
static inline i64 sys_unlink(const char *path)                { return syscall3(SYS_UNLINK, (i64)path, 0, 0); }
static inline i64 sys_rmdir(const char *path)                 { return syscall3(SYS_RMDIR, (i64)path, 0, 0); }
static inline i64 sys_rename(const char *from, const char *to) { return syscall3(SYS_RENAME, (i64)from, (i64)to, 0); }
static inline i64 sys_chdir(const char *path)                 { return syscall3(SYS_CHDIR, (i64)path, 0, 0); }
static inline i64 sys_getcwd(char *buf, u64 size)             { return syscall3(SYS_GETCWD, (i64)buf, (i64)size, 0); }
static inline i64 sys_lseek(int fd, i64 off, int whence)      { return syscall3(SYS_LSEEK, fd, off, whence); }
static inline i64 sys_stat(const char *path, struct user_stat *st) { return syscall3(SYS_STAT, (i64)path, (i64)st, 0); }
static inline i64 sys_fsinfo(struct user_fsinfo *info)        { return syscall3(SYS_FSINFO, (i64)info, 0, 0); }

/* ---------- строки ---------- */

static inline u64 u_strlen(const char *s) {
    u64 n = 0;
    while (s[n]) n++;
    return n;
}

static inline int u_streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
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

static inline char *u_append_snum(char *p, i64 v) {
    if (v < 0) {
        *p++ = '-';
        return u_append_num(p, (u64)(-v));
    }
    return u_append_num(p, (u64)v);
}

/* Число, выровненное пробелами слева до width символов */
static inline char *u_append_num_pad(char *p, u64 v, int width) {
    char tmp[24];
    char *e = u_append_num(tmp, v);
    for (int i = (int)(e - tmp); i < width; i++) *p++ = ' ';
    for (char *q = tmp; q < e; q++) *p++ = *q;
    return p;
}

/* Имя файла в конце пути */
static inline const char *u_basename(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' && p[1]) base = p + 1;
    return base;
}

/* out = dir + "/" + name (слэш не дублируется). 0 или -ENAMETOOLONG. */
static inline int u_join(char *out, u64 cap, const char *dir, const char *name) {
    u64 dl = u_strlen(dir), nl = u_strlen(name);
    int slash = dl > 0 && dir[dl - 1] != '/';
    if (dl + (u64)slash + nl + 1 > cap) return -ENAMETOOLONG;
    char *p = u_append(out, dir);
    if (slash) *p++ = '/';
    p = u_append(p, name);
    *p = 0;
    return 0;
}

/* ---------- вывод ---------- */

/* Пишет всё; файл может принять данные частями */
static inline i64 u_write_all(int fd, const char *buf, u64 len) {
    u64 done = 0;
    while (done < len) {
        i64 r = sys_write(fd, buf + done, len - done);
        if (r <= 0) return r < 0 ? r : -ENOSPC;
        done += (u64)r;
    }
    return 0;
}

static inline void u_puts(const char *s)  { u_write_all(1, s, u_strlen(s)); }
static inline void u_eputs(const char *s) { u_write_all(2, s, u_strlen(s)); }   /* ошибки: всегда на консоль */

static inline const char *u_strerror(i64 err) {
    switch (err < 0 ? -err : err) {
    case ENOENT:       return "no such file or directory";
    case E2BIG:        return "too many or too long arguments";
    case ENOEXEC:      return "cannot execute (not a valid program)";
    case EBADF:        return "bad file descriptor";
    case ENOMEM:       return "out of memory";
    case EFAULT:       return "bad address";
    case EBUSY:        return "resource busy";
    case EEXIST:       return "already exists";
    case ENOTDIR:      return "not a directory";
    case EISDIR:       return "is a directory";
    case EINVAL:       return "invalid argument";
    case EMFILE:       return "too many open files";
    case ENOSPC:       return "no space left (file too large)";
    case ERANGE:       return "result too large";
    case ENAMETOOLONG: return "name too long";
    case ENOTEMPTY:    return "directory not empty";
    default:           return "operation failed";
    }
}

/* "prog: what: сообщение" в stderr */
static inline void u_fail(const char *prog, const char *what, i64 err) {
    char msg[400];
    char *p = u_append(msg, prog);
    p = u_append(p, ": ");
    if (what) {
        u64 n = u_strlen(what);
        if (n > 200) n = 200;
        for (u64 i = 0; i < n; i++) *p++ = what[i];
        p = u_append(p, ": ");
    }
    p = u_append(p, u_strerror(err));
    *p++ = '\n';
    u_write_all(2, msg, (u64)(p - msg));
}
