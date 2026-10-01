/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_HEAP_H
#define HEXTRA_HEAP_H

#include <stdint.h>

typedef uint64_t size_t;

void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);

#endif
