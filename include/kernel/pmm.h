/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_PMM_H
#define HEXTRA_PMM_H

#include <stdint.h>
#include "kernel/limine.h"

#define PAGE_SIZE 4096ULL

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm);

uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t phys);

uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);
uint64_t pmm_used_pages(void);

#endif
