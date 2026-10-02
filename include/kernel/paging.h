/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef HEXTRA_PAGING_H
#define HEXTRA_PAGING_H

#include <stdint.h>

#define PTE_PRESENT (1ULL << 0)
#define PTE_WRITE (1ULL << 1)
#define PTE_USER (1ULL << 2)
#define PTE_PS (1ULL << 7)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

#define PAGE_SIZE 4096

#define USER_STACK_VIRT 0x0000000070000000ULL
#define USER_STACK_PAGES 4

void paging_init(uint64_t hhdm_offset);
void paging_load_cr3(uint64_t pml4_phys);
void paging_allow_user_access(void);

int paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
int paging_map_page_in(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);
void paging_unmap_page(uint64_t virt);

uint64_t paging_create_user_as(uint64_t *user_stack_top);
void paging_free_user_as(uint64_t pml4_phys);

uint64_t paging_hhdm(void);
void *paging_phys_to_virt(uint64_t phys);
uint64_t paging_virt_to_phys(uint64_t virt);

void paging_zero_page(uint64_t phys);

struct trapframe;
struct trapframe *page_fault_handler(struct trapframe *tf, uint64_t fault_addr, uint64_t error_code);

#endif