/* Сквозные сценарии: настоящие userland-программы (sh, ls, cp, rm, ...) на хосте.
 * Системные вызовы эмулирует host_syscall поверх настоящего kernel/fs/vfs.c, spawn выполняет
 * программу сразу. Проверяется логика оболочки и команд; сам слой syscall.c здесь не участвует. */
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <korelix/vfs.h>
#include <korelix/errno.h>
#include <korelix/heap.h>
#include <korelix/log.h>
#include "kstubs.h"
#include "check.h"

_Static_assert(sizeof(struct user_dirent) == 64, "user_dirent must be 64 bytes");
_Static_assert(sizeof(struct user_stat) == 24, "user_stat must be 24 bytes");
_Static_assert(sizeof(struct user_fsinfo) == 32, "user_fsinfo must be 32 bytes");

typedef void (*prog_fn)(int, char **);
void prog_sh(int, char **);    void prog_ls(int, char **);    void prog_cat(int, char **);
void prog_echo(int, char **);  void prog_mkdir(int, char **); void prog_rmdir(int, char **);
void prog_rm(int, char **);    void prog_touch(int, char **); void prog_cp(int, char **);
void prog_mv(int, char **);    void prog_stat(int, char **);  void prog_pwd(int, char **);
void prog_tree(int, char **);  void prog_wc(int, char **);    void prog_df(int, char **);

static const struct { const char *name; prog_fn fn; } progs[] = {
    {"ls", prog_ls}, {"cat", prog_cat}, {"echo", prog_echo}, {"mkdir", prog_mkdir},
    {"rmdir", prog_rmdir}, {"rm", prog_rm}, {"touch", prog_touch}, {"cp", prog_cp},
    {"mv", prog_mv}, {"stat", prog_stat}, {"pwd", prog_pwd}, {"tree", prog_tree},
    {"wc", prog_wc}, {"df", prog_df},
};

#define NFD 16
struct proc {
    struct vnode *cwd;
    struct file *fds[NFD];
    jmp_buf jb;
    long status;
};
static struct proc *cur;

static char con[1 << 18];
static size_t con_len;
static const char *con_in;
static size_t con_in_pos;
static struct { long pid, status; } finished[4096];
static int nfinished;
static long next_pid = 2;
static int max_open_fds;

static void con_put(const char *s, size_t n) {
    CHECK(con_len + n < sizeof(con));
    memcpy(con + con_len, s, n);
    con_len += n;
    con[con_len] = 0;
}

static struct file *fd_of(struct proc *p, long fd) {
    return (fd >= 0 && fd < NFD) ? p->fds[fd] : NULL;
}

static int run_child(const char *path, char **argv, long in_fd, long out_fd, long *pid_out) {
    struct proc *p = cur;
    void *img;
    size_t sz;
    int r = vfs_read_whole(p->cwd, path, &img, &sz);
    if (r) return r;

    prog_fn fn = NULL;
    if (sz > 5 && !memcmp(img, "PROG:", 5)) {
        char name[32] = {0};
        for (size_t i = 5; i < sz && i - 5 < sizeof(name) - 1 && ((char *)img)[i] != '\n'; i++)
            name[i - 5] = ((char *)img)[i];
        for (size_t i = 0; i < sizeof(progs) / sizeof(progs[0]); i++)
            if (!strcmp(progs[i].name, name)) fn = progs[i].fn;
    }
    kfree(img);
    if (!fn) return -ENOEXEC;

    struct proc child;
    memset(&child, 0, sizeof(child));
    child.cwd = p->cwd;
    for (int i = 0; i < 3; i++) child.fds[i] = p->fds[i];
    if (in_fd != -1 && !(child.fds[0] = fd_of(p, in_fd))) return -EBADF;
    if (out_fd != -1 && !(child.fds[1] = fd_of(p, out_fd))) return -EBADF;
    if (child.cwd) vfs_node_get(child.cwd);
    for (int i = 0; i < 3; i++) if (child.fds[i]) file_get(child.fds[i]);

    int ac = 0;
    if (argv) while (argv[ac]) ac++;
    char *fallback[2] = { (char *)path, NULL };
    cur = &child;
    if (setjmp(child.jb) == 0) {
        fn(ac ? ac : 1, ac ? argv : fallback);
        child.status = 0;
    }
    cur = p;

    for (int i = 0; i < NFD; i++) if (child.fds[i]) file_put(child.fds[i]);
    if (child.cwd) vfs_node_put(child.cwd);

    CHECK(nfinished < 4096);
    finished[nfinished].pid = next_pid;
    finished[nfinished++].status = child.status;
    *pid_out = next_pid++;
    return 0;
}

