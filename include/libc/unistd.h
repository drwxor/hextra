/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
ssize_t write_colored(int fd, const void *buf, size_t count, uint32_t color);
int exec(const char *path);
int execve(const char *path, char *const argv[], char *const envp[]);
void _exit(int status);
int brk(void *addr);
void *sbrk(intptr_t increment);
int isatty(int fd);
pid_t getpid(void);
unsigned int sleep(unsigned int seconds);
pid_t fork(void);
int wait(pid_t pid);
void yield(void);
void kdebug(const char *msg, size_t len);

#endif