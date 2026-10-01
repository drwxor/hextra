/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/pit.h"
#include "kernel/renderer.h"
#include "kernel/io.h"

#include <stdint.h>

#define PIT_FREQUENCY 1193182
#define PIT_TICK_RATE 100
#define PIT_DIVISOR (PIT_FREQUENCY / PIT_TICK_RATE)

void
pit_init(void)
{
    outb(0x43, 0x36);
    outb(0x40, PIT_DIVISOR & 0xFF);
    outb(0x40, (PIT_DIVISOR >> 8) & 0xFF);
}