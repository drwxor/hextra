/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef _FCNTL_H
#define _FCNTL_H

#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0040

int open(const char *path, int flags);

#endif
