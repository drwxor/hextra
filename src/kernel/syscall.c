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

#include <stdint.h>

static
int
copy_user_string(char *dst, uint64_t cap, uint64_t src)
{
    if (src < 0x1000 || src >= USER_LIMIT)
        return -1;

    for (uint64_t i = 0; i + 1 < cap; i++)
    {
        uint64_t va = src + i;

        if (va >= USER_LIMIT)
            return -1;

        uint64_t phys = paging_virt_to_phys(va);
        if (phys == 0)
            return -1;

        dst[i] = *(const char *)paging_phys_to_virt(phys);

        if (dst[i] == '\0')
            return 0;
    }

    dst[cap - 1] = '\0';
    return -1;
}

struct trapframe *
sys_exec(struct trapframe *tf, const char *user_path)
{
    char path[128];

    if (copy_user_string(path, sizeof(path), (uint64_t)user_path) != 0)
    {
        tf->rax = (uint64_t)-1;
        return tf;
    }

    if (!rootfs)
    {
        tf->rax = (uint64_t)-1;
        return tf;
    }

    void *file_buf = 0;

    uint64_t file_size = ext2_read_file(rootfs, path, &file_buf);

    if (file_size == (uint64_t)-1 || file_buf == 0)
    {
        tf->rax = (uint64_t)-1;
        return tf;
    }

    uint64_t user_stack_top = 0;

    uint64_t user_pml4 = paging_create_user_as(&user_stack_top);

    if (user_pml4 == 0)
    {
        kfree(file_buf);
        tf->rax = (uint64_t)-1;
        return tf;
    }

    uint64_t entry = 0;
    uint64_t brk = 0;

    int rc = elf_load(file_buf, file_size, user_pml4, &entry, &brk);

    kfree(file_buf);

    if (rc != 0)
    {
        tf->rax = (uint64_t)-1;
        return tf;
    }

    struct process *current = process_current();

    current->pml4 = user_pml4;
    current->brk = brk;
    current->brk_start = brk;
    current->tf = tf;

    tf->rip = entry;
    tf->rsp = user_stack_top;
    tf->rax = 0;

    paging_load_cr3(user_pml4);
    tss_set_rsp0(current->kstack_top);

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
    child->state = PROC_RUNNING;

    return process_switch(child);
}

static
struct trapframe *
sys_exit(struct trapframe *tf, int status)
{
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

    parent->state = PROC_RUNNING;

    if (!parent->tf)
    {
        render_printf("\nprocess %d: parent has no trapframe\n", child->pid);

        for (;;)
            __asm__ volatile ("hlt");
    }

    parent->tf->rax = (uint64_t)status;

    return process_switch(parent);
}

static
long
sys_spawn(const char *user_path)
{
    char path[128];

    if (copy_user_string(path, sizeof(path), (uint64_t)user_path) != 0)
    {
        return -1;
    }

    if (!rootfs)
        return -1;

    void *file_buf = 0;

    uint64_t file_size = ext2_read_file(rootfs, path, &file_buf);

    if (file_size == (uint64_t)-1 ||
        file_buf == 0)
    {
        return -1;
    }

    struct process *parent = process_current();

    struct process *child = process_create();

    if (!child)
    {
        kfree(file_buf);
        return -1;
    }

    uint64_t user_stack_top = 0;

    uint64_t user_pml4 = paging_create_user_as(&user_stack_top);

    if (!user_pml4)
    {
        kfree(file_buf);
        process_discard(child);
        return -1;
    }

    uint64_t entry = 0;
    uint64_t brk = 0;

    int rc = elf_load(file_buf, file_size, user_pml4, &entry, &brk);

    kfree(file_buf);

    if (rc != 0)
    {
        process_discard(child);
        return -1;
    }

    child->ppid = parent->pid;
    child->pml4 = user_pml4;
    child->brk = brk;
    child->brk_start = brk;

    process_make_user(child, entry, user_stack_top);

    child->state = PROC_RUNNABLE;

    return child->pid;
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
            void *buf = (void *)tf->rdi;
            uint64_t size = tf->rsi;

            if (size == 0)
            {
                tf->rax = 0;
                return tf;
            }

            tf->rax = vfs_read(0, buf, size);
            return tf;
        }

        case SYS_WRITE:
        {
            const void *buf = (const void *)tf->rdi;
            uint64_t size = tf->rsi;

            tf->rax = vfs_write(1, buf, size);
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

        case SYS_SPAWN:
            tf->rax = sys_spawn((const char *)tf->rdi);
            return tf;

        case SYS_WAIT:
            return sys_wait(tf, (int)tf->rdi);

        case SYS_EXIT:
            return sys_exit(tf, (int)tf->rdi);

        case SYS_EXEC:
            return sys_exec(tf, (const char *)tf->rdi);

        case SYS_OPEN:
        {
            char path[128];
            if (copy_user_string(path, sizeof(path), tf->rdi) != 0)
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
            char path[128];
            if (copy_user_string(path, sizeof(path), tf->rdi) != 0)
            {
                tf->rax = (uint64_t)-1;
                return tf;
            }
            tf->rax = ext2_chdir(path);
            return tf;
        }

        case SYS_GETCWD:
        {
            char *buf = (char *)tf->rdi;
            uint64_t size = tf->rsi;
            tf->rax = ext2_getcwd(buf, size);
            return tf;
        }

        case SYS_YIELD:
            tf->rax = 0;
            return sched_yield(tf);

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
