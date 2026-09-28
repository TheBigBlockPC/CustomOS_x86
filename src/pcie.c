#include "types.h"
#include "acpi.h"
#include "console.h"
#include "paging.h"
#include "pcie.h"
#include "util.h"

#define PAGE_2M (2u * 1024u * 1024u)

typedef struct {
    acpi_sdt_header_t header;
    uint64_t reserved;
} __attribute__((packed)) mcfg_t;

typedef struct {
    uint64_t base;
    uint16_t segment;
    uint8_t first_bus, last_bus;
    uint32_t reserved;
} __attribute__((packed)) mcfg_entry_t;

static mcfg_entry_t* mcfg_entries;
static uint32_t mcfg_count;

static volatile uint8_t* pcie_config(uint32_t range, uint8_t bus,
                                     uint8_t device, uint8_t function)
{
    mcfg_entry_t* entry = &mcfg_entries[range];
    uint64_t address = entry->base + ((uint64_t)(bus - entry->first_bus) << 20)
                     + ((uint64_t)device << 15) + ((uint64_t)function << 12);
    identitymap_2m(address & ~(uint64_t)(PAGE_2M - 1));
    return (volatile uint8_t*)address;
}

void pci_init(void)
{
    mcfg_t* mcfg = (mcfg_t*)acpi_find_table("MCFG");
    mcfg_count = (mcfg->header.length - sizeof(*mcfg)) / sizeof(mcfg_entry_t);
    mcfg_entries = (mcfg_entry_t*)(mcfg + 1);
    con_write("MCFG entries: ", 0x07);
    print_uint64(mcfg_count);
    con_write("\n", 0x07);
}

uint32_t pcie_find_device(uint8_t class_code, uint8_t subclass,
                          uint8_t prog_if, uint32_t after)
{
    for (uint32_t range = 0; range < mcfg_count; range++) {
        mcfg_entry_t* entry = &mcfg_entries[range];
        if (entry->segment) continue; /* This OS uses PCI segment zero. */
        for (uint32_t bus = entry->first_bus; bus <= entry->last_bus; bus++) {
            for (uint32_t device = 0; device < 32; device++) {
                volatile uint8_t* cfg = pcie_config(range, bus, device, 0);
                if (*(volatile uint16_t*)cfg == 0xffff) continue;
                uint32_t functions = (cfg[0x0e] & 0x80) ? 8 : 1;
                for (uint32_t function = 0; function < functions; function++) {
                    if (function) cfg = pcie_config(range, bus, device, function);
                    if (*(volatile uint16_t*)cfg == 0xffff) continue;
                    uint32_t id = (range << 16) | (bus << 8) |
                                  (device << 3) | function;
                    if (cfg[0x0b] == class_code && cfg[0x0a] == subclass &&
                        cfg[0x09] == prog_if &&
                        (after == PCIE_NO_DEVICE || id > after)) return id;
                }
            }
        }
    }
    return PCIE_NO_DEVICE;
}

uint64_t pcie_get_device_bar(uint32_t id, uint8_t bar)
{
    if (id == PCIE_NO_DEVICE || bar > 5 || (id >> 16) >= mcfg_count) return 0;
    volatile uint8_t* cfg = pcie_config(id >> 16, (id >> 8) & 0xff,
                                        (id >> 3) & 0x1f, id & 7);
    volatile uint32_t* bars = (volatile uint32_t*)(cfg + 0x10);
    uint32_t low = bars[bar];
    if (!low || low == 0xffffffffu || (low & 1) ||
        (((low >> 1) & 3) != 0 && ((low >> 1) & 3) != 2)) return 0;
    uint64_t address = low & ~15u;
    if (((low >> 1) & 3) == 2) {
        if (bar == 5) return 0;
        address |= (uint64_t)bars[bar + 1] << 32;
    }
    if (!address) return 0;

    volatile uint16_t* command = (volatile uint16_t*)(cfg + 4);
    *command |= 6; /* Memory space and bus mastering for AHCI/NVMe DMA. */
    identitymap_2m(address & ~(uint64_t)(PAGE_2M - 1));
    identitymap_2m((address + 0x2000) & ~(uint64_t)(PAGE_2M - 1));
    return address;
}
