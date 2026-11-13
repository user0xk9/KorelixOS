/* Записываемая ФС: пути, чтение/запись, COW, удаление, переименование, ссылки.
 * Под ASan/UBSan; заглушка блокировки ловит повторный захват. */
#include <stdint.h>
#include <string.h>
#include <korelix/vfs.h>
#include <korelix/errno.h>
#include <korelix/heap.h>
#include <korelix/log.h>
#include "kstubs.h"
#include "check.h"

static struct vnode *cwd;

static void cd(const char *path) {
    struct vnode *n;
    CHECK(vfs_chdir_at(cwd, path, &n) == 0);
    if (cwd) vfs_node_put(cwd);
    cwd = n;
}

static struct file *open_ok(const char *path, int flags) {
    struct file *f = NULL;
    int r = vfs_open_at(cwd, path, flags, &f);
    if (r) { fprintf(stderr, "open(%s) = %d\n", path, r); exit(1); }
    return f;
}

static void put_file(const char *path, const char *text) {
    struct file *f = open_ok(path, O_WRONLY | O_CREAT | O_TRUNC);
    CHECK(vfs_write(f, text, strlen(text)) == (long)strlen(text));
    file_put(f);
}

static void expect_text(const char *path, const char *text) {
    struct file *f = open_ok(path, O_RDONLY);
    char buf[256];
    long n = vfs_read(f, buf, sizeof(buf));
    file_put(f);
    CHECK(n == (long)strlen(text));
    CHECK(memcmp(buf, text, (size_t)n) == 0);
}

static int exists(const char *path) {
    struct user_stat st;
    return vfs_stat_at(cwd, path, &st) == 0;
}

static char pathbuf[VFS_PATH_MAX];
static const char *path_of(const char *path) {
    struct vnode *n;
    CHECK(vfs_chdir_at(cwd, path, &n) == 0);
    long r = vfs_path(n, pathbuf, sizeof(pathbuf));
    vfs_node_put(n);
    CHECK(r >= 0);
    return pathbuf;
}

static uint8_t big_static[1000];

