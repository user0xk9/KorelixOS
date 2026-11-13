#include <ulib.h>

static void cat(const char *path) {
    int fd = (int)sys_open(path, O_RDONLY);
    if (fd < 0) return;
    char buf[128];
    i64 n;
    while ((n = sys_read(fd, buf, sizeof(buf))) > 0)
        sys_write(1, buf, (u64)n);
    sys_close(fd);
}

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;

    u_puts("\n");
    cat("/etc/motd");
    u_puts("\n");

    for (;;) {
        i64 pid = sys_spawn("/bin/sh", 0, -1, -1);
        if (pid < 0) {
            u_puts("init: cannot start /bin/sh\n");
            sys_exit(1);
        }
        i64 status = 0;
        sys_wait(pid, &status);
        u_puts("init: shell exited, restarting\n");
    }
}