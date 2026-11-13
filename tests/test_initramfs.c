/* tar-разбор и VFS на хосте под ASan/UBSan, включая битые архивы. */
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <korelix/vfs.h>
#include <korelix/heap.h>
#include <korelix/log.h>
#include "check.h"

#include "kstubs.h"

static size_t put_entry(uint8_t *tar, size_t off, const char *prefix, const char *name,
                        char type, const void *data, size_t n) {
    uint8_t *h = tar + off;
    memset(h, 0, 512);
    strcpy((char *)h, name);
    snprintf((char *)h + 124, 12, "%011zo", n);
    h[156] = (uint8_t)type;
    memcpy(h + 257, "ustar", 5);
    if (prefix) strcpy((char *)h + 345, prefix);
    if (n) memcpy(h + 512, data, n);
    return off + 512 + ((n + 511) / 512) * 512;
}

/* Копия точного размера: любое чтение за границей поймает ASan */
static int mount_exact(const uint8_t *tar, size_t len) {
    uint8_t *copy = malloc(len ? len : 1);
    memcpy(copy, tar, len);
    vfs_init();
    int r = initramfs_mount(copy, len);
    /* copy не освобождаем: узлы VFS ссылаются на его данные */
    return r;
}

int main(void) {
    static uint8_t tar[16384];
    size_t off = 0;
    off = put_entry(tar, off, NULL, "./", '5', NULL, 0);
    off = put_entry(tar, off, NULL, "./bin/", '5', NULL, 0);
    off = put_entry(tar, off, NULL, "./bin/init", '0', "ELF!", 4);
    off = put_entry(tar, off, NULL, "./etc/motd", '0', "hello\n", 6);
    off = put_entry(tar, off, "a/b", "deep", '0', "x", 1);
    size_t good_len = off + 1024;                     /* два нулевых блока в конце */

    /* нормальный архив */
    CHECK(mount_exact(tar, good_len) == 3);
    struct vnode *n = vfs_lookup("/bin/init");
    CHECK(n && n->type == VFS_FILE && n->size == 4 && !memcmp(n->data, "ELF!", 4));
    n = vfs_lookup("//bin//init");
    CHECK(n && n->type == VFS_FILE);
    n = vfs_lookup("/a/b/deep");                      /* поле prefix */
    CHECK(n && n->size == 1);
    CHECK(vfs_lookup("/etc") && vfs_lookup("/etc")->type == VFS_DIR);
    CHECK(vfs_lookup("/nope") == NULL);
    CHECK(vfs_lookup("/bin/../etc") == vfs_lookup("/etc"));   /* ".." работает */
    CHECK(vfs_lookup("/bin/init/x") == NULL);         /* файл не каталог */
    struct vnode *bin = vfs_lookup("/bin");
    CHECK(vfs_child(bin, 0) && !strcmp(vfs_child(bin, 0)->name, "init"));
    CHECK(vfs_child(bin, 1) == NULL);
    CHECK(vfs_child(vfs_lookup("/bin/init"), 0) == NULL);

    /* без завершающих нулевых блоков тоже должен читаться */
    CHECK(mount_exact(tar, off) == 3);

    /* усечённые/битые архивы: не падать и не читать за границей */
    CHECK(mount_exact(tar, 0) == 0);
    CHECK(mount_exact(tar, 100) == 0);                /* меньше одного заголовка */
    for (size_t cut = 1; cut < off; cut += 61)        /* обрезка в произвольном месте */
        (void)mount_exact(tar, cut);

    static uint8_t bad[2048];
    memcpy(bad, tar, 1024);
    memcpy(bad + 257, "XXXXX", 5);                    /* неверная сигнатура ustar */
    CHECK(mount_exact(bad, 1024) == -1);

    memset(bad, 0, sizeof(bad));
    put_entry(bad, 0, NULL, "big", '0', NULL, 0);
    snprintf((char *)bad + 124, 12, "%011o", 0x7FFFFFFF);   /* размер больше архива */
    CHECK(mount_exact(bad, 1024) == -1);

    memset(bad, 0, sizeof(bad));                      /* имя без завершающего нуля */
    memset(bad, 'n', 100);
    memcpy(bad + 257, "ustar", 5);
    bad[156] = '0';
    (void)mount_exact(bad, 1024);

    /* слишком длинный компонент пути: файл просто не создаётся */
    char longname[80];
    memset(longname, 'q', 79);
    longname[79] = 0;
    memset(bad, 0, sizeof(bad));
    put_entry(bad, 0, NULL, longname, '0', "z", 1);
    CHECK(mount_exact(bad, 1024) == 0);

    puts("test_initramfs: OK");
    return 0;
}
