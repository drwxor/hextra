/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <sys/syscall.h>

static
void
read_line(char *buf, size_t size)
{
    size_t len = 0;
    while (len < size - 1)
    {
        int c = getchar();
        if (c == EOF || c < 0)
            continue;

        if (c == '\n' || c == '\r')
        {
            putchar('\n');
            break;
        }

        if (c == '\b' || c == 127)
        {
            if (len > 0)
            {
                len--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
            continue;
        }

        if (c >= 32 && c < 127)
        {
            buf[len++] = (char)c;
            putchar(c);
        }
    }
    buf[len] = '\0';
}

int
main(void)
{
    printf("userspace "); printf_colored("[OK]\n", GREEN_COLOR);

    printf_colored("welcome to hextra!\n", CYAN_COLOR);

    printf_colored("entering the shell...\n", WHITE_COLOR);

    char *argv[] = { "/bin/sh", 0 };
    int rc = execve("/bin/sh", argv, 0);

    if (rc != 0)
    {
        printf("exec /bin/sh failed: rc=%d, errno=%d\n", rc, errno);
    }

    char line[128];
    while (1)
    {
        printf("$ ");
        read_line(line, sizeof(line));

        if (strcmp(line, "exit") == 0)
            break;

        pid_t pid = fork();
        if (pid == 0)
        {
            char *cmd_argv[] = { line, 0 };
            execve(line, cmd_argv, 0);
            _exit(1);
        }
        else if (pid > 0)
        {
            wait(pid);
        }
    }

    return 0;
}
