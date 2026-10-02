/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/sched.h"
#include "kernel/process.h"
#include "kernel/renderer.h"

#include <stdint.h>

static uint64_t tick_count = 0;

void
sched_init(void)
{
    tick_count = 0;
}

struct trapframe *
sched_yield(struct trapframe *current_tf)
{
    return schedule(current_tf);
}

struct trapframe *
sched_tick(struct trapframe *current_tf)
{
    tick_count++;

    if (tick_count % 10 == 0)
    {
        return schedule(current_tf);
    }

    return current_tf;
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
