/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/dirent.h>
#include <sys/stat.h>

#define LINE_MAX 512
#define WORDS_MAX 64
#define WORDBUF_SIZE 2048
#define HISTORY_MAX 32
#define VARS_MAX 32
#define VAR_NAME_MAX 32
#define VAR_VALUE_MAX 256
#define PATH_MAX 256

#define CTRL(c) ((c) & 0x1F)

static const char *logo[] = {
    "H EEE X XTTTRRA A",
    "H E    X  TR RA A",
    "HHEE   X  TRR AAA",
    "H E    X  TR RA A",
    "H EEE X X TR RA A",
    0
};

static int last_status;

struct var
{
    char name[VAR_NAME_MAX];
    char value[VAR_VALUE_MAX];
};

static struct var vars[VARS_MAX];
static int var_count;

static
const char *
var_get(const char *name)
{
    for (int i = 0; i < var_count; i++)
    {
        if (strcmp(vars[i].name, name) == 0)
            return vars[i].value;
    }

    return 0;
}

static
int
var_set(const char *name, const char *value)
{
    if (strlen(name) >= VAR_NAME_MAX || strlen(value) >= VAR_VALUE_MAX)
        return -1;

    for (int i = 0; i < var_count; i++)
    {
        if (strcmp(vars[i].name, name) == 0)
        {
            strcpy(vars[i].value, value);
            return 0;
        }
    }

    if (var_count == VARS_MAX)
        return -1;

    strcpy(vars[var_count].name, name);
    strcpy(vars[var_count].value, value);
    var_count++;
    return 0;
}

static
void
var_unset(const char *name)
{
    for (int i = 0; i < var_count; i++)
    {
        if (strcmp(vars[i].name, name) == 0)
        {
            vars[i] = vars[--var_count];
            return;
        }
    }
}

static
int
is_name_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static
int
is_name_char(char c)
{
    return is_name_start(c) || (c >= '0' && c <= '9');
}

static
char *
assignment(char *word)
{
    if (!is_name_start(word[0]))
        return 0;

    char *p = word + 1;
    while (is_name_char(*p))
        p++;

    return *p == '=' ? p : 0;
}

static char history[HISTORY_MAX][LINE_MAX];
static int history_count;

static
void
history_add(const char *line)
{
    if (line[0] == 0)
        return;

    if (history_count > 0 && strcmp(history[history_count - 1], line) == 0)
        return;

    if (history_count == HISTORY_MAX)
    {
        memmove(history[0], history[1], sizeof(history[0]) * (HISTORY_MAX - 1));
        history_count--;
    }

    strcpy(history[history_count++], line);
}

static
void
echo_repeat(char c, int count)
{
    char buf[64];

    while (count > 0)
    {
        int n = count < (int)sizeof(buf) ? count : (int)sizeof(buf);
        memset(buf, c, n);
        write(STDOUT_FILENO, buf, n);
        count -= n;
    }
}

static
void
print_prompt(void)
{
    char cwd[PATH_MAX];

    if (!getcwd(cwd, sizeof(cwd)))
        strcpy(cwd, "?");

    printf("hextra:%s$ ", cwd);
}

static
void
replace_line(char *line, int *len, const char *text)
{
    echo_repeat('\b', *len);

    *len = 0;
    while (text[*len] && *len < LINE_MAX - 1)
    {
        line[*len] = text[*len];
        (*len)++;
    }
    line[*len] = 0;

    write(STDOUT_FILENO, line, *len);
}

