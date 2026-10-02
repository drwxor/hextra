/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/renderer.h"

#include <stdarg.h>
#include <stdint.h>

#include "kernel/io.h"
#include "kernel/limine.h"

#define COM1 0x3F8

static struct limine_framebuffer *framebuffer;

static const uint8_t *font;
static uint64_t font_width;
static uint64_t font_height;
static uint64_t font_spacing;

static uint64_t cursor_x;
static uint64_t cursor_y;

static uint32_t foreground = 0x00FFFFFF;
static uint32_t background = 0x00000000;

static void
serial_putc(char c)
{
    while (!(inb(COM1 + 5) & 0x20))
        ;
    outb(COM1, c);
}

static const uint8_t *
glyph(char c)
{
    if (font == 0 || font_height == 0)
        return 0;

    return font + ((uint8_t)c * font_height);
}

static void
putpixel(uint64_t x, uint64_t y, uint32_t color)
{
    if (framebuffer == 0)
        return;

    if (x >= framebuffer->width || y >= framebuffer->height)
        return;

    uint32_t *pixel =
        (uint32_t *)(
            (uint8_t *)framebuffer->address +
            y * framebuffer->pitch
        );

    pixel[x] = color;
}

static void
draw_glyph(const uint8_t *g, uint64_t x, uint64_t y, uint32_t color)
{
    if (g == 0)
        return;

    uint64_t width = font_width;

    if (width > 8)
        width = 8;

    for (uint64_t row = 0; row < font_height; row++)
    {
        for (uint64_t col = 0; col < width; col++)
        {
            if (g[row] & (1u << (7 - col)))
                putpixel(x + col, y + row, color);
        }
    }
}

void
renderer_init(struct limine_framebuffer *fb,struct limine_flanterm_fb_init_params *font_params)
{
    framebuffer = fb;

    font = 0;
    font_width = 0;
    font_height = 0;
    font_spacing = 0;

    if (font_params != 0 && font_params->font != 0)
    {
        font = (const uint8_t *)font_params->font;
        font_width = font_params->font_width;
        font_height = font_params->font_height;
        font_spacing = font_params->font_spacing;
    }

    cursor_x = 32;
    cursor_y = 32;
}

void
render_clear(uint32_t color)
{
    if (framebuffer == 0)
        return;

    background = color;

    for (uint64_t y = 0; y < framebuffer->height; y++)
    {
        uint32_t *row =
            (uint32_t *)(
                (uint8_t *)framebuffer->address +
                y * framebuffer->pitch
            );

        for (uint64_t x = 0; x < framebuffer->width; x++)
            row[x] = color;
    }

    cursor_x = 32;
    cursor_y = 32;
}

void
render_putc(char c, uint32_t color)
{
    serial_putc(c);

    if (framebuffer == 0 || font == 0)
        return;

    if (c == '\n')
    {
        cursor_x = 32;
        cursor_y += font_height + font_spacing;
        return;
    }

    if (c == '\r')
    {
        cursor_x = 32;
        return;
    }

    if (c == '\t')
    {
        cursor_x += 32;
        return;
    }

    if (c == '\b')
    {
        if (cursor_x >= 32 + font_width + font_spacing)
        {
            cursor_x -= font_width + font_spacing;

            for (uint64_t row = 0; row < font_height; row++)
            {
                for (uint64_t col = 0;
                     col < font_width + font_spacing;
                     col++)
                {
                    putpixel(
                        cursor_x + col,
                        cursor_y + row,
                        background
                    );
                }
            }
        }

        return;
    }

    draw_glyph(glyph(c), cursor_x, cursor_y, color);

    cursor_x += font_width + font_spacing;

    if (cursor_x + font_width >= framebuffer->width)
    {
        cursor_x = 32;
        cursor_y += font_height + font_spacing;
    }

    if (cursor_y + font_height >= framebuffer->height)
        cursor_y = 32;
}

void
render_puts(const char *s, uint32_t color)
{
    if (s == 0)
        return;

    while (*s)
        render_putc(*s++, color);
}

static void
render_uint(uint64_t value, uint32_t base, uint32_t color)
{
    char buffer[32];
    uint32_t length = 0;

    if (value == 0)
    {
        render_putc('0', color);
        return;
    }

    while (value != 0)
    {
        uint32_t digit = value % base;

        if (digit < 10)
            buffer[length++] = '0' + digit;
        else
            buffer[length++] = 'a' + digit - 10;

        value /= base;
    }

    while (length != 0)
        render_putc(buffer[--length], color);
}

static void
render_int(int64_t value, uint32_t color)
{
    if (value < 0)
    {
        render_putc('-', color);
        render_uint((uint64_t)(-value), 10, color);
    }
    else
    {
        render_uint((uint64_t)value, 10, color);
    }
}

void
render_printf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);

    while (*fmt != '\0')
    {
        if (*fmt != '%')
        {
            render_putc(*fmt, WHITE_COLOR);
            fmt++;
            continue;
        }

        fmt++;

        switch (*fmt)
        {
            case '%':
                render_putc('%', WHITE_COLOR);
                break;

            case 'c':
                render_putc((char)va_arg(args, int), WHITE_COLOR);
                break;

            case 's':
                render_puts(va_arg(args, const char *), WHITE_COLOR);
                break;

            case 'u':
                render_uint(va_arg(args, uint64_t), 10, WHITE_COLOR);
                break;

            case 'x':
                render_uint(va_arg(args, uint64_t), 16, WHITE_COLOR);
                break;

            case 'd':
                render_int(va_arg(args, int64_t), WHITE_COLOR);
                break;

            default:
                render_putc('%', WHITE_COLOR);
                render_putc(*fmt, WHITE_COLOR);
                break;
        }

        fmt++;
    }

    va_end(args);
}

void
render_printf_colored(const char *fmt, uint32_t color, ...)
{
    va_list args;

    va_start(args, color);

    while (*fmt != '\0')
    {
        if (*fmt != '%')
        {
            render_putc(*fmt, color);
            fmt++;
            continue;
        }

        fmt++;

        switch (*fmt)
        {
            case '%':
                render_putc('%', color);
                break;

            case 'c':
                render_putc((char)va_arg(args, int), color);
                break;

            case 's':
                render_puts(va_arg(args, const char *), color);
                break;

            case 'u':
                render_uint(va_arg(args, uint64_t), 10, color);
                break;

            case 'x':
                render_uint(va_arg(args, uint64_t), 16, color);
                break;

            case 'd':
                render_int(va_arg(args, int64_t), color);
                break;

            default:
                render_putc('%', color);
                render_putc(*fmt, color);
                break;
        }

        fmt++;
    }

    va_end(args);
}
