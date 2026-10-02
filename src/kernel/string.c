/* SPDX-License-Identifier: GPL-3.0-only */

#include <stddef.h>
#include <stdint.h>

int
strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

size_t
strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

void *
memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *
memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    while (n--)
        *d++ = (uint8_t)c;
    return dst;
}

int
memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a;
    const uint8_t *y = b;
    while (n--)
    {
        if (*x != *y)
            return (int)*x - (int)*y;
        x++;
        y++;
    }
    return 0;
}