#include "uefi.h"
#include "types.h"
#include "console_renderer.h"
#include "console.h"
#include "kernel.h"
#define FALSE 0

// Define the GOP GUID manually if your version of uefi.h doesn't export it globally
// {9042A9DE-23DC-4a38-96FB-7DEE0FA1B394}
static efi_guid_t gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
uintn_t width;
uintn_t height;

int exit_bootservices(efi_handle_t ImageHandle, efi_system_table_t *SystemTable){
    efi_status_t Status;
    efi_boot_services_t *BS = SystemTable->BootServices;
    
    uintn_t MemoryMapSize = 0;
    efi_memory_descriptor_t *MemoryMap = (void*)0; // NULL
    uintn_t MapKey = 0;
    uintn_t DescriptorSize = 0;
    uint32_t DescriptorVersion = 0;

    // 1. Get the required buffer size
    Status = BS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    if (Status != EFI_BUFFER_TOO_SMALL) {
        con_write("Failed: EFI_BUFFER_TOO_SMALL",0x01);
        return 1;
    }

    // 2. Add extra padding because the allocation itself can alter the memory map
    MemoryMapSize += (2 * DescriptorSize);

    // 3. Allocate the buffer using the raw pointer function
    Status = BS->AllocatePool(EfiLoaderData, MemoryMapSize, (void**)&MemoryMap);
    if (Status != EFI_SUCCESS) {
        con_write("Failed to allocate buffer",0x01);
        return 1;
    }

    // 4. Get the fresh memory map and the valid MapKey
    Status = BS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    if (Status != EFI_SUCCESS) {
        con_write("Failed to get memory map",0x01);
        BS->FreePool(MemoryMap);
        return 1;
    }

    // 5. Exit Boot Services immediately (no print or allocation statements between step 4 and 5)
    Status = BS->ExitBootServices(ImageHandle, MapKey);
    if (Status != EFI_SUCCESS) {
        con_write("Failed to exit boot services",0x01);
        // If it fails (e.g., timer interrupt changed the map), you must retry step 4 and 5
        BS->FreePool(MemoryMap);
        return 1;
    }
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
    width = gop->Mode->Information->HorizontalResolution;
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
    con_write("Console Subsystem Initialized\n",0x07);
    con_write("Exiting boot services...\n",0x07);
    if(exit_bootservices(ImageHandle,SystemTable)){
        while (1) {
            __asm__ __volatile__("hlt");
        }
    }
    con_write("Exiting boot services sucsessful\n",0x07);
    
    int k_status = k_main();
    if(k_status == 0){
        con_write("Kernel has exited\n",0x07);
    }else{
        con_write("Kernel has exited with non zero status\n",0x01);
    }
    // 5. Infinite loop to look at your blue screen
    while (1) {
        __asm__ __volatile__("hlt");
    }

    return EFI_SUCCESS;
}
