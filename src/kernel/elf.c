/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/elf.h"
#include "kernel/paging.h"
#include "kernel/pmm.h"
#include "kernel/renderer.h"
#include "kernel/process.h"

#include <stdint.h>

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define ET_DYN 3
#define EM_X86_64 62
#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4

#define ELF_MAGIC0 0x7F
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'

#define USER_LOAD_BASE 0x0000000000400000ULL
#define USER_LIMIT 0x00007FFFFFFFF000ULL

struct elf64_ehdr
{
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf64_phdr
{
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

static
void
kmemcpy(void *dst, const void *src, uint64_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--)
        *d++ = *s++;
}

static
void
kmemset(void *dst, uint8_t v, uint64_t n)
{
    uint8_t *d = dst;
    while (n--)
        *d++ = v;
}

static
int
in_user_range(uint64_t va, uint64_t len)
{
    if (va < 0x1000)
        return 0;
    if (va >= USER_LIMIT)
        return 0;
    if (len > USER_LIMIT - va)
        return 0;
    return 1;
}

static
int
map_segment(uint64_t pml4_phys, const uint8_t *file, uint64_t file_size, const struct elf64_phdr *ph, uint64_t bias)
{
    uint64_t seg_start = ph->p_vaddr + bias;
    uint64_t seg_end = seg_start + ph->p_memsz;

    if (!in_user_range(seg_start, ph->p_memsz))
        return -1;
    if (ph->p_filesz > ph->p_memsz)
        return -1;
    if (ph->p_offset + ph->p_filesz < ph->p_offset)
        return -1;
    if (ph->p_offset + ph->p_filesz > file_size)
        return -1;

    uint64_t flags = PTE_PRESENT | PTE_USER;
    if (ph->p_flags & PF_W)
        flags |= PTE_WRITE;

    uint64_t page_start = seg_start & ~(PAGE_SIZE - 1);
    uint64_t page_end = (seg_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uint64_t va = page_start; va < page_end; va += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        if (phys == 0)
            return -1;

        uint8_t *dst = (uint8_t *)paging_phys_to_virt(phys);
        kmemset(dst, 0, PAGE_SIZE);

        uint64_t copy_lo = va;
        uint64_t copy_hi = va + PAGE_SIZE;
        if (copy_lo < seg_start)
            copy_lo = seg_start;
        uint64_t file_end = seg_start + ph->p_filesz;
        if (copy_hi > file_end)
            copy_hi = file_end;

        if (copy_hi > copy_lo)
        {
            uint64_t file_off = ph->p_offset + (copy_lo - seg_start);
            kmemcpy(dst + (copy_lo - va), file + file_off, copy_hi - copy_lo);
        }

        if (paging_map_page_in(pml4_phys, va, phys, flags) != 0)
            return -1;
    }

    return 0;
}

int
elf_load(const void *data, uint64_t size, uint64_t pml4_phys, uint64_t *entry_out, uint64_t *brk_out)
{
    render_printf("elf_load: entry\n");
    if (data == 0 || size < sizeof(struct elf64_ehdr) || pml4_phys == 0)
        return -1;

    const uint8_t *file = (const uint8_t *)data;
    struct elf64_ehdr eh;
    render_printf("elf_load: before kmemcpy file=%x\n", (uint64_t)file);
    for (volatile int i = 0; i < 100000; i++);
    kmemcpy(&eh, file, sizeof(eh));
    render_printf("elf_load: ehdr copied\n");

    if (eh.e_ident[EI_MAG0] != ELF_MAGIC0 ||
        eh.e_ident[EI_MAG1] != ELF_MAGIC1 ||
        eh.e_ident[EI_MAG2] != ELF_MAGIC2 ||
        eh.e_ident[EI_MAG3] != ELF_MAGIC3) {
        render_printf("elf: bad magic\n");
        return -1;
    }
    render_printf("elf_load: magic ok\n");

    if (eh.e_ident[EI_CLASS] != ELFCLASS64 ||
        eh.e_ident[EI_DATA] != ELFDATA2LSB ||
        eh.e_machine != EM_X86_64) {
        render_printf("elf: not x86_64 le\n");
        return -1;
    }
    render_printf("elf_load: class/data/machine ok\n");

    if (eh.e_type != ET_EXEC && eh.e_type != ET_DYN) {
        render_printf("elf: not exec/dyn (type=%d)\n", eh.e_type);
        return -1;
    }
    render_printf("elf_load: type ok\n");

    if (eh.e_phentsize != sizeof(struct elf64_phdr) || eh.e_phnum == 0 ||
        eh.e_phnum > 128) {
        render_printf("elf: bad phdrs\n");
        return -1;
    }
    render_printf("elf_load: phdrs ok num=%d\n", eh.e_phnum);

    uint64_t ph_end = eh.e_phoff + (uint64_t)eh.e_phnum * eh.e_phentsize;
    if (ph_end < eh.e_phoff || ph_end > size) {
        render_printf("elf: phdrs out of range\n");
        return -1;
    }
    render_printf("elf_load: ph range ok\n");

    uint64_t bias = 0;
    if (eh.e_type == ET_DYN)
        bias = USER_LOAD_BASE;

    render_printf("elf_load: type=%s entry=%x bias=%x\n", eh.e_type == ET_DYN ? "DYN" : "EXEC", (uint32_t)eh.e_entry, (uint32_t)bias);

    uint64_t highest = 0;

    for (uint16_t i = 0; i < eh.e_phnum; i++) {
        struct elf64_phdr ph;
        kmemcpy(&ph, file + eh.e_phoff + (uint64_t)i * eh.e_phentsize, sizeof(ph));

        if (ph.p_type != PT_LOAD)
            continue;
        if (ph.p_memsz == 0)
            continue;

        if (map_segment(pml4_phys, file, size, &ph, bias) != 0) {
            render_printf("elf: map failed\n");
            return -1;
        }

        uint64_t end = ph.p_vaddr + bias + ph.p_memsz;
        if (end > highest)
            highest = end;
    }

    uint64_t entry = eh.e_entry + bias;
    if (!in_user_range(entry, 1)) {
        render_printf("elf: bad entry\n");
        return -1;
    }

    highest = (highest + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (entry_out)
        *entry_out = entry;
    if (brk_out)
        *brk_out = highest;

    render_printf("elf: entry=%x brk=%x\n", entry, highest);
    return 0;
}

void
elf_brk_init(uint64_t start)
{
    struct process *p = process_current();

    p->brk = start;
    p->brk_start = start;
}

uint64_t
elf_brk(uint64_t addr)
{
    struct process *p = process_current();

    if (!p)
        return 0;

    if (addr == 0)
        return p->brk;

    if (addr < p->brk_start)
        return p->brk;

    if (addr >= USER_STACK_VIRT - USER_STACK_PAGES * PAGE_SIZE)
        return p->brk;

    if (addr > p->brk)
    {
        uint64_t from =
        (p->brk + PAGE_SIZE - 1) &
        ~(PAGE_SIZE - 1);

        uint64_t to = (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        for (uint64_t va = from; va < to; va += PAGE_SIZE)
        {
            uint64_t phys = pmm_alloc_page();

            if (phys == 0)
                return p->brk;

            uint8_t *mem = (uint8_t *)paging_phys_to_virt(phys);

            kmemset(mem, 0, PAGE_SIZE);

            if (paging_map_page_in(p->pml4, va, phys, PTE_PRESENT | PTE_WRITE | PTE_USER) != 0)
            {
                return p->brk;
            }
        }
    }

    p->brk = addr;
    return p->brk;
}
