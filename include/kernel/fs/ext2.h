/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_EXT2_H
#define HEXTRA_EXT2_H

#include <stdint.h>

#define EXT2_START_LBA 65536
#define EXT2_SUPER_MAGIC 0xEF53
#define EXT2_S_IFMT 0xF000
#define EXT2_S_IFREG 0x8000
#define EXT2_S_IFDIR 0x4000

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

struct ext2_vfs_data {
    struct ext2_fs *fs;
    uint32_t ino;
};

struct ext2_fs
{
    uint32_t start_lba;
    uint32_t block_size;
    uint32_t inodes_per_group;
    uint32_t blocks_per_group;
    uint16_t inode_size;
    uint32_t first_data_block;
    struct ext2_superblock sb;
    uint64_t mod_base;
    uint64_t mod_size;
};

extern struct ext2_fs *rootfs;

struct ext2_fs *ext2_mount(uint32_t start_lba);
struct ext2_fs *ext2_mount_module(uint64_t mod_addr, uint64_t mod_size);
uint64_t ext2_read_file(struct ext2_fs *fs, const char *path, void **out_buf);
void ext2_unmount(struct ext2_fs *fs);
struct vfs_node *ext2_vfs_node(struct ext2_fs *fs, uint32_t ino);

#endif
