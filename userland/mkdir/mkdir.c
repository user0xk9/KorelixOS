#include <ulib.h>

/* mkdir -p: создаёт все каталоги пути, уже существующие каталоги не ошибка */
static i64 make_parents(const char *path) {
    char buf[256];
    if (u_strlen(path) >= sizeof(buf)) return -ENAMETOOLONG;
    char *p = u_append(buf, path);
    *p = 0;

    for (char *q = buf + 1; *q; q++) {
        if (*q != '/') continue;
        *q = 0;
        i64 r = sys_mkdir(buf);
        if (r < 0 && r != -EEXIST) return r;
        *q = '/';
    }
    i64 r = sys_mkdir(buf);
    if (r == -EEXIST) {                           /* ок, если это каталог */
        struct user_stat st;
        if (sys_stat(buf, &st) == 0 && st.type == T_DIR) return 0;
    }
    return r;
}

void _start(int argc, char **argv) {
    int parents = 0, status = 0, names = 0;

    for (int i = 1; i < argc; i++) {
        if (u_streq(argv[i], "-p")) { parents = 1; continue; }
        names++;
        i64 r = parents ? make_parents(argv[i]) : sys_mkdir(argv[i]);
        if (r < 0) { u_fail("mkdir", argv[i], r); status = 1; }
    }
    if (!names) { u_eputs("usage: mkdir [-p] dir...\n"); status = 2; }
    sys_exit(status);
}
