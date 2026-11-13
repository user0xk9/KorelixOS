#include <ulib.h>

/* Копирует fd в stdout. 0 или -errno. */
static i64 copy_out(int fd) {
    char buf[512];
    i64 n;
    while ((n = sys_read(fd, buf, sizeof(buf))) > 0) {
        i64 w = u_write_all(1, buf, (u64)n);
        if (w < 0) return w;
    }
    return n;
}

void _start(int argc, char **argv) {
    int status = 0;

    if (argc < 2) {                             /* без аргументов: stdin -> stdout */
        i64 r = copy_out(0);
        if (r < 0) { u_fail("cat", 0, r); status = 1; }
    } else {
        for (int i = 1; i < argc; i++) {
            int fd = (int)sys_open(argv[i], O_RDONLY);
            if (fd < 0) { u_fail("cat", argv[i], fd); status = 1; continue; }
            i64 r = copy_out(fd);
            if (r < 0) { u_fail("cat", argv[i], r); status = 1; }
            sys_close(fd);
        }
    }
    sys_exit(status);
}
