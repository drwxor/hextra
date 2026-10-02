/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/paging.h"
#include "kernel/pmm.h"
#include "kernel/renderer.h"

#include <stdint.h>

static uint64_t hhdm_offset;

static inline
uint64_t
read_cr3(void)
{
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline
void
invlpg(uint64_t virt)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

static inline
void
*phys_to_virt(uint64_t phys)
{
    return (void *)(phys + hhdm_offset);
}

static
void
zero_page(uint64_t phys)
{
    uint64_t *p = (uint64_t *)phys_to_virt(phys);
    for (int i = 0; i < 512; i++)
        p[i] = 0;
}

void
paging_init(uint64_t hhdm)
{
    hhdm_offset = hhdm;
}

uint64_t
paging_hhdm(void)
{
    return hhdm_offset;
}

void
*paging_phys_to_virt(uint64_t phys)
{
    return phys_to_virt(phys);
}

void
paging_load_cr3(uint64_t pml4_phys)
{
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"(pml4_phys)
        : "memory"
    );
}

static inline
int
is_kernel_pml4_index(int idx)
{
    return idx >= 256;
}

uint64_t
paging_virt_to_phys(uint64_t virt)
{
    uint64_t cr3 = read_cr3();
    uint64_t *pml4 = (uint64_t *)phys_to_virt(cr3 & PTE_ADDR_MASK);

    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdpt_i = (virt >> 30) & 0x1FF;
    uint64_t pd_i = (virt >> 21) & 0x1FF;
    uint64_t pt_i = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_i] & PTE_PRESENT))
        return 0;
    uint64_t *pdpt = (uint64_t *)phys_to_virt(pml4[pml4_i] & PTE_ADDR_MASK);

    if (!(pdpt[pdpt_i] & PTE_PRESENT))
        return 0;
    if (pdpt[pdpt_i] & PTE_PS)
        return (pdpt[pdpt_i] & PTE_ADDR_MASK) + (virt & 0x3FFFFFFFULL);

    uint64_t *pd = (uint64_t *)phys_to_virt(pdpt[pdpt_i] & PTE_ADDR_MASK);
    if (!(pd[pd_i] & PTE_PRESENT))
        return 0;
    if (pd[pd_i] & PTE_PS)
        return (pd[pd_i] & PTE_ADDR_MASK) + (virt & 0x1FFFFFULL);

    uint64_t *pt = (uint64_t *)phys_to_virt(pd[pd_i] & PTE_ADDR_MASK);
    if (!(pt[pt_i] & PTE_PRESENT))
        return 0;

    return (pt[pt_i] & PTE_ADDR_MASK) + (virt & 0xFFFULL);
}

int
paging_map_page_in(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t *pml4 = (uint64_t *)phys_to_virt(pml4_phys & PTE_ADDR_MASK);

    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdpt_i = (virt >> 30) & 0x1FF;
    uint64_t pd_i = (virt >> 21) & 0x1FF;
    uint64_t pt_i = (virt >> 12) & 0x1FF;

    uint64_t table_flags = (is_kernel_pml4_index(pml4_i) ? 0 : (flags & PTE_USER));

    if (!(pml4[pml4_i] & PTE_PRESENT))
    {
        uint64_t page = pmm_alloc_page();
        if (page == 0)
            return -1;
        zero_page(page);
        pml4[pml4_i] = page | PTE_PRESENT | PTE_WRITE | table_flags;
    }

    uint64_t *pdpt = (uint64_t *)phys_to_virt(pml4[pml4_i] & PTE_ADDR_MASK);

    if (!(pdpt[pdpt_i] & PTE_PRESENT))
    {
        uint64_t page = pmm_alloc_page();
        if (page == 0)
            return -1;
        zero_page(page);
        pdpt[pdpt_i] = page | PTE_PRESENT | PTE_WRITE | table_flags;
    }
    else if (pdpt[pdpt_i] & PTE_PS)
    {
        return -1;
    }

    uint64_t *pd = (uint64_t *)phys_to_virt(pdpt[pdpt_i] & PTE_ADDR_MASK);

    if (!(pd[pd_i] & PTE_PRESENT))
    {
        uint64_t page = pmm_alloc_page();
        if (page == 0)
            return -1;
        zero_page(page);
        pd[pd_i] = page | PTE_PRESENT | PTE_WRITE | table_flags;
    }
    else if (pd[pd_i] & PTE_PS)
    {
        return -1;
    }

    uint64_t *pt = (uint64_t *)phys_to_virt(pd[pd_i] & PTE_ADDR_MASK);
    pt[pt_i] = (phys & PTE_ADDR_MASK) | (flags & (PTE_PRESENT | PTE_WRITE | PTE_USER));
    invlpg(virt);
    return 0;
}

