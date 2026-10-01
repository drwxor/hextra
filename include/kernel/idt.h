/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef UORIX_IDT_H
#define UORIX_IDT_H

#include <stdint.h>

struct idt_entry
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void idt_init(void);
void idt_set_gate(uint8_t vector, uint64_t handler, uint8_t flags);

void isr_syscall(void);
void isr_timer(void);

#endif
