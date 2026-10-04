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

#define MARGIN 32
#define MAX_COLS 256
#define MAX_ROWS 128
#define TAB_WIDTH 8

static char cells_ch[MAX_ROWS][MAX_COLS];
static uint32_t cells_fg[MAX_ROWS][MAX_COLS];
static char shown_ch[MAX_ROWS][MAX_COLS];
static uint32_t shown_fg[MAX_ROWS][MAX_COLS];

static uint64_t cols;
static uint64_t rows;
static uint64_t cell_w;
static uint64_t cell_h;

static uint64_t scroll_top;
static uint64_t cursor_col;
static uint64_t cursor_row;
static int needs_repaint;

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
paint_cell(uint64_t screen_row, uint64_t col, char c, uint32_t color)
{
    uint64_t x0 = MARGIN + col * cell_w;
    uint64_t y0 = MARGIN + screen_row * cell_h;
    const uint8_t *g = (c != ' ' && c != 0) ? glyph(c) : 0;

    uint64_t width = font_width;
    if (width > 8)
        width = 8;

    for (uint64_t row = 0; row < cell_h; row++)
    {
        uint32_t *pixel =
            (uint32_t *)(
                (uint8_t *)framebuffer->address +
                (y0 + row) * framebuffer->pitch
            ) + x0;

        uint8_t bits = (g && row < font_height) ? g[row] : 0;

        for (uint64_t col_px = 0; col_px < cell_w; col_px++)
        {
            int on = col_px < width && (bits & (1u << (7 - col_px)));
            pixel[col_px] = on ? color : background;
        }
    }

    shown_ch[screen_row][col] = c;
    shown_fg[screen_row][col] = color;
}

static uint64_t
ring_row(uint64_t logical_row)
{
    return (scroll_top + logical_row) % rows;
}

static void
put_cell(uint64_t logical_row, uint64_t col, char c, uint32_t color)
{
    uint64_t r = ring_row(logical_row);
    cells_ch[r][col] = c;
    cells_fg[r][col] = color;

    if (!needs_repaint)
    {
        if (shown_ch[logical_row][col] != c || (c != ' ' && shown_fg[logical_row][col] != color))
            paint_cell(logical_row, col, c, color);
    }
}

static void
newline(void)
{
    cursor_col = 0;

    if (cursor_row + 1 < rows)
    {
        cursor_row++;
        return;
    }

    uint64_t r = scroll_top;
    for (uint64_t c = 0; c < cols; c++)
    {
        cells_ch[r][c] = ' ';
        cells_fg[r][c] = foreground;
    }
    scroll_top = (scroll_top + 1) % rows;
    needs_repaint = 1;
}

