#include "uefi.h"
#include "boot.h"
#include "paging.h"
#include "util.h"
#include "panic.h"

static efi_guid_t gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static efi_guid_t image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static efi_guid_t filesystem_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static efi_guid_t file_info_guid = EFI_FILE_INFO_GUID;
static efi_guid_t acpi20_guid = ACPI_20_TABLE_GUID;
static boot_info_t boot_info;

__attribute__((noreturn)) void panic(char* message)
{
    __asm__ volatile("cli" ::: "memory");
    con_write("Bootloader: ", 0x01);
    con_write(message, 0x07);
    for (;;) __asm__ volatile("hlt");
}

static int guid_equal(const efi_guid_t* a, const efi_guid_t* b)
{
    const uint8_t* left = (const void*)a;
    const uint8_t* right = (const void*)b;
    for (int i = 0; i < sizeof(*a); i++) if (left[i] != right[i]) return 0;
    return 1;
}

static uint64_t allocate_pages(efi_boot_services_t* bs, uint64_t pages, int below_4g)
{
    efi_physical_address_t address = below_4g ? 0xffffffffULL : 0;
    if (EFI_ERROR(bs->AllocatePages(below_4g ? AllocateMaxAddress : AllocateAnyPages,
                                   EfiLoaderData, pages, &address)))
        panic("Cannot allocate loader pages");
    memory_zero((void*)address, pages * PAGE_SIZE_4K);
    return address;
}

static uint8_t* read_kernel(efi_boot_services_t* bs, efi_handle_t device, uint64_t* size)
{
    efi_simple_file_system_protocol_t* fs;
    efi_file_handle_t *root, *file;
    if (EFI_ERROR(bs->HandleProtocol(device, &filesystem_guid, (void**)&fs)) ||
        EFI_ERROR(fs->OpenVolume(fs, &root))) panic("Cannot open boot volume");
    if (EFI_ERROR(root->Open(root, &file, L"\\EFI\\BOOT\\kernel.exe", EFI_FILE_MODE_READ, 0)))
        panic("Cannot open \\EFI\\BOOT\\kernel.exe");
    efi_file_info_t info;
    uintn_t info_size = sizeof(info);
    if (EFI_ERROR(file->GetInfo(file, &file_info_guid, &info_size, &info)) ||
        info.FileSize < 64 || info.FileSize > 64 * 1024 * 1024)
        panic("Invalid kernel file size");
    *size = info.FileSize;
    uint8_t* data;
    if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, *size, (void**)&data)))
        panic("Cannot allocate kernel read buffer");
    uintn_t read_size = *size;
    if (EFI_ERROR(file->Read(file, &read_size, data)) || read_size != *size)
        panic("Cannot read kernel image");
    file->Close(file);
    root->Close(root);
    return data;
}

/* Load a fixed-address AMD64 PE32+ executable. Firmware loads only this EFI
 * application; the kernel has no imports, relocations, or firmware entry point. */
