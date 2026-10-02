/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/paging.h"
#include "kernel/pmm.h"
#include "kernel/process.h"
#include "kernel/renderer.h"
#include "kernel/elf.h"

#include <stdint.h>

#define USER_STACK_VIRT 0x0000000070000000ULL
#define USER_STACK_PAGES 4
#define PAGE_SIZE 4096

#define PF_PRESENT (1 << 0)
#define PF_WRITE (1 << 1)
#define PF_USER (1 << 2)
#define PF_RSVD (1 << 3)
#define PF_INSTR (1 << 4)

extern uint64_t paging_hhdm(void);
extern void *paging_phys_to_virt(uint64_t phys);
extern int paging_map_page_in(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);
extern uint64_t pmm_alloc_page(void);
extern void pmm_free_page(uint64_t phys);

static int
is_stack_growth(uint64_t addr, uint64_t user_stack_top)
{
    uint64_t stack_bottom = USER_STACK_VIRT - USER_STACK_PAGES * PAGE_SIZE;
    uint64_t guard_page = stack_bottom - PAGE_SIZE;

    return (addr >= guard_page && addr < user_stack_top);
}

struct trapframe *
page_fault_handler(struct trapframe *tf, uint64_t fault_addr, uint64_t error_code)
{
    struct process *current = process_current();

    if (!current || current->pml4 == 0)
    {
        render_printf("page_fault: kernel fault at %x, error=%x\n", fault_addr, error_code);
        for (;;)
            __asm__ volatile ("hlt");
    }

    if (!(error_code & PF_USER))
    {
        render_printf("page_fault: kernel fault at %x, error=%x\n", fault_addr, error_code);
        for (;;)
            __asm__ volatile ("hlt");
    }

    uint64_t page_addr = fault_addr & ~(PAGE_SIZE - 1);

    if (is_stack_growth(page_addr, current->tf->rsp))
    {
        uint64_t phys = pmm_alloc_page();
        if (phys == 0)
        {
            render_printf("page_fault: OOM during stack growth\n");
            tf->rax = -1;
            return tf;
        }

        void *dst = paging_phys_to_virt(phys);
        for (int i = 0; i < 512; i++)
            ((uint64_t *)dst)[i] = 0;

        uint64_t flags = PTE_PRESENT | PTE_WRITE | PTE_USER;
        if (paging_map_page_in(current->pml4, page_addr, phys, flags) != 0)
        {
            pmm_free_page(phys);
            render_printf("page_fault: failed to map stack page\n");
            tf->rax = -1;
            return tf;
        }

        render_printf("page_fault: grew stack to %x\n", page_addr);
        return tf;
    }

    if (page_addr >= current->brk_start && page_addr < current->brk)
    {
        uint64_t phys = pmm_alloc_page();
        if (phys == 0)
        {
            render_printf("page_fault: OOM during heap access\n");
            tf->rax = -1;
            return tf;
        }

        void *dst = paging_phys_to_virt(phys);
        for (int i = 0; i < 512; i++)
            ((uint64_t *)dst)[i] = 0;

        uint64_t flags = PTE_PRESENT | PTE_WRITE | PTE_USER;
        if (paging_map_page_in(current->pml4, page_addr, phys, flags) != 0)
        {
            pmm_free_page(phys);
            render_printf("page_fault: failed to map heap page\n");
            tf->rax = -1;
            return tf;
        }

        return tf;
    }

    if ((error_code & PF_WRITE) && (error_code & PF_PRESENT))
    {
        render_printf("page_fault: write to read-only page at %x (COW not implemented)\n", fault_addr);
        tf->rax = -1;
        return tf;
    }

    render_printf("page_fault: unhandled fault at %x, error=%x, pid=%d\n", fault_addr, error_code, current->pid);

    current->exit_status = -11;
    current->state = PROC_ZOMBIE;

    struct process *parent = process_find(current->ppid);
    if (parent)
    {
        parent->state = PROC_RUNNING;
        parent->tf->rax = (uint64_t)current->exit_status;
        return process_switch(parent);
    }
    else
    {
        for (;;)
            __asm__ volatile ("hlt");
    }
}
