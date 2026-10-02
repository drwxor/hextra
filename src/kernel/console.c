/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/fs/vfs.h"
#include "kernel/heap.h"
#include "kernel/renderer.h"
#include "kernel/driver/keyboard.h"
#include "kernel/process.h"

#include <stdint.h>
#include <stddef.h>

static int
console_open(struct vfs_node *node, int flags)
{
    (void)node;
    (void)flags;
    return 0;
}

static int
console_close(struct vfs_node *node)
{
    (void)node;
    return 0;
}

static int
console_read(struct vfs_node *node, uint64_t offset, void *buf, size_t size)
{
    (void)node;
    (void)offset;

    if (size == 0)
        return 0;

    char *dst = (char *)buf;
    dst[0] = keyboard_getc();
    return 1;
}

// static int
// console_write(struct vfs_node *node, uint64_t offset, const void *buf, size_t size)
// {
//     (void)node;
//     (void)offset;

//     const char *src = (const char *)buf;
//     for (size_t i = 0; i < size; i++)
//     {
//         char c = src[i];
//         render_putc(c, 0xFFFFFF);
//         if (c == '\n')
//             render_putc('\r', 0xFFFFFF);
//     }

//     return (int)size;
// }

static int
console_write(struct vfs_node *node, uint64_t offset, const void *buf, size_t size)
{
    (void)node;
    (void)offset;

    const char *src = (const char *)buf;

    for (size_t i = 0; i < size; i++)
    {
        char c = src[i];
        render_putc(c, 0xFFFFFF);

        if (c == '\n')
            render_putc('\r', 0xFFFFFF);
    }

    return (int)size;
}

static int
console_readdir(struct vfs_node *node, uint32_t index, struct dirent *out)
{
    (void)node;
    (void)index;
    (void)out;
    return -1;
}

static struct file_ops console_ops = {
    .open = console_open,
    .close = console_close,
    .read = console_read,
    .write = console_write,
    .readdir = console_readdir
};

static struct vfs_node console_node = {
    .name = "console",
    .flags = S_IFREG,
    .size = 0,
    .ops = &console_ops,
    .data = 0
};

void
console_init_process(struct process *proc)
{
    if (!proc)
        return;

    struct file *stdin_f = kmalloc(sizeof(*stdin_f));
    if (stdin_f)
    {
        stdin_f->node = &console_node;
        stdin_f->offset = 0;
        stdin_f->flags = O_RDONLY;
        stdin_f->refcount = 1;
        proc->fds[0] = stdin_f;
    }

    struct file *stdout_f = kmalloc(sizeof(*stdout_f));
    if (stdout_f)
    {
        stdout_f->node = &console_node;
        stdout_f->offset = 0;
        stdout_f->flags = O_WRONLY;
        stdout_f->refcount = 1;
        proc->fds[1] = stdout_f;
    }

    struct file *stderr_f = kmalloc(sizeof(*stderr_f));
    if (stderr_f)
    {
        stderr_f->node = &console_node;
        stderr_f->offset = 0;
        stderr_f->flags = O_WRONLY;
        stderr_f->refcount = 1;
        proc->fds[2] = stderr_f;
    }
}
