/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/fs/gpt.h"
#include "kernel/ata.h"
#include "kernel/heap.h"
#include "kernel/renderer.h"

#include <stdint.h>
#include <stddef.h>

#define GPT_SIGNATURE "EFI PART"

static const uint8_t linux_fs_guid[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};

struct gpt_header
{
    char signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t partition_entry_size;
    uint32_t partition_entry_crc32;
} __attribute__((packed));

struct gpt_entry
{
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint8_t name[72];
} __attribute__((packed));

static int
guid_equal(const uint8_t *a, const uint8_t *b)
{
    for (int i = 0; i < 16; i++)
    {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

uint32_t
gpt_find_ext2(void)
{
    uint8_t sector[512];

    if (ata_read_sectors(1, 1, sector) != 0)
    {
        render_printf("gpt: failed to read header\n");
        return 0;
    }

    struct gpt_header *hdr = (struct gpt_header *)sector;

    for (int i = 0; i < 8; i++)
    {
        if (hdr->signature[i] != GPT_SIGNATURE[i])
        {
            render_printf("gpt: bad signature\n");
            return 0;
        }
    }

    render_printf("gpt: found, %u entries of %u bytes at LBA %u\n", hdr->num_partition_entries, hdr->partition_entry_size, (uint32_t)hdr->partition_entry_lba);

    uint32_t entries_bytes = hdr->num_partition_entries * hdr->partition_entry_size;
    uint32_t entries_sectors = (entries_bytes + 511) / 512;
    if (entries_sectors > 255)
        entries_sectors = 255;

    uint8_t *entries = kmalloc(entries_sectors * 512);
    if (!entries)
    {
        render_printf("gpt: out of memory\n");
        return 0;
    }

    if (ata_read_sectors((uint32_t)hdr->partition_entry_lba, (uint8_t)entries_sectors, entries) != 0)
    {
        render_printf("gpt: failed to read entries\n");
        kfree(entries);
        return 0;
    }

    for (uint32_t i = 0; i < hdr->num_partition_entries; i++)
    {
        struct gpt_entry *entry = (struct gpt_entry *)(entries + i * hdr->partition_entry_size);

        if (guid_equal(entry->type_guid, linux_fs_guid))
        {
            uint32_t first_lba = (uint32_t)entry->first_lba;
            render_printf("gpt: found ext2 partition at LBA %u\n", first_lba);
            kfree(entries);
            return first_lba;
        }
    }

    render_printf("gpt: no ext2 partition found\n");
    kfree(entries);
    return 0;
}
