#include "types.h"
#include "panic.h"
#include "console.h"
#include "acpi.h"
#include "paging.h"
#include "util.h"

#define HHDM_BASE 0xffff800000000000ULL
#define PAGE_2M_ADDR_MASK     0x000FFFFFFFE00000ULL
#define PAGE_SIZE_2M   0x200000ULL
typedef struct {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;

    // ACPI 2.0+
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} __attribute__((packed)) rsdp_t;

static rsdp_t *rsdp;
static acpi_sdt_header_t *xsdt;

static bool checksum_ok(const void *ptr, size_t length)
{
    const uint8_t *bytes = ptr;
    uint32_t sum = 0;

    for (size_t i = 0; i < length; i++){
        sum += bytes[i];
    }

    return (sum & 0xFF)  == 0;
}
static inline void *phys_to_virt(uint64_t phys)
{
    return (void *)(phys);
}
acpi_sdt_header_t* acpi_find_table(const char signature[4])
{
    size_t entry_count =
        (xsdt->length - sizeof(acpi_sdt_header_t))
        / sizeof(uint64_t);

    uint64_t *entries =
        (uint64_t *)((uint8_t *)xsdt
                   + sizeof(acpi_sdt_header_t));
    
    for (size_t i = 0; i < entry_count; i++) {
        uint64_t table_phys = entries[i];

        acpi_sdt_header_t *table =
            (acpi_sdt_header_t *)phys_to_virt(table_phys);

        identitymap_2m(((uint64_t)table) & PAGE_2M_ADDR_MASK);
        if (table->signature[0] == signature[0] &&
            table->signature[1] == signature[1] &&
            table->signature[2] == signature[2] &&
            table->signature[3] == signature[3])
        {
            return table;
        }
    }
    panic("no PCIe found");
}
/*
static void parse_xsdt(uint64_t xsdt_address){
    struct acpi_sdt_header *xsdt = (struct acpi_sdt_header*)phys_to_virt(xsdt_address);
    if (!checksum_ok(xsdt, xsdt->length)) {
        panic("Invalid XSDT checksum");
    }
    
    struct acpi_sdt_header *mcfg = acpi_find_table(xsdt, "MCFG");
    con_write("MCFG found @ ", 0x02);
    print_uint64((uint64_t)mcfg);
    con_write("\nMCFG length: ", 0x07);
    print_uint64(mcfg->length);
    con_write("\n", 0x07);
}
*/
void set_rsdp(const void *ptr){
    rsdp = (rsdp_t*)(ptr);
}
void print_acpi_sig(){
    con_write("ACPI sig: ",0x03);
    char* rsd_sig = (char*)rsdp;
    char seq[] = {
        rsd_sig[0],rsd_sig[1],rsd_sig[2],rsd_sig[3],
        rsd_sig[4],rsd_sig[5],rsd_sig[6],rsd_sig[7],
        0
    };
    con_write((char*)seq,0x07);
    con_write("\n",0x03);
}
void init_acpi(){
    identitymap_2m(((uint64_t)rsdp) & PAGE_2M_ADDR_MASK);
    print_acpi_sig();
    if (!checksum_ok(rsdp, 20)) {
        panic("Invalid RSDP checksum");
    }
    con_write("ACPI RSDP revision: ",0x07);
    print_uint64(rsdp->revision);
    con_write("\n",0x07);
    //printf("ACPI RSDP revision: %u\n", rsdp->revision);

    if (rsdp->revision >= 2) {
        if (!checksum_ok(rsdp, rsdp->length)) {
            panic("Invalid RSDP extended checksum");
        }
        con_write("XSDT physical address: ",0x07);
        print_uint64(rsdp->xsdt_address);
        con_write("\n",0x07);
        
        xsdt = (acpi_sdt_header_t*)rsdp->xsdt_address;
        identitymap_2m(((uint64_t)xsdt) & PAGE_2M_ADDR_MASK);
        if (!checksum_ok(xsdt, xsdt->length)) {
            panic("Invalid XSDT checksum");
        }
    }else{
        panic("incompatible PC");
    }
}