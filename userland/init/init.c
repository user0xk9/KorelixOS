#include <ulib.h>

static void cat(const char *path) {
    int fd = (int)sys_open(path);
    if (fd < 0) {
        u_puts("cannot open ");
        u_puts(path);
        u_puts("\n");
        return;
    }
    char buf[128];
    i64 n;
    while ((n = sys_read(fd, buf, sizeof(buf))) > 0)
        sys_write(1, buf, (u64)n);
    sys_close(fd);
}

static void ls(const char *path) {
    int fd = (int)sys_open(path);
    if (fd < 0) {
        u_puts("cannot open ");
        u_puts(path);
        u_puts("\n");
        return;
    }
    struct user_dirent d;
    for (u64 i = 0; sys_readdir(fd, i, &d) > 0; i++) {
        char line[96];
        char *p = line;
        p = u_append(p, d.type == 2 ? "  [dir]  " : "  [file] ");
        p = u_append(p, d.name);
        if (d.type != 2) {
            p = u_append(p, "  ");
            p = u_append_num(p, d.size);
            p = u_append(p, " bytes");
        }
        p = u_append(p, "\n");
        sys_write(1, line, (u64)(p - line));
    }
    sys_close(fd);
}

void _start(void) {
    char line[64];
    char *p = line;
    p = u_append(p, "init: started, pid ");
    p = u_append_num(p, (u64)sys_getpid());
    p = u_append(p, "\n");
    sys_write(1, line, (u64)(p - line));

    u_puts("\n== /etc/motd ==\n");
    cat("/etc/motd");

    u_puts("\n== ls / ==\n");
    ls("/");
    u_puts("\n== ls /bin ==\n");
    ls("/bin");

    u_puts("\n== starting two /bin/fpu processes ==\n");
    sys_spawn("/bin/fpu");
    sys_spawn("/bin/fpu");

    sys_sleep_ms(4000);
    u_puts("init: done\n");
    sys_exit(0);
}