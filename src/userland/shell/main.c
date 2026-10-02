/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/dirent.h>

static const char *logo[] = {
    "H EEE X XTTTRRA A",
    "H E    X  TR RA A",
    "HHEE   X  TRR AAA",
    "H E    X  TR RA A",
    "H EEE X X TR RA A",
    0
};

static const char *prompt = "$ ";

static
void
cmd_help(void)
{
    printf("commands:\n");
    printf("\thelp     show this message\n");
    printf("\tclear    clear the screen\n");
    printf("\techo     print text\n");
    printf("\tprintf   print text without making new line\n");
    printf("\trun      run binary by absolute path\n");
    printf("\texec     replace shell with binary\n");
    printf("\tuname    show system name\n");
    printf("\tfetch    fetch current system status\n");
    printf("\tmem      show free physical pages\n");
    printf("\tmalloc   exercise libc malloc\n");
    printf("\tls       list directory contents\n");
    printf("\tcd       change directory\n");
    printf("\texit     leave the shell\n");
}

static
void
cmd_mem(void)
{
    long pages = syscall0(SYS_MEMINFO);
    printf("free pages: %d\n", (int)pages);
}

static
void
cmd_ls(void)
{
    int fd = syscall2(SYS_OPEN, (long)"/", O_RDONLY);
    if (fd < 0)
    {
        printf("ls: cannot open directory\n");
        return;
    }

    struct dirent de;
    uint32_t index = 0;

    while (syscall3(SYS_READDIR, fd, index, (long)&de) == 0)
    {
        printf("%s\n", de.d_name);
        index++;
    }

    syscall1(SYS_CLOSE, fd);
}

static
void
cmd_cd(const char *path)
{
    if (syscall1(SYS_CHDIR, (long)path) != 0)
    {
        printf("cd: cannot change directory: %s\n", path);
    }
}

static
void
cmd_malloc(void)
{
    char *p = malloc(64);
    if (!p)
    {
        printf("malloc failed\n");
        return;
    }
    strcpy(p, "heap ok");
    printf("malloc: %s at %p\n", p, (void *)p);
    free(p);
}

static
void
cmd_fetch(void)
{
    printf("\n");

    for (int i = 0; logo[i] != 0; i++)
    {
        printf(logo[i]);

        if (i == 0)
            printf("\thextra x86_64");

        if (i == 2)
            printf("\tkernel: hextra");

        if (i == 3)
            printf("\tshell: hextra shell");

        if (i == 4)
            printf("\tbootloader: limine");

        printf("\n");
    }

    printf("\n");
}

int
main(void)
{
    char line[128];
    int len;

    printf(prompt);

    len = 0;
    line[0] = 0;

    for (;;)
    {
        int c = getchar();
        if (c == EOF)
            continue;

        if (c == '\n')
        {
            putchar('\n');
            line[len] = 0;

            if (len == 0)
            {
                printf(prompt);
                continue;
            }

            if (strcmp(line, "help") == 0)
                cmd_help();
            else if (strcmp(line, "clear") == 0)
                syscall0(SYS_CLEAR);
            else if (strcmp(line, "uname") == 0)
                printf("hextra x86_64\n");
            else if (strcmp(line, "mem") == 0)
                cmd_mem();
            else if (strcmp(line, "malloc") == 0)
                cmd_malloc();
            else if (strcmp(line, "fetch") == 0)
                cmd_fetch();
            else if (strcmp(line, "ls") == 0)
                cmd_ls();
            else if (strcmp(line, "exit") == 0)
            {
                printf("bye\n");
                return 0;
            }
            else if (strncmp(line, "echo ", 5) == 0)
                printf("%s\n", line + 5);
            else if (strncmp(line, "printf ", 5) == 0)
                printf("%s", line + 7);
            else if (strncmp(line, "run ", 4) == 0)
            {
                const char *path = line + 4;
                char *argv[] = { (char *)path, 0 };

                pid_t pid = fork();
                if (pid < 0)
                {
                    printf("hextra: failed to fork\n");
                }
                else if (pid == 0)
                {
                    execve(path, argv, 0);
                    printf("hextra: failed to exec: %s\n", path);
                    _exit(1);
                }
                else
                {
                    wait(pid);
                }
            }
            else if (strncmp(line, "exec ", 5) == 0)
            {
                const char *path = line + 5;
                char *argv[] = { (char *)path, 0 };
                execve(path, argv, 0);
                printf("hextra: failed to exec: %s\n", path);
            }
            else if (strncmp(line, "cd ", 3) == 0)
                cmd_cd(line + 3);
            else
                printf("hextra: command not found: %s\n", line);

            len = 0;
            line[0] = 0;
            printf(prompt);
            continue;
        }

        if (c == '\b')
        {
            if (len > 0)
            {
                len--;
                line[len] = 0;
                putchar('\b');
            }
            continue;
        }

        if (c < 32)
            continue;

        if (len < (int)sizeof(line) - 1)
        {
            line[len++] = (char)c;
            putchar(c);
        }
    }
}
