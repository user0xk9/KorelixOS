#include <stdint.h>
#include <stddef.h>
#include <korelix/vfs.h>
#include <korelix/mm.h>
#include <korelix/string.h>

struct tar_hdr {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} __attribute__((packed));            /* ровно 512 байт */

static uint64_t parse_octal(const char *s, size_t n) {
    uint64_t v = 0;
    size_t i = 0;
    while (i < n && s[i] == ' ') i++;
    for (; i < n && s[i] >= '0' && s[i] <= '7'; i++)
        v = v * 8 + (uint64_t)(s[i] - '0');
    return v;
}

static size_t put(char *dst, size_t pos, const char *src, size_t max) {
    for (size_t i = 0; i < max && src[i] && pos < 258; i++)
        dst[pos++] = src[i];
    return pos;
}

/* Возвращает число файлов или -1 при ошибке формата */
int initramfs_mount(const void *tar, size_t total) {
    const uint8_t *base = tar;
    size_t off = 0;
    int files = 0;

    while (off + 512 <= total) {
        const struct tar_hdr *h = (const struct tar_hdr *)(base + off);
        if (h->name[0] == 0) break;                       /* конец архива */
        if (memcmp(h->magic, "ustar", 5)) return -1;

        uint64_t size = parse_octal(h->size, sizeof(h->size));
        if (size > total - off - 512) return -1;          /* данные выходят за архив */
        const uint8_t *data = base + off + 512;

        char path[260];
        size_t pos = 0;
        if (h->prefix[0]) {
            pos = put(path, pos, h->prefix, sizeof(h->prefix));
            path[pos++] = '/';
        }
        pos = put(path, pos, h->name, sizeof(h->name));
        path[pos] = 0;

        if (h->typeflag == '0' || h->typeflag == 0) {
            if (vfs_create(path, VFS_FILE, data, (size_t)size)) files++;
        } else if (h->typeflag == '5') {
            vfs_create(path, VFS_DIR, NULL, 0);
        }                                                 /* остальные типы пропускаем */

        off += 512 + ALIGN_UP(size, 512);
    }
    return files;
}