static
int
read_line(char *line)
{
    char saved[LINE_MAX];
    int len = 0;
    int hist_pos = history_count;

    line[0] = 0;
    saved[0] = 0;

    for (;;)
    {
        int c = getchar();
        if (c == EOF)
            continue;

        switch (c)
        {
            case '\n':
            case '\r':
                putchar('\n');
                line[len] = 0;
                return 0;

            case '\b':
            case 127:
                if (len > 0)
                {
                    line[--len] = 0;
                    putchar('\b');
                }
                break;

            case CTRL('c'):
                printf("^C\n");
                line[0] = 0;
                return 0;

            case CTRL('d'):
                if (len == 0)
                {
                    putchar('\n');
                    return -1;
                }
                break;

            case CTRL('u'):
                replace_line(line, &len, "");
                break;

            case CTRL('l'):
                syscall0(SYS_CLEAR);
                print_prompt();
                write(STDOUT_FILENO, line, len);
                break;

            case CTRL('p'):
                if (hist_pos > 0)
                {
                    if (hist_pos == history_count)
                        strcpy(saved, line);
                    hist_pos--;
                    replace_line(line, &len, history[hist_pos]);
                }
                break;

            case CTRL('n'):
                if (hist_pos < history_count)
                {
                    hist_pos++;
                    replace_line(line, &len, hist_pos == history_count ? saved : history[hist_pos]);
                }
                break;

            case '\t':
                c = ' ';

            default:
                if (c >= 32 && c < 127 && len < LINE_MAX - 1)
                {
                    line[len++] = (char)c;
                    line[len] = 0;
                    putchar(c);
                }
                break;
        }
    }
}

enum token_kind
{
    TOK_WORD,
    TOK_SEMI,
    TOK_AND,
    TOK_OR,
    TOK_END
};

struct token
{
    enum token_kind kind;
    char *word;
};

struct strbuf
{
    char data[WORDBUF_SIZE];
    size_t len;
    int overflow;
};

static struct strbuf rawbuf;
static struct strbuf expbuf;

static
void
sb_reset(struct strbuf *sb)
{
    sb->len = 0;
    sb->overflow = 0;
}

static
void
sb_putc(struct strbuf *sb, char c)
{
    if (sb->len + 1 >= sizeof(sb->data))
    {
        sb->overflow = 1;
        return;
    }

    sb->data[sb->len++] = c;
}

static
void
sb_puts(struct strbuf *sb, const char *s)
{
    while (*s)
        sb_putc(sb, *s++);
}

static
int
is_word_end(char c)
{
    return c == 0 || c == ' ' || c == '\t' || c == ';' ||
           c == '&' || c == '|' || c == '<' || c == '>';
}

static
int
lex(const char *line, struct token *toks, int max)
{
    const char *p = line;
    int count = 0;

    sb_reset(&rawbuf);

    for (;;)
    {
        while (*p == ' ' || *p == '\t')
            p++;

        if (count >= max - 1)
        {
            printf("sh: too many words\n");
            return -1;
        }

        if (*p == 0 || *p == '#')
        {
            toks[count].kind = TOK_END;
            return count;
        }

        if (*p == ';')
        {
            toks[count++].kind = TOK_SEMI;
            p++;
            continue;
        }

        if (p[0] == '&' && p[1] == '&')
        {
            toks[count++].kind = TOK_AND;
            p += 2;
            continue;
        }

        if (p[0] == '|' && p[1] == '|')
        {
            toks[count++].kind = TOK_OR;
            p += 2;
            continue;
        }

        if (*p == '|' || *p == '&' || *p == '<' || *p == '>')
        {
            printf("sh: '%c' is not supported yet\n", *p);
            return -1;
        }

        size_t start = rawbuf.len;

        while (!is_word_end(*p))
        {
            if (*p == '\\')
            {
                sb_putc(&rawbuf, *p++);
                if (*p)
                    sb_putc(&rawbuf, *p++);
            }
            else if (*p == '\'' || *p == '"')
            {
                char quote = *p;
                sb_putc(&rawbuf, *p++);

                while (*p && *p != quote)
                {
                    if (quote == '"' && *p == '\\' && p[1])
                        sb_putc(&rawbuf, *p++);
                    sb_putc(&rawbuf, *p++);
                }

                if (*p != quote)
                {
                    printf("sh: unterminated quote\n");
                    return -1;
                }

                sb_putc(&rawbuf, *p++);
            }
            else
            {
                sb_putc(&rawbuf, *p++);
            }
        }

        sb_putc(&rawbuf, 0);

        if (rawbuf.overflow)
        {
            printf("sh: line too long\n");
            return -1;
        }

        toks[count].kind = TOK_WORD;
        toks[count].word = rawbuf.data + start;
        count++;
    }
}

