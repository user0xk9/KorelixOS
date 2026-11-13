#include <ulib.h>

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[256];
    i64 r = sys_getcwd(buf, sizeof(buf));
    if (r < 0) { u_fail("pwd", 0, r); sys_exit(1); }
    buf[r] = '\n';
    u_write_all(1, buf, (u64)r + 1);
    sys_exit(0);
}
