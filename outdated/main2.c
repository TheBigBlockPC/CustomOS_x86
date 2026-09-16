#include "uefi.h"
#define FALSE 0
typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef unsigned long long UINTN;
typedef unsigned long long UINT64;
typedef short CHAR16;
typedef unsigned long long EFI_STATUS;
typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;
typedef UINT64 EFI_PHYSICAL_ADDRESS;

efi_status_t efi_main(efi_handle_t ImageHandle, efi_system_table_t *SystemTable) {
    // 1. Clear the screen
    SystemTable->ConOut->Reset(SystemTable->ConOut, FALSE);
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Keyboard Echo Started. Type anything (Press 'ESC' to exit):\r\n\r\n");
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"> ");

    // 2. Clear out any leftover key strokes in the buffer
    SystemTable->ConIn->Reset(SystemTable->ConIn, FALSE);

    while (1) {
        UINTN Index;
        efi_input_key_t Key;

        // 3. Wait until a key is pressed (blocks execution until an event happens)
        SystemTable->BootServices->WaitForEvent(1, &SystemTable->ConIn->WaitForKey, &Index);

        // 4. Read the key from the buffer
        efi_status_t Status = SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key);
        
        if (Status == EFI_SUCCESS) {
            // Check if the user pressed the Escape key (ScanCode 0x17)
            if (Key.ScanCode == 0x17) {
                SystemTable->ConOut->OutputString(SystemTable->ConOut, L"\r\nExiting application...\r\n");
                break;
            }

            // 5. Echo printable characters (UnicodeChar) back to the screen
            if (Key.UnicodeChar != 0) {
                // ConOut->OutputString requires a null-terminated string.
                // We create a tiny 2-character array to print a single character safely.
                CHAR16 EchoStr[2] = { (CHAR16)Key.UnicodeChar, 0 };
                
                // If the user presses Enter (\r), make sure we add a line feed (\n)
                if (Key.UnicodeChar == L'\r') {
                    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"\r\n");
                } else {
                    SystemTable->ConOut->OutputString(SystemTable->ConOut, EchoStr);
                }
            }
        }
    }

    return EFI_SUCCESS;
}
