/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/driver/keyboard.h"
#include "kernel/renderer.h"

#include <stdint.h>

static inline
uint8_t
inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static
int
keyboard_ready(void)
{
    return inb(0x64) & 1;
}

static
uint8_t
keyboard_read_scancode(void)
{
    while (!keyboard_ready())
        ;

    return inb(0x60);
}

static const char scancode_ascii[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x0F] = '\t', [0x10] = 'q', [0x11] = 'w', [0x12] = 'e',
    [0x13] = 'r', [0x14] = 't', [0x15] = 'y', [0x16] = 'u',
    [0x17] = 'i', [0x18] = 'o', [0x19] = 'p', [0x1A] = '[',
    [0x1B] = ']', [0x1C] = '\n', [0x0E] = '\b', [0x1E] = 'a',
    [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`', [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/', [0x39] = ' '
};

static const char scancode_shift[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
    [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C',
    [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M',
    [0x33] = '<', [0x34] = '>', [0x35] = '?'
};

void
keyboard_init(void)
{
    while (inb(0x64) & 1)
        inb(0x60);

    render_printf("keyboard "); render_printf_colored("[OK]\n", GREEN_COLOR);
}

char
keyboard_getc(void)
{
    static int shift = 0;

    for (;;)
    {
        uint8_t scancode = keyboard_read_scancode();

        if (scancode == 0x2A || scancode == 0x36)
        {
            shift = 1;
            continue;
        }

        if (scancode == 0xAA || scancode == 0xB6)
        {
            shift = 0;
            continue;
        }

        if (scancode & 0x80)
            continue;

        if (scancode >= 128)
            continue;

        char c;

        if (shift)
            c = scancode_shift[scancode];
        else
            c = scancode_ascii[scancode];

        if (c != 0)
            return c;
    }
}
