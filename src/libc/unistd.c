/* SPDX-License-Identifier: GPL-3.0-only */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/syscall.h>

int
open(const char *path, int flags)
{
    if (path == 0)
    {
        errno = EFAULT;
        return -1;
    }

    long ret = syscall2(SYS_OPEN, (long)path, (long)flags);
    if (ret < 0)
    {
        errno = ENOENT;
        return -1;
    }

    return (int)ret;
}

int
close(int fd)
{
    if (syscall1(SYS_CLOSE, (long)fd) != 0)
    {
        errno = EBADF;
        return -1;
    }

    return 0;
}

ssize_t
read(int fd, void *buf, size_t count)
{
    if (buf == 0)
    {
        errno = EFAULT;
        return -1;
    }
    return (ssize_t)syscall3(SYS_READ, (long)fd, (long)buf, (long)count);
}

ssize_t
write(int fd, const void *buf, size_t count)
{
    if (buf == 0)
    {
        errno = EFAULT;
        return -1;
    }

    long ret = syscall3(SYS_WRITE, (long)fd, (long)buf, (long)count);

    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }

    return (ssize_t)ret;
}

ssize_t
write_colored(int fd, const void *buf, size_t count, uint32_t color)
{
    if (buf == 0)
    {
        errno = EFAULT;
        return -1;
    }
    return (ssize_t)syscall4(SYS_WRITE, (long)fd, (long)buf, (long)count, (long)color);
}

int
exec(const char *path)
{
    return execve(path, 0, 0);
}

int
execve(const char *path, char *const argv[], char *const envp[])
{
    long ret = syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp);
    if (ret < 0)
    {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

void
_exit(int status)
{
    syscall1(SYS_EXIT, status);
    for (;;)
        __asm__ volatile ("hlt");
}

int
brk(void *addr)
{
    if (addr == 0)
        return 0;

    long ret = syscall1(SYS_BRK, (long)addr);
    if (ret != (long)addr)
    {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

void *
sbrk(intptr_t increment)
{
    long cur = syscall1(SYS_BRK, 0);
    if (increment == 0)
        return (void *)cur;

    long next = cur + increment;
    long ret  = syscall1(SYS_BRK, next);
    if (ret != next)
    {
        errno = ENOMEM;
        return (void *)-1;
    }
    return (void *)cur;
}

int
isatty(int fd)
{
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}

pid_t
getpid(void)
{
    return (pid_t)syscall0(SYS_GETPID);
}

unsigned int
sleep(unsigned int seconds)
{
    (void)seconds;
    return 0;
}

pid_t
fork(void)
{
    long ret = syscall0(SYS_FORK);

    if (ret < 0)
    {
        errno = EAGAIN;
        return -1;
    }

    return (pid_t)ret;
}

int
wait(pid_t pid)
{
    long ret = syscall1(SYS_WAIT, (long)pid);

    if (ret < 0)
    {
        errno = EINVAL;
        return -1;
    }

    return (int)ret;
}

void
yield(void)
{
    syscall0(SYS_YIELD);
}

void
kdebug(const char *msg, size_t len)
{
    syscall2(SYS_KDEBUG, (long)msg, (long)len);
}
