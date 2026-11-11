#include <stdint.h>
#include <stddef.h>
#include <korelix/vfs.h>
#include <korelix/heap.h>
#include <korelix/string.h>
#include <korelix/log.h>

static struct vnode *root;

static size_t slen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static struct vnode *new_node(const char *name, size_t len, int type) {
    struct vnode *n = kzalloc(sizeof(*n));
    if (!n) return NULL;
    memcpy(n->name, name, len);          /* len < VFS_NAME_MAX проверяет вызывающий */
    n->type = type;
    return n;
}

void vfs_init(void) {
    root = new_node("", 0, VFS_DIR);
    if (!root) panic("vfs: out of memory");
}

static struct vnode *find_child(struct vnode *dir, const char *name, size_t len) {
    for (struct vnode *c = dir->children; c; c = c->next)
        if (slen(c->name) == len && !memcmp(c->name, name, len))
            return c;
    return NULL;
}

/* Абсолютный путь -> узел. "." игнорируется, ".." пока не поддерживается. */
struct vnode *vfs_lookup(const char *path) {
    struct vnode *cur = root;
    const char *p = path;

    for (;;) {
        while (*p == '/') p++;
        if (!*p) return cur;

        const char *start = p;
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - start);

        if (cur->type != VFS_DIR) return NULL;
        if (len == 1 && start[0] == '.') continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') return NULL;

        cur = find_child(cur, start, len);
        if (!cur) return NULL;
    }
}

/* Создаёт узел по пути (недостающие каталоги по дороге создаются как mkdir -p) */
struct vnode *vfs_create(const char *path, int type, const void *data, size_t size) {
    struct vnode *cur = root;
    const char *p = path;

    for (;;) {
        while (*p == '/') p++;
        if (!*p) return cur;

        const char *start = p;
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - start);

        if (len >= VFS_NAME_MAX) return NULL;
        if (len == 1 && start[0] == '.') continue;

        const char *q = p;
        while (*q == '/') q++;
        int last = (*q == 0);

        struct vnode *child = find_child(cur, start, len);
        if (!child) {
            child = new_node(start, len, last ? type : VFS_DIR);
            if (!child) return NULL;
            child->parent = cur;
            struct vnode **link = &cur->children;     /* в конец списка: порядок как в архиве */
            while (*link) link = &(*link)->next;
            *link = child;
        }

        if (last) {
            if (type == VFS_FILE && child->type == VFS_FILE) {
                child->data = data;
                child->size = size;
            }
            return child;
        }
        if (child->type != VFS_DIR) return NULL;
        cur = child;
    }
}

struct vnode *vfs_child(struct vnode *dir, size_t index) {
    if (dir->type != VFS_DIR) return NULL;
    struct vnode *c = dir->children;
    while (c && index--) c = c->next;
    return c;
}