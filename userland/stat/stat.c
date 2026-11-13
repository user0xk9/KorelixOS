#include <ulib.h>

void _start(int argc, char **argv) {
    int status = 0;
    if (argc < 2) { u_eputs("usage: stat path...\n"); sys_exit(2); }
    for (int i = 1; i < argc; i++) {
        struct user_stat st;
        i64 r = sys_stat(argv[i], &st);
        if (r < 0) { u_fail("stat", argv[i], r); status = 1; continue; }

        char line[400];
        char *p = u_append(line, "  Name: ");
        u64 n = u_strlen(argv[i]);
        if (n > 250) n = 250;
        for (u64 k = 0; k < n; k++) *p++ = argv[i][k];
        p = u_append(p, "\n  Type: ");
        p = u_append(p, st.type == T_DIR ? "directory" : "file");
        p = u_append(p, "\n  Size: ");
        p = u_append_num(p, st.size);
        p = u_append(p, " bytes\n    Id: ");
        p = u_append_num(p, st.id);
        *p++ = '\n';
        u_write_all(1, line, (u64)(p - line));
    }
    sys_exit(status);
}
