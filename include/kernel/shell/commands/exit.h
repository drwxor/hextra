/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_SHELL_EXIT_H
#define HEXTRA_SHELL_EXIT_H

#include "kernel/user/user.h"
#include "kernel/syscall.h"

static
void
shell_command_exit(void)
{
    user_puts("exiting\n");
    syscall0(SYS_EXIT);
}

#endif
