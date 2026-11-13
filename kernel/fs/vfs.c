#include <stdint.h>
#include <stddef.h>
#include <korelix/vfs.h>
#include <korelix/errno.h>
#include <korelix/heap.h>
#include <korelix/string.h>
#include <korelix/spinlock.h>
#include <korelix/log.h>

/* Дерево в памяти. Ядро вытесняемое, поэтому все операции идут под одной
 * блокировкой; публичные функции берут её сами, внутренние (*_locked) нет. */

static struct vnode *root;
static uint64_t next_id = 1;
static spinlock_t vfs_lock = SPINLOCK_INIT;

static size_t slen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* ---------- узлы ---------- */

static struct vnode *new_node(const char *name, size_t len, int type) {
    struct vnode *n = kzalloc(sizeof(*n));
    if (!n) return NULL;
    memcpy(n->name, name, len);          /* len < VFS_NAME_MAX проверяет вызывающий */
    n->type = type;
    n->id = next_id++;
    return n;
}

static void free_node(struct vnode *n) {
    if (n->owned) kfree(n->data);
    kfree(n);
}

static struct vnode *find_child(struct vnode *dir, const char *name, size_t len) {
    for (struct vnode *c = dir->children; c; c = c->next)
        if (slen(c->name) == len && !memcmp(c->name, name, len))
            return c;
    return NULL;
}

static void link_child(struct vnode *dir, struct vnode *n) {
    struct vnode **link = &dir->children;      /* в конец списка: порядок создания */
    while (*link) link = &(*link)->next;
    *link = n;
    n->next = NULL;
    n->parent = dir;
}

static void unlink_child(struct vnode *n) {
    struct vnode **link = &n->parent->children;
    while (*link != n) link = &(*link)->next;
    *link = n->next;
    n->next = NULL;
    n->parent = NULL;
}

/* Узел убран из дерева: освобождается сразу или после последнего close */
static void kill_node(struct vnode *n) {
    n->unlinked = 1;
    if (n->refs == 0) free_node(n);
}

static void node_put_locked(struct vnode *n) {
    if (n->refs) n->refs--;
    if (n->refs == 0 && n->unlinked) free_node(n);
}

void vfs_init(void) {
    root = new_node("", 0, VFS_DIR);
    if (!root) panic("vfs: out of memory");
}

/* ---------- разбор путей ---------- */

/* Следующий компонент: *pp сдвигается за него. 0 = компонентов больше нет. */
static int next_comp(const char **pp, const char **start, size_t *len) {
    const char *p = *pp;
    while (*p == '/') p++;
    if (!*p) { *pp = p; return 0; }
    *start = p;
    while (*p && *p != '/') p++;
    *len = (size_t)(p - *start);
    *pp = p;
    return 1;
}

static struct vnode *start_dir(struct vnode *cwd, const char *path) {
    return (path[0] == '/' || !cwd) ? root : cwd;
}

static int is_dot(const char *s, size_t len)    { return len == 1 && s[0] == '.'; }
static int is_dotdot(const char *s, size_t len) { return len == 2 && s[0] == '.' && s[1] == '.'; }

static int step(struct vnode **cur, const char *s, size_t len) {
    struct vnode *c = *cur;
    if (c->type != VFS_DIR) return -ENOTDIR;
    if (is_dot(s, len)) return 0;
    if (is_dotdot(s, len)) { if (c->parent) *cur = c->parent; return 0; }
    if (len >= VFS_NAME_MAX) return -ENAMETOOLONG;
    struct vnode *n = find_child(c, s, len);
    if (!n) return -ENOENT;
    *cur = n;
    return 0;
}

static int lookup_locked(struct vnode *cwd, const char *path, struct vnode **out) {
    if (!path[0]) return -ENOENT;
    struct vnode *cur = start_dir(cwd, path);
    const char *p = path, *s;
    size_t len;
    while (next_comp(&p, &s, &len)) {
        int r = step(&cur, s, len);
        if (r) return r;
    }
    *out = cur;
    return 0;
}