static
const char *
expand_var(const char *p, int *err)
{
    char name[VAR_NAME_MAX];
    size_t n = 0;

    if (*p == '?' || *p == '$')
    {
        char num[16];
        snprintf(num, sizeof(num), "%d", *p == '?' ? last_status : getpid());
        sb_puts(&expbuf, num);
        return p + 1;
    }

    int braced = *p == '{';
    if (braced)
        p++;

    while (is_name_char(*p) && n < sizeof(name) - 1)
        name[n++] = *p++;
    name[n] = 0;

    if (braced)
    {
        if (*p != '}' || n == 0)
        {
            printf("sh: bad substitution\n");
            *err = 1;
            return p;
        }
        p++;
    }
    else if (n == 0)
    {
        sb_putc(&expbuf, '$');
        return p;
    }

    const char *value = var_get(name);
    if (value)
        sb_puts(&expbuf, value);

    return p;
}

static
char *
expand_word(const char *p)
{
    size_t start = expbuf.len;
    int quoted = 0;
    int err = 0;

    if (*p == '~' && (p[1] == '/' || p[1] == 0))
    {
        const char *home = var_get("HOME");
        sb_puts(&expbuf, home ? home : "/");
        p++;
    }

    while (*p && !err)
    {
        if (*p == '\\')
        {
            p++;
            if (*p)
                sb_putc(&expbuf, *p++);
            quoted = 1;
        }
        else if (*p == '\'')
        {
            p++;
            while (*p != '\'')
                sb_putc(&expbuf, *p++);
            p++;
            quoted = 1;
        }
        else if (*p == '"')
        {
            p++;
            while (*p != '"' && !err)
            {
                if (*p == '\\' && (p[1] == '"' || p[1] == '\\' || p[1] == '$'))
                {
                    sb_putc(&expbuf, p[1]);
                    p += 2;
                }
                else if (*p == '$')
                {
                    p = expand_var(p + 1, &err);
                }
                else
                {
                    sb_putc(&expbuf, *p++);
                }
            }
            p++;
            quoted = 1;
        }
        else if (*p == '$')
        {
            p = expand_var(p + 1, &err);
        }
        else
        {
            sb_putc(&expbuf, *p++);
        }
    }

    if (err)
        return (char *)-1;

    sb_putc(&expbuf, 0);

    if (expbuf.overflow)
    {
        printf("sh: line too long\n");
        return (char *)-1;
    }

    if (expbuf.data[start] == 0 && !quoted)
    {
        expbuf.len = start;
        return 0;
    }

    return expbuf.data + start;
}

static
int
is_executable(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static
int
find_command(const char *name, char *out, size_t size)
{
    if (strchr(name, '/'))
    {
        if (strlen(name) >= size || !is_executable(name))
            return -1;
        strcpy(out, name);
        return 0;
    }

    const char *path = var_get("PATH");
    if (!path)
        path = "/bin";

    while (*path)
    {
        const char *end = strchr(path, ':');
        size_t len = end ? (size_t)(end - path) : strlen(path);

        if (len > 0 && len + 1 + strlen(name) + 1 <= size)
        {
            memcpy(out, path, len);
            out[len] = 0;
            if (out[len - 1] != '/')
                strcat(out, "/");
            strcat(out, name);

            if (is_executable(out))
                return 0;
        }

        if (!end)
            break;
        path = end + 1;
    }

    return -1;
}

static
int
run_external(int argc, char **argv)
{
    char path[PATH_MAX];

    (void)argc;

    if (find_command(argv[0], path, sizeof(path)) != 0)
    {
        printf("sh: command not found: %s\n", argv[0]);
        return 127;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        printf("sh: fork failed\n");
        return 1;
    }

    if (pid == 0)
    {
        execve(path, argv, 0);
        printf("sh: %s: cannot execute\n", argv[0]);
        _exit(126);
    }

    int status = wait(pid);
    return status < 0 ? 1 : status;
}

struct builtin
{
    const char *name;
    int (*fn)(int argc, char **argv);
    const char *help;
};

static int bi_help(int argc, char **argv);

static
int
bi_cd(int argc, char **argv)
{
    char old[PATH_MAX];
    const char *target;

    if (argc > 2)
    {
        printf("cd: too many arguments\n");
        return 1;
    }

    if (argc < 2)
    {
        target = var_get("HOME");
        if (!target)
            target = "/";
    }
    else if (strcmp(argv[1], "-") == 0)
    {
        target = var_get("OLDPWD");
        if (!target)
        {
            printf("cd: OLDPWD not set\n");
            return 1;
        }
    }
    else
    {
        target = argv[1];
    }

    if (!getcwd(old, sizeof(old)))
        old[0] = 0;

    if (chdir(target) != 0)
    {
        printf("cd: %s: no such directory\n", target);
        return 1;
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)))
        var_set("PWD", cwd);
    if (old[0])
        var_set("OLDPWD", old);

    if (argc == 2 && strcmp(argv[1], "-") == 0)
        printf("%s\n", cwd);

    return 0;
}

