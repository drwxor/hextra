/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef UORIX_EXT2_H
#define UORIX_EXT2_H

#include <stdint.h>

#define EXT2_START_LBA 65536

struct ext2_fs;

extern struct ext2_fs *rootfs;

struct ext2_fs *ext2_mount(uint32_t start_lba);
uint64_t ext2_read_file(struct ext2_fs *fs, const char *path, void **out_buf);
void ext2_unmount(struct ext2_fs *fs);
struct vfs_node *ext2_vfs_node(struct ext2_fs *fs, uint32_t ino);
int ext2_chdir(const char *path);
int ext2_getcwd(char *buf, uint64_t size);

#endif
