/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>

#define SYSNAME  "hextra"
#define NODENAME "hextra"
#define RELEASE  "dev"
#define MACHINE  "x86_64"

enum {
    F_SYSNAME  = 1 << 0,
    F_NODENAME = 1 << 1,
    F_RELEASE  = 1 << 2,
    F_MACHINE  = 1 << 3,
    F_ALL      = F_SYSNAME | F_NODENAME | F_RELEASE | F_MACHINE
};

static
void
usage(void)
{
    printf("usage: uname [-asnrm]\n");
}

int
main(int argc, char **argv)
{
    int flags = 0;

    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];

        if (arg[0] != '-' || arg[1] == 0)
        {
            usage();
            return 1;
        }

        for (const char *f = arg + 1; *f; f++)
        {
            switch (*f)
            {
                case 'a': flags |= F_ALL; break;
                case 's': flags |= F_SYSNAME; break;
                case 'n': flags |= F_NODENAME; break;
                case 'r': flags |= F_RELEASE; break;
                case 'm': flags |= F_MACHINE; break;
                default:
                    printf("uname: invalid option -- '%c'\n", *f);
                    usage();
                    return 1;
            }
        }
    }

    if (flags == 0)
        flags = F_SYSNAME;

    const char *sep = "";

    if (flags & F_SYSNAME)
    {
        printf("%s%s", sep, SYSNAME);
        sep = " ";
    }
    if (flags & F_NODENAME)
    {
        printf("%s%s", sep, NODENAME);
        sep = " ";
    }
    if (flags & F_RELEASE)
    {
        printf("%s%s", sep, RELEASE);
        sep = " ";
    }
    if (flags & F_MACHINE)
        printf("%s%s", sep, MACHINE);

    printf("\n");
    return 0;
}
