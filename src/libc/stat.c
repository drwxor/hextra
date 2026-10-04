/* SPDX-License-Identifier: GPL-3.0-only */

#include <sys/stat.h>
#include <sys/syscall.h>
#include <errno.h>

int
stat(const char *path, struct stat *buf)
{
    if (path == 0 || buf == 0)
    {
        errno = EFAULT;
        return -1;
    }

    if (syscall2(SYS_STAT, (long)path, (long)buf) != 0)
    {
        errno = ENOENT;
        return -1;
    }

    return 0;
}

int
fstat(int fd, struct stat *buf)
{
    if (buf == 0)
    {
        errno = EFAULT;
        return -1;
    }

    if (syscall2(SYS_FSTAT, (long)fd, (long)buf) != 0)
    {
        errno = EBADF;
        return -1;
    }

    return 0;
}
