/* SPDX-License-Identifier: GPL-3.0-only */

#include "kernel/syscall.h"
#include "kernel/renderer.h"
#include "kernel/driver/keyboard.h"
#include "kernel/pmm.h"
#include "kernel/elf.h"
#include "kernel/fs/ext2.h"
#include "kernel/fs/vfs.h"
#include "kernel/paging.h"
#include "kernel/heap.h"
#include "kernel/process.h"
#include "kernel/gdt.h"
#include "kernel/sched.h"
#include "kernel/uaccess.h"
#include "kernel/string.h"

#include <stdint.h>

static int
copy_argv_envp(const char *const *user_argv, char ***out_argv, char ***out_envp)
{
    if (!user_argv)
    {
        *out_argv = 0;
        *out_envp = 0;
        return 0;
    }

    int argc = 0;

    while (1)
    {
        const char *arg;

        if (copyin(&arg, &user_argv[argc], sizeof(arg)) != 0)
        {
            render_printf("exec argv: pointer copy failed at %d\n", argc);
            return -1;
        }

        if (!arg)
            break;

        argc++;

        if (argc > 64)
        {
            render_printf("exec argv: too many args\n");
            return -1;
        }
    }

    render_printf("exec argv: argc=%d\n", argc);

    char **kargv = kmalloc((argc + 1) * sizeof(char *));
    if (!kargv)
    {
        render_printf("exec argv: unable to allocate args\n");
        return -1;
    }

    for (int i = 0; i < argc; i++)
    {
        const char *user_arg;

        if (copyin(&user_arg, &user_argv[i], sizeof(user_arg)) != 0)
        {
            render_printf("exec argv: pointer copy failed at %d\n", i);

            for (int j = 0; j < i; j++)
                kfree(kargv[j]);

            kfree(kargv);
            return -1;
        }

        size_t len = 0;

        while (1)
        {
            char c;

            if (copyin(&c, user_arg + len, 1) != 0)
            {
                render_printf("exec argv: string copy failed at %d\n", i);

                for (int j = 0; j < i; j++)
                    kfree(kargv[j]);

                kfree(kargv);
                return -1;
            }

            if (c == '\0')
                break;

            len++;

            if (len > 4096)
            {
                render_printf("exec argv: argument too long\n");

                for (int j = 0; j < i; j++)
                    kfree(kargv[j]);

                kfree(kargv);
                return -1;
            }
        }

        kargv[i] = kmalloc(len + 1);

        if (!kargv[i])
        {
            for (int j = 0; j < i; j++)
                kfree(kargv[j]);

            kfree(kargv);
            return -1;
        }

        if (copyin(kargv[i], user_arg, len + 1) != 0)
        {
            render_printf("exec argv: string copy failed at %d\n", i);

            for (int j = 0; j <= i; j++)
                kfree(kargv[j]);

            kfree(kargv);
            return -1;
        }
    }

    kargv[argc] = 0;

    *out_argv = kargv;
    *out_envp = 0;

    return 0;
}

static void
free_argv(char **argv)
{
    if (!argv)
        return;
    for (int i = 0; argv[i]; i++)
        kfree(argv[i]);
    kfree(argv);
}

