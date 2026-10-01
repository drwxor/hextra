/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_GDT_H
#define HEXTRA_GDT_H

#include <stdint.h>

#define KERNEL_CS   0x08
#define KERNEL_DS   0x10
#define USER_CS     0x1B
#define USER_DS     0x23
#define TSS_SEL     0x28

struct gdt_entry
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_tss_entry
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_ptr
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss
{
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);

#endif