static void load_kernel(efi_boot_services_t* bs, const uint8_t* file, uint64_t size)
{
    if (*(const uint16_t*)file != 0x5a4d) panic("Invalid kernel DOS signature");
    uint32_t pe_offset = *(const uint32_t*)(file + 0x3c);
    if (pe_offset > size - 24) panic("Invalid kernel PE offset");
    const uint8_t* pe = file + pe_offset;
    uint16_t count = *(const uint16_t*)(pe + 6);
    uint16_t optional_size = *(const uint16_t*)(pe + 20);
    uint64_t section_offset = (uint64_t)pe_offset + 24 + optional_size;
    if (*(const uint32_t*)pe != 0x00004550 || *(const uint16_t*)(pe + 4) != 0x8664 ||
        optional_size < 128 || section_offset > size || !count ||
        count >= BOOT_MAX_SECTIONS || count > (size - section_offset) / 40)
        panic("Invalid kernel PE header");
    const uint8_t* optional = pe + 24;
    uint32_t image_size = *(const uint32_t*)(optional + 56);
    uint32_t headers_size = *(const uint32_t*)(optional + 60);
    uint32_t entry = *(const uint32_t*)(optional + 16);
    if (*(const uint16_t*)optional != 0x20b ||
        *(const uint64_t*)(optional + 24) != KERNEL_BASE ||
        *(const uint32_t*)(optional + 32) != PAGE_SIZE_4K ||
        !image_size || image_size > 128 * 1024 * 1024 || (image_size & 0xfff) ||
        headers_size < section_offset + count * 40 || headers_size > size ||
        headers_size > image_size || entry >= image_size ||
        *(const uint32_t*)(optional + 108) < 2 ||
        *(const uint64_t*)(optional + 120) != 0)
        panic("Unsupported kernel PE layout");
    boot_info.kernel_size = image_size;
    boot_info.kernel_physical = allocate_pages(bs, image_size / PAGE_SIZE_4K, 1);
    boot_info.kernel_entry = KERNEL_BASE + entry;
    uint8_t* image = (void*)boot_info.kernel_physical;
    memory_copy(image, file, headers_size);
    boot_info.sections[0] = (boot_section_t){0, (headers_size + 0xfffULL) & ~0xfffULL, PAGE_NX};
    boot_info.section_count = count + 1;
    int entry_executable = 0;
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t* section = file + section_offset + i * 40;
        uint64_t length = *(const uint32_t*)(section + 8);
        uint64_t offset = *(const uint32_t*)(section + 12);
        uint64_t raw_size = *(const uint32_t*)(section + 16);
        uint64_t raw_offset = *(const uint32_t*)(section + 20);
        uint32_t attributes = *(const uint32_t*)(section + 36);
        if (raw_size > length) length = raw_size;
        length = (length + 0xfffULL) & ~0xfffULL;
        if ((offset & 0xfff) || offset > image_size || length > image_size - offset ||
            raw_offset > size || raw_size > size - raw_offset)
            panic("Invalid kernel section range");
        for (uint16_t j = 0; j <= i; j++) {
            boot_section_t* previous = &boot_info.sections[j];
            if (length && offset < previous->offset + previous->size &&
                previous->offset < offset + length) panic("Overlapping kernel sections");
        }
        uint64_t flags = (attributes & 0x80000000u) ? PAGE_RW : 0;
        if (!(attributes & 0x20000000u)) flags |= PAGE_NX;
        if ((flags & PAGE_RW) && !(flags & PAGE_NX)) panic("Writable executable kernel section");
        if (entry >= offset && entry - offset < length && !(flags & PAGE_NX)) entry_executable = 1;
        boot_info.sections[i + 1] = (boot_section_t){offset, length, flags};
        memory_copy(image + offset, file + raw_offset, raw_size);
    }
    if (!entry_executable) panic("Kernel entry is not executable");
}

static uint64_t* next_table(efi_boot_services_t* bs, uint64_t* table, uint64_t index)
{
    if (!(table[index] & PAGE_PRESENT))
        table[index] = allocate_pages(bs, 1, 0) | PAGE_PRESENT | PAGE_RW;
    return (void*)(table[index] & 0x000ffffffffff000ULL);
}

static uint64_t prepare_page_tables(efi_boot_services_t* bs)
{
    uint64_t cr3, cr4;
    __asm__ volatile("mov %%cr3, %0; mov %%cr4, %1" : "=r"(cr3), "=r"(cr4));
    if (cr4 & (1ULL << 12)) panic("Five-level firmware paging unsupported");
    uint64_t* root = (void*)allocate_pages(bs, 1, 0);
    /* Preserve the firmware's lower-half identity mappings until the kernel
     * creates its own PMM and tables. Never alter firmware-owned page tables. */
    memory_copy(root, (void*)(cr3 & 0x000ffffffffff000ULL), 256 * sizeof(uint64_t));
    for (uint64_t i = 0; i < boot_info.section_count; i++) {
        boot_section_t section = boot_info.sections[i];
        for (uint64_t offset = 0; offset < section.size; offset += PAGE_SIZE_4K) {
            uint64_t address = KERNEL_BASE + section.offset + offset;
            uint64_t* pdp = next_table(bs, root, (address >> 39) & 511);
            uint64_t* pd = next_table(bs, pdp, (address >> 30) & 511);
            uint64_t* pt = next_table(bs, pd, (address >> 21) & 511);
            pt[(address >> 12) & 511] = (boot_info.kernel_physical + section.offset + offset) |
                                       section.flags | PAGE_PRESENT;
        }
    }
    return (uint64_t)root;
}