long host_syscall(long n, long a, long b, long c, long d) {
    struct proc *p = cur;
    switch (n) {
    case 0:                                                  /* exit */
        p->status = a;
        longjmp(p->jb, 1);
    case 1: {                                                /* write */
        struct file *f = fd_of(p, a);
        if (!f) {
            if (a != 1 && a != 2) return -EBADF;
            size_t len = (size_t)c > 4096 ? 4096 : (size_t)c;
            con_put((const char *)b, len);
            return (long)len;
        }
        return vfs_write(f, (const void *)b, (size_t)c);
    }
    case 4: {                                                /* open */
        int fd = 3;
        while (fd < NFD && p->fds[fd]) fd++;
        if (fd == NFD) return -EMFILE;
        struct file *f;
        int r = vfs_open_at(p->cwd, (const char *)a, (int)b, &f);
        if (r) return r;
        p->fds[fd] = f;
        int used = 0;
        for (int i = 3; i < NFD; i++) used += p->fds[i] != NULL;
        if (used > max_open_fds) max_open_fds = used;
        return fd;
    }
    case 5: {                                                /* read */
        struct file *f = fd_of(p, a);
        if (!f) {
            if (a != 0) return -EBADF;
            size_t len = strlen(con_in);
            if (con_in_pos >= len) return 0;                 /* конец ввода (Ctrl-D) */
            size_t k = 0;
            while (con_in_pos + k < len && k < (size_t)c) {
                ((char *)b)[k] = con_in[con_in_pos + k];
                k++;
                if (con_in[con_in_pos + k - 1] == '\n') break;
            }
            con_in_pos += k;
            return (long)k;
        }
        return vfs_read(f, (void *)b, (size_t)c);
    }
    case 6: {                                                /* close */
        struct file *f = fd_of(p, a);
        if (!f) return -EBADF;
        p->fds[a] = NULL;
        file_put(f);
        return 0;
    }
    case 7: {                                                /* readdir */
        struct file *f = fd_of(p, a);
        if (!f) return -EBADF;
        return vfs_readdir(f, (size_t)b, (struct user_dirent *)c);
    }
    case 8: {                                                /* spawn */
        long pid;
        int r = run_child((const char *)a, (char **)b, c, d, &pid);
        return r ? r : pid;
    }
    case 9:                                                  /* wait */
        for (int i = 0; i < nfinished; i++)
            if (finished[i].pid == a) { *(long *)b = finished[i].status; return 0; }
        return -1;
    case 10: return vfs_mkdir_at(p->cwd, (const char *)a);
    case 11: return vfs_unlink_at(p->cwd, (const char *)a);
    case 12: return vfs_rmdir_at(p->cwd, (const char *)a);
    case 13: return vfs_rename_at(p->cwd, (const char *)a, (const char *)b);
    case 14: {                                               /* chdir */
        struct vnode *nn;
        int r = vfs_chdir_at(p->cwd, (const char *)a, &nn);
        if (r) return r;
        if (p->cwd) vfs_node_put(p->cwd);
        p->cwd = nn;
        return 0;
    }
    case 15: {                                               /* getcwd */
        struct vnode *cw = p->cwd ? p->cwd : vfs_lookup("/");
        char tmp[VFS_PATH_MAX];
        long r = vfs_path(cw, tmp, sizeof(tmp));
        if (r < 0) return r;
        if ((size_t)r + 1 > (size_t)b) return -ERANGE;
        memcpy((char *)a, tmp, (size_t)r + 1);
        return r;
    }
    case 16: {
        struct file *f = fd_of(p, a);
        return f ? vfs_lseek(f, b, (int)c) : -EBADF;
    }
    case 17: return vfs_stat_at(p->cwd, (const char *)a, (struct user_stat *)b);
    case 18: vfs_stats((struct user_fsinfo *)a); return 0;
    default: return -1;
    }
}