int
paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags)
{
    return paging_map_page_in(read_cr3() & PTE_ADDR_MASK, virt, phys, flags);
}

void
paging_unmap_page(uint64_t virt)
{
    uint64_t cr3 = read_cr3();
    uint64_t *pml4 = (uint64_t *)phys_to_virt(cr3 & PTE_ADDR_MASK);

    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdpt_i = (virt >> 30) & 0x1FF;
    uint64_t pd_i = (virt >> 21) & 0x1FF;
    uint64_t pt_i = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_i] & PTE_PRESENT))
        return;
    uint64_t *pdpt = (uint64_t *)phys_to_virt(pml4[pml4_i] & PTE_ADDR_MASK);
    if (!(pdpt[pdpt_i] & PTE_PRESENT) || (pdpt[pdpt_i] & PTE_PS))
        return;
    uint64_t *pd = (uint64_t *)phys_to_virt(pdpt[pdpt_i] & PTE_ADDR_MASK);
    if (!(pd[pd_i] & PTE_PRESENT) || (pd[pd_i] & PTE_PS))
        return;
    uint64_t *pt = (uint64_t *)phys_to_virt(pd[pd_i] & PTE_ADDR_MASK);

    pt[pt_i] = 0;
    invlpg(virt);
}

static void
free_page_table(uint64_t table_phys, int level)
{
    uint64_t *table = (uint64_t *)phys_to_virt(table_phys);

    if (level > 0)
    {
        for (int i = 0; i < 512; i++)
        {
            if (table[i] & PTE_PRESENT)
            {
                uint64_t child_phys = table[i] & PTE_ADDR_MASK;
                free_page_table(child_phys, level - 1);
            }
        }
    }

    pmm_free_page(table_phys);
}

void
paging_free_user_as(uint64_t pml4_phys)
{
    uint64_t *pml4 = (uint64_t *)phys_to_virt(pml4_phys & PTE_ADDR_MASK);

    for (int i = 0; i < 256; i++)
    {
        if (pml4[i] & PTE_PRESENT)
        {
            uint64_t pdpt_phys = pml4[i] & PTE_ADDR_MASK;
            free_page_table(pdpt_phys, 2);
            pml4[i] = 0;
        }
    }
}

uint64_t
paging_create_user_as(uint64_t *user_stack_top)
{
    uint64_t kernel_cr3 = read_cr3() & PTE_ADDR_MASK;
    uint64_t *kernel_pml4 = (uint64_t *)phys_to_virt(kernel_cr3);

    uint64_t user_pml4_phys = pmm_alloc_page();
    if (user_pml4_phys == 0)
        return 0;
    zero_page(user_pml4_phys);

    uint64_t *user_pml4 = (uint64_t *)phys_to_virt(user_pml4_phys);

    for (int i = 256; i < 512; i++)
    {
        if (kernel_pml4[i] & PTE_PRESENT)
        {
            user_pml4[i] = kernel_pml4[i] & ~PTE_USER;
        }
    }

    uint64_t stack_bottom = USER_STACK_VIRT - USER_STACK_PAGES * PAGE_SIZE;
    for (uint64_t i = 0; i < USER_STACK_PAGES; i++)
    {
        uint64_t phys = pmm_alloc_page();
        if (phys == 0)
        {
            paging_free_user_as(user_pml4_phys);
            pmm_free_page(user_pml4_phys);
            return 0;
        }
        zero_page(phys);
        if (paging_map_page_in(user_pml4_phys, stack_bottom + i * PAGE_SIZE, phys, PTE_PRESENT | PTE_WRITE | PTE_USER) != 0)
        {
            paging_free_user_as(user_pml4_phys);
            pmm_free_page(user_pml4_phys);
            return 0;
        }
    }

    if (user_stack_top)
        *user_stack_top = USER_STACK_VIRT;

    render_printf("user as: pml4=%x stack=%x\n", user_pml4_phys, USER_STACK_VIRT);
    return user_pml4_phys;
}
