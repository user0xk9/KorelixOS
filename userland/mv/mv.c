#include <ulib.h>

void _start(int argc, char **argv) {
    if (argc != 3) { u_eputs("usage: mv src dst\n"); sys_exit(2); }
    const char *src = argv[1];
    const char *dst = argv[2];
    char target[256];
    struct user_stat ds;

    if (sys_stat(dst, &ds) == 0 && ds.type == T_DIR) {       /* mv x dir -> dir/x */
        i64 r = u_join(target, sizeof(target), dst, u_basename(src));
        if (r < 0) { u_fail("mv", dst, r); sys_exit(1); }
        dst = target;
    }
    i64 r = sys_rename(src, dst);
    if (r < 0) { u_fail("mv", src, r); sys_exit(1); }
    sys_exit(0);
}