static
int
bi_pwd(int argc, char **argv)
{
    char cwd[PATH_MAX];

    (void)argc;
    (void)argv;

    if (!getcwd(cwd, sizeof(cwd)))
    {
        printf("pwd: cannot get working directory\n");
        return 1;
    }

    printf("%s\n", cwd);
    return 0;
}

static
int
bi_echo(int argc, char **argv)
{
    int i = 1;
    int newline = 1;

    if (argc > 1 && strcmp(argv[1], "-n") == 0)
    {
        newline = 0;
        i++;
    }

    for (; i < argc; i++)
    {
        write(STDOUT_FILENO, argv[i], strlen(argv[i]));
        if (i + 1 < argc)
            write(STDOUT_FILENO, " ", 1);
    }

    if (newline)
        write(STDOUT_FILENO, "\n", 1);

    return 0;
}

static
int
bi_exit(int argc, char **argv)
{
    int status = argc > 1 ? atoi(argv[1]) : last_status;
    printf("bye\n");
    exit(status);
    return status;
}

static
int
bi_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    syscall0(SYS_CLEAR);
    return 0;
}

static
int
bi_history(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    for (int i = 0; i < history_count; i++)
        printf("%3d  %s\n", i + 1, history[i]);

    return 0;
}

static
int
bi_set(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    for (int i = 0; i < var_count; i++)
        printf("%s=%s\n", vars[i].name, vars[i].value);

    return 0;
}

static
int
bi_unset(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        var_unset(argv[i]);

    return 0;
}

static
int
bi_exec(int argc, char **argv)
{
    char path[PATH_MAX];

    if (argc < 2)
        return 0;

    if (find_command(argv[1], path, sizeof(path)) != 0)
    {
        printf("exec: %s: not found\n", argv[1]);
        return 127;
    }

    execve(path, argv + 1, 0);
    printf("exec: %s: cannot execute\n", argv[1]);
    return 126;
}

static const struct builtin *find_builtin(const char *name);

static
int
bi_type(int argc, char **argv)
{
    char path[PATH_MAX];
    int ret = 0;

    for (int i = 1; i < argc; i++)
    {
        if (find_builtin(argv[i]))
        {
            printf("%s is a shell builtin\n", argv[i]);
        }
        else if (find_command(argv[i], path, sizeof(path)) == 0)
        {
            printf("%s is %s\n", argv[i], path);
        }
        else
        {
            printf("type: %s: not found\n", argv[i]);
            ret = 1;
        }
    }

    return ret;
}

