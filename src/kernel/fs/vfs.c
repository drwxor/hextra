/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/fs/vfs.h"
#include "kernel/fs/ext2.h"
#include "kernel/heap.h"
#include "kernel/renderer.h"
#include "kernel/process.h"
#include "kernel/string.h"
#include "kernel/uaccess.h"

#include <stdint.h>
#include <stddef.h>

static struct vfs_node *root_node = 0;
static struct ext2_fs *root_fs = 0;

void
vfs_init(void)
{
    root_node = 0;
    root_fs = 0;
}

int
vfs_set_root(struct vfs_node *node)
{
    root_node = node;
    return 0;
}

int
vfs_set_root_fs(struct ext2_fs *fs)
{
    root_fs = fs;
    if (fs)
    {
        struct vfs_node *root = ext2_vfs_node(fs, 2);
        if (!root)
            return -1;
        vfs_set_root(root);
    }
    return 0;
}

static void
vfs_node_put(struct vfs_node *node)
{
    if (!node || node == root_node)
        return;

    if (node->ops && node->ops->release)
        node->ops->release(node);
}

static int
vfs_append_path(char *out, size_t *len, const char *path)
{
    const char *p = path;

    while (*p)
    {
        while (*p == '/')
            p++;

        if (*p == 0)
            break;

        const char *start = p;
        while (*p && *p != '/')
            p++;

        size_t n = (size_t)(p - start);

        if (n == 1 && start[0] == '.')
            continue;

        if (n == 2 && start[0] == '.' && start[1] == '.')
        {
            while (*len > 0 && out[*len - 1] != '/')
                (*len)--;
            if (*len > 0)
                (*len)--;
            out[*len] = 0;
            continue;
        }

        if (n > 255 || *len + 1 + n >= MAX_PATH)
            return -1;

        out[(*len)++] = '/';
        memcpy(out + *len, start, n);
        *len += n;
        out[*len] = 0;
    }

    return 0;
}

static int
vfs_normalize(const char *path, char *out)
{
    size_t len = 0;
    out[0] = 0;

    if (!path)
        return -1;

    struct process *proc = process_current();
    if (path[0] != '/' && proc)
    {
        if (vfs_append_path(out, &len, proc->cwd) != 0)
            return -1;
    }

    if (vfs_append_path(out, &len, path) != 0)
        return -1;

    if (len == 0)
    {
        out[0] = '/';
        out[1] = 0;
    }

    return 0;
}

int
vfs_abspath(const char *path, char *out)
{
    return vfs_normalize(path, out);
}

static struct vfs_node *
vfs_get_child(struct vfs_node *parent, const char *name)
{
    if (!parent || !parent->ops || !parent->ops->readdir)
        return 0;

    if ((parent->flags & S_IFMT) != S_IFDIR)
        return 0;

    struct dirent de;
    for (uint32_t i = 0;; i++)
    {
        if (parent->ops->readdir(parent, i, &de) != 0)
            break;

        if (strcmp(de.d_name, name) == 0)
            return ext2_vfs_node(root_fs, de.d_ino);
    }

    return 0;
}

static struct vfs_node *
resolve_path(const char *path)
{
    if (!path || !root_node || !root_fs)
        return 0;

    char abs[MAX_PATH];
    if (vfs_normalize(path, abs) != 0)
        return 0;

    struct vfs_node *current = root_node;
    char component[256];
    const char *p = abs;

    while (*p)
    {
        while (*p == '/')
            p++;

        if (*p == 0)
            break;

        uint32_t i = 0;
        while (*p && *p != '/' && i < sizeof(component) - 1)
            component[i++] = *p++;
        component[i] = 0;

        struct vfs_node *child = vfs_get_child(current, component);
        vfs_node_put(current);

        if (!child)
            return 0;

        current = child;
    }

    return current;
}

int
vfs_open(const char *path, int flags)
{
    struct process *proc = process_current();
    if (!proc)
        return -1;

    int fd = -1;
    for (int i = 0; i < MAX_FDS; i++)
    {
        if (proc->fds[i] == 0)
        {
            fd = i;
            break;
        }
    }

    if (fd < 0)
        return -1;

    struct vfs_node *node = resolve_path(path);
    if (!node)
        return -1;

    struct file *f = kmalloc(sizeof(*f));
    if (!f)
    {
        vfs_node_put(node);
        return -1;
    }

    f->node = node;
    f->offset = 0;
    f->flags = flags;
    f->refcount = 1;

    proc->fds[fd] = f;
    return fd;
}

