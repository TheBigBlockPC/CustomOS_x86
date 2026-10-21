#include "boot.h"
#include "uefi.h"
#include "memory.h"
#include "paging.h"
#include "panic.h"
#include "acpi.h"
#include "cpu.h"
#include "kernel.h"

void boot_import_console(const boot_info_t* info)
{
    set_FB(info->framebuffer, info->framebuffer_stride, info->framebuffer_height);
    con_import_state(&info->console);
}

void boot_import_memory(const boot_info_t* info)
{
    if (!info->memory_map || info->descriptor_size < sizeof(efi_memory_descriptor_t) ||
        info->memory_map_size % info->descriptor_size)
        panic("Invalid boot memory map");
    mem_set_kernel_image(info->kernel_physical, info->kernel_size);
    for (uint64_t offset = 0; offset < info->memory_map_size; offset += info->descriptor_size) {
        const efi_memory_descriptor_t* desc = (const void*)((const uint8_t*)info->memory_map + offset);
        if (desc->Type == EfiConventionalMemory)
            mem_add_free_pages(desc->PhysicalStart, desc->NumberOfPages);
    }
    init_memory_subsystem();
    for (uint64_t i = 0; i < info->section_count; i++) {
        const boot_section_t* section = &info->sections[i];
        for (uint64_t offset = 0; offset < section->size; offset += PAGE_SIZE_4K)
            mmu_map_4k(KERNEL_BASE + section->offset + offset,
                       info->kernel_physical + section->offset + offset, section->flags);
    }
    mmu_identity_map_range((uint64_t)info->framebuffer, info->framebuffer_size,
                           PAGE_RW | PAGE_NX | PAGE_PCD | PAGE_PWT);
}

__attribute__((noreturn)) void kernel_entry(const boot_info_t* info)
{
    if (info->version != BOOT_INFO_VERSION || info->section_count > BOOT_MAX_SECTIONS)
        for (;;) __asm__ volatile("cli; hlt");
    boot_import_console(info);
    con_write("Exited boot services\n", 0x02);
    boot_import_memory(info);
    set_rsdp(info->rsdp);
    /* The kernel owns its console/PMM state now. Drop the loader stack before
     * k_main installs the kernel's own page tables. */
    cpu_enter_kernel(k_main);
    __builtin_unreachable();
}
