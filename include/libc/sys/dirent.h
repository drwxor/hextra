/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_DIRENT_H
#define HEXTRA_DIRENT_H

#include <stdint.h>

#include <fcntl.h>

struct dirent {
    uint32_t d_ino;
    char d_name[256];
};

int readdir(int fd, uint32_t index, struct dirent *out);

#endif
