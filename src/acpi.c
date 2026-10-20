#include "types.h"
#include "panic.h"
#include "console.h"
#include "acpi.h"
#include "paging.h"

typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) rsdp_t;

static rsdp_t* rsdp;
static acpi_sdt_header_t* xsdt;

static bool checksum_ok(const void* ptr, size_t length)
{
    const uint8_t* bytes = ptr;
    uint8_t sum = 0;
    for (size_t i = 0; i < length; i++) sum += bytes[i];
    return sum == 0;
}

static acpi_sdt_header_t* map_table(uint64_t address)
{
    if (!address) panic("ACPI: null table address");
    mmu_identity_map_range(address, sizeof(acpi_sdt_header_t), PAGE_NX);
    acpi_sdt_header_t* table = (void*)address;
    if (table->length < sizeof(*table) || table->length > 0x1000000)
        panic("ACPI: invalid table length");
    mmu_identity_map_range(address, table->length, PAGE_NX);
    if (!checksum_ok(table, table->length)) panic("ACPI: invalid table checksum");
    return table;
}

acpi_sdt_header_t* acpi_find_table(const char signature[4])
{
    if (!xsdt) panic("ACPI: not initialized");
    size_t entry_count = (xsdt->length - sizeof(*xsdt)) / sizeof(uint64_t);
    uint64_t* entries = (void*)((uint8_t*)xsdt + sizeof(*xsdt));
    for (size_t i = 0; i < entry_count; i++) {
        acpi_sdt_header_t* table = map_table(entries[i]);
        if (table->signature[0] == signature[0] && table->signature[1] == signature[1] &&
            table->signature[2] == signature[2] && table->signature[3] == signature[3])
            return table;
    }
    panic("ACPI: requested table not found");
}

void set_rsdp(const void* ptr) { rsdp = (rsdp_t*)ptr; }

void print_acpi_sig(void)
{
    char signature[9];
    for (int i = 0; i < 8; i++) signature[i] = rsdp->signature[i];
    signature[8] = 0;
    con_write("ACPI sig: ", 0x03);
    con_write(signature, 0x07);
    con_write("\n", 0x07);
}

void init_acpi(void)
{
    if (!rsdp) panic("ACPI: missing RSDP");
    mmu_identity_map_range((uint64_t)rsdp, sizeof(*rsdp), PAGE_NX);
    const char signature[] = "RSD PTR ";
    for (int i = 0; i < 8; i++)
        if (rsdp->signature[i] != signature[i]) panic("ACPI: invalid RSDP signature");
    if (!checksum_ok(rsdp, 20)) panic("ACPI: invalid RSDP checksum");
    if (rsdp->revision < 2) panic("ACPI: XSDT required");
    if (rsdp->length < sizeof(*rsdp) || rsdp->length > PAGE_SIZE_4K)
        panic("ACPI: invalid RSDP length");
    mmu_identity_map_range((uint64_t)rsdp, rsdp->length, PAGE_NX);
    if (!checksum_ok(rsdp, rsdp->length)) panic("ACPI: invalid extended checksum");
    xsdt = map_table(rsdp->xsdt_address);
    if (xsdt->signature[0] != 'X' || xsdt->signature[1] != 'S' ||
        xsdt->signature[2] != 'D' || xsdt->signature[3] != 'T' ||
        (xsdt->length - sizeof(*xsdt)) % sizeof(uint64_t)) panic("ACPI: invalid XSDT");
    print_acpi_sig();
}
