/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>

#include "kernel/limine.h"
#include "kernel/renderer.h"
#include "kernel/gdt.h"
#include "kernel/idt.h"
#include "kernel/paging.h"
#include "kernel/pmm.h"
#include "kernel/heap.h"
#include "kernel/syscall.h"
#include "kernel/elf.h"
#include "kernel/ata.h"
#include "kernel/fs/ext2.h"
#include "kernel/fs/vfs.h"
#include "kernel/fs/gpt.h"
#include "kernel/shell/shell.h"
#include "kernel/process.h"
#include "kernel/sched.h"
#include "kernel/pit.h"

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_flanterm_fb_init_params_request flanterm_request = {
    .id = LIMINE_FLANTERM_FB_INIT_PARAMS_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

static uint8_t kernel_stack[16384] __attribute__((aligned(16)));
#define KERNEL_STACK_TOP ((uint64_t)&kernel_stack[sizeof(kernel_stack)])

extern void user_enter(uint64_t entry, uint64_t user_stack);

extern char init_elf_start[];
extern char init_elf_end[];

static
int
try_load_elf(const void *data, uint64_t size, uint64_t pml4, uint64_t *entry, uint64_t *brk)
{
    return elf_load(data, size, pml4, entry, brk);
}

void
kmain(void)
{
    struct limine_framebuffer_response *fb_resp = framebuffer_request.response;

    if (fb_resp == 0 || fb_resp->framebuffer_count == 0)
    {
        for (;;)
            __asm__ volatile ("hlt");
    }

    struct limine_framebuffer *fb = fb_resp->framebuffers[0];

    struct limine_flanterm_fb_init_params *font_params = 0;

    if (flanterm_request.response != 0 &&
        flanterm_request.response->entry_count > 0)
    {
        font_params = flanterm_request.response->entries[0];
    }

    renderer_init(fb, font_params);
    render_clear(0x00000000);

    render_printf("renderer "); render_printf_colored("[OK]\n", GREEN_COLOR);

    gdt_init();
    render_printf("gdt "); render_printf_colored("[OK]\n", GREEN_COLOR);
    tss_set_rsp0(KERNEL_STACK_TOP);
    render_printf("tss "); render_printf_colored("[OK]\n", GREEN_COLOR);
    idt_init();
    render_printf("idt "); render_printf_colored("[OK]\n", GREEN_COLOR);

    pit_init();
    render_printf("pit "); render_printf_colored("[OK]\n", GREEN_COLOR);

    sched_init();
    render_printf("sched "); render_printf_colored("[OK]\n", GREEN_COLOR);

    uint64_t hhdm = 0;
    if (hhdm_request.response != 0)
        hhdm = hhdm_request.response->offset;

    paging_init(hhdm);
    render_printf("paging "); render_printf_colored("[OK]\n", GREEN_COLOR);

    if (memmap_request.response != 0)
        pmm_init(memmap_request.response, hhdm);
    else
        render_printf("pmm: no memmap from limine!\n");

    heap_init();
    paging_allow_user_access();
    process_init();

    uint64_t user_stack_top = 0;
    uint64_t user_pml4 = paging_create_user_as(&user_stack_top);
    if (user_pml4 == 0)
    {
        render_printf("unable to create user!\n");
        render_printf("falling back to kernel shell\n");
        static uint8_t fallback_stack[16384] __attribute__((aligned(16)));
        shell_init();
        user_enter((uint64_t)shell_run, (uint64_t)&fallback_stack[sizeof(fallback_stack)]);
        for (;;)
            __asm__ volatile ("hlt");
    }

    paging_load_cr3(user_pml4);
    render_printf("switched to user pml4\n");

    uint64_t entry = 0;
    uint64_t brk = 0;
    int loaded = -1;

    if (ata_init() == 0)
    {
        uint32_t ext2_lba = gpt_find_ext2();
        if (ext2_lba == 0)
        {
            render_printf("gpt: falling back to EXT2_START_LBA\n");
            ext2_lba = EXT2_START_LBA;
        }
        struct ext2_fs *fs = ext2_mount(ext2_lba);
        if (fs)
        {
            rootfs = fs;

            vfs_init();
            struct vfs_node *root = ext2_vfs_node(fs, 2);
            if (root)
                vfs_set_root(root);

            void *file_buf = 0;
            uint64_t file_size = ext2_read_file(fs, "/bin/init", &file_buf);

            if (file_size != (uint64_t)-1 && file_buf)
            {
                render_printf("elf: loading /bin/init from ext2 (%u bytes)\n", (uint32_t)file_size);
                loaded = try_load_elf(file_buf, file_size, user_pml4, &entry, &brk);
            }
            else
            {
                render_printf("ext2: /bin/init not found or unreadable!\n");
            }
        }
        else
        {
            render_printf("ext2: unable to find a ext2 file system!\n");
        }
    }

    if (loaded != 0 && module_request.response != 0 && module_request.response->module_count > 0)
    {
        struct limine_file *mod = module_request.response->modules[0];
        render_printf("elf: loading module (%u bytes)\n", mod->size);
        loaded = try_load_elf(mod->address, mod->size, user_pml4, &entry, &brk);
    }

    if (loaded != 0)
    {
        uint64_t embedded = (uint64_t)(init_elf_end - init_elf_start);
        render_printf("elf: loading embedded init (%u bytes)\n", embedded);
        loaded = try_load_elf(init_elf_start, embedded, user_pml4, &entry, &brk);
    }

    if (loaded != 0)
    {
        render_printf("elf: load failed! using kernel shell\n");
        shell_init();
        user_enter((uint64_t)shell_run, user_stack_top);
        for (;;)
            __asm__ volatile ("hlt");
    }

    if (process_bootstrap(user_pml4, brk) != 0)
    {
        render_printf("process: unable to create init\n");

        for (;;)
            __asm__ volatile ("hlt");
    }

    elf_brk_init(brk);

    render_printf("process: init pid=%d\n", process_current()->pid);

    render_printf("elf: entering userspace\n");

    user_enter(entry, user_stack_top);

    for (;;)
        __asm__ volatile ("hlt");
}