static int
copy_address_space(struct process *dst, struct process *src)
{
    if (!src->pml4)
        return -1;

    uint64_t child_pml4 = paging_create_user_as(0);
    if (!child_pml4)
        return -1;

    uint64_t *parent_pml4 = (uint64_t *)paging_phys_to_virt(src->pml4 & PTE_ADDR_MASK);
    uint64_t *child_pml4_ptr = (uint64_t *)paging_phys_to_virt(child_pml4);

    for (int i = 0; i < 256; i++)
    {
        if (!(parent_pml4[i] & PTE_PRESENT))
            continue;

        uint64_t parent_pdpt = parent_pml4[i] & PTE_ADDR_MASK;
        uint64_t child_pdpt = pmm_alloc_page();
        if (!child_pdpt)
        {
            paging_free_user_as(child_pml4);
            pmm_free_page(child_pml4);
            return -1;
        }
        paging_zero_page(child_pdpt);
        memcpy(paging_phys_to_virt(child_pdpt), paging_phys_to_virt(parent_pdpt), PAGE_SIZE);
        child_pml4_ptr[i] = child_pdpt | (parent_pml4[i] & ~PTE_ADDR_MASK);

        uint64_t *parent_pdpt_ptr = (uint64_t *)paging_phys_to_virt(parent_pdpt);
        uint64_t *child_pdpt_ptr = (uint64_t *)paging_phys_to_virt(child_pdpt);

        for (int j = 0; j < 512; j++)
        {
            if (!(parent_pdpt_ptr[j] & PTE_PRESENT) || (parent_pdpt_ptr[j] & PTE_PS))
                continue;

            uint64_t parent_pd = parent_pdpt_ptr[j] & PTE_ADDR_MASK;
            uint64_t child_pd = pmm_alloc_page();
            if (!child_pd)
            {
                paging_free_user_as(child_pml4);
                pmm_free_page(child_pml4);
                return -1;
            }
            paging_zero_page(child_pd);
            memcpy(paging_phys_to_virt(child_pd), paging_phys_to_virt(parent_pd), PAGE_SIZE);
            child_pdpt_ptr[j] = child_pd | (parent_pdpt_ptr[j] & ~PTE_ADDR_MASK);

            uint64_t *parent_pd_ptr = (uint64_t *)paging_phys_to_virt(parent_pd);
            uint64_t *child_pd_ptr = (uint64_t *)paging_phys_to_virt(child_pd);

            for (int k = 0; k < 512; k++)
            {
                if (!(parent_pd_ptr[k] & PTE_PRESENT) || (parent_pd_ptr[k] & PTE_PS))
                    continue;

                uint64_t parent_pt = parent_pd_ptr[k] & PTE_ADDR_MASK;
                uint64_t child_pt = pmm_alloc_page();
                if (!child_pt)
                {
                    paging_free_user_as(child_pml4);
                    pmm_free_page(child_pml4);
                    return -1;
                }
                paging_zero_page(child_pt);
                memcpy(paging_phys_to_virt(child_pt), paging_phys_to_virt(parent_pt), PAGE_SIZE);
                child_pd_ptr[k] = child_pt | (parent_pd_ptr[k] & ~PTE_ADDR_MASK);

                uint64_t *child_pt_ptr = (uint64_t *)paging_phys_to_virt(child_pt);

                for (int l = 0; l < 512; l++)
                {
                    if (!(child_pt_ptr[l] & PTE_PRESENT))
                        continue;

                    uint64_t page = pmm_alloc_page();
                    if (!page)
                    {
                        paging_free_user_as(child_pml4);
                        pmm_free_page(child_pml4);
                        return -1;
                    }

                    memcpy(paging_phys_to_virt(page), paging_phys_to_virt(child_pt_ptr[l] & PTE_ADDR_MASK), PAGE_SIZE);
                    child_pt_ptr[l] = page | (child_pt_ptr[l] & ~PTE_ADDR_MASK);
                }
            }
        }
    }

    dst->pml4 = child_pml4;
    dst->brk = src->brk;
    dst->brk_start = src->brk_start;
    return 0;
}

struct trapframe *
sys_fork(struct trapframe *tf)
{
    struct process *parent = process_current();

    struct process *child = process_create();
    if (!child)
    {
        tf->rax = (uint64_t)-1;
        return tf;
    }

    child->ppid = parent->pid;

    if (copy_address_space(child, parent) != 0)
    {
        process_discard(child);
        tf->rax = (uint64_t)-1;
        return tf;
    }

    for (int i = 0; i < MAX_FDS; i++)
    {
        vfs_file_put(child->fds[i]);
        child->fds[i] = parent->fds[i];

        if (child->fds[i])
            child->fds[i]->refcount++;
    }

    memcpy(child->cwd, parent->cwd, MAX_PATH);

    child->tf = tf;

    struct trapframe *child_tf = (struct trapframe *)(child->kstack_top - sizeof(struct trapframe));
    memcpy(child_tf, tf, sizeof(struct trapframe));
    child_tf->rax = 0;
    child->tf = child_tf;

    child->state = PROC_RUNNABLE;

    tf->rax = child->pid;
    return tf;
}

