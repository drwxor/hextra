/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef UORIX_SCHED_H
#define UORIX_SCHED_H

#include <stdint.h>

void sched_init(void);
void sched_yield(void);
void sched_tick(void);
struct trapframe *schedule(struct trapframe *current_tf);

#endif