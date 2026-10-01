/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/sched.h"
#include "kernel/process.h"
#include "kernel/renderer.h"
#include "kernel/idt.h"

#include <stdint.h>

static uint64_t tick_count = 0;

void
sched_init(void)
{
    tick_count = 0;
    render_printf("sched: initialized\n");
}

void
sched_yield(void)
{
    struct trapframe tf_dummy;
    struct trapframe *new_tf = schedule(&tf_dummy);
    if (new_tf != &tf_dummy) {
        __asm__ volatile (
            "mov %0, %%rsp\n\t"
            "pop %%r15\n\t"
            "pop %%r14\n\t"
            "pop %%r13\n\t"
            "pop %%r12\n\t"
            "pop %%r11\n\t"
            "pop %%r10\n\t"
            "pop %%r9\n\t"
            "pop %%r8\n\t"
            "pop %%rbp\n\t"
            "pop %%rdi\n\t"
            "pop %%rsi\n\t"
            "pop %%rdx\n\t"
            "pop %%rcx\n\t"
            "pop %%rbx\n\t"
            "pop %%rax\n\t"
            "add $16, %%rsp\n\t"
            "iretq"
            :
            : "r"(new_tf)
            : "memory"
        );
    }
}

void
sched_tick(void)
{
    tick_count++;

    if (tick_count % 10 == 0)
    {
        sched_yield();
    }
}

struct trapframe *
schedule(struct trapframe *current_tf)
{
    struct process *current = process_current();
    if (!current)
        return current_tf;

    current->tf = current_tf;
    current->state = PROC_RUNNABLE;

    struct process *next = 0;
    int start_pid = current->pid;

    struct process *procs = process_get_table();

    for (int i = 0; i < MAX_PROCS; i++)
    {
        struct process *p = &procs[i];
        if (p->state == PROC_RUNNABLE && p->pid != start_pid)
        {
            next = p;
            break;
        }
    }

    if (!next)
    {
        for (int i = 0; i < MAX_PROCS; i++)
        {
            struct process *p = &procs[i];
            if (p->state == PROC_RUNNABLE)
            {
                next = p;
                break;
            }
        }
    }

    if (!next)
        return current_tf;

    return process_switch(next);
}