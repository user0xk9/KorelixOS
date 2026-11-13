#include <ulib.h>

void _start(int argc, char **argv) {
    if (argc != 3) { u_eputs("usage: cp src dst\n"); sys_exit(2); }
    const char *src = argv[1];
    char target[256];
    const char *dst = argv[2];
    struct user_stat ss, ds;

    i64 r = sys_stat(src, &ss);
    if (r < 0) { u_fail("cp", src, r); sys_exit(1); }
    if (ss.type == T_DIR) { u_fail("cp", src, -EISDIR); sys_exit(1); }   /* каталоги не копируем */

    if (sys_stat(dst, &ds) == 0 && ds.type == T_DIR) {       /* cp file dir -> dir/file */
        r = u_join(target, sizeof(target), dst, u_basename(src));
        if (r < 0) { u_fail("cp", dst, r); sys_exit(1); }
        dst = target;
    }
    if (sys_stat(dst, &ds) == 0 && ds.id == ss.id) {
        u_eputs("cp: source and destination are the same file\n");
        sys_exit(1);
    }

    int in = (int)sys_open(src, O_RDONLY);
    if (in < 0) { u_fail("cp", src, in); sys_exit(1); }
    int out = (int)sys_open(dst, O_WRONLY | O_CREAT | O_TRUNC);
    if (out < 0) { u_fail("cp", dst, out); sys_close(in); sys_exit(1); }

    char buf[1024];
    i64 n;
    int status = 0;
    while ((n = sys_read(in, buf, sizeof(buf))) > 0) {
        r = u_write_all(out, buf, (u64)n);
        if (r < 0) { u_fail("cp", dst, r); status = 1; break; }
    }
    if (n < 0) { u_fail("cp", src, n); status = 1; }
    sys_close(in);
    sys_close(out);
    sys_exit(status);
}
