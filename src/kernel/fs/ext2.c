/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/fs/ext2.h"
#include "kernel/fs/vfs.h"
#include "kernel/ata.h"
#include "kernel/heap.h"
#include "kernel/renderer.h"
#include "kernel/pmm.h"
#include "kernel/process.h"

#include <stdint.h>
#include <stddef.h>

#include "kernel/string.h"
#include "kernel/uaccess.h"

struct ext2_fs *rootfs;

static int
read_blocks(struct ext2_fs *fs, uint32_t block_nr, uint32_t count, void *buf)
{
    if (fs->mod_base)
    {
        uint64_t offset = (uint64_t)block_nr * fs->block_size;
        if (offset + (uint64_t)count * fs->block_size > fs->mod_size)
            return -1;
        memcpy(buf, (const void *)(fs->mod_base + offset), count * fs->block_size);
        return 0;
    }
    else
    {
        uint32_t sectors_per_block = fs->block_size / 512;
        uint32_t lba = fs->start_lba + block_nr * sectors_per_block;
        return ata_read_sectors(lba, (uint8_t)(count * sectors_per_block), buf);
    }
}

struct ext2_fs *
ext2_mount(uint32_t start_lba)
{
    uint8_t sector[1024];

    render_printf("ext2: reading LBA %u\n", start_lba + 2);

    if (ata_read_sectors(start_lba + 2, 2, sector) != 0)
    {
        render_printf("ext2: failed to read superblock\n");
        return 0;
    }

    render_printf("ext2: magic=0x%x\n", *(uint16_t *)(sector + 56));

    struct ext2_superblock *sb = (struct ext2_superblock *)sector;
    if (sb->s_magic != EXT2_SUPER_MAGIC)
    {
        render_printf("ext2: bad magic 0x%x\n", sb->s_magic);
        return 0;
    }

    struct ext2_fs *fs = kmalloc(sizeof(*fs));
    if (!fs)
    {
        render_printf("ext2: unable to create filesystem\n");
        return 0;
    }

    fs->start_lba = start_lba;
    fs->block_size = 1024 << sb->s_log_block_size;
    fs->inodes_per_group = sb->s_inodes_per_group;
    fs->blocks_per_group = sb->s_blocks_per_group;
    fs->inode_size = sb->s_inode_size ? sb->s_inode_size : 128;
    fs->first_data_block = sb->s_first_data_block;
    fs->sb = *sb;

    render_printf("ext2: mounted, block_size=%u inodes/group=%u\n", fs->block_size, fs->inodes_per_group);
    return fs;
}

struct ext2_fs *
ext2_mount_module(uint64_t mod_addr, uint64_t mod_size)
{
    if (mod_size < 2048)
        return 0;

    uint8_t *mod_data = (uint8_t *)mod_addr;
    struct ext2_superblock *sb = (struct ext2_superblock *)(mod_data + 1024);

    if (sb->s_magic != EXT2_SUPER_MAGIC)
    {
        render_printf("ext2: module bad magic 0x%x\n", sb->s_magic);
        return 0;
    }

    struct ext2_fs *fs = kmalloc(sizeof(*fs));
    if (!fs)
    {
        render_printf("ext2: unable to create filesystem\n");
        return 0;
    }

    fs->start_lba = 0;
    fs->block_size = 1024 << sb->s_log_block_size;
    fs->inodes_per_group = sb->s_inodes_per_group;
    fs->blocks_per_group = sb->s_blocks_per_group;
    fs->inode_size = sb->s_inode_size ? sb->s_inode_size : 128;
    fs->first_data_block = sb->s_first_data_block;
    fs->sb = *sb;
    fs->mod_base = mod_addr;
    fs->mod_size = mod_size;

    render_printf("ext2: mounted module, block_size=%u inodes/group=%u\n", fs->block_size, fs->inodes_per_group);
    return fs;
}

static int
read_inode(struct ext2_fs *fs, uint32_t ino, struct ext2_inode *out)
{
    if (ino == 0)
        return -1;

    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;

    uint32_t bgd_offset = group * sizeof(struct ext2_bgd);
    uint32_t bgd_block = fs->first_data_block + 1 + bgd_offset / fs->block_size;
    uint8_t *bgd_buf = kmalloc(fs->block_size);
    if (!bgd_buf)
        return -1;

    if (read_blocks(fs, bgd_block, 1, bgd_buf) != 0)
    {
        kfree(bgd_buf);
        return -1;
    }

    struct ext2_bgd *bgd = (struct ext2_bgd *)(bgd_buf + bgd_offset % fs->block_size);
    uint32_t inode_table = bgd->bg_inode_table;
    kfree(bgd_buf);

    uint32_t offset = index * fs->inode_size;
    uint32_t block = inode_table + (offset / fs->block_size);
    uint32_t block_off = offset % fs->block_size;

    uint8_t *ibuf = kmalloc(fs->block_size);
    if (!ibuf)
        return -1;

    if (read_blocks(fs, block, 1, ibuf) != 0)
    {
        kfree(ibuf);
        return -1;
    }

    struct ext2_inode *src = (struct ext2_inode *)(ibuf + block_off);
    *out = *src;
    kfree(ibuf);
    return 0;
}