int main(void) {
    vfs_init();
    vfs_install("/bin/init", VFS_FILE, "ELF!", 4);
    vfs_install("/etc/motd", VFS_FILE, "hello\n", 6);
    vfs_install("/a/b/deep", VFS_FILE, "x", 1);
    for (int i = 0; i < (int)sizeof(big_static); i++) big_static[i] = (uint8_t)i;
    vfs_install("/big", VFS_FILE, big_static, sizeof(big_static));

    struct user_stat st;

    /* ---------- пути ---------- */
    CHECK(vfs_stat_at(NULL, "/a/b/deep", &st) == 0 && st.type == VFS_FILE && st.size == 1);
    CHECK(vfs_stat_at(NULL, "a/b/deep", &st) == 0);                 /* cwd = NULL: от корня */
    CHECK(vfs_stat_at(NULL, "/../../etc/motd", &st) == 0);          /* .. у корня остаётся корнем */
    CHECK(vfs_stat_at(NULL, "/etc/", &st) == 0 && st.type == VFS_DIR);
    CHECK(vfs_stat_at(NULL, "//etc//motd", &st) == 0);
    CHECK(vfs_stat_at(NULL, "", &st) == -ENOENT);
    CHECK(vfs_stat_at(NULL, "/etc/motd/x", &st) == -ENOTDIR);
    CHECK(vfs_stat_at(NULL, "/nope", &st) == -ENOENT);
    char longname[80];
    memset(longname, 'q', 79); longname[79] = 0;
    CHECK(vfs_stat_at(NULL, longname, &st) == -ENAMETOOLONG);

    cd("/a/b");
    CHECK(exists("deep") && exists("./deep") && exists("../b/deep") && exists("../../etc/motd"));
    CHECK(!exists("../../nothing"));
    struct user_stat s1, s2;
    CHECK(vfs_stat_at(cwd, "deep", &s1) == 0 && vfs_stat_at(cwd, "../b/./deep", &s2) == 0);
    CHECK(s1.id == s2.id);                                          /* один узел = один id */
    CHECK(!strcmp(path_of("."), "/a/b"));
    CHECK(!strcmp(path_of(".."), "/a"));
    CHECK(!strcmp(path_of("/"), "/"));
    cd("/");

    /* ---------- создание, запись, чтение ---------- */
    put_file("/hello.txt", "hello");
    expect_text("/hello.txt", "hello");
    CHECK(vfs_stat_at(cwd, "hello.txt", &st) == 0 && st.size == 5);

    struct file *f;
    CHECK(vfs_open_at(cwd, "/hello.txt", O_WRONLY | O_CREAT | O_EXCL, &f) == -EEXIST);
    CHECK(vfs_open_at(cwd, "/missing", O_RDONLY, &f) == -ENOENT);
    CHECK(vfs_open_at(cwd, "/nodir/x", O_WRONLY | O_CREAT, &f) == -ENOENT);
    CHECK(vfs_open_at(cwd, "/hello.txt/x", O_WRONLY | O_CREAT, &f) == -ENOTDIR);
    CHECK(vfs_open_at(cwd, "/hello.txt", 3, &f) == -EINVAL);        /* недопустимый режим */

    f = open_ok("/hello.txt", O_WRONLY | O_APPEND);                 /* дозапись */
    CHECK(vfs_write(f, " world", 6) == 6);
    char tmp[64];
    CHECK(vfs_read(f, tmp, 4) == -EBADF);                           /* читать из write-only нельзя */
    file_put(f);
    expect_text("/hello.txt", "hello world");

    f = open_ok("/hello.txt", O_RDONLY);
    CHECK(vfs_write(f, "x", 1) == -EBADF);                          /* писать в read-only нельзя */
    CHECK(vfs_lseek(f, 6, VFS_SEEK_SET) == 6);
    CHECK(vfs_read(f, tmp, 100) == 5 && !memcmp(tmp, "world", 5));
    CHECK(vfs_read(f, tmp, 100) == 0);                              /* конец файла */
    CHECK(vfs_lseek(f, -5, VFS_SEEK_END) == 6);
    CHECK(vfs_lseek(f, -2, VFS_SEEK_CUR) == 4);
    CHECK(vfs_lseek(f, -100, VFS_SEEK_CUR) == -EINVAL);
    CHECK(vfs_lseek(f, 0, 77) == -EINVAL);
    CHECK(vfs_lseek(f, (int64_t)VFS_MAX_FILE + 1, VFS_SEEK_SET) == -EINVAL);
    file_put(f);

    f = open_ok("/hello.txt", O_RDWR | O_TRUNC);                    /* усечение */
    CHECK(vfs_stat_at(cwd, "/hello.txt", &st) == 0 && st.size == 0);
    CHECK(vfs_write(f, "abc", 3) == 3);
    CHECK(vfs_lseek(f, 10, VFS_SEEK_SET) == 10);                    /* запись за концом: дыра из нулей */
    CHECK(vfs_write(f, "Z", 1) == 1);
    CHECK(vfs_lseek(f, 0, VFS_SEEK_SET) == 0);
    CHECK(vfs_read(f, tmp, 64) == 11);
    CHECK(!memcmp(tmp, "abc", 3) && tmp[3] == 0 && tmp[9] == 0 && tmp[10] == 'Z');
    file_put(f);

    /* большой файл: рост буфера кусками и проверка содержимого */
    f = open_ok("/grow.bin", O_RDWR | O_CREAT);
    uint8_t chunk[333];
    for (int i = 0; i < 300; i++) {
        for (int j = 0; j < (int)sizeof(chunk); j++) chunk[j] = (uint8_t)(i + j);
        CHECK(vfs_write(f, chunk, sizeof(chunk)) == (long)sizeof(chunk));
    }
    CHECK(vfs_lseek(f, 0, VFS_SEEK_SET) == 0);
    for (int i = 0; i < 300; i++) {
        CHECK(vfs_read(f, chunk, sizeof(chunk)) == (long)sizeof(chunk));
        for (int j = 0; j < (int)sizeof(chunk); j++) CHECK(chunk[j] == (uint8_t)(i + j));
    }
    file_put(f);

    /* предел размера файла */
    f = open_ok("/limit.bin", O_WRONLY | O_CREAT);
    CHECK(vfs_lseek(f, (int64_t)VFS_MAX_FILE - 10, VFS_SEEK_SET) > 0);
    uint8_t hundred[100] = {0};
    CHECK(vfs_write(f, hundred, 100) == 10);                        /* записалось только до предела */
    CHECK(vfs_write(f, hundred, 100) == -ENOSPC);
    file_put(f);

    /* ---------- копирование при записи для файлов из initramfs ---------- */
    f = open_ok("/bin/init", O_RDWR);
    CHECK(vfs_write(f, "XY", 2) == 2);
    CHECK(vfs_lseek(f, 0, VFS_SEEK_SET) == 0);
    CHECK(vfs_read(f, tmp, 16) == 4 && !memcmp(tmp, "XYF!", 4));
    file_put(f);
    expect_text("/bin/init", "XYF!");

    f = open_ok("/big", O_WRONLY);                                  /* малая запись в начало большого */
    CHECK(vfs_write(f, "AB", 2) == 2);
    file_put(f);
    f = open_ok("/big", O_RDONLY);
    uint8_t back[1000];
    CHECK(vfs_read(f, back, sizeof(back)) == 1000);
    CHECK(back[0] == 'A' && back[1] == 'B');
    for (int i = 2; i < 1000; i++) CHECK(back[i] == (uint8_t)i);
    file_put(f);
    CHECK(big_static[0] == 0 && big_static[1] == 1);                /* исходная память не тронута */

    /* ---------- каталоги ---------- */
    CHECK(vfs_mkdir_at(cwd, "/docs") == 0);
    CHECK(vfs_mkdir_at(cwd, "/docs") == -EEXIST);
    CHECK(vfs_mkdir_at(cwd, "/hello.txt") == -EEXIST);
    CHECK(vfs_mkdir_at(cwd, "/no/such/dir") == -ENOENT);
    CHECK(vfs_mkdir_at(cwd, "/hello.txt/sub") == -ENOTDIR);
    CHECK(vfs_mkdir_at(cwd, "/docs/..") == -EINVAL);
    CHECK(vfs_mkdir_at(cwd, "/docs/.") == -EINVAL);
    CHECK(vfs_mkdir_at(cwd, "/") == -EINVAL);
    CHECK(vfs_mkdir_at(cwd, longname) == -ENAMETOOLONG);
    CHECK(vfs_mkdir_at(cwd, "/docs/sub/") == 0);                    /* слэш на конце допустим */
    put_file("/docs/sub/note", "n");

    CHECK(vfs_open_at(cwd, "/docs", O_WRONLY, &f) == -EISDIR);
    f = open_ok("/docs", O_RDONLY);                                 /* каталог читается через readdir */
    CHECK(vfs_read(f, tmp, 4) == -EISDIR);
    CHECK(vfs_lseek(f, 0, VFS_SEEK_SET) == -EISDIR);
    struct user_dirent d;
    CHECK(vfs_readdir(f, 0, &d) == 1 && d.type == VFS_DIR && !strcmp(d.name, "sub"));
    CHECK(vfs_readdir(f, 1, &d) == 0);
    file_put(f);
    f = open_ok("/hello.txt", O_RDONLY);
    CHECK(vfs_readdir(f, 0, &d) == -ENOTDIR);
    file_put(f);

    CHECK(vfs_rmdir_at(cwd, "/docs") == -ENOTEMPTY);
    CHECK(vfs_rmdir_at(cwd, "/hello.txt") == -ENOTDIR);
    CHECK(vfs_rmdir_at(cwd, "/") == -EINVAL);
    CHECK(vfs_rmdir_at(cwd, "/nope") == -ENOENT);

    /* ---------- удаление, в том числе открытого файла ---------- */
    CHECK(vfs_unlink_at(cwd, "/docs") == -EISDIR);
    CHECK(vfs_unlink_at(cwd, "/nope") == -ENOENT);

    f = open_ok("/docs/sub/note", O_RDONLY);
    CHECK(vfs_unlink_at(cwd, "/docs/sub/note") == 0);
    CHECK(!exists("/docs/sub/note"));
    CHECK(vfs_read(f, tmp, 8) == 1 && tmp[0] == 'n');              /* открытый файл ещё жив */
    {
        struct vnode *held = f->node;
        CHECK(vfs_path(held, pathbuf, sizeof(pathbuf)) == -ENOENT); /* но в дереве его уже нет */
    }
    file_put(f);                                                   /* здесь узел освобождается */

    CHECK(vfs_rmdir_at(cwd, "/docs/sub") == 0);
    CHECK(vfs_rmdir_at(cwd, "/docs") == 0);
    CHECK(!exists("/docs"));

    /* рабочий каталог нельзя удалить, пока он занят */
    CHECK(vfs_mkdir_at(cwd, "/work") == 0);
    cd("/work");
    CHECK(vfs_rmdir_at(NULL, "/work") == -EBUSY);
    cd("/");
    CHECK(vfs_rmdir_at(NULL, "/work") == 0);

    /* ---------- переименование ---------- */
    CHECK(vfs_mkdir_at(cwd, "/d1") == 0 && vfs_mkdir_at(cwd, "/d2") == 0);
    put_file("/d1/f", "one");
    put_file("/d2/g", "two");
    CHECK(vfs_rename_at(cwd, "/d1/f", "/d1/f2") == 0);              /* на месте */
    CHECK(!exists("/d1/f") && exists("/d1/f2"));
    CHECK(vfs_rename_at(cwd, "/d1/f2", "/d2/moved") == 0);          /* в другой каталог */
    expect_text("/d2/moved", "one");
    CHECK(vfs_rename_at(cwd, "/d2/moved", "/d2/g") == 0);           /* файл заменяет файл */
    expect_text("/d2/g", "one");
    CHECK(!exists("/d2/moved"));
    CHECK(vfs_rename_at(cwd, "/d2/g", "/d2/g") == 0);               /* на себя: ничего не делает */
    CHECK(vfs_rename_at(cwd, "/d2/g", "/d1") == -EISDIR);           /* файл поверх каталога */
    CHECK(vfs_rename_at(cwd, "/d1", "/d2") == -EEXIST);             /* каталог поверх существующего */
    CHECK(vfs_rename_at(cwd, "/d1", "/d1/inner") == -EINVAL);       /* каталог внутрь себя */
    CHECK(vfs_rename_at(cwd, "/nope", "/x") == -ENOENT);
    CHECK(vfs_rename_at(cwd, "/d2/g", "/nodir/g") == -ENOENT);
    CHECK(vfs_rename_at(cwd, "/d2/g", longname) == -ENAMETOOLONG);
    CHECK(vfs_rename_at(cwd, "/", "/x") == -EINVAL);
    CHECK(vfs_rename_at(cwd, "/d2/..", "/x") == -EINVAL);

    CHECK(vfs_rename_at(cwd, "/d2", "/d1/moved_dir") == 0);         /* каталог целиком */
    expect_text("/d1/moved_dir/g", "one");
    CHECK(!strcmp(path_of("/d1/moved_dir"), "/d1/moved_dir"));
    cd("/d1/moved_dir");                                            /* cwd переживает переименование */
    CHECK(vfs_rename_at(cwd, "/d1/moved_dir", "/d1/renamed") == 0);
    CHECK(!strcmp(path_of("."), "/d1/renamed"));
    expect_text("g", "one");
    cd("/");

    /* ---------- read_whole, статистика, пути ---------- */
    void *copy;
    size_t csize;
    CHECK(vfs_read_whole(cwd, "/etc/motd", &copy, &csize) == 0 && csize == 6 && !memcmp(copy, "hello\n", 6));
    put_file("/etc/motd", "changed");                              /* копия не зависит от файла */
    CHECK(!memcmp(copy, "hello\n", 6));
    kfree(copy);
    CHECK(vfs_read_whole(cwd, "/etc", &copy, &csize) == -EISDIR);
    CHECK(vfs_read_whole(cwd, "/nope", &copy, &csize) == -ENOENT);
    f = open_ok("/empty", O_WRONLY | O_CREAT);
    file_put(f);
    CHECK(vfs_read_whole(cwd, "/empty", &copy, &csize) == 0 && csize == 0);
    kfree(copy);

    struct user_fsinfo info;
    vfs_stats(&info);
    CHECK(info.dirs >= 5 && info.files >= 6 && info.bytes > 0 && info.max_file == VFS_MAX_FILE);

    struct vnode *deep;
    CHECK(vfs_chdir_at(NULL, "/a/b", &deep) == 0);
    CHECK(vfs_path(deep, pathbuf, 4) == -ERANGE);                   /* буфер мал: "/a/b" + NUL = 5 */
    CHECK(vfs_path(deep, pathbuf, 5) == 4);
    vfs_node_put(deep);

    CHECK(vfs_chdir_at(NULL, "/hello.txt", &deep) == -ENOTDIR);
    CHECK(vfs_chdir_at(NULL, "/nope", &deep) == -ENOENT);

    if (cwd) vfs_node_put(cwd);
    puts("test_vfs: OK");
    return 0;
}
