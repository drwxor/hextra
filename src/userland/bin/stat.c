/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <sys/stat.h>

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
int
stat_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
    {
        printf("stat: cannot stat '%s': no such file or directory\n", path);
        return 1;
    }

    char perm[11];
    format_mode(perm, st.st_mode);

    printf("  File: %s\n", path);
    printf("  Size: %-15ld Blocks: %-10ld IO Block: %-6ld %s\n",
           st.st_size, st.st_blocks, st.st_blksize, file_type(st.st_mode));
    printf("Device: %-14lu Inode: %-11lu Links: %u\n",
           st.st_dev, st.st_ino, st.st_nlink);
    printf("Access: (%04o/%s)  Uid: %5u   Gid: %5u\n",
           st.st_mode & 07777, perm, st.st_uid, st.st_gid);
    printf("Access: %ld\n", st.st_atime);
    printf("Modify: %ld\n", st.st_mtime);
    printf("Change: %ld\n", st.st_ctime);
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: stat <file>...\n");
        return 1;
    }

    int ret = 0;

    for (int i = 1; i < argc; i++)
        ret |= stat_file(argv[i]);

    return ret;
}
