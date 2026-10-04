/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_PROCESS_H
#define HEXTRA_PROCESS_H

#include <stdint.h>

#include "kernel/fs/vfs.h"

#define MAX_PROCS 16
#define KSTACK_SIZE 16384

enum process_state
{
    PROC_UNUSED,
    PROC_RUNNABLE,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE
};

struct trapframe
{
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

struct process
{
    int pid;
    int ppid;

    enum process_state state;

    uint64_t pml4;
    uint64_t brk;
    uint64_t brk_start;

    uint8_t *kstack;
    uint64_t kstack_top;

    struct trapframe *tf;

    int exit_status;
    int wait_pid;

    struct file *fds[MAX_FDS];
    char cwd[MAX_PATH];
};

void process_init(void);

struct process *process_create(void);
void process_discard(struct process *p);

struct process *process_current(void);
struct process *process_find(int pid);
struct process *process_get_table(void);

int process_bootstrap(uint64_t pml4, uint64_t brk);

void process_make_user(struct process *p, uint64_t entry, uint64_t user_stack);

struct trapframe *process_switch(struct process *p);

#endif
