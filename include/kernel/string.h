/* SPDX-License-Identifier: GPL-3.0-only */

#include <stddef.h>
#include <stdint.h>

int strcmp(const char *a, const char *b);
size_t strlen(const char *s);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);