void
render_flush(void)
{
    if (!needs_repaint || framebuffer == 0 || font == 0)
        return;

    needs_repaint = 0;

    for (uint64_t y = 0; y < rows; y++)
    {
        uint64_t r = ring_row(y);
        for (uint64_t c = 0; c < cols; c++)
        {
            char ch = cells_ch[r][c];
            uint32_t fg = cells_fg[r][c];

            if (shown_ch[y][c] != ch || (ch != ' ' && shown_fg[y][c] != fg))
                paint_cell(y, c, ch, fg);
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

    cols = 0;
    rows = 0;

    if (framebuffer && font)
    {
        cell_w = font_width + font_spacing;
        cell_h = font_height + font_spacing;

        if (framebuffer->width > 2 * MARGIN)
            cols = (framebuffer->width - 2 * MARGIN) / cell_w;
        if (framebuffer->height > 2 * MARGIN)
            rows = (framebuffer->height - 2 * MARGIN) / cell_h;

        if (cols > MAX_COLS)
            cols = MAX_COLS;
        if (rows > MAX_ROWS)
            rows = MAX_ROWS;
    }

    for (uint64_t r = 0; r < MAX_ROWS; r++)
    {
        for (uint64_t c = 0; c < MAX_COLS; c++)
        {
            cells_ch[r][c] = ' ';
            cells_fg[r][c] = foreground;
            shown_ch[r][c] = ' ';
            shown_fg[r][c] = foreground;
        }
    }

    scroll_top = 0;
    cursor_col = 0;
    cursor_row = 0;
    needs_repaint = 0;
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

    for (uint64_t r = 0; r < rows; r++)
    {
        for (uint64_t c = 0; c < cols; c++)
        {
            cells_ch[r][c] = ' ';
            cells_fg[r][c] = foreground;
            shown_ch[r][c] = ' ';
            shown_fg[r][c] = foreground;
        }
    }

    scroll_top = 0;
    cursor_col = 0;
    cursor_row = 0;
    needs_repaint = 0;
}

void
render_putc(char c, uint32_t color)
{
    serial_putc(c);

    if (framebuffer == 0 || font == 0 || rows == 0 || cols == 0)
        return;

    if (c == '\n')
    {
        newline();
        return;
    }

    if (c == '\r')
    {
        cursor_col = 0;
        return;
    }

    if (c == '\t')
    {
        uint64_t next = (cursor_col / TAB_WIDTH + 1) * TAB_WIDTH;
        if (next >= cols)
            newline();
        else
            cursor_col = next;
        return;
    }

    if (c == '\b')
    {
        if (cursor_col > 0)
        {
            cursor_col--;
            put_cell(cursor_row, cursor_col, ' ', color);
        }
        return;
    }

    if ((uint8_t)c < 32)
        return;

    put_cell(cursor_row, cursor_col, c, color);

    if (++cursor_col >= cols)
        newline();
}

void
render_puts(const char *s, uint32_t color)
{
    if (s == 0)
        return;

    while (*s)
        render_putc(*s++, color);

    render_flush();
}

static int print_screen = 1;

static void
out_putc(char c, uint32_t color)
{
    if (print_screen)
        render_putc(c, color);
    else
        serial_putc(c);
}

static void
out_puts(const char *s, uint32_t color)
{
    if (s == 0)
        s = "(null)";

    while (*s)
        out_putc(*s++, color);
}

static void
out_uint(uint64_t value, uint32_t base, uint32_t color)
{
    char buffer[32];
    uint32_t length = 0;

    if (value == 0)
    {
        out_putc('0', color);
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
        out_putc(buffer[--length], color);
}

static void
render_vprintf(const char *fmt, uint32_t color, va_list args)
{
    while (*fmt != '\0')
    {
        if (*fmt != '%')
        {
            out_putc(*fmt, color);
            fmt++;
            continue;
        }

        fmt++;

        switch (*fmt)
        {
            case '%':
                out_putc('%', color);
                break;

            case 'c':
                out_putc((char)va_arg(args, int), color);
                break;

            case 's':
                out_puts(va_arg(args, const char *), color);
                break;

            case 'u':
                out_uint(va_arg(args, uint64_t), 10, color);
                break;

            case 'x':
                out_uint(va_arg(args, uint64_t), 16, color);
                break;

            case 'd':
            {
                int64_t value = va_arg(args, int64_t);
                if (value < 0)
                {
                    out_putc('-', color);
                    out_uint((uint64_t)(-value), 10, color);
                }
                else
                {
                    out_uint((uint64_t)value, 10, color);
                }
                break;
            }

            default:
                out_putc('%', color);
                out_putc(*fmt, color);
                break;
        }

        if (*fmt)
            fmt++;
    }

    if (print_screen)
        render_flush();
}

void
render_printf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    render_vprintf(fmt, WHITE_COLOR, args);
    va_end(args);
}

void
render_printf_colored(const char *fmt, uint32_t color, ...)
{
    va_list args;

    va_start(args, color);
    render_vprintf(fmt, color, args);
    va_end(args);
}

void
debug_printf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    print_screen = 0;
    render_vprintf(fmt, WHITE_COLOR, args);
    print_screen = 1;
    va_end(args);
}
