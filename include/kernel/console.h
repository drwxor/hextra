/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_CONSOLE_H
#define HEXTRA_CONSOLE_H

#include "kernel/fs/vfs.h"
#include "kernel/process.h"

void console_init_process(struct process *proc);

#endif