static void exit_boot_services(efi_handle_t handle, efi_boot_services_t* bs)
{
    uintn_t size = 0, key = 0, stride = 0;
    uint32_t version = 0;
    efi_status_t status = bs->GetMemoryMap(&size, NULL, &key, &stride, &version);
    if (status != EFI_BUFFER_TOO_SMALL || !stride) panic("Cannot size memory map");
    uintn_t capacity = size + 8 * stride;
    if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, capacity, &boot_info.memory_map)))
        panic("Cannot allocate memory map");
    /* All allocations and firmware file operations are complete. After the
     * first exit attempt use only GetMemoryMap/ExitBootServices, even on retry. */
    for (int attempt = 0; attempt < 3; attempt++) {
        size = capacity;
        status = bs->GetMemoryMap(&size, boot_info.memory_map, &key, &stride, &version);
        if (EFI_ERROR(status)) panic("Cannot refresh memory map");
        status = bs->ExitBootServices(handle, key);
        if (status == EFI_SUCCESS) break;
        if (status != EFI_INVALID_PARAMETER) panic("Cannot exit boot services");
    }
    if (status != EFI_SUCCESS) panic("ExitBootServices retries exhausted");
    __asm__ volatile("cli" ::: "memory");
    boot_info.memory_map_size = size;
    boot_info.descriptor_size = stride;
    boot_info.descriptor_version = version;
}

__attribute__((naked, noreturn))
static void enter_kernel(uint64_t root, uint64_t entry, const boot_info_t* info)
{
    __asm__ volatile(
        "cli\n"
        "cld\n"
        "mov %rcx, %cr3\n"
        "mov %r8, %rcx\n"
        "and $-16, %rsp\n"
        "sub $32, %rsp\n"
        "call *%rdx\n"
        "1: cli; hlt; jmp 1b\n"
    );
}

efi_status_t efi_main(efi_handle_t handle, efi_system_table_t* system)
{
    efi_boot_services_t* bs = system->BootServices;
    efi_gop_t* gop;
    efi_status_t status = bs->LocateProtocol(&gop_guid, NULL, (void**)&gop);
    if (EFI_ERROR(status) || !gop || !gop->Mode || !gop->Mode->Information) return EFI_UNSUPPORTED;
    efi_gop_mode_info_t* mode = gop->Mode->Information;
    if (!gop->Mode->FrameBufferBase || mode->PixelFormat > PixelBlueGreenRedReserved8BitPerColor ||
        mode->HorizontalResolution < 1024 || mode->VerticalResolution < 640 ||
        mode->PixelsPerScanLine < mode->HorizontalResolution || mode->PixelsPerScanLine > 65535 ||
        mode->VerticalResolution > 65535 ||
        (uint64_t)mode->PixelsPerScanLine * mode->VerticalResolution * 4 > gop->Mode->FrameBufferSize)
        return EFI_UNSUPPORTED;
    boot_info.version = BOOT_INFO_VERSION;
    boot_info.framebuffer = (void*)gop->Mode->FrameBufferBase;
    boot_info.framebuffer_size = gop->Mode->FrameBufferSize;
    boot_info.framebuffer_stride = mode->PixelsPerScanLine;
    boot_info.framebuffer_height = mode->VerticalResolution;
    memory_zero(boot_info.framebuffer, boot_info.framebuffer_size);
    set_FB(boot_info.framebuffer, boot_info.framebuffer_stride, boot_info.framebuffer_height);
    con_clear();
    con_write("Console Subsystem Initialized\nLoading kernel.exe...\n", 0x07);
    efi_loaded_image_protocol_t* image;
    if (EFI_ERROR(bs->HandleProtocol(handle, &image_guid, (void**)&image)))
        panic("Cannot locate loaded image");
    for (uintn_t i = 0; i < system->NumberOfTableEntries; i++) {
        efi_configuration_table_t* table = &system->ConfigurationTable[i];
        if (guid_equal(&table->VendorGuid, &acpi20_guid)) boot_info.rsdp = table->VendorTable;
    }
    if (!boot_info.rsdp) panic("ACPI RSDP not found");
    uint64_t size;
    uint8_t* file = read_kernel(bs, image->DeviceHandle, &size);
    load_kernel(bs, file, size);
    bs->FreePool(file);
    uint64_t root = prepare_page_tables(bs);
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000), "c"(0));
    if (a < 0x80000001) panic("NX capability required");
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001), "c"(0));
    if (!(d & (1u << 20))) panic("NX pages unsupported");
    con_write("Exiting boot services...\n", 0x07);
    con_export_state(&boot_info.console);
    exit_boot_services(handle, bs);
    __asm__ volatile("rdmsr" : "=a"(a), "=d"(d) : "c"(0xc0000080));
    a |= 1u << 11;
    __asm__ volatile("wrmsr" :: "a"(a), "d"(d), "c"(0xc0000080) : "memory");
    /* Flush firmware global/PCID translations before using the new root. */
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 &= ~((1ULL << 7) | (1ULL << 17));
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");
    enter_kernel(root, boot_info.kernel_entry, &boot_info);
}
