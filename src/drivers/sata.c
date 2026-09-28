#include "sata.h"
#include "nvme.h"
#include "../pcie.h"
#include "../console.h"
#include "../util.h"

#define SATA_TIMEOUT 100000000u
#define SATA_TFES (1u << 30)

typedef struct {
    volatile uint64_t clb, fb;
    volatile uint32_t is, ie, cmd, reserved, tfd, sig, ssts, sctl, serr, sact, ci;
    uint32_t padding[13];
    volatile uint32_t vendor[4];
} sata_port_t;

typedef struct {
    volatile uint32_t cap, ghc, is, pi, vs;
    uint8_t reserved[0x100 - 0x14];
    sata_port_t ports[32];
} sata_hba_t;

typedef struct __attribute__((packed)) {
    uint16_t flags, prdt_length;
    uint32_t bytes_transferred;
    uint64_t table_address;
    uint32_t reserved[4];
} sata_header_t;

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint32_t reserved, byte_count;
} sata_prdt_t;

typedef struct __attribute__((packed)) {
    uint8_t fis[64];
    uint8_t atapi[16];
    uint8_t reserved[48];
    sata_prdt_t prdt;
} sata_table_t;

static sata_header_t command_list[32] __attribute__((aligned(1024)));
static uint8_t received_fis[256] __attribute__((aligned(256)));
static sata_table_t command_table __attribute__((aligned(128)));
static uint8_t data_page[4096] __attribute__((aligned(4096)));
static volatile sata_port_t* port;
static uint64_t sector_count;
static uint32_t sector_size;
static uint8_t storage_kind; /* 1 = NVMe, 2 = SATA */

static int sata_wait(volatile uint32_t* reg, uint32_t mask, uint32_t value)
{
    for (uint32_t i = 0; i < SATA_TIMEOUT; i++) {
        if ((*reg & mask) == value) return 1;
        __asm__ volatile("pause");
    }
    return 0;
}

static int sata_stop_port(void)
{
    port->cmd &= ~1u;
    if (!sata_wait(&port->cmd, 1u << 15, 0)) return 0;
    port->cmd &= ~(1u << 4);
    return sata_wait(&port->cmd, 1u << 14, 0);
}

static int sata_command(uint8_t opcode, uint64_t lba, uint16_t count, uint32_t bytes)
{
    if (!sata_wait(&port->tfd, 0x88, 0)) return 0;
    memory_zero(&command_table, sizeof(command_table));
    command_table.prdt.address = (uint64_t)data_page;
    command_table.prdt.byte_count = bytes - 1;

    uint8_t* fis = command_table.fis;
    fis[0] = 0x27; /* Host to device register FIS. */
    fis[1] = 0x80; /* Command. */
    fis[2] = opcode;
    fis[4] = (uint8_t)lba;
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 0x40; /* LBA mode. */
    fis[8] = (uint8_t)(lba >> 24);
    fis[9] = (uint8_t)(lba >> 32);
    fis[10] = (uint8_t)(lba >> 40);
    fis[12] = (uint8_t)count;
    fis[13] = (uint8_t)(count >> 8);

    command_list[0].flags = 5; /* 20-byte FIS, read, no ATAPI. */
    command_list[0].prdt_length = 1;
    command_list[0].bytes_transferred = 0;
    __sync_synchronize();
    port->is = 0xffffffffu;
    port->ci = 1;
    for (uint32_t i = 0; i < SATA_TIMEOUT; i++) {
        if (port->is & SATA_TFES) return 0;
        if (!(port->ci & 1)) return !(port->tfd & 1);
        __asm__ volatile("pause");
    }
    return 0;
}

static int sata_setup_port(void)
{
    if (!sata_stop_port()) return 0; /* Change DMA buffers only while stopped. */

    memory_zero(command_list, sizeof(command_list));
    memory_zero(received_fis, sizeof(received_fis));
    command_list[0].table_address = (uint64_t)&command_table;
    port->clb = (uint64_t)command_list;
    port->fb = (uint64_t)received_fis;
    port->serr = 0xffffffffu;
    port->is = 0xffffffffu;
    port->cmd |= 1u << 4; /* Receive FISes before starting commands. */
    port->cmd |= 1u;

    if (!sata_command(0xec, 0, 0, 512)) return 0; /* IDENTIFY DEVICE. */
    uint16_t* identify = (uint16_t*)data_page;
    if (!(identify[83] & (1u << 10))) return 0; /* Require LBA48. */
    sector_count = (uint64_t)identify[100] |
                   ((uint64_t)identify[101] << 16) |
                   ((uint64_t)identify[102] << 32) |
                   ((uint64_t)identify[103] << 48);
    sector_size = 512;
    if ((identify[106] & 0xd000u) == 0x5000u)
        sector_size = (((uint32_t)identify[118] << 16) | identify[117]) * 2;
    if (!sector_size || sector_size > 4096 || 4096 % sector_size ||
        sector_count < 4096 / sector_size ||
        !sata_command(0x25, 0, 4096 / sector_size, 4096)) return 0;
    con_write("SATA ready\n", 0x02);
    return 1;
}

