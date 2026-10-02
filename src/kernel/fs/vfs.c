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

static struct vfs_node *vfs_walk(struct vfs_node *start, const char *path);
static struct vfs_node *vfs_get_child(struct vfs_node *parent, const char *name);
static struct vfs_node *vfs_get_parent(struct vfs_node *node);

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
        if (root)
            vfs_set_root(root);
    }
    return 0;
}

static struct vfs_node *
resolve_path(const char *path)
{
    if (!path || !root_node || !root_fs)
        return 0;

    struct vfs_node *current = root_node;
    struct process *proc = process_current();

    if (path[0] == '/')
    {
        current = root_node;
        path++;
    }
    else if (proc && proc->cwd[0])
    {
        current = vfs_walk(root_node, proc->cwd);
        if (!current)
            current = root_node;
    }

    if (!current)
        return 0;

    char component[256];
    const char *p = path;

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

        if (component[0] == 0)
            continue;

        if (component[0] == '.' && component[1] == 0)
        {
            continue;
        }

        if (component[0] == '.' && component[1] == '.' && component[2] == 0)
        {
            struct vfs_node *parent = vfs_get_parent(current);
            if (parent)
                current = parent;
            continue;
        }

        struct vfs_node *child = vfs_get_child(current, component);
        if (!child)
            return 0;

        current = child;
    }

    return current;
}

static struct vfs_node *
vfs_walk(struct vfs_node *start, const char *path)
{
    if (!start || !path)
        return 0;

    struct vfs_node *current = start;
    char component[256];
    const char *p = path;

    if (*p == '/')
    {
        current = root_node;
        p++;
    }

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

        if (component[0] == 0)
            continue;

        if (component[0] == '.' && component[1] == 0)
            continue;

        if (component[0] == '.' && component[1] == '.' && component[2] == 0)
        {
            struct vfs_node *parent = vfs_get_parent(current);
            if (parent)
                current = parent;
            continue;
        }

        struct vfs_node *child = vfs_get_child(current, component);
        if (!child)
            return 0;

        current = child;
    }

    return current;
}

static struct vfs_node *
vfs_get_child(struct vfs_node *parent, const char *name)
{
    if (!parent || !parent->ops || !parent->ops->readdir)
        return 0;

    if (!(parent->flags & S_IFDIR))
        return 0;

    struct dirent de;
    for (uint32_t i = 0;; i++)
    {
        if (parent->ops->readdir(parent, i, &de) != 0)
            break;

        if (strcmp(de.d_name, name) == 0)
        {
            return ext2_vfs_node(root_fs, de.d_ino);
        }
    }

    return 0;
}

static struct vfs_node *
vfs_get_parent(struct vfs_node *node)
{
    if (!node || !root_fs)
        return 0;

    if (node->ops && node->ops->readdir)
    {
        struct dirent de;
        if (node->ops->readdir(node, 0, &de) == 0)
        {
            if (strcmp(de.d_name, ".") == 0)
            {
                if (node->ops->readdir(node, 1, &de) == 0)
                {
                    if (strcmp(de.d_name, "..") == 0)
                    {
                        return ext2_vfs_node(root_fs, de.d_ino);
                    }
                }
            }
        }
    }

    return root_node;
}

int
vfs_open(const char *path, int flags)
{
    struct vfs_node *node = resolve_path(path);
    if (!node)
        return -1;

    struct process *proc = process_current();
    if (!proc)
        return -1;

    for (int i = 0; i < MAX_FDS; i++)
    {
        if (proc->fds[i] == 0)
        {
            struct file *f = kmalloc(sizeof(*f));
            if (!f)
                return -1;

            f->node = node;
            f->offset = 0;
            f->flags = flags;
            f->refcount = 1;

            proc->fds[i] = f;
            return i;
        }
    }

    return -1;
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

    if (--f->refcount == 0)
    {
        if (f->node->ops && f->node->ops->close)
            f->node->ops->close(f->node);
        kfree(f);
    }

    proc->fds[fd] = 0;
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