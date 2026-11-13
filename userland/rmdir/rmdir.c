#include <ulib.h>

void _start(int argc, char **argv) {
    int status = 0;
    if (argc < 2) { u_eputs("usage: rmdir dir...\n"); sys_exit(2); }
    for (int i = 1; i < argc; i++) {
        i64 r = sys_rmdir(argv[i]);
        if (r < 0) { u_fail("rmdir", argv[i], r); status = 1; }
    }
    sys_exit(status);
}
