/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

int
putchar(int c)
{
    char ch = (char)c;
    if (write(STDOUT_FILENO, &ch, 1) != 1)
        return EOF;
    return (unsigned char)ch;
}

int
puts(const char *s)
{
    size_t n = strlen(s);
    if (write(STDOUT_FILENO, s, n) != (ssize_t)n)
        return EOF;
    if (putchar('\n') == EOF)
        return EOF;
    return 1;
}

int
getchar(void)
{
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1)
        return EOF;
    return (unsigned char)c;
}

char *
fgets(char *s, int size, void *unused)
{
    (void)unused;
    int i = 0;
    if (size <= 0)
        return 0;

    while (i < size - 1) {
        int c = getchar();
        if (c == EOF)
            break;
        s[i++] = (char)c;
        if (c == '\n')
            break;
    }
    if (i == 0)
        return 0;
    s[i] = 0;
    return s;
}

struct fmt_out
{
    char *buf;
    size_t size;
    size_t len;
};

static
void
out_char(struct fmt_out *o, char c)
{
    if (o->buf && o->len + 1 < o->size)
        o->buf[o->len] = c;
    o->len++;
}

static
void
out_field(struct fmt_out *o, const char *s, size_t n, int width, int left, char pad)
{
    int fill = width > (int)n ? width - (int)n : 0;

    if (pad == '0' && n > 0 && s[0] == '-')
    {
        out_char(o, '-');
        s++;
        n--;
    }

    if (!left)
        while (fill-- > 0)
            out_char(o, pad);

    for (size_t i = 0; i < n; i++)
        out_char(o, s[i]);

    if (left)
        while (fill-- > 0)
            out_char(o, ' ');
}

static
size_t
fmt_uint(char *buf, unsigned long v, unsigned base)
{
    char tmp[32];
    size_t n = 0;
    size_t len = 0;

    do {
        unsigned d = v % base;
        tmp[n++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
        v /= base;
    } while (v);

    while (n--)
        buf[len++] = tmp[n];

    return len;
}

int
vsnprintf(char *out, size_t n, const char *fmt, va_list ap)
{
    struct fmt_out o = { out, n, 0 };

    while (*fmt)
    {
        if (*fmt != '%')
        {
            out_char(&o, *fmt++);
            continue;
        }
        fmt++;

        int left = 0;
        char pad = ' ';
        int width = 0;
        int is_long = 0;

        for (;; fmt++)
        {
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '0')
                pad = '0';
            else
                break;
        }

        if (*fmt == '*')
        {
            width = va_arg(ap, int);
            fmt++;
        }
        else
        {
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');
        }

        while (*fmt == 'l')
        {
            is_long = 1;
            fmt++;
        }

        if (left)
            pad = ' ';

        char num[34];
        size_t len;

        switch (*fmt)
        {
            case '%':
                out_char(&o, '%');
                break;
            case 'c':
                num[0] = (char)va_arg(ap, int);
                out_field(&o, num, 1, width, left, ' ');
                break;
            case 's':
            {
                const char *s = va_arg(ap, const char *);
                if (!s)
                    s = "(null)";
                out_field(&o, s, strlen(s), width, left, ' ');
                break;
            }
            case 'd':
            case 'i':
            {
                long v = is_long ? va_arg(ap, long) : va_arg(ap, int);
                len = 0;
                if (v < 0)
                {
                    num[len++] = '-';
                    len += fmt_uint(num + len, (unsigned long)(-v), 10);
                }
                else
                {
                    len = fmt_uint(num, (unsigned long)v, 10);
                }
                out_field(&o, num, len, width, left, pad);
                break;
            }
            case 'u':
            case 'x':
            case 'o':
            {
                unsigned long v = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                unsigned base = *fmt == 'u' ? 10 : *fmt == 'x' ? 16 : 8;
                len = fmt_uint(num, v, base);
                out_field(&o, num, len, width, left, pad);
                break;
            }
            case 'p':
            {
                num[0] = '0';
                num[1] = 'x';
                len = 2 + fmt_uint(num + 2, (unsigned long)va_arg(ap, void *), 16);
                out_field(&o, num, len, width, left, ' ');
                break;
            }
            case 0:
                out_char(&o, '%');
                continue;
            default:
                out_char(&o, '%');
                out_char(&o, *fmt);
                break;
        }

        fmt++;
    }

    if (out && n)
        out[o.len < n ? o.len : n - 1] = 0;

    return (int)o.len;
}

int
snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int
vprintf(const char *fmt, va_list ap)
{
    char buf[1024];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n < 0)
        return n;
    size_t w = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
    write(STDOUT_FILENO, buf, w);
    return n;
}

int
vprintf_colored(const char *fmt, uint32_t color, va_list ap)
{
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n < 0)
        return n;
    size_t w = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
    write_colored(STDOUT_FILENO, buf, w, color);
    return n;
}

int
printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}

int
printf_colored(const char *fmt, uint32_t color, ...)
{
    va_list ap;
    va_start(ap, color);
    int r = vprintf_colored(fmt, color, ap);
    va_end(ap);
    return r;
}
