/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef UORIX_DIRENT_H
#define UORIX_DIRENT_H

#include <stdint.h>

#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0040

struct dirent {
    uint32_t d_ino;
    char d_name[256];
};

#endif
