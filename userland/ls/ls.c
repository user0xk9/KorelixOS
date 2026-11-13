#include <ulib.h>

#define MAX_ENTRIES 128
#define SCREEN_COLS 78

/* Порядок по имени без копирования структур */
static void sort_index(const struct user_dirent *e, unsigned char *idx, int n) {
    for (int i = 0; i < n; i++) idx[i] = (unsigned char)i;
    for (int i = 1; i < n; i++) {
        unsigned char key = idx[i];
        int j = i - 1;
        while (j >= 0) {
            const char *a = e[idx[j]].name, *b = e[key].name;
            while (*a && *a == *b) { a++; b++; }
            if ((unsigned char)*a <= (unsigned char)*b) break;
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = key;
    }
}

static void print_entry(const char *name, u32 type, u64 size, int longfmt, int *col) {
    char line[100];
    char *p = line;
    if (longfmt) {
        *p++ = type == T_DIR ? 'd' : '-';
        *p++ = ' ';
        if (type == T_DIR) p = u_append(p, "       -");
        else p = u_append_num_pad(p, size, 8);
        p = u_append(p, "  ");
        p = u_append(p, name);
        if (type == T_DIR) *p++ = '/';
        *p++ = '\n';
        u_write_all(1, line, (u64)(p - line));
        return;
    }

    p = u_append(p, name);
    if (type == T_DIR) *p++ = '/';
    int len = (int)(p - line);
    if (*col > 0 && *col + len + 2 > SCREEN_COLS) { u_puts("\n"); *col = 0; }
    if (*col > 0) { u_puts("  "); *col += 2; }
    u_write_all(1, line, (u64)len);
    *col += len;
}

static int list_dir(const char *path, int longfmt) {
    int fd = (int)sys_open(path, O_RDONLY);
    if (fd < 0) { u_fail("ls", path, fd); return 1; }

    struct user_dirent ents[MAX_ENTRIES];
    unsigned char idx[MAX_ENTRIES];
    int n = 0;
    i64 r = 0;
    while (n < MAX_ENTRIES && (r = sys_readdir(fd, (u64)n, &ents[n])) > 0) n++;
    if (n == MAX_ENTRIES) {                       /* проверяем, есть ли записи сверх лимита */
        struct user_dirent extra;
        r = sys_readdir(fd, (u64)n, &extra) > 0 ? 2 : 0;
    }
    sys_close(fd);
    if (r < 0) { u_fail("ls", path, r); return 1; }

    sort_index(ents, idx, n);
    int col = 0;
    for (int i = 0; i < n; i++)
        print_entry(ents[idx[i]].name, ents[idx[i]].type, ents[idx[i]].size, longfmt, &col);
    if (!longfmt && col > 0) u_puts("\n");
    if (r == 2) u_puts("ls: only the first 128 entries are shown\n");
    return 0;
}

static int list_path(const char *path, int longfmt, int header) {
    struct user_stat st;
    i64 r = sys_stat(path, &st);
    if (r < 0) { u_fail("ls", path, r); return 1; }

    if (st.type != T_DIR) {                       /* ls файл: показать сам файл */
        int col = 0;
        print_entry(u_basename(path), st.type, st.size, longfmt, &col);
        if (!longfmt) u_puts("\n");
        return 0;
    }
    if (header) { u_puts(path); u_puts(":\n"); }
    return list_dir(path, longfmt);
}

void _start(int argc, char **argv) {
    int longfmt = 0, status = 0, paths = 0;
    char *list[16];

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1]) {
            for (const char *f = argv[i] + 1; *f; f++) {
                if (*f == 'l') longfmt = 1;
                else { u_eputs("ls: unknown option (usage: ls [-l] [path...])\n"); sys_exit(2); }
            }
        } else if (paths < 16) {
            list[paths++] = argv[i];
        }
    }

    if (paths == 0) {
        status = list_path(".", longfmt, 0);
    } else {
        for (int i = 0; i < paths; i++) {
            if (i > 0 && paths > 1) u_puts("\n");
            status |= list_path(list[i], longfmt, paths > 1);
        }
    }
    sys_exit(status);
}
