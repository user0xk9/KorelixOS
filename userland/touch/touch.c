#include <ulib.h>

void _start(int argc, char **argv) {
    int status = 0;
    if (argc < 2) { u_eputs("usage: touch file...\n"); sys_exit(2); }
    for (int i = 1; i < argc; i++) {
        struct user_stat st;
        if (sys_stat(argv[i], &st) == 0) continue;          /* уже есть (времени у файлов пока нет) */
        i64 fd = sys_open(argv[i], O_WRONLY | O_CREAT | O_EXCL);
        if (fd < 0) { u_fail("touch", argv[i], fd); status = 1; continue; }
        sys_close((int)fd);
    }
    sys_exit(status);
}
