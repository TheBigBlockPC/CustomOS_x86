#ifndef BOOT_H
#define BOOT_H

#include "types.h"
#include "console.h"

#define KERNEL_BASE 0xffffffff80000000ULL
#define BOOT_INFO_VERSION 1
#define BOOT_MAX_SECTIONS 16

typedef struct {
    uint64_t offset, size, flags;
} boot_section_t;

/* All pointers here are physical/identity addresses, except the fixed kernel
 * entry. Loader allocations remain reserved in the firmware memory map. */
typedef struct {
    uint64_t version;
    uint64_t kernel_physical, kernel_size, kernel_entry;
    uint64_t section_count;
    boot_section_t sections[BOOT_MAX_SECTIONS];
    void* memory_map;
    uint64_t memory_map_size, descriptor_size;
    uint32_t descriptor_version;
    uint32_t* framebuffer;
    uint64_t framebuffer_size;
    uint16_t framebuffer_stride, framebuffer_height;
    const void* rsdp;
    console_state_t console;
} boot_info_t;

void boot_import_console(const boot_info_t* info);
void boot_import_memory(const boot_info_t* info);
__attribute__((noreturn)) void kernel_entry(const boot_info_t* info);

#endif
