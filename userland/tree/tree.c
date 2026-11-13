#include <ulib.h>

#define MAX_DEPTH 12          /* предел вложенности (стек и длина префикса) */

static u64 dirs, files;

/* Каждая запись читается заново по индексу: открытых fd между уровнями нет */
static void walk(const char *dir, char *prefix, int depth) {
    for (u64 i = 0;; i++) {
        struct user_dirent d, next;
        int fd = (int)sys_open(dir, O_RDONLY);
        if (fd < 0) { u_fail("tree", dir, fd); return; }
        i64 r = sys_readdir(fd, i, &d);
        int last = r > 0 && sys_readdir(fd, i + 1, &next) <= 0;
        sys_close(fd);
        if (r <= 0) return;

        char line[200];
        char *p = u_append(line, prefix);
        p = u_append(p, last ? "`-- " : "|-- ");
        p = u_append(p, d.name);
        if (d.type == T_DIR) *p++ = '/';
        *p++ = '\n';
        u_write_all(1, line, (u64)(p - line));

        if (d.type != T_DIR) { files++; continue; }
        dirs++;
        if (depth + 1 >= MAX_DEPTH) { u_eputs("tree: too deep, stopping here\n"); continue; }

        char child[256];
        if (u_join(child, sizeof(child), dir, d.name) < 0) continue;
        char sub[80];
        char *q = u_append(sub, prefix);
        q = u_append(q, last ? "    " : "|   ");
        *q = 0;
        walk(child, sub, depth + 1);
    }
}

void _start(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : ".";
    struct user_stat st;
    i64 r = sys_stat(root, &st);
    if (r < 0) { u_fail("tree", root, r); sys_exit(1); }
    if (st.type != T_DIR) { u_fail("tree", root, -ENOTDIR); sys_exit(1); }

    u_puts(root);
    u_puts("\n");
    char prefix[80];
    prefix[0] = 0;
    walk(root, prefix, 0);

    char line[80];
    char *p = u_append_num(line, dirs);
    p = u_append(p, dirs == 1 ? " directory, " : " directories, ");
    p = u_append_num(p, files);
    p = u_append(p, files == 1 ? " file\n" : " files\n");
    u_write_all(1, line, (u64)(p - line));
    sys_exit(0);
}
