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

static void pci_scan_bus(
    const struct acpi_mcfg_entry *entry,
    uint8_t bus)
{
    for (uint8_t device = 0; device < 32; device++) {

        uint16_t vendor =
            pci_read16(
                entry,
                bus,
                device,
                0,
                PCI_VENDOR_ID
            );

        if (vendor == 0xFFFF)
            continue;

        uint16_t device_id =
            pci_read16(
                entry,
                bus,
                device,
                0,
                PCI_DEVICE_ID
            );

        uint8_t class_code =
            pci_read8(
                entry,
                bus,
                device,
                0,
                PCI_CLASS_CODE
            );

        uint8_t subclass =
            pci_read8(
                entry,
                bus,
                device,
                0,
                PCI_SUBCLASS
            );

        uint8_t prog_if =
            pci_read8(
                entry,
                bus,
                device,
                0,
                PCI_PROG_IF
            );

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

        con_write(" class=", 0x07);
        print_uint64(class_code);

        con_write(" subclass=", 0x07);
        print_uint64(subclass);

        con_write(" prog_if=", 0x07);
        print_uint64(prog_if);

        con_write("\n", 0x07);
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