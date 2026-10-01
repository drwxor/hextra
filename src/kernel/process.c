/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/process.h"
#include "kernel/gdt.h"
#include "kernel/paging.h"

static struct process processes[MAX_PROCS];

static uint8_t process_stacks[MAX_PROCS][KSTACK_SIZE]
__attribute__((aligned(16)));

static struct process *current_process;
static int next_pid = 1;

void
process_init(void)
{
    current_process = 0;
    next_pid = 1;

    for (int i = 0; i < MAX_PROCS; i++)
        processes[i].state = PROC_UNUSED;
}

struct process *
process_create(void)
{
    for (int i = 0; i < MAX_PROCS; i++)
    {
        struct process *p = &processes[i];

        if (p->state != PROC_UNUSED)
            continue;

        p->pid = next_pid++;
        p->ppid = 0;

        p->state = PROC_RUNNABLE;

        p->pml4 = 0;
        p->brk = 0;
        p->brk_start = 0;

        p->kstack = process_stacks[i];
        p->kstack_top = (uint64_t)p->kstack + KSTACK_SIZE;

        p->tf = 0;
        p->exit_status = 0;

        for (int i = 0; i < MAX_FDS; i++)
            p->fds[i] = 0;

        p->cwd[0] = '/';
        p->cwd[1] = 0;

        return p;
    }

    return 0;
}

void
process_discard(struct process *p)
{
    if (!p)
        return;

    for (int i = 0; i < MAX_FDS; i++)
    {
        if (p->fds[i])
        {
            vfs_close(i);
            p->fds[i] = 0;
        }
    }

    p->state = PROC_UNUSED;
}

struct process *
process_current(void)
{
    return current_process;
}

struct process *
process_find(int pid)
{
    for (int i = 0; i < MAX_PROCS; i++)
    {
        if (processes[i].state == PROC_UNUSED)
            continue;

        if (processes[i].pid == pid)
            return &processes[i];
    }

    return 0;
}

struct process *
process_get_table(void)
{
    return processes;
}

int
process_bootstrap(uint64_t pml4, uint64_t brk)
{
    struct process *p = process_create();

    if (!p)
        return -1;

    p->ppid = 0;
    p->state = PROC_RUNNING;

    p->pml4 = pml4;
    p->brk = brk;
    p->brk_start = brk;

    current_process = p;

    paging_load_cr3(p->pml4);
    tss_set_rsp0(p->kstack_top);

    return 0;
}

void
process_make_user(struct process *p, uint64_t entry, uint64_t user_stack)
{
    struct trapframe *tf;

    tf = (struct trapframe *)(p->kstack_top - sizeof(struct trapframe));

    uint64_t *words = (uint64_t *)tf;

    for (uint64_t i = 0; i < sizeof(struct trapframe) / sizeof(uint64_t); i++)
    {
        words[i] = 0;
    }

    tf->rip = entry;
    tf->cs = USER_CS;
    tf->rflags = 0x202;
    tf->rsp = user_stack;
    tf->ss = USER_DS;

    p->tf = tf;
}

struct trapframe *
process_switch(struct process *p)
{
    current_process = p;
    p->state = PROC_RUNNING;

    paging_load_cr3(p->pml4);
    tss_set_rsp0(p->kstack_top);

    return p->tf;
}
