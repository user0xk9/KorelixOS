#include <ulib.h>

/* Считает строки, слова и байты потока. 0 или -errno. */
static i64 count(int fd, u64 *lines, u64 *words, u64 *bytes) {
    char buf[512];
    i64 n;
    int in_word = 0;
    *lines = *words = *bytes = 0;
    while ((n = sys_read(fd, buf, sizeof(buf))) > 0) {
        *bytes += (u64)n;
        for (i64 i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') (*lines)++;
            if (c == ' ' || c == '\n' || c == '\t' || c == '\r') in_word = 0;
            else if (!in_word) { in_word = 1; (*words)++; }
        }
    }
    return n;
}

static void print_counts(u64 l, u64 w, u64 b, const char *name) {
    char line[300];
    char *p = u_append_num_pad(line, l, 7);
    p = u_append_num_pad(p, w, 7);
    p = u_append_num_pad(p, b, 8);
    if (name) {
        *p++ = ' ';
        u64 n = u_strlen(name);
        if (n > 200) n = 200;
        for (u64 i = 0; i < n; i++) *p++ = name[i];
    }
    *p++ = '\n';
    u_write_all(1, line, (u64)(p - line));
}

void _start(int argc, char **argv) {
    int status = 0;
    u64 l, w, b;

    if (argc < 2) {
        i64 r = count(0, &l, &w, &b);
        if (r < 0) { u_fail("wc", 0, r); sys_exit(1); }
        print_counts(l, w, b, 0);
        sys_exit(0);
    }
    for (int i = 1; i < argc; i++) {
        int fd = (int)sys_open(argv[i], O_RDONLY);
        if (fd < 0) { u_fail("wc", argv[i], fd); status = 1; continue; }
        i64 r = count(fd, &l, &w, &b);
        sys_close(fd);
        if (r < 0) { u_fail("wc", argv[i], r); status = 1; continue; }
        print_counts(l, w, b, argv[i]);
    }
    sys_exit(status);
}