struct trapframe *
sys_execve(struct trapframe *tf, const char *user_path, char *const *user_argv, char *const *user_envp)
{
    char path[128];

    if (copyin_str(path, sizeof(path), user_path) != 0)
    {
        render_printf("exec: copy path failed\n");
        tf->rax = (uint64_t)-1;
        return tf;
    }

    char **kargv = 0;
    char **kenvp = 0;
    if (copy_argv_envp(user_argv, &kargv, &kenvp) != 0)
    {
        render_printf("exec: argv failed\n");
        free_argv(kargv);
        tf->rax = (uint64_t)-1;
        return tf;
    }

    if (!rootfs)
    {
        render_printf("exec: no rootfs\n");
        free_argv(kargv);
        tf->rax = (uint64_t)-1;
        return tf;
    }

    void *file_buf = 0;

    uint64_t file_size = ext2_read_file(rootfs, path, &file_buf);

    if (file_size == (uint64_t)-1 || file_buf == 0)
    {
        render_printf("exec: read '%s' failed\n", path);
        free_argv(kargv);
        tf->rax = (uint64_t)-1;
        return tf;
    }

    render_printf("exec: loaded '%s', size=%x\n", path, file_size);

    uint64_t user_stack_top = 0;

    uint64_t user_pml4 = paging_create_user_as(&user_stack_top);

    if (user_pml4 == 0)
    {
        render_printf("exec: create user AS failed\n");
        kfree(file_buf);
        free_argv(kargv);
        tf->rax = (uint64_t)-1;
        return tf;
    }

    uint64_t entry = 0;
    uint64_t brk = 0;

    int rc = elf_load(file_buf, file_size, user_pml4, &entry, &brk);

    kfree(file_buf);

    if (rc != 0)
    {
        render_printf("exec: elf_load failed rc=%d\n", rc);
        free_argv(kargv);
        tf->rax = (uint64_t)-1;
        return tf;
    }

    render_printf("exec: ELF okay entry=%x\n", entry);

    struct process *current = process_current();

    if (current->pml4)
    {
        paging_free_user_as(current->pml4);
        pmm_free_page(current->pml4);
    }

    current->pml4 = user_pml4;
    current->brk = brk;
    current->brk_start = brk;
    current->tf = tf;

    uint64_t stack_ptr = user_stack_top;

    tf->rip = entry;
    tf->rsp = user_stack_top;
    tf->rax = 0;

    paging_load_cr3(user_pml4);
    tss_set_rsp0(current->kstack_top);

    free_argv(kargv);
    return tf;
}

static
struct trapframe *
sys_wait(struct trapframe *tf, int pid)
{
    struct process *parent = process_current();
    struct process *child = process_find(pid);

    if (!child || child->ppid != parent->pid)
    {
        tf->rax = (uint64_t)-1;
        return tf;
    }

    if (child->state == PROC_ZOMBIE)
    {
        int status = child->exit_status;

        process_discard(child);

        tf->rax = (uint64_t)status;
        return tf;
    }

    parent->state = PROC_BLOCKED;
    parent->wait_pid = pid;
    child->state = PROC_RUNNING;

    return process_switch(child);
}

static
struct trapframe *
sys_exit(struct trapframe *tf, int status)
{
    (void)tf;

    struct process *child = process_current();
    struct process *parent = process_find(child->ppid);

    child->exit_status = status;
    child->state = PROC_ZOMBIE;

    if (!parent)
    {
        render_printf("\nprocess %d: exited\n", child->pid);

        for (;;)
            __asm__ volatile ("hlt");
    }

    if (parent->state == PROC_BLOCKED && parent->wait_pid == child->pid)
    {
        if (!parent->tf)
        {
            render_printf("\nprocess %d: parent has no trapframe\n", child->pid);

            for (;;)
                __asm__ volatile ("hlt");
        }

        parent->tf->rax = (uint64_t)status;
        parent->wait_pid = 0;

        struct trapframe *next = process_switch(parent);
        process_discard(child);
        return next;
    }

    struct process *procs = process_get_table();
    for (int i = 0; i < MAX_PROCS; i++)
    {
        if (procs[i].state == PROC_RUNNABLE)
            return process_switch(&procs[i]);
    }

    render_printf("\nprocess %d: exited, nothing left to run\n", child->pid);

    for (;;)
        __asm__ volatile ("hlt");
}

