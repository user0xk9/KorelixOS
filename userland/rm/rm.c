#include <ulib.h>

#define MAX_DEPTH 24          /* глубина рекурсии ограничена размером стека */

static i64 remove_path(const char *path, int recursive, int depth);

/* Пока в каталоге есть записи, удаляем первую (индексы при удалении сдвигаются) */
static i64 remove_children(const char *dir, int depth) {
    if (depth >= MAX_DEPTH) return -ENAMETOOLONG;
    for (;;) {
        struct user_dirent d;
        int fd = (int)sys_open(dir, O_RDONLY);
        if (fd < 0) return fd;
        i64 r = sys_readdir(fd, 0, &d);
        sys_close(fd);
        if (r < 0) return r;
        if (r == 0) return 0;

        char child[256];
        r = u_join(child, sizeof(child), dir, d.name);
        if (r < 0) return r;
        r = remove_path(child, 1, depth + 1);
        if (r < 0) return r;                       /* не удалось: выходим, иначе вечный цикл */
    }
}

static i64 remove_path(const char *path, int recursive, int depth) {
    struct user_stat st;
    i64 r = sys_stat(path, &st);
    if (r < 0) return r;
    if (st.type != T_DIR) return sys_unlink(path);
    if (!recursive) return -EISDIR;
    r = remove_children(path, depth);
    if (r < 0) return r;
    return sys_rmdir(path);
}

void _start(int argc, char **argv) {
    int recursive = 0, force = 0, status = 0, names = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1]) {
            for (const char *f = argv[i] + 1; *f; f++) {
                if (*f == 'r' || *f == 'R') recursive = 1;
                else if (*f == 'f') force = 1;
                else { u_eputs("rm: unknown option (usage: rm [-rf] path...)\n"); sys_exit(2); }
            }
            continue;
        }
        names++;
        if (u_streq(argv[i], "/") || u_streq(argv[i], "..") || u_streq(argv[i], ".")) {
            u_fail("rm", argv[i], -EINVAL);
            status = 1;
            continue;
        }
        i64 r = remove_path(argv[i], recursive, 0);
        if (r < 0 && !(force && r == -ENOENT)) { u_fail("rm", argv[i], r); status = 1; }
    }
    if (!names) { u_eputs("usage: rm [-rf] path...\n"); status = 2; }
    sys_exit(status);
}