/* Каталог, в котором лежит последний компонент пути, и его имя.
 * "/", "." и ".." в конце пути для создания/удаления не годятся. */
static int split_parent(struct vnode *cwd, const char *path, struct vnode **parent, char *name) {
    if (!path[0]) return -ENOENT;
    struct vnode *cur = start_dir(cwd, path);
    const char *p = path, *s;
    size_t len;
    if (!next_comp(&p, &s, &len)) return -EINVAL;

    for (;;) {
        const char *q = p, *s2;
        size_t len2;
        if (!next_comp(&q, &s2, &len2)) {            /* s — последний компонент */
            if (is_dot(s, len) || is_dotdot(s, len)) return -EINVAL;
            if (len >= VFS_NAME_MAX) return -ENAMETOOLONG;
            if (cur->type != VFS_DIR) return -ENOTDIR;
            memcpy(name, s, len);
            name[len] = 0;
            *parent = cur;
            return 0;
        }
        int r = step(&cur, s, len);
        if (r) return r;
        s = s2;
        len = len2;
        p = q;
    }
}

/* ---------- содержимое файлов ---------- */

static void clear_data(struct vnode *n) {
    if (n->owned) kfree(n->data);
    n->data = NULL;
    n->size = n->cap = 0;
    n->owned = 0;
}

/* Гарантирует собственный буфер на need байт; файл из initramfs при этом копируется (COW) */
static int ensure_cap(struct vnode *n, size_t need) {
    if (need > VFS_MAX_FILE || n->size > VFS_MAX_FILE) return -ENOSPC;
    if (n->owned && n->cap >= need) return 0;

    size_t want = need > n->size ? need : n->size;      /* копируем весь файл, даже если пишем в начало */
    size_t cap = n->owned ? n->cap : 0;
    if (cap < 64) cap = 64;
    while (cap < want) cap *= 2;
    if (cap > VFS_MAX_FILE) cap = VFS_MAX_FILE;

    uint8_t *buf = kmalloc(cap);
    if (!buf) return -ENOSPC;
    if (n->size) memcpy(buf, n->data, n->size);
    if (n->owned) kfree(n->data);
    n->data = buf;
    n->cap = cap;
    n->owned = 1;
    return 0;
}

static long read_locked(struct file *f, void *buf, size_t len) {
    struct vnode *n = f->node;
    if (n->type != VFS_FILE) return -EISDIR;
    if ((f->flags & O_ACCMODE) == O_WRONLY) return -EBADF;
    if (f->offset >= n->size) return 0;

    size_t avail = n->size - (size_t)f->offset;
    if (len > avail) len = avail;
    memcpy(buf, n->data + f->offset, len);
    f->offset += len;
    return (long)len;
}

static long write_locked(struct file *f, const void *buf, size_t len) {
    struct vnode *n = f->node;
    if (n->type != VFS_FILE) return -EISDIR;
    if ((f->flags & O_ACCMODE) == O_RDONLY) return -EBADF;
    if (len == 0) return 0;

    if (f->flags & O_APPEND) f->offset = n->size;
    uint64_t off = f->offset;
    if (off >= VFS_MAX_FILE) return -ENOSPC;
    if (len > VFS_MAX_FILE - off) len = (size_t)(VFS_MAX_FILE - off);

    size_t end = (size_t)off + len;
    int r = ensure_cap(n, end);
    if (r) return r;
    if (off > n->size) memset(n->data + n->size, 0, (size_t)off - n->size);   /* дыра = нули */
    memcpy(n->data + off, buf, len);
    if (end > n->size) n->size = end;
    f->offset = end;
    return (long)len;
}

/* ---------- публичные операции ---------- */

