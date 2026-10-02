/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/uaccess.h"
#include "kernel/paging.h"
#include "kernel/process.h"
#include "kernel/string.h"

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define USER_LIMIT 0x00007FFFFFFFF000ULL

static int
validate_user_range(uint64_t va, size_t len, int write)
{
    if (va < 0x1000)
        return 0;
    if (va >= USER_LIMIT)
        return 0;
    if (len > USER_LIMIT - va)
        return 0;

    uint64_t end = va + len;
    uint64_t page_start = va & ~(PAGE_SIZE - 1);
    uint64_t page_end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    struct process *proc = process_current();
    if (!proc || !proc->pml4)
        return 0;

    for (uint64_t page = page_start; page < page_end; page += PAGE_SIZE)
    {
        uint64_t phys = paging_virt_to_phys(page);
        if (phys == 0)
            return 0;

        uint64_t *pml4 = (uint64_t *)paging_phys_to_virt(proc->pml4 & PTE_ADDR_MASK);
        uint64_t pml4_i = (page >> 39) & 0x1FF;
        uint64_t pdpt_i = (page >> 30) & 0x1FF;
        uint64_t pd_i = (page >> 21) & 0x1FF;
        uint64_t pt_i = (page >> 12) & 0x1FF;

        if (!(pml4[pml4_i] & PTE_PRESENT))
            return 0;
        uint64_t *pdpt = (uint64_t *)paging_phys_to_virt(pml4[pml4_i] & PTE_ADDR_MASK);
        if (!(pdpt[pdpt_i] & PTE_PRESENT))
            return 0;
        if (pdpt[pdpt_i] & PTE_PS)
        {
            if (!(pdpt[pdpt_i] & PTE_USER))
                return 0;
            if (write && !(pdpt[pdpt_i] & PTE_WRITE))
                return 0;
            continue;
        }
        uint64_t *pd = (uint64_t *)paging_phys_to_virt(pdpt[pdpt_i] & PTE_ADDR_MASK);
        if (!(pd[pd_i] & PTE_PRESENT))
            return 0;
        if (pd[pd_i] & PTE_PS)
        {
            if (!(pd[pd_i] & PTE_USER))
                return 0;
            if (write && !(pd[pd_i] & PTE_WRITE))
                return 0;
            continue;
        }
        uint64_t *pt = (uint64_t *)paging_phys_to_virt(pd[pd_i] & PTE_ADDR_MASK);
        if (!(pt[pt_i] & PTE_PRESENT))
            return 0;
        if (!(pt[pt_i] & PTE_USER))
            return 0;
        if (write && !(pt[pt_i] & PTE_WRITE))
            return 0;
    }

    return 1;
}

int
copyin(void *dst, const void *user_src, size_t len)
{
    uint64_t src = (uint64_t)user_src;

    if (!validate_user_range(src, len, 0))
        return -1;

    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)user_src;
    size_t remaining = len;

    while (remaining > 0)
    {
        uint64_t page_offset = src & (PAGE_SIZE - 1);
        size_t chunk = PAGE_SIZE - page_offset;
        if (chunk > remaining)
            chunk = remaining;

        uint64_t phys = paging_virt_to_phys(src);
        if (phys == 0)
            return -1;

        // const void *kaddr = paging_phys_to_virt(phys);
        // memcpy(d, (const uint8_t *)kaddr + page_offset, chunk);

        const void *kaddr = paging_phys_to_virt(phys);
        memcpy(d, kaddr, chunk);

        d += chunk;
        s += chunk;
        src += chunk;
        remaining -= chunk;
    }

    return 0;
}

int
copyout(void *user_dst, const void *src, size_t len)
{
    uint64_t dst = (uint64_t)user_dst;

    if (!validate_user_range(dst, len, 1))
        return -1;

    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)user_dst;
    size_t remaining = len;

    while (remaining > 0)
    {
        uint64_t page_offset = dst & (PAGE_SIZE - 1);
        size_t chunk = PAGE_SIZE - page_offset;
        if (chunk > remaining)
            chunk = remaining;

        // uint64_t phys = paging_virt_to_phys(dst);
        // if (phys == 0)
        //     return -1;

        // void *kaddr = paging_phys_to_virt(phys);
        // memcpy((uint8_t *)kaddr + page_offset, s, chunk);

        uint64_t phys = paging_virt_to_phys(dst);
        if (phys == 0)
            return -1;

        void *kaddr = paging_phys_to_virt(phys);
        memcpy(kaddr, s, chunk);

        s += chunk;
        d += chunk;
        dst += chunk;
        remaining -= chunk;
    }

    return 0;
}

int
copyin_str(char *dst, size_t dst_size, const char *user_src)
{
    if (dst_size == 0)
        return -1;

    uint64_t src = (uint64_t)user_src;

    if (!validate_user_range(src, 1, 0))
        return -1;

    size_t i = 0;
    while (i + 1 < dst_size)
    {
        if (src >= USER_LIMIT)
            return -1;

        uint64_t phys = paging_virt_to_phys(src);
        if (phys == 0)
            return -1;

        char c = *(const char *)paging_phys_to_virt(phys);
        dst[i] = c;

        if (c == '\0')
            return 0;

        i++;
        src++;
    }

    dst[dst_size - 1] = '\0';
    return -1;
}

int
copyout_str(char *user_dst, size_t dst_size, const char *src)
{
    if (dst_size == 0)
        return -1;

    size_t len = strlen(src);
    if (len >= dst_size)
        len = dst_size - 1;

    if (copyout(user_dst, src, len) != 0)
        return -1;

    char zero = '\0';
    if (copyout(user_dst + len, &zero, 1) != 0)
        return -1;

    return 0;
}
