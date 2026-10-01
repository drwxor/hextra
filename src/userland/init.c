/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <sys/syscall.h>

int
main(void)
{
    printf("userspace "); printf_colored("[OK]\n", GREEN_COLOR);

    printf_colored("welcome to hextra!\n", CYAN_COLOR);

    printf_colored("entering the shell...\n", WHITE_COLOR);

    int rc = exec("/bin/sh");

    printf("exec /bin/sh failed: rc=%d\n", rc);
    printf("errno=%d\n", errno);

    _exit(1);

    return 0;
}