void sata_driver_init(void)
{
    uint32_t id = PCIE_NO_DEVICE;
    uint8_t found = 0;
    while ((id = pcie_find_device(1, 6, 1, id)) != PCIE_NO_DEVICE) {
        found = 1;
        con_write("AHCI PCI ID: ", 0x07);
        print_uint64(id);
        con_write("\n", 0x07);
        sata_hba_t* hba = (sata_hba_t*)pcie_get_device_bar(id, 5);
        if (!hba) {
            con_write("AHCI BAR unavailable\n", 0x04);
            continue;
        }
        if (!(hba->cap & (1u << 31)) &&
            ((uint64_t)command_list >> 32 || (uint64_t)received_fis >> 32 ||
             (uint64_t)&command_table >> 32 || (uint64_t)data_page >> 32)) {
            con_write("AHCI requires DMA memory below 4 GiB\n", 0x04);
            continue;
        }
        volatile uint32_t* cap2 = (volatile uint32_t*)((uint8_t*)hba + 0x24);
        volatile uint32_t* bohc = (volatile uint32_t*)((uint8_t*)hba + 0x28);
        if (*cap2 & 1) {
            *bohc |= 2; /* Request ownership from firmware. */
            if (!sata_wait(bohc, 1u | (1u << 4), 0)) {
                con_write("AHCI firmware handoff timeout\n", 0x04);
                continue;
            }
        }
        hba->ghc = (hba->ghc | (1u << 31)) & ~2u; /* AHCI, polled commands. */
        con_write("AHCI ports: ", 0x07);
        print_uint64(hba->pi);
        con_write("\n", 0x07);
        for (uint32_t i = 0; i < 32; i++) {
            if (!(hba->pi & (1u << i))) continue;
            port = &hba->ports[i];
            if ((port->ssts & 0x0f) != 3 ||
                ((port->ssts >> 8) & 0x0f) != 1) continue;
            port->ie = 0;
            con_write("AHCI port status/signature: ", 0x07);
            print_uint64(port->ssts);
            con_write(" ", 0x07);
            print_uint64(port->sig);
            con_write("\n", 0x07);
            if (sata_setup_port()) return;
            con_write("AHCI disk init failed\n", 0x04);
            if (!sata_stop_port()) {
                port = 0;
                return; /* DMA buffers may still be in use. */
            }
        }
        port = 0;
    }
    port = 0;
    if (!found) con_write("AHCI controller not found\n", 0x04);
    else con_write("AHCI: no usable SATA disk\n", 0x04);
}

uint8_t* sata_read_block(uint8_t sata_index, uint64_t lba)
{
    if (sata_index || !port || !sector_size) return 0;
    uint16_t count = 4096 / sector_size;
    if (lba >= sector_count || count > sector_count - lba ||
        lba >> 48) return 0;
    if (!sata_command(0x25, lba, count, 4096)) {
        con_write("SATA read failed\n", 0x04);
        return 0;
    }
    return data_page;
}

void block_storage_init(void)
{
    if (pcie_find_device(1, 8, 2, PCIE_NO_DEVICE) != PCIE_NO_DEVICE) {
        nvme_driver_init();
        if (nvme_read_block(0, 0)) {
            storage_kind = 1;
            return;
        }
    }
    sata_driver_init();
    if (sata_read_block(0, 0)) storage_kind = 2;
    else con_write("No usable storage drive\n", 0x04);
}

uint8_t* block_storage_read_block(uint8_t drive_index, uint64_t lba)
{
    if (drive_index) return 0;
    if (storage_kind == 1) return nvme_read_block(0, lba);
    if (storage_kind == 2) return sata_read_block(0, lba);
    return 0;
}