void
vfs_file_put(struct file *f)
{
    if (!f)
        return;

    if (--f->refcount > 0)
        return;

    if (f->node->ops && f->node->ops->close)
        f->node->ops->close(f->node);

    vfs_node_put(f->node);
    kfree(f);
}

int
vfs_close(int fd)
{
    struct process *proc = process_current();
    if (!proc || fd < 0 || fd >= MAX_FDS)
        return -1;

    struct file *f = proc->fds[fd];
    if (!f)
        return -1;

    proc->fds[fd] = 0;
    vfs_file_put(f);
    return 0;
}

int
vfs_read(int fd, void *user_buf, size_t size)
{
    struct process *proc = process_current();
    if (!proc || fd < 0 || fd >= MAX_FDS)
        return -1;

    struct file *f = proc->fds[fd];
    if (!f || !f->node->ops || !f->node->ops->read)
        return -1;

    if ((f->node->flags & S_IFMT) == S_IFDIR)
        return -1;

    if (size == 0)
        return 0;

    void *kbuf = kmalloc(size);
    if (!kbuf)
        return -1;

    int ret = f->node->ops->read(f->node, f->offset, kbuf, size);
    if (ret > 0)
    {
        f->offset += ret;
        if (copyout(user_buf, kbuf, ret) != 0)
        {
            kfree(kbuf);
            return -1;
        }
    }

    kfree(kbuf);
    return ret;
}

int
vfs_write(int fd, const void *user_buf, size_t size)
{
    struct process *proc = process_current();
    if (!proc || fd < 0 || fd >= MAX_FDS)
        return -1;

    struct file *f = proc->fds[fd];
    if (!f || !f->node->ops || !f->node->ops->write)
        return -1;

    if (size == 0)
        return 0;

    void *kbuf = kmalloc(size);
    if (!kbuf)
        return -1;

    if (copyin(kbuf, user_buf, size) != 0)
    {
        kfree(kbuf);
        return -1;
    }

    int ret = f->node->ops->write(f->node, f->offset, kbuf, size);
    if (ret > 0)
        f->offset += ret;

    kfree(kbuf);
    return ret;
}

int
vfs_readdir(int fd, uint32_t index, struct dirent *user_out)
{
    struct process *proc = process_current();
    if (!proc || fd < 0 || fd >= MAX_FDS)
        return -1;

    struct file *f = proc->fds[fd];
    if (!f || !f->node->ops || !f->node->ops->readdir)
        return -1;

    struct dirent kdirent;
    int ret = f->node->ops->readdir(f->node, index, &kdirent);
    if (ret == 0)
    {
        if (copyout(user_out, &kdirent, sizeof(kdirent)) != 0)
            return -1;
    }

    return ret;
}

static int
vfs_node_stat(struct vfs_node *node, struct stat *user_out)
{
    struct stat st;

    if (node->ops && node->ops->stat)
    {
        if (node->ops->stat(node, &st) != 0)
            return -1;
    }
    else
    {
        memset(&st, 0, sizeof(st));
        st.st_mode = node->flags;
        st.st_nlink = 1;
        st.st_size = (int64_t)node->size;
    }

    if (copyout(user_out, &st, sizeof(st)) != 0)
        return -1;

    return 0;
}

int
vfs_stat(const char *path, struct stat *user_out)
{
    struct vfs_node *node = resolve_path(path);
    if (!node)
        return -1;

    int ret = vfs_node_stat(node, user_out);
    vfs_node_put(node);
    return ret;
}

int
vfs_fstat(int fd, struct stat *user_out)
{
    struct process *proc = process_current();
    if (!proc || fd < 0 || fd >= MAX_FDS)
        return -1;

    struct file *f = proc->fds[fd];
    if (!f)
        return -1;

    return vfs_node_stat(f->node, user_out);
}

int
vfs_chdir(const char *path)
{
    struct process *proc = process_current();
    if (!proc)
        return -1;

    char abs[MAX_PATH];
    if (vfs_normalize(path, abs) != 0)
        return -1;

    struct vfs_node *node = resolve_path(abs);
    if (!node)
        return -1;

    int is_dir = (node->flags & S_IFMT) == S_IFDIR;
    vfs_node_put(node);

    if (!is_dir)
        return -1;

    memcpy(proc->cwd, abs, strlen(abs) + 1);
    return 0;
}

int
vfs_getcwd(char *user_buf, uint64_t size)
{
    struct process *proc = process_current();
    if (!proc || !user_buf)
        return -1;

    if (strlen(proc->cwd) + 1 > size)
        return -1;

    return copyout_str(user_buf, size, proc->cwd);
}
