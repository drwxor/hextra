/* SPDX-License-Identifier: GPL-3.0-only */

#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>

ssize_t
read(int fd, void *buf, size_t count)
{
    (void)fd;
    if (buf == 0)
    {
        errno = EFAULT;
        return -1;
    }
    return (ssize_t)syscall2(SYS_READ, (long)buf, (long)count);
}

ssize_t
write(int fd, const void *buf, size_t count)
{
    (void)fd;
    if (buf == 0)
    {
        errno = EFAULT;
        return -1;
    }
    return (ssize_t)syscall3(SYS_WRITE, (long)buf, (long)count, 0x00FFFFFFu);
}

ssize_t
write_colored(int fd, const void *buf, size_t count, uint32_t color)
{
    (void)fd;
    if (buf == 0)
    {
        errno = EFAULT;
        return -1;
    }
    return (ssize_t)syscall3(SYS_WRITE, (long)buf, (long)count, color);
}

int
exec(const char *path)
{
    long ret = syscall1(SYS_EXEC, (long)path);
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
spawn(const char *path)
{
    long ret = syscall1(SYS_SPAWN, (long)path);

    if (ret < 0)
    {
        errno = EINVAL;
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
