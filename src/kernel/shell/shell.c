/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/shell/shell.h"

#include "kernel/user/user.h"
#include "kernel/fs/vfs.h"
#include "kernel/fs/ext2.h"

#include "kernel/shell/commands/help.h"
#include "kernel/shell/commands/exit.h"
#include "kernel/shell/commands/mem.h"

#include <stdint.h>

#define LINE_SIZE 128

static char line[LINE_SIZE];
static uint64_t line_length;

static
int
strcmp_local(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static
void
line_clear(void)
{
    line_length = 0;
    line[0] = '\0';
}

static
void
shell_command_ls(void)
{
    int fd = vfs_open("/", O_RDONLY);
    if (fd < 0)
    {
        user_puts("ls: cannot open directory\n");
        return;
    }

    struct dirent de;
    uint32_t index = 0;

    while (vfs_readdir(fd, index, &de) == 0)
    {
        user_puts(de.d_name);
        user_putc('\n');
        index++;
    }

    vfs_close(fd);
}

static
void
shell_command_cd(const char *path)
{
    if (!rootfs)
    {
        user_puts("cd: no filesystem mounted\n");
        return;
    }

    if (ext2_chdir(path) != 0)
    {
        user_puts("cd: cannot change directory: ");
        user_puts(path);
        user_putc('\n');
    }
}

static
void
shell_execute(void)
{
    if (line_length == 0)
        return;

    if (strcmp_local(line, "help") == 0)
    {
        shell_command_help();
        return;
    }

    if (strcmp_local(line, "exit") == 0)
    {
        shell_command_exit();
        return;
    }

    if (strcmp_local(line, "mem") == 0)
    {
        shell_command_mem();
        return;
    }

    if (strcmp_local(line, "ls") == 0)
    {
        shell_command_ls();
        return;
    }

    if (line_length >= 3 &&
        line[0] == 'c' &&
        line[1] == 'd' &&
        line[2] == ' ')
    {
        shell_command_cd(line + 3);
        return;
    }

    if (line_length >= 5 &&
        line[0] == 'e' &&
        line[1] == 'c' &&
        line[2] == 'h' &&
        line[3] == 'o' &&
        line[4] == ' ')
    {
        user_puts(line + 5);
        user_putc('\n');
        return;
    }

    if (line_length >= 5 &&
        line[0] == 'p' &&
        line[1] == 'r' &&
        line[2] == 'i' &&
        line[3] == 'n' &&
        line[4] == 't' &&
        line[5] == 'f' &&
        line[6] == ' ')
    {
        user_puts(line + 7);
        return;
    }

    user_puts("uorix: command not found: ");
    user_puts(line);
    user_putc('\n');
}

void
shell_init(void)
{
    line_clear();
}

void
shell_run(void)
{
    user_puts("# ");

    for (;;)
    {
        char c = user_getc();

        if (c == '\n')
        {
            user_putc('\n');
            shell_execute();
            line_clear();
            user_puts("# ");
            continue;
        }

        if (c == '\b')
        {
            if (line_length > 0) {
                line_length--;
                line[line_length] = '\0';
                user_putc('\b');
            }
            continue;
        }

        if ((uint8_t)c < 32)
            continue;

        if (line_length >= LINE_SIZE - 1)
            continue;

        line[line_length++] = c;
        line[line_length] = '\0';
        user_putc(c);
    }
}
