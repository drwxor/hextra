/* SPDX-License-Identifier: GPL-3.0-only */

#include <sys/dirent.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <stddef.h>

int
readdir(int fd, uint32_t index, struct dirent *out)
{
    return (int)syscall3(SYS_READDIR, fd, (long)index, (long)out);
}

int
chdir(const char *path)
{
    return (int)syscall1(SYS_CHDIR, (long)path);
}

char *
getcwd(char *buf, size_t size)
{
    if (syscall2(SYS_GETCWD, (long)buf, (long)size) != 0)
        return 0;
    return buf;
}
