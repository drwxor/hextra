/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_LIMINE_H
#define HEXTRA_LIMINE_H 1

#include <stdint.h>

#define LIMINE_PTR(type) type

#define LIMINE_COMMON_MAGIC \
    0xc7b1dd30df4c8b88, 0x0a82e883a194f07b

#define LIMINE_REQUESTS_START_MARKER \
    { \
        0xf6b8f4b39de7d1ae, \
        0xfab91a6940fcb9cf, \
        0x785c6ed015d3e316, \
        0x181e920a7852b9d9 \
    }

#define LIMINE_REQUESTS_END_MARKER \
    { \
        0xadc0e0531bb10d03, \
        0x9572709f31764c62 \
    }

#define LIMINE_BASE_REVISION(n) \
    { \
        0xf9562b2d5c95a6c8, \
        0x6a7b384944536bdc, \
        (n) \
    }

#define LIMINE_BASE_REVISION_SUPPORTED(x) \
    ((x)[2] == 0)

#define LIMINE_LOADED_BASE_REVISION_VALID(x) \
    ((x)[1] != 0x6a7b384944536bdc)

#define LIMINE_LOADED_BASE_REVISION(x) \
    ((x)[1])

#define LIMINE_HHDM_REQUEST_ID \
    { \
        LIMINE_COMMON_MAGIC, \
        0x48dcf1cb8ad2b852, \
        0x63984e959a98244b \
    }

struct limine_hhdm_response
{
    uint64_t revision;
    uint64_t offset;
};

struct limine_hhdm_request
{
    uint64_t id[4];
    uint64_t revision;
    LIMINE_PTR(struct limine_hhdm_response *) response;
};

#define LIMINE_FRAMEBUFFER_REQUEST_ID \
    { \
        LIMINE_COMMON_MAGIC, \
        0x9d5827dcd881dd75, \
        0xa3148604f6fab11b \
    }

#define LIMINE_FRAMEBUFFER_RGB 1

struct limine_video_mode
{
    uint64_t pitch;
    uint64_t width;
    uint64_t height;

    uint16_t bpp;
    uint8_t memory_model;

    uint8_t red_mask_size;
    uint8_t red_mask_shift;

    uint8_t green_mask_size;
    uint8_t green_mask_shift;

    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

struct limine_framebuffer
{
    LIMINE_PTR(void *) address;

    uint64_t width;
    uint64_t height;
    uint64_t pitch;

    uint16_t bpp;
    uint8_t memory_model;

    uint8_t red_mask_size;
    uint8_t red_mask_shift;

    uint8_t green_mask_size;
    uint8_t green_mask_shift;

    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;

    uint8_t unused[7];

    uint64_t edid_size;
    LIMINE_PTR(void *) edid;

    uint64_t mode_count;
    LIMINE_PTR(struct limine_video_mode **) modes;
};

struct limine_framebuffer_response
{
    uint64_t revision;
    uint64_t framebuffer_count;
    LIMINE_PTR(struct limine_framebuffer **) framebuffers;
};

struct limine_framebuffer_request
{
    uint64_t id[4];
    uint64_t revision;
    LIMINE_PTR(struct limine_framebuffer_response *) response;
};

// memory map

#define LIMINE_MEMMAP_REQUEST_ID \
    { \
        LIMINE_COMMON_MAGIC, \
        0x67cf3d9d378a806f, \
        0xe304acdfc50c3c62 \
    }

#define LIMINE_MEMMAP_USABLE 0
#define LIMINE_MEMMAP_RESERVED 1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE 2
#define LIMINE_MEMMAP_ACPI_NVS 3
#define LIMINE_MEMMAP_BAD_MEMORY 4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_KERNEL_AND_MODULES 6
#define LIMINE_MEMMAP_FRAMEBUFFER 7

struct limine_memmap_entry
{
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct limine_memmap_response
{
    uint64_t revision;
    uint64_t entry_count;
    LIMINE_PTR(struct limine_memmap_entry **) entries;
};

struct limine_memmap_request
{
    uint64_t id[4];
    uint64_t revision;
    LIMINE_PTR(struct limine_memmap_response *) response;
};

// modules

#define LIMINE_MODULE_REQUEST_ID \
    { \
        LIMINE_COMMON_MAGIC, \
        0x3e7e279702be32af, \
        0xca1c4f3bd1280cee \
    }

struct limine_file
{
    uint64_t revision;
    LIMINE_PTR(void *) address;
    uint64_t size;
    LIMINE_PTR(char *) path;
    LIMINE_PTR(char *) cmdline;
};

struct limine_module_response
{
    uint64_t revision;
    uint64_t module_count;
    LIMINE_PTR(struct limine_file **) modules;
};

struct limine_module_request
{
    uint64_t id[4];
    uint64_t revision;
    LIMINE_PTR(struct limine_module_response *) response;
};

// flanterm

#define LIMINE_FLANTERM_FB_INIT_PARAMS_REQUEST_ID \
    { \
        LIMINE_COMMON_MAGIC, \
        0x3259399fe7c5f126, \
        0xe01c1c8c5db9d1a9 \
    }

#define LIMINE_FLANTERM_FB_ROTATE_0   0
#define LIMINE_FLANTERM_FB_ROTATE_90  1
#define LIMINE_FLANTERM_FB_ROTATE_180 2
#define LIMINE_FLANTERM_FB_ROTATE_270 3

struct limine_flanterm_fb_init_params
{
    LIMINE_PTR(uint32_t *) canvas;
    uint64_t canvas_size;

    uint32_t ansi_colours[8];
    uint32_t ansi_bright_colours[8];

    uint32_t default_bg;
    uint32_t default_fg;
    uint32_t default_bg_bright;
    uint32_t default_fg_bright;

    LIMINE_PTR(void *) font;

    uint64_t font_width;
    uint64_t font_height;
    uint64_t font_spacing;

    uint64_t font_scale_x;
    uint64_t font_scale_y;

    uint64_t margin;
    uint64_t rotation;
};

struct limine_flanterm_fb_init_params_response
{
    uint64_t revision;
    uint64_t entry_count;
    LIMINE_PTR(struct limine_flanterm_fb_init_params **) entries;
};

struct limine_flanterm_fb_init_params_request
{
    uint64_t id[4];
    uint64_t revision;
    LIMINE_PTR(struct limine_flanterm_fb_init_params_response *) response;
};

#endif
