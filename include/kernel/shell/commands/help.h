/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef UORIX_SHELL_HELP_H
#define UORIX_SHELL_HELP_H

#include "kernel/user/user.h"

static
void
shell_command_help(void)
{
    user_puts("commands:\n");
    user_puts("  help     show this message\n");
    user_puts("  echo     print text\n");
    user_puts("  printf   print text without making new line\n");
    user_puts("  exit     leave the shell\n");
    user_puts("  mem      show free physical pages\n");
    user_puts("  ls       list directory contents\n");
    user_puts("  cd       change directory\n");
}

#endif
