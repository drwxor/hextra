/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef UORIX_VFS_H
#define UORIX_VFS_H

#include <stdint.h>
#include <stddef.h>

#define MAX_FDS 16
#define MAX_PATH 256

#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0040

#define S_IFMT   0xF000
#define S_IFREG  0x8000
#define S_IFDIR  0x4000

struct vfs_node;
struct file;

struct dirent {
    uint32_t d_ino;
    char d_name[256];
};

struct file_ops {
    int (*open)(struct vfs_node *node, int flags);
    int (*close)(struct vfs_node *node);
    int (*read)(struct vfs_node *node, uint64_t offset, void *buf, size_t size);
    int (*write)(struct vfs_node *node, uint64_t offset, const void *buf, size_t size);
    int (*readdir)(struct vfs_node *node, uint32_t index, struct dirent *out);
};

struct vfs_node {
    char name[MAX_PATH];
    uint32_t flags;
    uint64_t size;
    struct file_ops *ops;
    void *data;
};

struct file {
    struct vfs_node *node;
    uint64_t offset;
    int flags;
    int refcount;
};

void vfs_init(void);
int vfs_set_root(struct vfs_node *node);
int vfs_open(const char *path, int flags);
int vfs_close(int fd);
int vfs_read(int fd, void *buf, size_t size);
int vfs_write(int fd, const void *buf, size_t size);
int vfs_readdir(int fd, uint32_t index, struct dirent *out);

#endif
