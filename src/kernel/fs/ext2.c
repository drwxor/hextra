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

struct ext2_fs *rootfs;

#define EXT2_SUPER_MAGIC 0xEF53

struct ext2_superblock
{
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;

    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char s_volume_name[16];
    char s_last_mounted[64];
    uint32_t s_algo_bitmap;
} __attribute__((packed));

struct ext2_bgd
{
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} __attribute__((packed));

struct ext2_inode
{
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t i_osd2[12];
} __attribute__((packed));

struct ext2_dirent
{
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char name[];
} __attribute__((packed));

#define EXT2_S_IFREG 0x8000
#define EXT2_S_IFDIR 0x4000

struct ext2_fs
{
    uint32_t start_lba;
    uint32_t block_size;
    uint32_t inodes_per_group;
    uint32_t blocks_per_group;
    uint16_t inode_size;
    uint32_t first_data_block;
    struct ext2_superblock sb;
};

static int
read_blocks(struct ext2_fs *fs, uint32_t block_nr, uint32_t count, void *buf)
{
    uint32_t sectors_per_block = fs->block_size / 512;
    uint32_t lba = fs->start_lba + block_nr * sectors_per_block;
    return ata_read_sectors(lba, (uint8_t)(count * sectors_per_block), buf);
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

static int
read_inode(struct ext2_fs *fs, uint32_t ino, struct ext2_inode *out)
{
    if (ino == 0)
        return -1;

    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;

    uint32_t bgd_block = fs->first_data_block + 1;
    uint8_t *bgd_buf = kmalloc(fs->block_size);
    if (!bgd_buf)
        return -1;

    if (read_blocks(fs, bgd_block, 1, bgd_buf) != 0)
    {
        kfree(bgd_buf);
        return -1;
    }

    struct ext2_bgd *bgd = (struct ext2_bgd *)(bgd_buf + group * sizeof(struct ext2_bgd));
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
read_inode_data(struct ext2_fs *fs, struct ext2_inode *inode, uint32_t offset, uint32_t size, void *buf)
{
    uint8_t *dst = buf;
    uint32_t left = size;
    uint32_t pos = offset;

    while (left > 0)
    {
        uint32_t block_idx = pos / fs->block_size;
        uint32_t block_off = pos % fs->block_size;
        uint32_t chunk = fs->block_size - block_off;
        if (chunk > left)
            chunk = left;

        uint32_t phys_block = 0;

        if (block_idx < 12)
        {
            phys_block = inode->i_block[block_idx];
        }
        else if (block_idx < 12 + (fs->block_size / 4))
        {
            uint32_t *ind = kmalloc(fs->block_size);
            if (!ind)
                return -1;
            if (read_blocks(fs, inode->i_block[12], 1, ind) != 0)
            {
                kfree(ind);
                return -1;
            }
            phys_block = ind[block_idx - 12];
            kfree(ind);
        }
        else
        {
            return -1;
        }

        if (phys_block == 0)
            return -1;

        uint8_t *bbuf = kmalloc(fs->block_size);
        if (!bbuf)
            return -1;
        if (read_blocks(fs, phys_block, 1, bbuf) != 0)
        {
            kfree(bbuf);
            return -1;
        }
        for (uint32_t i = 0; i < chunk; i++)
            dst[i] = bbuf[block_off + i];
        kfree(bbuf);

        dst += chunk;
        pos += chunk;
        left -= chunk;
    }
    return 0;
}

static uint32_t
lookup_in_dir(struct ext2_fs *fs, struct ext2_inode *dir, const char *name)
{
    if (!(dir->i_mode & EXT2_S_IFDIR))
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

    if (!(inode.i_mode & EXT2_S_IFREG))
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

struct ext2_vfs_data {
    struct ext2_fs *fs;
    uint32_t ino;
};

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

    if (!(inode.i_mode & EXT2_S_IFDIR))
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

static struct file_ops ext2_file_ops = {
    .open = 0,
    .close = ext2_vfs_close,
    .read = ext2_vfs_read,
    .write = ext2_vfs_write,
    .readdir = ext2_vfs_readdir
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

    node->flags = (inode.i_mode & 0xF000) == EXT2_S_IFDIR ? S_IFDIR : S_IFREG;
    node->size = inode.i_size;
    node->ops = &ext2_file_ops;
    node->data = vfs_data;

    return node;
}

static
int
ext2_path_resolve(const char *path, struct ext2_inode *out_inode)
{
    if (!path || !out_inode || !rootfs)
        return -1;

    struct ext2_inode inode;
    if (path[0] == '/') {
        if (read_inode(rootfs, 2, &inode) != 0)
            return -1;
    } else {
        struct process *proc = process_current();
        if (!proc)
            return -1;

        if (proc->cwd[0] == '/') {
            if (read_inode(rootfs, 2, &inode) != 0)
                return -1;
            const char *p = proc->cwd + 1;
            char component[64];
            while (*p) {
                uint32_t i = 0;
                while (*p && *p != '/' && i < sizeof(component) - 1)
                    component[i++] = *p++;
                component[i] = 0;
                if (*p == '/')
                    p++;
                if (component[0] == 0)
                    continue;
                uint32_t next = lookup_in_dir(rootfs, &inode, component);
                if (next == 0)
                    return -1;
                if (read_inode(rootfs, next, &inode) != 0)
                    return -1;
            }
        } else {
            return -1;
        }
    }

    const char *p = path;
    if (path[0] == '/')
        p++;

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

        if (component[0] == '.' && component[1] == 0)
            continue;

        if (component[0] == '.' && component[1] == '.' && component[2] == 0)
        {
            uint32_t parent = lookup_in_dir(rootfs, &inode, "..");
            if (parent == 0)
                return -1;
            if (read_inode(rootfs, parent, &inode) != 0)
                return -1;
            continue;
        }

        uint32_t next = lookup_in_dir(rootfs, &inode, component);
        if (next == 0)
            return -1;
        if (read_inode(rootfs, next, &inode) != 0)
            return -1;
    }

    *out_inode = inode;
    return 0;
}

int
ext2_chdir(const char *path)
{
    if (!path)
        return -1;

    struct ext2_inode inode;
    if (ext2_path_resolve(path, &inode) != 0)
        return -1;

    if (!(inode.i_mode & EXT2_S_IFDIR))
        return -1;

    struct process *proc = process_current();
    if (!proc)
        return -1;

    if (path[0] == '/') {
        uint32_t i = 0;
        while (path[i] && i < MAX_PATH - 1)
        {
            proc->cwd[i] = path[i];
            i++;
        }
        proc->cwd[i] = 0;
    } else {
        uint32_t len = 0;
        while (proc->cwd[len])
            len++;

        if (len > 0 && proc->cwd[len - 1] != '/')
        {
            proc->cwd[len++] = '/';
        }

        uint32_t i = 0;
        while (path[i] && len < MAX_PATH - 1)
        {
            proc->cwd[len++] = path[i++];
        }
        proc->cwd[len] = 0;
    }

    return 0;
}

int
ext2_getcwd(char *buf, uint64_t size)
{
    if (!buf || size == 0)
        return -1;

    struct process *proc = process_current();
    if (!proc)
        return -1;

    uint32_t i = 0;
    while (proc->cwd[i] && i < size - 1)
    {
        buf[i] = proc->cwd[i];
        i++;
    }
    buf[i] = 0;

    return 0;
}
