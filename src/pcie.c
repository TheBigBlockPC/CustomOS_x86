#include "types.h"
#include "acpi.h"
#include "util.h"
#include "console.h"
#include "paging.h"
#include "panic.h"

#define PAGE_2M_ADDR_MASK     0x000FFFFFFFE00000ULL
#define PAGE_SIZE_2M   0x200000ULL

#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_PROG_IF         0x09
#define PCI_SUBCLASS        0x0A
#define PCI_CLASS_CODE      0x0B
#define PCI_HEADER_TYPE     0x0E

struct acpi_mcfg {
    acpi_sdt_header_t header;  // 0x24 bytes
    uint64_t reserved;              // 0x08 bytes
} __attribute__((packed));

struct acpi_mcfg_entry {
    uint64_t base_address;
    uint16_t segment_group;
    uint8_t  start_bus;
    uint8_t  end_bus;
    uint32_t reserved;
} __attribute__((packed));

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t  revision_id;
    uint8_t  class_prog_if;
    uint8_t  subclass;
    uint8_t  class_code;
    uint8_t  cache_line_size;
    uint8_t  latency_timer;
    uint8_t  header_type;
    uint8_t  bist;
    uint32_t bars[6]; // Offset 0x10: BAR0 through BAR5
} __attribute__((packed)) pcie_device_config_t;

static uint64_t pci_cfg_phys(
    const struct acpi_mcfg_entry *entry,
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint16_t offset)
{
    if (bus < entry->start_bus || bus > entry->end_bus)
        panic("PCI bus out of range");

    if (device >= 32)
        panic("PCI device out of range");

    if (function >= 8)
        panic("PCI function out of range");

    if (offset >= 4096)
        panic("PCI config offset out of range");

    return entry->base_address
        + ((uint64_t)bus << 20)
        + ((uint64_t)device << 15)
        + ((uint64_t)function << 12)
        + offset;
}

static void *pci_cfg_ptr(
    const struct acpi_mcfg_entry *entry,
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint16_t offset)
{
    uint64_t phys =
        pci_cfg_phys(entry, bus, device, function, offset);

    uint64_t page_2m =
        phys & ~(PAGE_SIZE_2M - 1);

    identitymap_2m(page_2m);

    return (void *)phys;
}

uint8_t pci_read8(
    const struct acpi_mcfg_entry *entry,
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint16_t offset)
{
    volatile uint8_t *ptr =
        pci_cfg_ptr(entry, bus, device, function, offset);

    return *ptr;
}

uint16_t pci_read16(
    const struct acpi_mcfg_entry *entry,
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint16_t offset)
{
    volatile uint16_t *ptr =
        pci_cfg_ptr(entry, bus, device, function, offset);

    return *ptr;
}

uint32_t pci_read32(
    const struct acpi_mcfg_entry *entry,
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint16_t offset)
{
    volatile uint32_t *ptr =
        pci_cfg_ptr(entry, bus, device, function, offset);

    return *ptr;
}

bool get_pcie_bar64_address(volatile pcie_device_config_t* config, uint8_t bar_index, uint64_t* out_address) {
    // 1. Sanity check: Ensure the index is within range
    if (bar_index > 5) return false;

    uint32_t bar_low = config->bars[bar_index];

    // 2. Check Bit 0: Must be 0 for Memory Space
    if ((bar_low & 0x1) != 0) {
        // This is an I/O space BAR, not supported by this 64-bit memory function
        return false; 
    }

    // 3. Check Bits 1-2: Memory Type
    uint8_t mem_type = (bar_low >> 1) & 0x3;

    if (mem_type == 0x2) {
        // 64-bit Memory Space: Requires this BAR and the next one
        if (bar_index >= 5) {
            // A 64-bit BAR cannot start at the final index (BAR5) because it needs BAR6 (which doesn't exist)
            return false; 
        }

        uint32_t bar_high = config->bars[bar_index + 1];

        // 4. Combine low and high, masking out the lower 4 flag bits of the low BAR
        uint64_t address_low  = (uint64_t)(bar_low & 0xFFFFFFF0);
        uint64_t address_high = ((uint64_t)bar_high) << 32;

        *out_address = address_high | address_low;
        return true;

    } else if (mem_type == 0x0) {
        // 32-bit Memory Space: Only occupies this single BAR slot
        *out_address = (uint64_t)(bar_low & 0xFFFFFFF0);
        return true;
    }

    // mem_type 0x1 is reserved by the PCI specification
    return false;
}

static void pci_scan_bus(
    const struct acpi_mcfg_entry *entry,
    uint8_t bus)
{
    for (uint8_t device = 0; device < 32; device++) {
        pcie_device_config_t *dev_config = pci_cfg_ptr(
            entry,
            bus,
            device,
            0,
            0
        );
        uint16_t vendor = dev_config->vendor_id;

        if (vendor == 0xFFFF)
            continue;

        uint16_t device_id = dev_config->device_id;
        uint8_t class_code =dev_config->class_code;
        uint8_t subclass = dev_config->subclass;
        uint8_t prog_if = dev_config->class_prog_if;

        uint64_t bar_0_phys = 0;
        bool valid = get_pcie_bar64_address(dev_config,0,&bar_0_phys);

        if(bar_0_phys == 0){
            continue;
        }

        con_write("PCI ", 0x07);

        print_uint64(entry->segment_group);
        con_write(":", 0x07);

        print_uint64(bus);
        con_write(":", 0x07);

        print_uint64(device);
        con_write(".0 ", 0x07);

        con_write("vendor=", 0x07);
        print_uint64(vendor);

        con_write(" device=", 0x07);
        print_uint64(device_id);

        //con_write(" class=", 0x07);
        con_write(" ", 0x07);
        print_uint64(class_code);

        //con_write(" subclass=", 0x07);
        con_write(":", 0x07); 
        print_uint64(subclass);

        //con_write(" prog_if=", 0x07);
        con_write(":", 0x07);
        print_uint64(prog_if);
        con_write("\n", 0x07);
        if(valid){
            con_write("Bar_0 @ ", 0x07);
            print_uint64(bar_0_phys);
            con_write("\n", 0x07);
        }else{
            con_write("INVALLID BAR 0\n", 0x01);
        }
    }
}

void pci_init(void)
{
    struct acpi_mcfg *mcfg =
        (struct acpi_mcfg *)acpi_find_table("MCFG");
    //identitymap_2m(((uint64_t)mcfg) & PAGE_2M_ADDR_MASK);
    size_t entry_count =
        (mcfg->header.length - sizeof(struct acpi_mcfg))
        / sizeof(struct acpi_mcfg_entry);

    struct acpi_mcfg_entry *entries =
        (struct acpi_mcfg_entry *)
        ((uint8_t *)mcfg + sizeof(struct acpi_mcfg));

    con_write("MCFG entries: ", 0x07);
    print_uint64(entry_count);

    con_write("\nECAM base: ", 0x07);
    print_uint64(entries[0].base_address);

    con_write("\nSegment: ", 0x07);
    print_uint64(entries[0].segment_group);

    con_write("\nStart bus: ", 0x07);
    print_uint64(entries[0].start_bus);

    con_write("\nEnd bus: ", 0x07);
    print_uint64(entries[0].end_bus);

    con_write("\n", 0x07);
    
    //uint16_t vendor =
    //pci_read16(&entries[0], 0, 0, 0, 0x00);

    //con_write("PCI 00:00.0 vendor: ", 0x07);
    //print_uint64(vendor);
    //con_write("\n", 0x07);

    pci_scan_bus(&entries[0], entries[0].start_bus);
}