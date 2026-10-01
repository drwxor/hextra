/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/idt.h"
#include "kernel/gdt.h"

static struct idt_entry idt[256];
static struct idt_ptr idtr;

void
idt_set_gate(uint8_t vector, uint64_t handler, uint8_t flags)
{
    idt[vector].offset_low = handler & 0xFFFF;
    idt[vector].selector = KERNEL_CS;
    idt[vector].ist = 0;
    idt[vector].type_attr = flags;
    idt[vector].offset_mid = (handler >> 16) & 0xFFFF;
    idt[vector].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[vector].zero = 0;
}

extern void isr_null(void);

void
idt_init(void)
{
    for (int i = 0; i < 256; i++)
        idt_set_gate(i, (uint64_t)isr_null, 0x8E);
    idt_set_gate(0x20, (uint64_t)isr_timer, 0x8E);
    idt_set_gate(0x80, (uint64_t)isr_syscall, 0xEE);

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)&idt;

    __asm__ volatile (
        "lidt %0"
        :
        : "m"(idtr)
        : "memory"
    );
}
