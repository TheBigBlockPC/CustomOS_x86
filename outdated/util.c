#include "efi_local.h"
void PrintMessage(EFI_SYSTEM_TABLE *SystemTable, CHAR16 *String) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, String);
}
