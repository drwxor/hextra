/* SPDX-License-Identifier: GPL-3.0-only */

#include <sys/syscall.h>

long
syscall0(long n)
{
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n)
        : "memory"
    );
    return ret;
}

long
syscall1(long n, long a1)
{
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1)
        : "memory"
    );
    return ret;
}

long
syscall2(long n, long a1, long a2)
{
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2)
        : "memory"
    );
    return ret;
}

long
syscall3(long n, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

long
syscall4(long n, long a1, long a2, long a3, long a4)
{
    long ret;
    register long r10_val asm("r10") = a4;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10_val)
        : "memory"
    );
    return ret;
}