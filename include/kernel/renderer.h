/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_RENDERER_H
#define HEXTRA_RENDERER_H

#include <stdint.h>

#include "kernel/limine.h"

#define WHITE_COLOR 0x00FFFFFFu
#define RED_COLOR 0x00FF0000u
#define GREEN_COLOR 0x0000FF00u
#define CYAN_COLOR 0x0000FFFFu

void renderer_init(struct limine_framebuffer *fb, struct limine_flanterm_fb_init_params *font_params);

void render_clear(uint32_t color);

void render_putc(char c, uint32_t color);
void render_puts(const char *s, uint32_t color);

void render_printf(const char *fmt, ...);
void render_printf_colored(const char *fmt, uint32_t color, ...);

#endif
