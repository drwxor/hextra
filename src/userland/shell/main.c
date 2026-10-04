/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/dirent.h>
#include <sys/stat.h>

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
    printf("\tpwd      print working directory\n");
    printf("\tcat      print file contents\n");
    printf("\tstat     show file status\n");
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
char *
next_arg(char **cursor)
{
    char *p = *cursor;

    while (*p == ' ')
        p++;

    if (*p == 0)
    {
        *cursor = p;
        return 0;
    }

    char *arg = p;
    while (*p && *p != ' ')
        p++;

    if (*p)
        *p++ = 0;

    *cursor = p;
    return arg;
}

static
void
join_path(char *out, size_t size, const char *dir, const char *name)
{
    size_t len = strlen(dir);

    if (len + 1 + strlen(name) + 1 > size)
    {
        out[0] = 0;
        return;
    }

    strcpy(out, dir);
    if (len > 0 && out[len - 1] != '/')
        strcat(out, "/");
    strcat(out, name);
}

static
void
cmd_ls(char *args)
{
    const char *path = next_arg(&args);
    if (!path)
        path = ".";

    struct stat st;
    if (stat(path, &st) != 0)
    {
        printf("ls: cannot access '%s': no such file or directory\n", path);
        return;
    }

    if (!S_ISDIR(st.st_mode))
    {
        printf("%s\n", path);
        return;
    }

    int fd = syscall2(SYS_OPEN, (long)path, O_RDONLY);
    if (fd < 0)
    {
        printf("ls: cannot open directory '%s'\n", path);
        return;
    }

    struct dirent de;
    uint32_t index = 0;
    char full[256];

    while (syscall3(SYS_READDIR, fd, index, (long)&de) == 0)
    {
        index++;

        if (strcmp(de.d_name, ".") == 0 || strcmp(de.d_name, "..") == 0)
            continue;

        join_path(full, sizeof(full), path, de.d_name);
        if (full[0] && stat(full, &st) == 0 && S_ISDIR(st.st_mode))
            printf("%s/\n", de.d_name);
        else
            printf("%s\n", de.d_name);
    }

    syscall1(SYS_CLOSE, fd);
}

static
void
cmd_cd(char *args)
{
    const char *path = next_arg(&args);
    if (!path)
        path = "/";

    if (syscall1(SYS_CHDIR, (long)path) != 0)
    {
        printf("cd: cannot change directory: %s\n", path);
    }
}

static
void
cmd_pwd(void)
{
    char cwd[256];

    if (syscall2(SYS_GETCWD, (long)cwd, sizeof(cwd)) != 0)
    {
        printf("pwd: cannot get working directory\n");
        return;
    }

    printf("%s\n", cwd);
}

static
void
cmd_cat(char *args)
{
    const char *path = next_arg(&args);
    if (!path)
    {
        printf("usage: cat <file>...\n");
        return;
    }

    for (; path; path = next_arg(&args))
    {
        int fd = syscall2(SYS_OPEN, (long)path, O_RDONLY);
        if (fd < 0)
        {
            printf("cat: %s: no such file or directory\n", path);
            continue;
        }

        struct stat st;
        if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode))
        {
            printf("cat: %s: is a directory\n", path);
            syscall1(SYS_CLOSE, fd);
            continue;
        }

        char buf[512];
        ssize_t n;

        while ((n = read(fd, buf, sizeof(buf))) > 0)
            write(STDOUT_FILENO, buf, n);

        if (n < 0)
            printf("cat: %s: read error\n", path);

        syscall1(SYS_CLOSE, fd);
    }
}

static
const char *
file_type(uint32_t mode)
{
    if (S_ISREG(mode))
        return "regular file";
    if (S_ISDIR(mode))
        return "directory";
    if (S_ISLNK(mode))
        return "symbolic link";
    if (S_ISCHR(mode))
        return "character device";
    if (S_ISBLK(mode))
        return "block device";
    if (S_ISFIFO(mode))
        return "fifo";
    if (S_ISSOCK(mode))
        return "socket";
    return "unknown";
}

static
void
format_mode(char *out, uint32_t mode)
{
    static const char rwx[] = "rwxrwxrwx";

    out[0] = S_ISDIR(mode) ? 'd' :
             S_ISLNK(mode) ? 'l' :
             S_ISCHR(mode) ? 'c' :
             S_ISBLK(mode) ? 'b' :
             S_ISFIFO(mode) ? 'p' :
             S_ISSOCK(mode) ? 's' : '-';

    for (int i = 0; i < 9; i++)
        out[i + 1] = (mode & (0400 >> i)) ? rwx[i] : '-';

    out[10] = 0;
}

static
void
format_octal(char *out, uint32_t mode)
{
    for (int i = 0; i < 4; i++)
        out[i] = '0' + ((mode >> (9 - i * 3)) & 7);

    out[4] = 0;
}

static
void
cmd_stat(char *args)
{
    const char *path = next_arg(&args);
    if (!path)
    {
        printf("usage: stat <file>...\n");
        return;
    }

    for (; path; path = next_arg(&args))
    {
        struct stat st;
        if (stat(path, &st) != 0)
        {
            printf("stat: cannot stat '%s': no such file or directory\n", path);
            continue;
        }

        char perm[11];
        char octal[5];
        format_mode(perm, st.st_mode);
        format_octal(octal, st.st_mode & 07777);

        printf("  File: %s\n", path);
        printf("  Size: %u\tBlocks: %u\tIO Block: %u\t%s\n",
               (unsigned)st.st_size, (unsigned)st.st_blocks,
               (unsigned)st.st_blksize, file_type(st.st_mode));
        printf("Device: %u\tInode: %u\tLinks: %u\n",
               (unsigned)st.st_dev, (unsigned)st.st_ino, st.st_nlink);
        printf("Access: (%s/%s)\tUid: %u\tGid: %u\n",
               octal, perm, st.st_uid, st.st_gid);
        printf("Access: %u\n", (unsigned)st.st_atime);
        printf("Modify: %u\n", (unsigned)st.st_mtime);
        printf("Change: %u\n", (unsigned)st.st_ctime);
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
            else if (strcmp(line, "ls") == 0 || strncmp(line, "ls ", 3) == 0)
                cmd_ls(line + 2);
            else if (strcmp(line, "cd") == 0 || strncmp(line, "cd ", 3) == 0)
                cmd_cd(line + 2);
            else if (strcmp(line, "pwd") == 0)
                cmd_pwd();
            else if (strcmp(line, "cat") == 0 || strncmp(line, "cat ", 4) == 0)
                cmd_cat(line + 3);
            else if (strcmp(line, "stat") == 0 || strncmp(line, "stat ", 5) == 0)
                cmd_stat(line + 4);
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