static
int
bi_true(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

static
int
bi_false(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 1;
}

static
int
bi_mem(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    long pages = syscall0(SYS_MEMINFO);
    printf("free pages: %ld (%ld KiB)\n", pages, pages * 4);
    return 0;
}

static
int
bi_fetch(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("\n");

    for (int i = 0; logo[i] != 0; i++)
    {
        printf("%s", logo[i]);

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
    return 0;
}

static const struct builtin builtins[] = {
    { "help",    bi_help,    "show this message" },
    { "cd",      bi_cd,      "change directory (cd, cd -, cd DIR)" },
    { "pwd",     bi_pwd,     "print working directory" },
    { "echo",    bi_echo,    "print arguments (-n: no newline)" },
    { "exit",    bi_exit,    "leave the shell" },
    { "clear",   bi_clear,   "clear the screen" },
    { "history", bi_history, "show command history" },
    { "set",     bi_set,     "list shell variables (NAME=value sets one)" },
    { "unset",   bi_unset,   "remove shell variables" },
    { "exec",    bi_exec,    "replace the shell with a program" },
    { "type",    bi_type,    "show how a command name resolves" },
    { "true",    bi_true,    "do nothing, successfully" },
    { "false",   bi_false,   "do nothing, unsuccessfully" },
    { "mem",     bi_mem,     "show free physical pages" },
    { "fetch",   bi_fetch,   "show system information" },
    { 0, 0, 0 }
};

static
int
bi_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("builtins:\n");
    for (const struct builtin *b = builtins; b->name; b++)
        printf("  %-9s %s\n", b->name, b->help);

    printf("\nprograms in /bin:\n ");
    int fd = open("/bin", O_RDONLY);
    if (fd >= 0)
    {
        struct dirent de;
        for (uint32_t i = 0; readdir(fd, i, &de) == 0; i++)
        {
            if (de.d_name[0] != '.')
                printf(" %s", de.d_name);
        }
        close(fd);
    }
    return 0;
}

static
const struct builtin *
find_builtin(const char *name)
{
    for (const struct builtin *b = builtins; b->name; b++)
    {
        if (strcmp(b->name, name) == 0)
            return b;
    }

    return 0;
}

static
int
execute(int argc, char **argv)
{
    int all_assignments = 1;
    for (int i = 0; i < argc; i++)
    {
        if (!assignment(argv[i]))
        {
            all_assignments = 0;
            break;
        }
    }

    if (all_assignments)
    {
        for (int i = 0; i < argc; i++)
        {
            char *eq = assignment(argv[i]);
            *eq = 0;
            if (var_set(argv[i], eq + 1) != 0)
            {
                printf("sh: cannot set %s\n", argv[i]);
                return 1;
            }
        }
        return 0;
    }

    const struct builtin *b = find_builtin(argv[0]);
    if (b)
        return b->fn(argc, argv);

    return run_external(argc, argv);
}

static
int
check_syntax(const struct token *toks, int count)
{
    int expect_command = 1;

    for (int i = 0; i < count; i++)
    {
        if (toks[i].kind == TOK_WORD)
        {
            expect_command = 0;
            continue;
        }

        if (expect_command)
        {
            const char *op = toks[i].kind == TOK_SEMI ? ";" :
                             toks[i].kind == TOK_AND ? "&&" : "||";
            printf("sh: syntax error near '%s'\n", op);
            return -1;
        }

        expect_command = 1;
    }

    if (count > 0 && (toks[count - 1].kind == TOK_AND || toks[count - 1].kind == TOK_OR))
    {
        printf("sh: syntax error: unexpected end of line\n");
        return -1;
    }

    return 0;
}

static
void
run_command(char **raw, int count)
{
    char *argv[WORDS_MAX + 1];
    int argc = 0;

    sb_reset(&expbuf);

    for (int i = 0; i < count; i++)
    {
        char *word = expand_word(raw[i]);

        if (word == (char *)-1)
        {
            last_status = 1;
            return;
        }

        if (word)
            argv[argc++] = word;
    }

    argv[argc] = 0;

    if (argc > 0)
        last_status = execute(argc, argv);
}

static
void
run_line(const char *line)
{
    struct token toks[WORDS_MAX + 1];
    char *raw[WORDS_MAX + 1];
    int count_raw = 0;

    int count = lex(line, toks, WORDS_MAX + 1);
    if (count < 0 || check_syntax(toks, count) != 0)
    {
        last_status = 2;
        return;
    }

    enum token_kind connector = TOK_SEMI;

    for (int i = 0; i <= count; i++)
    {
        if (toks[i].kind == TOK_WORD)
        {
            raw[count_raw++] = toks[i].word;
            continue;
        }

        if (count_raw > 0)
        {
            int run = connector == TOK_SEMI ||
                      (connector == TOK_AND && last_status == 0) ||
                      (connector == TOK_OR && last_status != 0);

            if (run)
                run_command(raw, count_raw);
        }

        count_raw = 0;
        connector = toks[i].kind;
    }
}

int
main(void)
{
    char line[LINE_MAX];
    char cwd[PATH_MAX];

    var_set("PATH", "/bin");
    var_set("HOME", "/");
    if (getcwd(cwd, sizeof(cwd)))
        var_set("PWD", cwd);

    printf("type 'help' for commands\n");

    for (;;)
    {
        print_prompt();

        if (read_line(line) < 0)
        {
            char *argv[] = { "exit", 0 };
            bi_exit(1, argv);
        }

        history_add(line);
        run_line(line);
    }
}
