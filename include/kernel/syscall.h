/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_SYSCALL_H
#define HEXTRA_SYSCALL_H

#include <stdint.h>

#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_CLEAR 2
#define SYS_MEMINFO 3
#define SYS_BRK 12
#define SYS_GETPID 39
#define SYS_FORK 57
#define SYS_EXECVE 59
#define SYS_EXIT 60
#define SYS_WAIT 61
#define SYS_OPEN 62
#define SYS_CLOSE 63
#define SYS_FREAD 64
#define SYS_FWRITE 65
#define SYS_READDIR 66
#define SYS_CHDIR 67
#define SYS_GETCWD 68
#define SYS_YIELD 69
#define SYS_KDEBUG 70

#define USER_LIMIT 0x00007FFFFFFFF000ULL

struct trapframe;

struct trapframe *sys_fork(struct trapframe *tf);
struct trapframe *sys_execve(struct trapframe *tf, const char *user_path, char *const *user_argv, char *const *user_envp);

long syscall0(long n);
long syscall1(long n, long a1);
long syscall2(long n, long a1, long a2);
long syscall3(long n, long a1, long a2, long a3);

struct trapframe *syscall_handler(struct trapframe *tf);

#endif
