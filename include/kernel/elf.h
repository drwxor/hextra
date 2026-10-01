/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_ELF_H
#define HEXTRA_ELF_H

#include <stdint.h>

int elf_load(const void *data, uint64_t size, uint64_t pml4_phys, uint64_t *entry_out, uint64_t *brk_out);

void elf_brk_init(uint64_t start);
uint64_t elf_brk(uint64_t addr);

#endif
