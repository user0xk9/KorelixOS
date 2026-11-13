#include <ulib.h>

void _start(int argc, char **argv) {
    char line[1100];                            /* аргументы <= 1024 байт + пробелы */
    char *p = line;
    int first = 1, newline = 1;

    if (argc > 1 && u_streq(argv[1], "-n")) { newline = 0; first = 2; }
    for (int i = first; i < argc; i++) {
        if (i > first) *p++ = ' ';
        p = u_append(p, argv[i]);
    }
    if (newline) *p++ = '\n';

    i64 r = u_write_all(1, line, (u64)(p - line));
    if (r < 0) u_fail("echo", 0, r);
    sys_exit(r < 0);
}
