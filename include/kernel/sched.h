/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_SCHED_H
#define HEXTRA_SCHED_H

#include <stdint.h>

struct trapframe;

void sched_init(void);
struct trapframe *sched_yield(struct trapframe *current_tf);
struct trapframe *sched_tick(struct trapframe *current_tf);
struct trapframe *schedule(struct trapframe *current_tf);

#endif