struct vnode *vfs_install(const char *path, int type, const void *data, size_t size) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    struct vnode *cur = root, *result = NULL;
    const char *p = path, *s;
    size_t len;

    while (next_comp(&p, &s, &len)) {
        if (len >= VFS_NAME_MAX) goto out;
        if (is_dot(s, len)) continue;

        const char *q = p, *s2;
        size_t len2;
        int last = !next_comp(&q, &s2, &len2);

        struct vnode *child = find_child(cur, s, len);
        if (!child) {
            child = new_node(s, len, last ? type : VFS_DIR);
            if (!child) goto out;
            link_child(cur, child);
        }
        if (last) {
            if (type == VFS_FILE && child->type == VFS_FILE) {
                child->data = (uint8_t *)data;          /* чужая память: owned = 0 */
                child->size = size;
            }
            result = child;
            goto out;
        }
        if (child->type != VFS_DIR) goto out;
        cur = child;
    }
    result = cur;                                       /* путь "/" */
out:
    spin_unlock_irqrestore(&vfs_lock, fl);
    return result;
}

struct vnode *vfs_lookup(const char *path) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    struct vnode *n = NULL;
    if (lookup_locked(NULL, path, &n)) n = NULL;
    spin_unlock_irqrestore(&vfs_lock, fl);
    return n;
}

struct vnode *vfs_child(struct vnode *dir, size_t index) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    struct vnode *c = NULL;
    if (dir->type == VFS_DIR) {
        c = dir->children;
        while (c && index--) c = c->next;
    }
    spin_unlock_irqrestore(&vfs_lock, fl);
    return c;
}

static int open_locked(struct vnode *cwd, const char *path, int flags, struct file **out) {
    struct vnode *n;
    int r = lookup_locked(cwd, path, &n);

    if (r == -ENOENT && (flags & O_CREAT)) {
        struct vnode *parent;
        char name[VFS_NAME_MAX];
        r = split_parent(cwd, path, &parent, name);
        if (r) return r;
        n = new_node(name, slen(name), VFS_FILE);
        if (!n) return -ENOMEM;
        link_child(parent, n);
    } else if (r) {
        return r;
    } else if ((flags & O_CREAT) && (flags & O_EXCL)) {
        return -EEXIST;
    }

    int acc = flags & O_ACCMODE;
    if (acc > O_RDWR) return -EINVAL;
    if (n->type == VFS_DIR) {
        if (acc != O_RDONLY) return -EISDIR;          /* каталог можно только читать (readdir) */
    } else if ((flags & O_TRUNC) && acc != O_RDONLY) {
        clear_data(n);
    }

    struct file *f = kzalloc(sizeof(*f));
    if (!f) return -ENOMEM;
    f->node = n;
    f->flags = flags;
    f->refs = 1;
    n->refs++;
    *out = f;
    return 0;
}

int vfs_open_at(struct vnode *cwd, const char *path, int flags, struct file **out) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    int r = open_locked(cwd, path, flags, out);
    spin_unlock_irqrestore(&vfs_lock, fl);
    return r;
}

int vfs_mkdir_at(struct vnode *cwd, const char *path) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    struct vnode *parent;
    char name[VFS_NAME_MAX];
    int r = split_parent(cwd, path, &parent, name);
    if (!r) {
        if (find_child(parent, name, slen(name))) {
            r = -EEXIST;
        } else {
            struct vnode *n = new_node(name, slen(name), VFS_DIR);
            if (n) link_child(parent, n); else r = -ENOMEM;
        }
    }
    spin_unlock_irqrestore(&vfs_lock, fl);
    return r;
}

int vfs_unlink_at(struct vnode *cwd, const char *path) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    struct vnode *parent;
    char name[VFS_NAME_MAX];
    int r = split_parent(cwd, path, &parent, name);
    if (!r) {
        struct vnode *n = find_child(parent, name, slen(name));
        if (!n) r = -ENOENT;
        else if (n->type == VFS_DIR) r = -EISDIR;
        else { unlink_child(n); kill_node(n); }
    }
    spin_unlock_irqrestore(&vfs_lock, fl);
    return r;
}

