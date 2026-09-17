#ifndef TYPES_H
#define TYPES_H
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

#if !defined(_STDINT_H) && !defined(_GCC_STDINT_H) && !defined(_EFI_INCLUDE_)
#define _STDINT_H
typedef char                int8_t;
typedef unsigned char       uint8_t;
typedef short               int16_t;
typedef unsigned short      uint16_t;
typedef int                 int32_t;
typedef unsigned int        uint32_t;
#ifndef __clang__
typedef long int            int64_t;
typedef unsigned long int   uint64_t;
typedef unsigned long int   uintptr_t;
#else
typedef long long           int64_t;
typedef unsigned long long  uint64_t;
typedef unsigned long long  uintptr_t;
#endif
#endif

typedef uint64_t size_t;

typedef struct {
    uint64_t start;
    uint64_t page_count;
} FreeRange;

#endif