#include "uefi.h"
#include "types.h"
#include "console_renderer.h"
#include "console.h"
#include "kernel.h"
#include "memory.h"
#include "cpu.h"
#include "util.h"
#include "panic.h"
#include "acpi.h"

#include "paging.h"
#define FALSE 0

// Define the GOP GUID manually if your version of uefi.h doesn't export it globally
// {9042A9DE-23DC-4a38-96FB-7DEE0FA1B394}
static efi_guid_t gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static efi_guid_t gEfiLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static efi_guid_t acpi20_guid = ACPI_20_TABLE_GUID;
uintn_t width;
uintn_t height;

int guid_equal(const efi_guid_t *a, const efi_guid_t *b)
{
    if (a->Data1 != b->Data1) return 0;
    if (a->Data2 != b->Data2) return 0;
    if (a->Data3 != b->Data3) return 0;

    for (int i = 0; i < 8; i++) {
        if (a->Data4[i] != b->Data4[i])
            return 0;
    }

    return 1;
}

void* get_rsdp(efi_system_table_t *SystemTable){
    for(uintn_t i = 0;i < SystemTable->NumberOfTableEntries; i++){
        efi_configuration_table_t *table = &SystemTable->ConfigurationTable[i];
        if (guid_equal(&table->VendorGuid, &acpi20_guid)) {
            return table->VendorTable;
        }
    }
    panic("RSP not found");
}

/* UEFI loads a PE32+ image. Map its pages according to section permissions. */
static void map_kernel_image(uint64_t base, uint64_t size)
{
    uint8_t* image = (uint8_t*)base;
    if ((base & (PAGE_SIZE_4K - 1)) || size < 64 || *(uint16_t*)image != 0x5a4d)
        panic("Invalid kernel image");
    uint32_t pe_offset = *(uint32_t*)(image + 0x3c);
    if (pe_offset > size - 24) panic("Invalid PE header offset");
    uint8_t* pe = image + pe_offset;
    if (*(uint32_t*)pe != 0x00004550) panic("Invalid PE signature");
    uint16_t count = *(uint16_t*)(pe + 6);
    uint16_t optional_size = *(uint16_t*)(pe + 20);
    uint64_t sections = (uint64_t)pe_offset + 24 + optional_size;
    if (optional_size < 36 || sections > size || count > (size - sections) / 40 ||
        *(uint16_t*)(pe + 24) != 0x20b || *(uint32_t*)(pe + 56) < PAGE_SIZE_4K)
        panic("Unsupported PE section layout");
    mmu_identity_map_range(base, size, PAGE_NX);
    for (uint16_t i = 0; i < count; i++) {
        uint8_t* section = image + sections + i * 40;
        uint32_t length = *(uint32_t*)(section + 8);
        uint32_t raw_size = *(uint32_t*)(section + 16);
        uint32_t offset = *(uint32_t*)(section + 12);
        uint32_t attributes = *(uint32_t*)(section + 36);
        if (raw_size > length) length = raw_size;
        if ((offset & (PAGE_SIZE_4K - 1)) || offset > size || length > size - offset)
            panic("Invalid PE section range");
        uint64_t flags = (attributes & 0x80000000u) ? PAGE_RW : 0;
        if (!(attributes & 0x20000000u)) flags |= PAGE_NX;
        if ((flags & PAGE_RW) && !(flags & PAGE_NX)) panic("Writable executable kernel section");
        mmu_identity_map_range(base + offset, length, flags);
    }
}

