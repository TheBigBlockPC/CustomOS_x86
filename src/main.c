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

void print_uint64_dec(uint64_t val){
    char dec_chars[10] = {'0','1','2','3','4','5','6','7','8','9'};
    int digits = 0;
    char buf[32];
    uint64_t val_temp = val;
    for(int i =0;i<32;i++){
        buf[i] = 0;
    }
    while(val_temp != 0){
        uint64_t digit = val_temp % 10;
        buf[31-digits] = dec_chars[digit];
        digits++;
        val_temp /= 10;
    }
    con_write((char*)(&buf[32-digits]),0x07);
}
// Define the GOP GUID manually if your version of uefi.h doesn't export it globally
// {9042A9DE-23DC-4a38-96FB-7DEE0FA1B394}
static efi_guid_t gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static efi_guid_t gEfiLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static efi_guid_t acpi20_guid = ACPI_20_TABLE_GUID;
uintn_t width;
uintn_t height;

#define PAGE_SIZE_2M   0x200000ULL
#define PAGE_2M_ADDR_MASK     0x000FFFFFFFE00000ULL
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

int exit_bootservices(efi_handle_t ImageHandle, efi_system_table_t *SystemTable){
    efi_status_t Status;
    efi_boot_services_t *BS = SystemTable->BootServices;
    
    uintn_t MemoryMapSize = 0;
    efi_memory_descriptor_t *MemoryMap = (void*)0; // NULL
    uintn_t MapKey = 0;
    uintn_t DescriptorSize = 0;
    uint32_t DescriptorVersion = 0;
    efi_loaded_image_protocol_t *image;

    EFI_STATUS status =
        SystemTable->BootServices->HandleProtocol(
            ImageHandle,
            &gEfiLoadedImageProtocolGuid,
            (void**)&image
        );
    
    if (EFI_ERROR(status)) {
        panic("Failed: ERROR");
    }


    // 1. Get the required buffer size
    Status = BS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    if (Status != EFI_BUFFER_TOO_SMALL) {
        panic("Failed: EFI_BUFFER_TOO_SMALL");
    }

    // 2. Add extra padding because the allocation itself can alter the memory map
    MemoryMapSize += (2 * DescriptorSize);

    // 3. Allocate the buffer using the raw pointer function
    Status = BS->AllocatePool(EfiLoaderData, MemoryMapSize, (void**)&MemoryMap);
    if (Status != EFI_SUCCESS) {
        panic("Failed to allocate buffer");
    }

    // 4. Get the fresh memory map and the valid MapKey
    Status = BS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    if (Status != EFI_SUCCESS) {
        BS->FreePool(MemoryMap);
        panic("Failed to get memory map");
    }

    uintn_t descriptor_count = MemoryMapSize / DescriptorSize;
    for(uintn_t i = 0; i < descriptor_count; i++){
        efi_memory_descriptor_t *desc = (efi_memory_descriptor_t *)((uint8_t*)MemoryMap + i * DescriptorSize);
        if( desc->Type == EfiConventionalMemory){
            efi_physical_address_t start = desc->PhysicalStart;
            uint64_t page_count = desc->NumberOfPages;
            //con_write("found descriptor ",0x02);
            //print_uint64(start);
            //con_write(" ",0x02);
            //print_uint64(page_count);
            //con_write("\n",0x02);
            mem_add_free_pages(start,page_count);
        }
    }
    uint64_t free_memory = mem_get_memory_size();
    con_write("Memory: ",0x02);
    print_uint64_dec(free_memory/1024/1024);
    con_write(" MB\n",0x07);

    // 5. Exit Boot Services immediately (no print or allocation statements between step 4 and 5)
    Status = BS->ExitBootServices(ImageHandle, MapKey);
    if (Status != EFI_SUCCESS) {
        // If it fails (e.g., timer interrupt changed the map), you must retry step 4 and 5
        BS->FreePool(MemoryMap);
        panic("Failed to exit boot services");
    }
    con_write("Exiting boot services sucsessful\n",0x07);

    init_memory_subsystem();

    uint64_t image_base = (uint64_t)image->ImageBase & PAGE_2M_ADDR_MASK;
    uint64_t image_size = image->ImageSize;
    uint64_t mb_chunks = int_div_round_up(image_size,PAGE_SIZE_2M);
    con_write("EFI image base: ",0x02);
    print_uint64(image_base);
    con_write("\nEFI image size: ",0x02);
    print_uint64(image_size);
    con_write("\nEFI image size (2mb pages): ",0x02);
    print_uint64(mb_chunks);
    con_write("\n",0x02);

    for(int i=0;i<mb_chunks+1;i++){
        identitymap_2m(image_base + i * PAGE_SIZE_2M);
    }
    
    void* rsdp = get_rsdp(SystemTable);
    set_rsdp(rsdp);
    print_acpi_sig();
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

    uint64_t mb_chunks = int_div_round_up(fb_size,PAGE_SIZE_2M);
    for(int i=0;i<mb_chunks+1;i++){
        identitymap_2m(((uint64_t)fb_base) + i * PAGE_SIZE_2M);
    }
    cpu_enter_kernel(k_main);
    // 5. Infinite loop to look at your blue screen
    while (1) {
        __asm__ __volatile__("hlt");
    }

    return EFI_SUCCESS;
}
