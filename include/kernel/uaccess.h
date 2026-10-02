/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_UACCESS_H
#define HEXTRA_UACCESS_H

#include <stdint.h>
#include <stddef.h>

int copyin(void *dst, const void *user_src, size_t len);
int copyout(void *user_dst, const void *src, size_t len);

int copyin_str(char *dst, size_t dst_size, const char *user_src);
int copyout_str(char *user_dst, size_t dst_size, const char *src);

#endif