static int
read_indirect(struct ext2_fs *fs, uint32_t block, uint32_t index, uint32_t *out)
{
    if (block == 0)
    {
        *out = 0;
        return 0;
    }

    uint32_t *ind = kmalloc(fs->block_size);
    if (!ind)
        return -1;

    if (read_blocks(fs, block, 1, ind) != 0)
    {
        kfree(ind);
        return -1;
    }

    *out = ind[index];
    kfree(ind);
    return 0;
}

static int
map_block(struct ext2_fs *fs, struct ext2_inode *inode, uint32_t block_idx, uint32_t *out)
{
    uint32_t per_block = fs->block_size / 4;

    if (block_idx < 12)
    {
        *out = inode->i_block[block_idx];
        return 0;
    }
    block_idx -= 12;

    if (block_idx < per_block)
        return read_indirect(fs, inode->i_block[12], block_idx, out);
    block_idx -= per_block;

    if (block_idx < per_block * per_block)
    {
        uint32_t ind;
        if (read_indirect(fs, inode->i_block[13], block_idx / per_block, &ind) != 0)
            return -1;
        return read_indirect(fs, ind, block_idx % per_block, out);
    }

    return -1;
}

static int
read_inode_data(struct ext2_fs *fs, struct ext2_inode *inode, uint32_t offset, uint32_t size, void *buf)
{
    uint8_t *dst = buf;
    uint32_t left = size;
    uint32_t pos = offset;

    uint8_t *bbuf = kmalloc(fs->block_size);
    if (!bbuf)
        return -1;

    while (left > 0)
    {
        uint32_t block_idx = pos / fs->block_size;
        uint32_t block_off = pos % fs->block_size;
        uint32_t chunk = fs->block_size - block_off;
        if (chunk > left)
            chunk = left;

        uint32_t phys_block = 0;
        if (map_block(fs, inode, block_idx, &phys_block) != 0)
        {
            kfree(bbuf);
            return -1;
        }

        if (phys_block == 0)
        {
            memset(dst, 0, chunk);
        }
        else
        {
            if (read_blocks(fs, phys_block, 1, bbuf) != 0)
            {
                kfree(bbuf);
                return -1;
            }
            memcpy(dst, bbuf + block_off, chunk);
        }

        dst += chunk;
        pos += chunk;
        left -= chunk;
    }

    kfree(bbuf);
    return 0;
}

