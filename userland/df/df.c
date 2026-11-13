#include <ulib.h>

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;
    struct user_fsinfo fi;
    i64 r = sys_fsinfo(&fi);
    if (r < 0) { u_fail("df", 0, r); sys_exit(1); }

    char line[256];
    char *p = u_append(line, "files: ");
    p = u_append_num(p, fi.files);
    p = u_append(p, "\ndirectories: ");
    p = u_append_num(p, fi.dirs);
    p = u_append(p, "\ndata: ");
    p = u_append_num(p, fi.bytes);
    p = u_append(p, " bytes\nmax file size: ");
    p = u_append_num(p, fi.max_file);
    p = u_append(p, " bytes\nstorage: memory only (contents are lost on reboot)\n");
    u_write_all(1, line, (u64)(p - line));
    sys_exit(0);
}