/* ---------- стенд ---------- */

static void fresh_fs(void) {
    vfs_init();
    for (size_t i = 0; i < sizeof(progs) / sizeof(progs[0]); i++) {
        char path[64], body[64];
        snprintf(path, sizeof(path), "/bin/%s", progs[i].name);
        snprintf(body, sizeof(body), "PROG:%s\n", progs[i].name);
        vfs_install(path, VFS_FILE, strdup(body), strlen(body));
    }
    vfs_install("/etc/motd", VFS_FILE, "Welcome\n", 8);
}

/* Выполняет сценарий в оболочке, возвращает весь вывод консоли */
static const char *session(const char *script) {
    con_len = 0;
    con[0] = 0;
    con_in = script;
    con_in_pos = 0;
    struct proc shell;
    memset(&shell, 0, sizeof(shell));
    cur = &shell;
    if (setjmp(shell.jb) == 0) prog_sh(0, NULL);
    for (int i = 0; i < NFD; i++) if (shell.fds[i]) file_put(shell.fds[i]);
    if (shell.cwd) vfs_node_put(shell.cwd);
    cur = NULL;
    return con;
}

#define HAS(out, text) do {                                                                   \
    if (!strstr((out), (text))) {                                                             \
        fprintf(stderr, "%s:%d: expected in output: [%s]\n---- output ----\n%s\n----\n",     \
                __FILE__, __LINE__, (text), (out));                                           \
        exit(1);                                                                              \
    }                                                                                         \
} while (0)
#define HAS_NOT(out, text) do {                                                               \
    if (strstr((out), (text))) {                                                              \
        fprintf(stderr, "%s:%d: unexpected in output: [%s]\n---- output ----\n%s\n----\n",   \
                __FILE__, __LINE__, (text), (out));                                           \
        exit(1);                                                                              \
    }                                                                                         \
} while (0)

