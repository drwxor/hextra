/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_SHELL_MEM_H
#define HEXTRA_SHELL_MEM_H

#include "kernel/user/user.h"
#include "kernel/syscall.h"

#define SYS_MEMINFO 3

static
void
shell_command_mem(void)
{
    long free_pages = syscall0(SYS_MEMINFO);
    char buf[32];
    int i = 0;
    unsigned long n = (unsigned long)free_pages;

    if (n == 0)
    {
        buf[i++] = '0';
    }
    else
    {
        char tmp[20];
        int t = 0;
        while (n > 0)
        {
            tmp[t++] = '0' + (n % 10);
            n /= 10;
        }
        while (t > 0)
            buf[i++] = tmp[--t];
    }
    buf[i] = '\0';

    user_puts("free pages: ");
    user_puts(buf);
    user_putc('\n');
}

#endif
