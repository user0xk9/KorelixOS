#include <ulib.h>

#define MAX_ARGS 16
#define MAX_TOK  48
#define LINE_MAX 256

/* Разобранная команда */
struct cmd {
    char *argv[MAX_ARGS + 1];
    int argc;
    const char *in;          /* < файл */
    const char *out;         /* > файл или >> файл */
    int append;
};

static void help(void) {
    u_puts("builtins:  cd [dir]   help   exit\n");
    u_puts("files:     ls [-l] [path]   cat [file]   cp src dst   mv src dst   rm [-rf] path\n");
    u_puts("           mkdir [-p] dir   rmdir dir   touch file   stat path   pwd   tree [dir]\n");
    u_puts("           wc [file]   df   echo [-n] text\n");
    u_puts("other:     fpu (FPU test)\n");
    u_puts("redirects: cmd > file   cmd >> file   cmd < file\n");
    u_puts("quotes:    echo \"two  words\"     paths can be relative: cd ..   ls /bin\n");
    u_puts("note:      files live in memory and are lost on reboot\n");
}

/* Режет строку на слова; < > >> отдельные операторы, "..." и '...' защищают пробелы.
 * Слова копируются в store (там же ставятся нули). Возвращает число слов или -1 (сообщение уже выведено). */
static int tokenize(const char *line, char *store, int store_cap, char **tok, char *isop) {
    int n = 0;
    char *w = store;
    char *end = store + store_cap - 2;
    const char *r = line;

    for (;;) {
        while (*r == ' ' || *r == '\t' || *r == '\n') r++;
        if (!*r) break;
        if (n >= MAX_TOK) { u_eputs("sh: too many words\n"); return -1; }

        tok[n] = w;
        isop[n] = 0;
        if (*r == '<' || *r == '>') {
            isop[n] = 1;
            *w++ = *r++;
            if (w[-1] == '>' && *r == '>') *w++ = *r++;
        } else {
            char quote = 0;
            while (*r) {
                if (w >= end) { u_eputs("sh: line too long\n"); return -1; }
                if (quote) {
                    if (*r == quote) { quote = 0; r++; }
                    else *w++ = *r++;
                } else if (*r == '"' || *r == '\'') {
                    quote = *r++;
                } else if (*r == ' ' || *r == '\t' || *r == '\n' || *r == '<' || *r == '>') {
                    break;
                } else {
                    *w++ = *r++;
                }
            }
            if (quote) { u_eputs("sh: unclosed quote\n"); return -1; }
        }
        *w++ = 0;
        n++;
    }
    return n;
}

/* Слова -> команда с перенаправлениями. 0 или -1 (сообщение выведено). */
static int build(char **tok, const char *isop, int n, struct cmd *c) {
    c->argc = 0;
    c->in = c->out = 0;
    c->append = 0;

    for (int i = 0; i < n; i++) {
        if (!isop[i]) {
            if (c->argc >= MAX_ARGS) { u_eputs("sh: too many arguments\n"); return -1; }
            c->argv[c->argc++] = tok[i];
            continue;
        }
        if (i + 1 >= n || isop[i + 1]) {
            u_eputs("sh: syntax error: file name expected after ");
            u_eputs(tok[i]);
            u_eputs("\n");
            return -1;
        }
        if (tok[i][0] == '<') {
            c->in = tok[i + 1];
        } else {
            c->out = tok[i + 1];
            c->append = tok[i][1] == '>';
        }
        i++;
    }
    c->argv[c->argc] = 0;
    return 0;
}

static int has_slash(const char *s) {
    for (; *s; s++)
        if (*s == '/') return 1;
    return 0;
}

static void run(struct cmd *c) {
    char path[128];
    if (has_slash(c->argv[0])) {
        if (u_strlen(c->argv[0]) >= sizeof(path)) { u_eputs("sh: path too long\n"); return; }
        char *p = u_append(path, c->argv[0]);
        *p = 0;
    } else {
        if (u_strlen(c->argv[0]) + 6 > sizeof(path)) { u_eputs("sh: name too long\n"); return; }
        char *p = u_append(path, "/bin/");
        p = u_append(p, c->argv[0]);
        *p = 0;
    }

    int in_fd = -1, out_fd = -1;
    if (c->in) {
        in_fd = (int)sys_open(c->in, O_RDONLY);
        if (in_fd < 0) { u_fail("sh", c->in, in_fd); return; }
    }
    if (c->out) {
        out_fd = (int)sys_open(c->out, O_WRONLY | O_CREAT | (c->append ? O_APPEND : O_TRUNC));
        if (out_fd < 0) {
            u_fail("sh", c->out, out_fd);
            if (in_fd >= 0) sys_close(in_fd);
            return;
        }
    }

    i64 pid = sys_spawn(path, c->argv, in_fd, out_fd);
    if (in_fd >= 0) sys_close(in_fd);               /* у ребёнка свои ссылки на эти файлы */
    if (out_fd >= 0) sys_close(out_fd);

    if (pid < 0) {
        if (pid == -ENOENT) {
            u_eputs("sh: command not found: ");
            u_eputs(c->argv[0]);
            u_eputs("\n");
        } else {
            u_fail("sh", c->argv[0], pid);
        }
        return;
    }

    i64 status = 0;
    sys_wait(pid, &status);
    if (status != 0) {
        char msg[64];
        char *p = u_append(msg, "[exit status ");
        p = u_append_snum(p, status);
        p = u_append(p, "]\n");
        u_write_all(2, msg, (u64)(p - msg));
    }
}

static void prompt(void) {
    char buf[300];
    char cwd[200];
    char *p = u_append(buf, "korelix:");
    if (sys_getcwd(cwd, sizeof(cwd)) < 0) p = u_append(p, "?");
    else p = u_append(p, cwd);
    p = u_append(p, "> ");
    u_write_all(1, buf, (u64)(p - buf));
}

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;

    u_puts("KorelixOS shell. Type 'help'.\n");

    char line[LINE_MAX + 4];
    for (;;) {
        prompt();
        i64 n = sys_read(0, line, LINE_MAX);
        if (n <= 0) {                           /* Ctrl-D */
            u_puts("\n");
            sys_exit(0);
        }
        line[n] = 0;

        char store[2 * LINE_MAX + 8];
        char *tok[MAX_TOK];
        char isop[MAX_TOK];
        int nt = tokenize(line, store, sizeof(store), tok, isop);
        if (nt <= 0) continue;

        struct cmd c;
        if (build(tok, isop, nt, &c) < 0) continue;
        if (c.argc == 0) { u_eputs("sh: missing command\n"); continue; }

        if (u_streq(c.argv[0], "exit")) sys_exit(0);
        if (u_streq(c.argv[0], "help")) { help(); continue; }
        if (u_streq(c.argv[0], "cd")) {
            if (c.argc > 2) { u_eputs("cd: too many arguments\n"); continue; }
            const char *dir = c.argc == 2 ? c.argv[1] : "/";
            i64 r = sys_chdir(dir);
            if (r < 0) u_fail("cd", dir, r);
            continue;
        }

        run(&c);
    }
}
