#ifndef EFI_H
#define EFI_H

// Basic UEFI Data Types
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

// Explicitly define an EFI GUID structure
typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

// GOP GUID Definition {9042A9DE-23DC-4a38-96FB-7DEE0FA1B394}
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042A9DE, 0x23DC, 0x4a38, { 0x96, 0xFB, 0x7D, 0xEE, 0x0F, 0xA1, 0xB3, 0x94 } }

// ---------------------------------------------------------
// Graphics Output Protocol (GOP) Structures
// ---------------------------------------------------------
typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
    void *QueryMode;
    void *SetMode;
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

// ---------------------------------------------------------
// Core EFI System & Boot Services Structures
// ---------------------------------------------------------
typedef struct _EFI_BOOT_SERVICES {
    char Reserved1[120]; // Skips standard header and initial handles/mem functions
    // LocateProtocol offset in EFI_BOOT_SERVICES
    EFI_STATUS (*LocateProtocol)(EFI_GUID *Protocol, void *Registration, void **Interface);
} EFI_BOOT_SERVICES;

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void *Reset;
    EFI_STATUS (*OutputString)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
    char Reserved1[52]; // Standard offset to find headers and runtime/boot services pointers
    EFI_HANDLE ConInHandle;
    void *ConIn;
    EFI_HANDLE ConOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    void *StandardErrorHandle;
    void *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices; // Exposed BootServices pointer
} EFI_SYSTEM_TABLE;

// Function Declarations
void PrintMessage(EFI_SYSTEM_TABLE *SystemTable, CHAR16 *String);

#endif
