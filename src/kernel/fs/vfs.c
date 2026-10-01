/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/fs/vfs.h"
#include "kernel/fs/ext2.h"
#include "kernel/heap.h"
#include "kernel/renderer.h"
#include "kernel/process.h"

#include <stdint.h>
#include <stddef.h>

static struct vfs_node *root_node = 0;

void
vfs_init(void)
{
    root_node = 0;
}

int
vfs_set_root(struct vfs_node *node)
{
    root_node = node;
    return 0;
}

static struct vfs_node *
resolve_path(const char *path)
{
    if (root_node)
        return root_node;
    return 0;
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
vfs_read(int fd, void *buf, size_t size)
{
    struct process *proc = process_current();
    if (!proc || fd < 0 || fd >= MAX_FDS)
        return -1;

    struct file *f = proc->fds[fd];
    if (!f || !f->node->ops || !f->node->ops->read)
        return -1;

    int ret = f->node->ops->read(f->node, f->offset, buf, size);
    if (ret > 0)
        f->offset += ret;

    return ret;
}

int
vfs_write(int fd, const void *buf, size_t size)
{
    struct process *proc = process_current();
    if (!proc || fd < 0 || fd >= MAX_FDS)
        return -1;

    struct file *f = proc->fds[fd];
    if (!f || !f->node->ops || !f->node->ops->write)
        return -1;

    int ret = f->node->ops->write(f->node, f->offset, buf, size);
    if (ret > 0)
        f->offset += ret;

    return ret;
}

int
vfs_readdir(int fd, uint32_t index, struct dirent *out)
{
    struct process *proc = process_current();
    if (!proc || fd < 0 || fd >= MAX_FDS)
        return -1;

    struct file *f = proc->fds[fd];
    if (!f || !f->node->ops || !f->node->ops->readdir)
        return -1;

    return f->node->ops->readdir(f->node, index, out);
}