int vfs_rmdir_at(struct vnode *cwd, const char *path) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    struct vnode *parent;
    char name[VFS_NAME_MAX];
    int r = split_parent(cwd, path, &parent, name);
    if (!r) {
        struct vnode *n = find_child(parent, name, slen(name));
        if (!n) r = -ENOENT;
        else if (n->type != VFS_DIR) r = -ENOTDIR;
        else if (n->children) r = -ENOTEMPTY;
        else if (n->refs) r = -EBUSY;                 /* чей-то рабочий каталог или открыт */
        else { unlink_child(n); kill_node(n); }
    }
    spin_unlock_irqrestore(&vfs_lock, fl);
    return r;
}

/* Переименование/перенос. Файл поверх существующего файла заменяет его;
 * остальные случаи с существующим назначением — ошибка. */
static int rename_locked(struct vnode *cwd, const char *from, const char *to) {
    struct vnode *fp, *tp;
    char fname[VFS_NAME_MAX], tname[VFS_NAME_MAX];
    int r = split_parent(cwd, from, &fp, fname);
    if (r) return r;
    r = split_parent(cwd, to, &tp, tname);
    if (r) return r;

    struct vnode *src = find_child(fp, fname, slen(fname));
    if (!src) return -ENOENT;

    if (src->type == VFS_DIR)                         /* нельзя переносить каталог в самого себя */
        for (struct vnode *a = tp; a; a = a->parent)
            if (a == src) return -EINVAL;

    struct vnode *dst = find_child(tp, tname, slen(tname));
    if (dst == src) return 0;
    if (dst) {
        if (src->type == VFS_FILE && dst->type == VFS_FILE) {
            unlink_child(dst);
            kill_node(dst);
        } else {
            return dst->type == VFS_DIR && src->type == VFS_FILE ? -EISDIR : -EEXIST;
        }
    }

    unlink_child(src);
    memset(src->name, 0, sizeof(src->name));
    memcpy(src->name, tname, slen(tname));
    link_child(tp, src);
    return 0;
}

int vfs_rename_at(struct vnode *cwd, const char *from, const char *to) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    int r = rename_locked(cwd, from, to);
    spin_unlock_irqrestore(&vfs_lock, fl);
    return r;
}

int vfs_stat_at(struct vnode *cwd, const char *path, struct user_stat *st) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    struct vnode *n;
    int r = lookup_locked(cwd, path, &n);
    if (!r) {
        memset(st, 0, sizeof(*st));
        st->type = (uint32_t)n->type;
        st->size = n->size;
        st->id = n->id;
    }
    spin_unlock_irqrestore(&vfs_lock, fl);
    return r;
}

int vfs_chdir_at(struct vnode *cwd, const char *path, struct vnode **out) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    struct vnode *n;
    int r = lookup_locked(cwd, path, &n);
    if (!r && n->type != VFS_DIR) r = -ENOTDIR;
    if (!r) { n->refs++; *out = n; }
    spin_unlock_irqrestore(&vfs_lock, fl);
    return r;
}

int vfs_read_whole(struct vnode *cwd, const char *path, void **buf, size_t *size) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    struct vnode *n;
    int r = lookup_locked(cwd, path, &n);
    if (!r && n->type != VFS_FILE) r = -EISDIR;
    if (!r) {
        void *copy = kmalloc(n->size ? n->size : 1);
        if (!copy) {
            r = -ENOMEM;
        } else {
            if (n->size) memcpy(copy, n->data, n->size);
            *buf = copy;
            *size = n->size;
        }
    }
    spin_unlock_irqrestore(&vfs_lock, fl);
    return r;
}

long vfs_read(struct file *f, void *buf, size_t len) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    long r = read_locked(f, buf, len);
    spin_unlock_irqrestore(&vfs_lock, fl);
    return r;
}

