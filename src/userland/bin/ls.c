/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/dirent.h>

static int show_all;
static int long_format;

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
print_entry(const char *name, const char *path)
{
    struct stat st;
    int have_stat = path[0] && stat(path, &st) == 0;
    const char *suffix = have_stat && S_ISDIR(st.st_mode) ? "/" : "";

    if (long_format && have_stat)
    {
        char perm[11];
        format_mode(perm, st.st_mode);
        printf("%s %3u %5u %5u %8ld %s%s\n",
               perm, st.st_nlink, st.st_uid, st.st_gid, st.st_size, name, suffix);
    }
    else
    {
        printf("%s%s\n", name, suffix);
    }
}

static
void
sort_names(char **names, int count)
{
    for (int i = 1; i < count; i++)
    {
        char *key = names[i];
        int j = i - 1;

        while (j >= 0 && strcmp(names[j], key) > 0)
        {
            names[j + 1] = names[j];
            j--;
        }

        names[j + 1] = key;
    }
}

static
int
list_dir(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        printf("ls: cannot open directory '%s'\n", path);
        return 1;
    }

    int count = 0;
    int capacity = 32;
    char **names = malloc(capacity * sizeof(char *));
    struct dirent de;

    for (uint32_t index = 0; names && readdir(fd, index, &de) == 0; index++)
    {
        if (de.d_name[0] == '.' && !show_all)
            continue;

        if (count == capacity)
        {
            capacity *= 2;
            char **grown = realloc(names, capacity * sizeof(char *));
            if (!grown)
                break;
            names = grown;
        }

        size_t len = strlen(de.d_name) + 1;
        names[count] = malloc(len);
        if (!names[count])
            break;
        memcpy(names[count], de.d_name, len);
        count++;
    }

    close(fd);

    if (!names)
    {
        printf("ls: out of memory\n");
        return 1;
    }

    sort_names(names, count);

    char full[256];
    for (int i = 0; i < count; i++)
    {
        join_path(full, sizeof(full), path, names[i]);
        print_entry(names[i], full);
        free(names[i]);
    }

    free(names);
    return 0;
}

static
int
list_path(const char *path, int show_header)
{
    struct stat st;
    if (stat(path, &st) != 0)
    {
        printf("ls: cannot access '%s': no such file or directory\n", path);
        return 1;
    }

    if (!S_ISDIR(st.st_mode))
    {
        print_entry(path, path);
        return 0;
    }

    if (show_header)
        printf("%s:\n", path);

    return list_dir(path);
}

int
main(int argc, char **argv)
{
    int first_path = argc;

    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];

        if (arg[0] != '-' || arg[1] == 0)
        {
            first_path = i;
            break;
        }

        for (const char *f = arg + 1; *f; f++)
        {
            switch (*f)
            {
                case 'a': show_all = 1; break;
                case 'l': long_format = 1; break;
                case '1': break;
                default:
                    printf("ls: invalid option -- '%c'\n", *f);
                    printf("usage: ls [-al1] [path]...\n");
                    return 2;
            }
        }
    }

    if (first_path >= argc)
        return list_path(".", 0);

    int ret = 0;
    int many = argc - first_path > 1;

    for (int i = first_path; i < argc; i++)
    {
        if (many && i > first_path)
            printf("\n");
        ret |= list_path(argv[i], many);
    }

    return ret;
}