int exit_bootservices(efi_handle_t ImageHandle, efi_system_table_t *SystemTable)
{
    efi_boot_services_t* BS = SystemTable->BootServices;
    efi_loaded_image_protocol_t* image;
    efi_status_t status = BS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (void**)&image);
    if (EFI_ERROR(status)) panic("Failed to locate loaded image");
    uint64_t image_base = (uint64_t)image->ImageBase;
    uint64_t image_size = image->ImageSize;
    set_rsdp(get_rsdp(SystemTable));

    uintn_t map_size = 0, map_key = 0, descriptor_size = 0;
    uint32_t descriptor_version = 0;
    efi_memory_descriptor_t* map = NULL;
    status = BS->GetMemoryMap(&map_size, map, &map_key, &descriptor_size, &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL || !descriptor_size) panic("Failed to size memory map");
    uintn_t capacity = map_size + 8 * descriptor_size;
    status = BS->AllocatePool(EfiLoaderData, capacity, (void**)&map);
    if (EFI_ERROR(status)) panic("Failed to allocate memory map");

    /* Retry a stale key using only GetMemoryMap/ExitBootServices. No firmware
     * allocation, freeing, or logging after the first exit attempt. */
    for (int attempt = 0; attempt < 3; attempt++) {
        map_size = capacity;
        status = BS->GetMemoryMap(&map_size, map, &map_key, &descriptor_size, &descriptor_version);
        if (EFI_ERROR(status)) panic("Failed to refresh memory map");
        status = BS->ExitBootServices(ImageHandle, map_key);
        if (status == EFI_SUCCESS) break;
        if (status != EFI_INVALID_PARAMETER) panic("Failed to exit boot services");
    }
    if (status != EFI_SUCCESS) panic("ExitBootServices retries exhausted");
    __asm__ volatile("cli" ::: "memory");
    if (descriptor_size < sizeof(efi_memory_descriptor_t) || map_size % descriptor_size)
        panic("Invalid firmware memory map");
    for (uintn_t offset = 0; offset < map_size; offset += descriptor_size) {
        efi_memory_descriptor_t* desc = (void*)((uint8_t*)map + offset);
        if (desc->Type == EfiConventionalMemory)
            mem_add_free_pages(desc->PhysicalStart, desc->NumberOfPages);
    }
    con_write("Exited boot services\n", 0x02);
    init_memory_subsystem();
    map_kernel_image(image_base, image_size);
    return 0;
}

efi_status_t efi_main(efi_handle_t ImageHandle, efi_system_table_t *SystemTable) {
    efi_status_t status;
    efi_gop_t *gop = NULL;
    
    // 1. Clear the text screen
    SystemTable->ConOut->Reset(SystemTable->ConOut, FALSE);
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Locating GOP Framebuffer...\r\n");

    // 2. Locate the Graphics Output Protocol
    status = SystemTable->BootServices->LocateProtocol(&gop_guid, NULL, (void**)&gop);
    if (EFI_ERROR(status) || gop == NULL) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Error: Could not find GOP!\r\n");
        while (1){}
        return status;
    }

    // 3. Extract the physical Framebuffer Address and Size
    // POSIX-UEFI maps the Mode structure layout to match the UEFI standard spec
    uint32_t *fb_base = (uint32_t *)gop->Mode->FrameBufferBase;
    uintn_t fb_size   = gop->Mode->FrameBufferSize;
    width = gop->Mode->Information->PixelsPerScanLine;
    height = gop->Mode->Information->VerticalResolution;
    if (fb_base == 0) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Error: Framebuffer base is null!\r\n");
        while (1){}
        return EFI_NOT_STARTED;
    }

    //SystemTable->BootServices->ExitBootServices(ImageHandle,0);
    
    // clearing screen
    uintn_t total_pixels = fb_size / sizeof(uint32_t);
    for (uintn_t i = 0; i < total_pixels; i++) {
        fb_base[i] = 0x00000000; // Black
    }

    // initializing
    set_FB(fb_base,width,height);
    con_clear();
    con_write("Framebuffer: ",0x07);
    print_uint64((uint64_t)fb_base);
    con_write("\n",0x07);
    con_write("Console Subsystem Initialized\n",0x07);
    con_write("Exiting boot services...\n",0x07);
    if(exit_bootservices(ImageHandle,SystemTable)){
        while (1) {
            __asm__ __volatile__("hlt");
        }
    }

    mmu_identity_map_range((uint64_t)fb_base, fb_size,
                           PAGE_RW | PAGE_NX | PAGE_PCD | PAGE_PWT);
    cpu_enter_kernel(k_main);
    // 5. Infinite loop to look at your blue screen
    while (1) {
        __asm__ __volatile__("hlt");
    }

    return EFI_SUCCESS;
}