struct trapframe *
syscall_handler(struct trapframe *tf)
{
    struct process *current = process_current();

    current->tf = tf;

    switch (tf->rax)
    {
        case SYS_READ:
        {
            int fd = (int)tf->rdi;
            void *buf = (void *)tf->rsi;
            uint64_t size = tf->rdx;

            if (size == 0)
            {
                tf->rax = 0;
                return tf;
            }

            tf->rax = vfs_read(fd, buf, size);
            return tf;
        }

        case SYS_WRITE:
        {
            int fd = (int)tf->rdi;
            const void *buf = (const void *)tf->rsi;
            uint64_t size = tf->rdx;

            tf->rax = vfs_write(fd, buf, size);
            return tf;
        }

        case SYS_CLEAR:
            render_clear(0x00000000);
            tf->rax = 0;
            return tf;

        case SYS_MEMINFO:
            tf->rax = pmm_free_pages();
            return tf;

        case SYS_GETPID:
            tf->rax = current->pid;
            return tf;

        case SYS_BRK:
            tf->rax = elf_brk(tf->rdi);
            return tf;

        case SYS_FORK:
            return sys_fork(tf);

        case SYS_WAIT:
            return sys_wait(tf, (int)tf->rdi);

        case SYS_EXIT:
            return sys_exit(tf, (int)tf->rdi);

        case SYS_EXECVE:
            return sys_execve(tf, (const char *)tf->rdi, (char *const *)tf->rsi, (char *const *)tf->rdx);

        case SYS_OPEN:
        {
            char path[MAX_PATH];
            if (copyin_str(path, sizeof(path), (const char *)tf->rdi) != 0)
            {
                tf->rax = (uint64_t)-1;
                return tf;
            }
            tf->rax = vfs_open(path, (int)tf->rsi);
            return tf;
        }

        case SYS_CLOSE:
            tf->rax = vfs_close((int)tf->rdi);
            return tf;

        case SYS_FREAD:
        {
            int fd = (int)tf->rdi;
            void *buf = (void *)tf->rsi;
            uint64_t size = tf->rdx;
            tf->rax = vfs_read(fd, buf, size);
            return tf;
        }

        case SYS_FWRITE:
        {
            int fd = (int)tf->rdi;
            const void *buf = (const void *)tf->rsi;
            uint64_t size = tf->rdx;
            tf->rax = vfs_write(fd, buf, size);
            return tf;
        }

        case SYS_READDIR:
        {
            int fd = (int)tf->rdi;
            uint32_t index = (uint32_t)tf->rsi;
            struct dirent *out = (struct dirent *)tf->rdx;
            tf->rax = vfs_readdir(fd, index, out);
            return tf;
        }

        case SYS_CHDIR:
        {
            char path[MAX_PATH];
            if (copyin_str(path, sizeof(path), (const char *)tf->rdi) != 0)
            {
                tf->rax = (uint64_t)-1;
                return tf;
            }
            tf->rax = vfs_chdir(path);
            return tf;
        }

        case SYS_GETCWD:
        {
            char *buf = (char *)tf->rdi;
            uint64_t size = tf->rsi;
            tf->rax = vfs_getcwd(buf, size);
            return tf;
        }

        case SYS_STAT:
        {
            char path[MAX_PATH];
            if (copyin_str(path, sizeof(path), (const char *)tf->rdi) != 0)
            {
                tf->rax = (uint64_t)-1;
                return tf;
            }
            tf->rax = vfs_stat(path, (struct stat *)tf->rsi);
            return tf;
        }

        case SYS_FSTAT:
            tf->rax = vfs_fstat((int)tf->rdi, (struct stat *)tf->rsi);
            return tf;

        case SYS_YIELD:
            tf->rax = 0;
            return sched_yield(tf);

        case SYS_KDEBUG:
        {
            const char *msg = (const char *)tf->rdi;
            uint64_t len = tf->rsi;
            for (uint64_t i = 0; i < len; i++)
                render_putc(msg[i], 0xFFFFFF);
            tf->rax = 0;
            return tf;
        }

        default:
            tf->rax = (uint64_t)-1;
            return tf;
    }
}

long
syscall0(long n)
{
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n)
        : "memory"
    );
    return ret;
}

long
syscall1(long n, long a1)
{
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1)
        : "memory"
    );
    return ret;
}

long
syscall2(long n, long a1, long a2)
{
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2)
        : "memory"
    );
    return ret;
}

long
syscall3(long n, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

long
syscall4(long n, long a1, long a2, long a3, long a4)
{
    long ret;
    register long r10_val asm("r10") = a4;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10_val)
        : "memory"
    );
    return ret;
}