int main(void) {
    const char *out;

    /* --- полный протокол короткой сессии: формат приглашения, cd, pwd, ls, ls -l --- */
    fresh_fs();
    out = session("echo hi\npwd\nmkdir d\ncd d\npwd\necho x > f\ncat f\nls\nls -l\ncd ..\nls\n");
    const char *want =
        "KorelixOS shell. Type 'help'.\n"
        "korelix:/> hi\n"
        "korelix:/> /\n"
        "korelix:/> korelix:/> korelix:/d> /d\n"
        "korelix:/d> korelix:/d> x\n"
        "korelix:/d> f\n"
        "korelix:/d> -        2  f\n"
        "korelix:/d> korelix:/> bin/  d/  etc/\n"
        "korelix:/> \n";
    if (strcmp(out, want)) {
        fprintf(stderr, "session 1 transcript differs\n---- got ----\n%s\n---- want ----\n%s\n", out, want);
        return 1;
    }

    /* --- файлы: перенаправления, cp, mv, rm, wc, stat, относительные пути --- */
    fresh_fs();
    out = session(
        "mkdir -p a/b/c\n"
        "echo one > a/f1\n"
        "echo \"two  words\" >> a/f1\n"
        "cat a/f1\n"
        "cp a/f1 a/b/copy\n"
        "cat < a/b/copy > a/b/c/third\n"
        "cat a/b/c/third\n"
        "wc a/f1\n"
        "mv a/b/copy a/moved\n"
        "ls a\n"
        "cd a/b/c\n"
        "pwd\n"
        "cat ../../moved\n"
        "cd /\n"
        "stat a/f1\n"
        "tree a\n"
        "touch a/new\n"
        "ls -l a\n"
        "mv a/moved a/b\n"
        "ls a/b\n"
        "cp a/f1 a/b\n"
        "ls a/b\n"
        "rm a/new a/f1\n"
        "ls a\n"
        "df\n");
    HAS(out, "one\ntwo  words\n");                           /* кавычки сохранили два пробела */
    HAS(out, "two  words\nkorelix:/> korelix:/> korelix:/> one\ntwo  words\n");   /* cp, cat < > и cat результата */
    HAS(out, "      2      3      15 a/f1\n");              /* wc: строки слова байты */
    HAS(out, "b/  f1  moved\n");                             /* mv внутри каталога, сортировка */
    HAS(out, "/a/b/c\n");                                    /* pwd после cd с вложенными путями */
    HAS(out, "  Name: a/f1\n  Type: file\n  Size: 15 bytes\n");
    HAS(out, "a\n|-- b/\n|   `-- c/\n|       `-- third\n|-- f1\n`-- moved\n2 directories, 3 files\n");
    HAS(out, "d        -  b/\n-       15  f1\n-       15  moved\n-        0  new\n");   /* ls -l */
    HAS(out, "korelix:/> c/  moved\n");                      /* mv moved в каталог b */
    HAS(out, "c/  f1  moved\n");                             /* cp в каталог */
    HAS(out, "directories: ");
    HAS_NOT(out, "No such");                                 /* ошибок не было */

    /* --- ошибки и сообщения --- */
    fresh_fs();
    out = session(
        "cat nothere\n"
        "rm nothere\n"
        "rm -f nothere\n"
        "mkdir /x/y/z\n"
        "cd /nope\n"
        "cd /etc/motd\n"
        "mkdir d\n"
        "mkdir d\n"
        "touch d/f\n"
        "rmdir d\n"
        "rm d\n"
        "cp d /x\n"
        "cp /etc/motd /etc/motd\n"
        "cp /etc/motd /etc/../etc/motd\n"
        "ls nodir\n"
        "frobnicate\n"
        "echo >\n"
        "echo > > f\n"
        "echo \"open\n"
        "cd a b\n"
        "ls -z\n"
        "mv /nope /x\n"
        "mkdir e\n"
        "mkdir e/d\n"
        "mv d e\n"
        "rm /\n"
        "cat d\n"
        "mkdir -p /etc/motd/sub\n"
        "/etc/motd\n"
        "< f\n"
        "cat < /nope\n");
    HAS(out, "cat: nothere: no such file or directory\n");
    HAS(out, "rm: nothere: no such file or directory\n");
    HAS(out, "mkdir: /x/y/z: no such file or directory\n");
    HAS(out, "cd: /nope: no such file or directory\n");
    HAS(out, "cd: /etc/motd: not a directory\n");
    HAS(out, "mkdir: d: already exists\n");
    HAS(out, "rmdir: d: directory not empty\n");
    HAS(out, "rm: d: is a directory\n");
    HAS(out, "cp: d: is a directory\n");
    HAS(out, "cp: source and destination are the same file\n");
    HAS(out, "ls: nodir: no such file or directory\n");
    HAS(out, "sh: command not found: frobnicate\n");
    HAS(out, "sh: syntax error: file name expected after >\n");
    HAS(out, "sh: unclosed quote\n");
    HAS(out, "cd: too many arguments\n");
    HAS(out, "ls: unknown option");
    HAS(out, "mv: /nope: no such file or directory\n");
    HAS(out, "mv: d: already exists\n");                     /* e/d уже есть */
    HAS(out, "rm: /: invalid argument\n");
    HAS(out, "cat: d: is a directory\n");
    HAS(out, "mkdir: /etc/motd/sub: not a directory\n");
    HAS(out, "sh: /etc/motd: cannot execute (not a valid program)\n");
    HAS(out, "sh: missing command\n");
    HAS(out, "sh: /nope: no such file or directory\n");

    /* --- stderr не попадает в файл, а файл создаётся --- */
    fresh_fs();
    out = session("cat nope > out.txt\nls -l\ncat out.txt\n");
    HAS(out, "cat: nope: no such file or directory\n");
    HAS(out, "-        0  out.txt\n");
    HAS(out, "[exit status 1]\n");

    /* --- дозапись и замена --- */
    fresh_fs();
    out = session("echo a > f\necho b >> f\necho c >> f\ncat f\necho z > f\ncat f\nwc f\n");
    HAS(out, "a\nb\nc\n");
    HAS(out, "korelix:/> z\n");
    HAS(out, "      1      1       2 f\n");

    /* --- rm -r, mkdir -p, глубокие деревья, лимит ls --- */
    fresh_fs();
    out = session(
        "mkdir -p t/a/b t/c\n"
        "touch t/a/b/f t/c/g t/h\n"
        "rm t\n"
        "rm -r t\n"
        "ls\n"
        "mkdir -p /p/q\n"
        "mkdir -p /p/q\n"
        "mkdir -p /etc/motd\n");
    HAS(out, "rm: t: is a directory\n");
    HAS(out, "korelix:/> bin/  etc/\n");                      /* t исчез целиком */
    HAS(out, "mkdir: /etc/motd: already exists\n");          /* -p на существующий файл */

    fresh_fs();
    char big[8192] = "mkdir many\ncd many\n";
    for (int i = 0; i < 130; i++) {
        char l[32];
        snprintf(l, sizeof(l), "touch f%03d\n", i);
        strcat(big, l);
    }
    strcat(big, "ls\ncd /\nrm -r many\nls\n");
    out = session(big);
    HAS(out, "ls: only the first 128 entries are shown\n");
    HAS(out, "f127");
    HAS_NOT(out, "f129");
    HAS(out, "korelix:/> bin/  etc/\n");                     /* many удалён целиком */

    fresh_fs();
    {
        char deep[4096] = "mkdir -p ";
        for (int i = 0; i < 12; i++) strcat(deep, "d/");
        strcat(deep, "\nrm -r d\nls\n");
        out = session(deep);
        HAS(out, "korelix:/> bin/  etc/\n");                 /* вложенность 12 удаляется */

        char toodeep[4096] = "mkdir -p ";
        for (int i = 0; i < 30; i++) strcat(toodeep, "d/");
        strcat(toodeep, "\nrm -r d\ntree d\nls\n");
        out = session(toodeep);
        HAS(out, "rm: d: name too long\n");                  /* вместо переполнения стека */
        HAS(out, "tree: too deep");
    }

    /* --- каталог занят чужим cwd: rmdir даёт EBUSY, потом освобождается --- */
    fresh_fs();
    out = session("mkdir w\ncd w\ncd /\nrmdir w\nls\n");
    HAS(out, "korelix:/> bin/  etc/\n");                     /* cd / отпустил w */

    /* --- ресурсы: много команд с перенаправлением не оставляют открытых fd --- */
    fresh_fs();
    max_open_fds = 0;
    {
        char many[16384] = "";
        for (int i = 0; i < 200; i++) strcat(many, "echo line > f\ncat < f > g\n");
        strcat(many, "cat g\n");
        out = session(many);
        HAS(out, "line\n");
        HAS_NOT(out, "too many open files");
        CHECK(max_open_fds <= 3);                            /* оболочка держит максимум in+out */
    }

    /* --- help и exit --- */
    fresh_fs();
    out = session("help\nexit\necho not reached\n");
    HAS(out, "redirects:");
    HAS_NOT(out, "not reached");

    /* после всех сессий деревья не держат ссылок: каталоги удаляются */
    fresh_fs();
    session("mkdir z\ncd z\ntouch f\ncd /\n");
    CHECK(vfs_rmdir_at(NULL, "/z") == -ENOTEMPTY);
    CHECK(vfs_unlink_at(NULL, "/z/f") == 0);
    CHECK(vfs_rmdir_at(NULL, "/z") == 0);                    /* не EBUSY: cwd оболочки отпущен */

    puts("test_userland: OK");
    return 0;
}