static uint32_t
lookup_in_dir(struct ext2_fs *fs, struct ext2_inode *dir, const char *name)
{
    if ((dir->i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
        return 0;

    uint32_t size = dir->i_size;
    uint8_t *buf = kmalloc(size);
    if (!buf)
        return 0;

    if (read_inode_data(fs, dir, 0, size, buf) != 0)
    {
        kfree(buf);
        return 0;
    }

    uint32_t pos = 0;
    uint32_t name_len = 0;
    while (name[name_len])
        name_len++;

    while (pos < size)
    {
        struct ext2_dirent *de = (struct ext2_dirent *)(buf + pos);
        if (de->rec_len == 0)
            break;
        if (de->inode != 0 && de->name_len == name_len)
        {
            int match = 1;
            for (uint8_t i = 0; i < de->name_len; i++)
            {
                if (de->name[i] != name[i])
                {
                    match = 0;
                    break;
                }
            }
            if (match)
            {
                uint32_t ino = de->inode;
                kfree(buf);
                return ino;
            }
        }
        pos += de->rec_len;
    }
    kfree(buf);
    return 0;
}

uint64_t
ext2_read_file(struct ext2_fs *fs, const char *path, void **out_buf)
{
    if (!fs || !path || path[0] != '/' || !out_buf)
        return (uint64_t)-1;

    struct ext2_inode inode;
    if (read_inode(fs, 2, &inode) != 0)
        return (uint64_t)-1;

    const char *p = path + 1;
    char component[64];

    while (*p)
    {
        uint32_t i = 0;
        while (*p && *p != '/' && i < sizeof(component) - 1)
            component[i++] = *p++;
        component[i] = 0;
        if (*p == '/')
            p++;

        if (component[0] == 0)
            continue;

        uint32_t next = lookup_in_dir(fs, &inode, component);
        if (next == 0)
        {
            render_printf("ext2: path component '%s' not found\n", component);
            return (uint64_t)-1;
        }
        if (read_inode(fs, next, &inode) != 0)
            return (uint64_t)-1;
    }

    if ((inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFREG)
    {
        render_printf("ext2: not a regular file\n");
        return (uint64_t)-1;
    }

    uint32_t size = inode.i_size;
    void *buf = kmalloc(size);
    if (!buf)
        return (uint64_t)-1;

    if (read_inode_data(fs, &inode, 0, size, buf) != 0)
    {
        kfree(buf);
        return (uint64_t)-1;
    }

    *out_buf = buf;
    return size;
}

void
ext2_unmount(struct ext2_fs *fs)
{
    if (fs)
        kfree(fs);
}

static int
ext2_vfs_read(struct vfs_node *node, uint64_t offset, void *buf, size_t size)
{
    if (!node || !node->data || !buf || size == 0)
        return -1;

    struct ext2_vfs_data *vfs_data = (struct ext2_vfs_data *)node->data;
    struct ext2_fs *fs = vfs_data->fs;
    uint32_t ino = vfs_data->ino;

    struct ext2_inode inode;
    if (read_inode(fs, ino, &inode) != 0)
        return -1;

    if (offset >= inode.i_size)
        return 0;

    if (offset + size > inode.i_size)
        size = inode.i_size - offset;

    if (read_inode_data(fs, &inode, (uint32_t)offset, (uint32_t)size, buf) != 0)
        return -1;

    return (int)size;
}

static int
ext2_vfs_write(struct vfs_node *node, uint64_t offset, const void *buf, size_t size)
{
    (void)node;
    (void)offset;
    (void)buf;
    (void)size;
    return -1;
}

static int
ext2_vfs_close(struct vfs_node *node)
{
    (void)node;
    return 0;
}

static int
ext2_vfs_readdir(struct vfs_node *node, uint32_t index, struct dirent *out)
{
    if (!node || !node->data || !out)
        return -1;

    struct ext2_vfs_data *vfs_data = (struct ext2_vfs_data *)node->data;
    struct ext2_fs *fs = vfs_data->fs;
    uint32_t ino = vfs_data->ino;

    struct ext2_inode inode;
    if (read_inode(fs, ino, &inode) != 0)
        return -1;

    if ((inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
        return -1;

    uint32_t size = inode.i_size;
    uint8_t *buf = kmalloc(size);
    if (!buf)
        return -1;

    if (read_inode_data(fs, &inode, 0, size, buf) != 0)
    {
        kfree(buf);
        return -1;
    }

    uint32_t pos = 0;
    uint32_t current_index = 0;

    while (pos < size)
    {
        struct ext2_dirent *de = (struct ext2_dirent *)(buf + pos);
        if (de->rec_len == 0)
            break;

        if (de->inode != 0)
        {
            if (current_index == index)
            {
                out->d_ino = de->inode;
                uint8_t name_len = de->name_len;
                if (name_len > 255)
                    name_len = 255;
                for (uint8_t i = 0; i < name_len; i++)
                    out->d_name[i] = de->name[i];
                out->d_name[name_len] = 0;
                kfree(buf);
                return 0;
            }
            current_index++;
        }

        pos += de->rec_len;
    }

    kfree(buf);
    return -1;
}

static int
ext2_vfs_stat(struct vfs_node *node, struct stat *out)
{
    if (!node || !node->data || !out)
        return -1;

    struct ext2_vfs_data *vfs_data = (struct ext2_vfs_data *)node->data;
    struct ext2_fs *fs = vfs_data->fs;

    struct ext2_inode inode;
    if (read_inode(fs, vfs_data->ino, &inode) != 0)
        return -1;

    memset(out, 0, sizeof(*out));
    out->st_dev = fs->mod_base ? 0 : fs->start_lba;
    out->st_ino = vfs_data->ino;
    out->st_mode = inode.i_mode;
    out->st_nlink = inode.i_links_count;
    out->st_uid = inode.i_uid;
    out->st_gid = inode.i_gid;
    out->st_size = inode.i_size;
    out->st_blksize = fs->block_size;
    out->st_blocks = inode.i_blocks;
    out->st_atime = inode.i_atime;
    out->st_mtime = inode.i_mtime;
    out->st_ctime = inode.i_ctime;
    return 0;
}

static void
ext2_vfs_release(struct vfs_node *node)
{
    if (!node)
        return;

    kfree(node->data);
    kfree(node);
}

static struct file_ops ext2_file_ops = {
    .open = 0,
    .close = ext2_vfs_close,
    .read = ext2_vfs_read,
    .write = ext2_vfs_write,
    .readdir = ext2_vfs_readdir,
    .stat = ext2_vfs_stat,
    .release = ext2_vfs_release
};

struct vfs_node *
ext2_vfs_node(struct ext2_fs *fs, uint32_t ino)
{
    if (!fs || ino == 0)
        return 0;

    struct ext2_inode inode;
    if (read_inode(fs, ino, &inode) != 0)
        return 0;

    struct ext2_vfs_data *vfs_data = kmalloc(sizeof(*vfs_data));
    if (!vfs_data)
        return 0;

    vfs_data->fs = fs;
    vfs_data->ino = ino;

    struct vfs_node *node = kmalloc(sizeof(*node));
    if (!node)
    {
        kfree(vfs_data);
        return 0;
    }

    node->name[0] = 0;
    node->flags = inode.i_mode & EXT2_S_IFMT;
    node->size = inode.i_size;
    node->ops = &ext2_file_ops;
    node->data = vfs_data;

    return node;
}