long vfs_write(struct file *f, const void *buf, size_t len) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    long r = write_locked(f, buf, len);
    spin_unlock_irqrestore(&vfs_lock, fl);
    return r;
}

long vfs_lseek(struct file *f, int64_t off, int whence) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    long r;
    int64_t base = 0;
    if (f->node->type != VFS_FILE) {
        r = -EISDIR;
    } else if (whence != VFS_SEEK_SET && whence != VFS_SEEK_CUR && whence != VFS_SEEK_END) {
        r = -EINVAL;
    } else {
        if (whence == VFS_SEEK_CUR) base = (int64_t)f->offset;
        if (whence == VFS_SEEK_END) base = (int64_t)f->node->size;
        int64_t pos;
        if (__builtin_add_overflow(base, off, &pos) || pos < 0 || pos > (int64_t)VFS_MAX_FILE) {
            r = -EINVAL;
        } else {
            f->offset = (uint64_t)pos;
            r = (long)pos;
        }
    }
    spin_unlock_irqrestore(&vfs_lock, fl);
    return r;
}

int vfs_readdir(struct file *f, size_t index, struct user_dirent *d) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    int r;
    if (f->node->type != VFS_DIR) {
        r = -ENOTDIR;
    } else {
        struct vnode *c = f->node->children;
        while (c && index--) c = c->next;
        if (!c) {
            r = 0;
        } else {
            memset(d, 0, sizeof(*d));
            d->type = (uint32_t)c->type;
            d->size = (uint32_t)c->size;
            memcpy(d->name, c->name, sizeof(d->name) - 1);
            r = 1;
        }
    }
    spin_unlock_irqrestore(&vfs_lock, fl);
    return r;
}

void file_get(struct file *f) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    f->refs++;
    spin_unlock_irqrestore(&vfs_lock, fl);
}

void file_put(struct file *f) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    if (--f->refs == 0) {
        node_put_locked(f->node);
        kfree(f);
    }
    spin_unlock_irqrestore(&vfs_lock, fl);
}

void vfs_node_get(struct vnode *n) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    n->refs++;
    spin_unlock_irqrestore(&vfs_lock, fl);
}

void vfs_node_put(struct vnode *n) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    node_put_locked(n);
    spin_unlock_irqrestore(&vfs_lock, fl);
}

long vfs_path(struct vnode *n, char *buf, size_t size) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    char tmp[VFS_PATH_MAX];
    size_t pos = sizeof(tmp);
    long r = 0;

    tmp[--pos] = 0;
    if (n == root) {
        tmp[--pos] = '/';
    } else {
        for (struct vnode *c = n; c != root; c = c->parent) {
            if (!c) { r = -ENOENT; break; }           /* узел уже удалён из дерева */
            size_t l = slen(c->name);
            if (pos < l + 1) { r = -ENAMETOOLONG; break; }
            pos -= l;
            memcpy(tmp + pos, c->name, l);
            tmp[--pos] = '/';
        }
    }
    if (!r) {
        size_t len = sizeof(tmp) - 1 - pos;
        if (len + 1 > size) r = -ERANGE;
        else { memcpy(buf, tmp + pos, len + 1); r = (long)len; }
    }
    spin_unlock_irqrestore(&vfs_lock, fl);
    return r;
}

void vfs_stats(struct user_fsinfo *info) {
    uint64_t fl = spin_lock_irqsave(&vfs_lock);
    memset(info, 0, sizeof(*info));
    info->max_file = VFS_MAX_FILE;

    struct vnode *n = root;                           /* обход без рекурсии */
    for (;;) {
        if (n->type == VFS_DIR) info->dirs++;
        else { info->files++; info->bytes += n->size; }

        if (n->type == VFS_DIR && n->children) { n = n->children; continue; }
        while (n != root && !n->next) n = n->parent;
        if (n == root) break;
        n = n->next;
    }
    spin_unlock_irqrestore(&vfs_lock, fl);
}
