/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static
int
cat_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        printf("cat: %s: no such file or directory\n", path);
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode))
    {
        printf("cat: %s: is a directory\n", path);
        close(fd);
        return 1;
    }

    char buf[512];
    ssize_t n;
    int ret = 0;

    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write(STDOUT_FILENO, buf, n);

    if (n < 0)
    {
        printf("cat: %s: read error\n", path);
        ret = 1;
    }

    close(fd);
    return ret;
}

int
main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: cat <file>...\n");
        return 1;
    }

    int ret = 0;

    for (int i = 1; i < argc; i++)
        ret |= cat_file(argv[i]);

    return ret;
}
