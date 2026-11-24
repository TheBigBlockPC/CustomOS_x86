/* Polling xHCI hosts with USB 1.x/2.0 device trees and boot keyboards.
 * DMA pages stay owned until Disable Slot completes; on a timeout the controller
 * is stopped and all its pages are retained, never reused. */
#include "../../types.h"
#include "../../pcie.h"
#include "../../paging.h"
#include "../../hpet.h"
#include "../../console.h"
#include "../../util.h"
#include "../../memory.h"
#include "xhci.h"

/* Capability registers (relative to BAR0). */
#define XHCI_CAPLENGTH    0x00
#define XHCI_HCSPARAMS1   0x04
#define XHCI_HCSPARAMS2   0x08
#define XHCI_HCCPARAMS1   0x10
#define XHCI_DBOFF        0x14
#define XHCI_RTSOFF       0x18

/* Operational registers (relative to BAR0 + CAPLENGTH). */
#define XHCI_USBCMD       0x00
#define XHCI_USBSTS       0x04
#define XHCI_PAGESIZE     0x08
#define XHCI_PORT_BASE    0x400
#define XHCI_PORT_STRIDE  0x10

/* USBCMD and USBSTS bits. */
#define XHCI_RUN          (1U << 0)
#define XHCI_HCRST        (1U << 1)
#define XHCI_INTE         (1U << 2)
#define XHCI_HSEE         (1U << 3)
#define XHCI_EWE          (1U << 10)
#define XHCI_HALTED       (1U << 0)
#define XHCI_CNR          (1U << 11)

/* PORTSC bits. */
#define XHCI_PORT_CCS     (1U << 0)
#define XHCI_PORT_PED     (1U << 1)
#define XHCI_PORT_PP      (1U << 9)

/* USB Legacy Support extended capability. */
#define XHCI_EXT_LEGACY   1U
#define XHCI_BIOS_OWNED   (1U << 16)
#define XHCI_OS_OWNED     (1U << 24)
#define XHCI_SMI_PRESERVE 0x000E1FEEU
#define XHCI_SMI_STATUS   0xE0000000U

#define XHCI_MMIO_FLAGS   (PAGE_RW | PAGE_NX | PAGE_UC)

#define TRB_TYPE(n) ((uint32_t)(n) << 10)
#define TRB_IOC (1U << 5)
#define TRB_ISP (1U << 2)
#define RING_ENTRIES 256U
#define PORT_CHANGE_BITS 0x00FE0000U

typedef struct {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} xhci_trb_t;

typedef struct {
    uint64_t physical;
    volatile xhci_trb_t* entries;
    uint16_t enqueue;
    uint8_t cycle;
} xhci_ring_t;

_Static_assert(sizeof(xhci_trb_t) == 16, "xHCI TRB size");

struct xhci_device {
    xhci_controller_t* host;
    xhci_device_t* parent;
    uint64_t allocation;
    uint32_t route;
    uint8_t slot, port, speed, depth, parent_port, tt_slot, tt_port;
    uint8_t hub_ports, tt_think, keyboard_dci;
    uint16_t ep0_packet, keyboard_packet;
    uint64_t input_context, output_context, control_data, keyboard_data;
    xhci_ring_t control_ring, keyboard_ring;
    uint64_t keyboard_trb;
    bool keyboard_pending, keyboard_failed;
    uint8_t report[8];
};

struct xhci_controller {
    xhci_controller_t* next;
    volatile uint8_t* base;
    uint64_t base_phys;
    volatile uint8_t* operational;
    uint64_t operational_phys;
    volatile uint8_t* runtime;
    uint64_t runtime_phys;
    volatile uint8_t* doorbell;
    uint64_t doorbell_phys;
    uint8_t max_slots, max_ports;
    uint16_t max_interrupters, version, scratchpad_count;
    uint8_t context_size;
    bool dma64, ready;
    uint64_t dcbaa, event_page, erst, scratch_array;
    xhci_ring_t commands;
    uint16_t event_index;
    uint8_t event_cycle;
    bool protocol_ports, usb2_port[256];
    xhci_device_t* devices[256];
};

_Static_assert(sizeof(xhci_controller_t) <= PAGE_SIZE, "controller metadata page");
_Static_assert(sizeof(xhci_device_t) <= PAGE_SIZE, "device metadata page");
static xhci_controller_t* controllers;
static bool attempted;
static void xhci_keyboard_event(xhci_controller_t* hc, const xhci_trb_t* event);

static inline uint32_t xhci_read32(volatile uint8_t* base, uint32_t offset)
{
    return *(volatile uint32_t*)(base + offset);
}

static inline void xhci_write32(volatile uint8_t* base, uint32_t offset,
                                uint32_t value)
{
    *(volatile uint32_t*)(base + offset) = value;
}

static void xhci_map_mmio(volatile uint8_t* base, uint64_t offset,
                          uint64_t size)
{
    /* PCI BARs in CustomOS are identity mapped, not HHDM RAM pointers. */
    mmu_identity_map_range((uint64_t)base + offset, size, XHCI_MMIO_FLAGS);
}

static bool xhci_wait(volatile uint8_t* base, uint32_t offset,
                      uint32_t mask, uint32_t expected, uint64_t timeout_ms)
{
    uint64_t start = hpet_millis();
    for (;;) {
        uint32_t value = xhci_read32(base, offset);
        if (value == 0xFFFFFFFFU) return false;
        if ((value & mask) == expected) return true;
        if (hpet_millis() - start >= timeout_ms) return false;
        __asm__ volatile("pause");
    }
}

static bool xhci_read_capabilities(xhci_controller_t* hc)
{
    /* Read CAPLENGTH and HCIVERSION together using a DWORD access. */
    uint32_t cap = xhci_read32(hc->base, XHCI_CAPLENGTH);
    uint32_t params1 = xhci_read32(hc->base, XHCI_HCSPARAMS1);
    uint32_t params2 = xhci_read32(hc->base, XHCI_HCSPARAMS2);
    uint32_t hcc = xhci_read32(hc->base, XHCI_HCCPARAMS1);
    uint32_t dboff = xhci_read32(hc->base, XHCI_DBOFF);
    uint32_t rtsoff = xhci_read32(hc->base, XHCI_RTSOFF);
    uint32_t caplength = cap & 0xFFU;

    if (cap == 0xFFFFFFFFU || params1 == 0xFFFFFFFFU ||
        params2 == 0xFFFFFFFFU || hcc == 0xFFFFFFFFU ||
        dboff == 0xFFFFFFFFU || rtsoff == 0xFFFFFFFFU ||
        caplength < 0x20 || (caplength & 3)) return false;
    
    hc->version = cap >> 16;
    hc->operational_phys = hc->base_phys + caplength;
    hc->operational = (volatile uint8_t*)mem_phys_to_virt(hc->operational_phys);
    hc->doorbell_phys = hc->base_phys + (dboff & ~3U);
    hc->doorbell = (volatile uint8_t*)mem_phys_to_virt(hc->doorbell_phys);
    hc->runtime_phys = hc->base_phys + (rtsoff & ~31U);
    hc->runtime = (volatile uint8_t*)mem_phys_to_virt(hc->runtime_phys);
    hc->max_slots = params1 & 0xFFU;
    hc->max_interrupters = (params1 >> 8) & 0x7FFU;
    hc->max_ports = (params1 >> 24) & 0xFFU;
    hc->dma64 = (hcc & 1U) != 0;
    hc->context_size = (hcc & (1U << 2)) ? 64 : 32;
    hc->scratchpad_count = (((params2 >> 21) & 0x1FU) << 5) |
                            ((params2 >> 27) & 0x1FU);
    
    
    /* Runtime interrupter zero and doorbells are mapped during ring setup. */
    xhci_map_mmio((volatile uint8_t*)hc->operational_phys, 0, 0x40);
    con_write("xHCI version: ", 0x07);
    print_uint64_full(hc->version);
    con_write("\n  Slots: ", 0x07);
    print_uint64_dec(hc->max_slots);
    con_write("  Ports: ", 0x07);
    print_uint64_dec(hc->max_ports);
    con_write("  Interrupters: ", 0x07);
    print_uint64_dec(hc->max_interrupters);
    con_write("\n  Context bytes: ", 0x07);
    print_uint64_dec(hc->context_size);
    con_write("  Scratchpads: ", 0x07);
    print_uint64_dec(hc->scratchpad_count);
    con_write(hc->dma64 ? "  DMA: 64-bit\n" : "  DMA: 32-bit\n", 0x07);

    return hc->max_slots && hc->max_interrupters && hc->max_ports;
}

static void xhci_list_ports(xhci_controller_t* hc)
{
    xhci_map_mmio((volatile uint8_t*)hc->operational_phys, XHCI_PORT_BASE,
                  (uint64_t)hc->max_ports * XHCI_PORT_STRIDE);
    
    for (uint32_t i = 0; i < hc->max_ports; i++) {
        uint32_t offset = XHCI_PORT_BASE + i * XHCI_PORT_STRIDE;
        uint32_t status = xhci_read32(hc->operational, offset);
        con_write("USB port ", 0x07);
        print_uint64_dec(i + 1);
        if (status == 0xFFFFFFFFU) {
            con_write(": MMIO read failed\n", 0x04);
            continue;
        }
        if (status & XHCI_PORT_CCS) {
            con_write(": Connected", 0x02);
            con_write("  Speed ID: ", 0x07);
            print_uint64_dec((status >> 10) & 0xFU);
        } else {
            con_write(": Empty", 0x07);
        }
        con_write((status & XHCI_PORT_PED) ? "  Enabled" : "  Disabled", 0x07);
        con_write((status & XHCI_PORT_PP) ? "  Powered\n" : "  Unpowered\n", 0x07);
    }
}

static bool xhci_take_ownership(xhci_controller_t* hc)
{
    uint32_t hcc = xhci_read32(hc->base, XHCI_HCCPARAMS1);
    if (hcc == 0xFFFFFFFFU) return false;
    uint32_t offset = (hcc >> 16) * 4U;
    
    /* xECP is an absolute DWORD offset; Next is a relative DWORD offset. */
    for (uint32_t i = 0; offset && i < 256; i++) {
        xhci_map_mmio((volatile uint8_t*)hc->base_phys, offset, 4);
        uint32_t cap = xhci_read32(hc->base, offset);
        if (cap == 0xFFFFFFFFU) return false;

        if ((cap & 0xFFU) == XHCI_EXT_LEGACY) {
            xhci_map_mmio((volatile uint8_t*)hc->base_phys, offset + 4, 4);
            xhci_write32(hc->base, offset, cap | XHCI_OS_OWNED);
            if (!xhci_wait(hc->base, offset,
                           XHCI_BIOS_OWNED | XHCI_OS_OWNED,
                           XHCI_OS_OWNED, 5000)) {
                con_write("xHCI: BIOS did not release ownership\n", 0x04);
                return false;
            }

            /* Disable legacy SMI enables and acknowledge pending SMI status.
             * Keep the register's preserved bits, not the enable bits. */
            uint32_t ctl = xhci_read32(hc->base, offset + 4);
            if (ctl == 0xFFFFFFFFU) return false;
            ctl = (ctl & XHCI_SMI_PRESERVE) | XHCI_SMI_STATUS;
            xhci_write32(hc->base, offset + 4, ctl);
            con_write("xHCI: OS ownership acquired\n", 0x02);
            return true;
        }

        uint32_t next = (cap >> 8) & 0xFFU;
        if (!next) {
            offset = 0;
            break;
        }
        offset += next * 4U;
    }
    
    if (offset) return false;  /* Malformed/overlong capability list. */
    con_write("xHCI: no legacy ownership capability\n", 0x07);
    return true;
}

static bool xhci_protocols(xhci_controller_t* hc)
{
    uint32_t offset = (xhci_read32(hc->base, XHCI_HCCPARAMS1) >> 16) * 4U;
    for (uint32_t i = 0; offset && i < 256; i++) {
        xhci_map_mmio((volatile uint8_t*)hc->base_phys, offset, 16);
        uint32_t cap = xhci_read32(hc->base, offset);
        if (cap == 0xFFFFFFFFU) return false;
        if ((cap & 0xFFU) == 2) { /* Supported Protocol capability. */
            uint32_t ports = xhci_read32(hc->base, offset + 8);
            uint32_t first = ports & 0xFFU, count = (ports >> 8) & 0xFFU;
            if (!first || !count || first + count - 1 > hc->max_ports) return false;
            hc->protocol_ports = true;
            if ((cap >> 24) == 2)
                for (uint32_t port = first; port < first + count; port++) hc->usb2_port[port] = true;
        }
        uint32_t next = (cap >> 8) & 0xFFU;
        if (!next) return true;
        offset += next * 4U;
    }
    return !offset;
}

static bool xhci_reset(xhci_controller_t* hc)
{
    volatile uint8_t* op = hc->operational;

    if (!xhci_wait(op, XHCI_USBSTS, XHCI_CNR, 0, 5000)) {
        con_write("xHCI: controller not ready before reset\n", 0x04);
        return false;
    }

    /* Halt execution and disable interrupts before resetting. */
    uint32_t cmd = xhci_read32(op, XHCI_USBCMD);
    if (cmd == 0xFFFFFFFFU) return false;
    cmd &= ~(XHCI_RUN | XHCI_INTE | XHCI_HSEE | XHCI_EWE);
    xhci_write32(op, XHCI_USBCMD, cmd);

    if (!xhci_wait(op, XHCI_USBSTS, XHCI_HALTED, XHCI_HALTED, 100)) {
        con_write("xHCI: controller did not halt\n", 0x04);
        return false;
    }

    /* Reset while halted. This is not a USB2 device/port reset. */
    xhci_write32(op, XHCI_USBCMD, cmd | XHCI_HCRST);

    /* Allow at least 1 ms before further register access (Intel quirk).
     * Two millisecond ticks avoid rounding down to less than 1 ms. */
    uint64_t start = hpet_millis();
    while (hpet_millis() - start < 2) __asm__ volatile("pause");

    if (!xhci_wait(op, XHCI_USBCMD, XHCI_HCRST, 0, 5000)) {
        con_write("xHCI: reset bit did not clear\n", 0x04);
        return false;
    }
    if (!xhci_wait(op, XHCI_USBSTS, XHCI_CNR, 0, 5000)) {
        con_write("xHCI: controller not ready after reset\n", 0x04);
        return false;
    }
    if (!xhci_wait(op, XHCI_USBSTS, XHCI_HALTED, XHCI_HALTED, 100)) {
        con_write("xHCI: not halted after reset\n", 0x04);
        return false;
    }

    con_write("xHCI: reset successful; controller halted\n", 0x02);
    return true;
}

/* Ring ownership is published through the cycle bit, after descriptor/data
 * writes. The fences also keep the compiler from moving DMA accesses. */
static void xhci_barrier(void) { __sync_synchronize(); }

static void xhci_write_address(xhci_controller_t* hc, volatile uint8_t* base,
                                uint32_t offset, uint64_t address)
{
    xhci_write32(base, offset, (uint32_t)address);
    if (hc->dma64) xhci_write32(base, offset + 4, address >> 32);
}

static uint64_t xhci_dma_page(xhci_controller_t* hc)
{
    uint64_t physical = mem_alloc_page();
    if (!hc->dma64 && physical > 0xFFFFF000ULL) {
        mem_free_page(physical);
        con_write("xHCI: DMA page above controller address limit\n", 0x04);
        return 0;
    }
    return physical;
}

static bool xhci_ring_init(xhci_controller_t* hc, xhci_ring_t* ring)
{
    ring->physical = xhci_dma_page(hc);
    if (!ring->physical) return false;
    ring->entries = mem_phys_to_virt(ring->physical);
    ring->enqueue = 0;
    ring->cycle = 1;
    ring->entries[RING_ENTRIES - 1].parameter = ring->physical;
    ring->entries[RING_ENTRIES - 1].control = TRB_TYPE(6) | 2U | 1U;
    return true;
}

/* One outstanding command/TD per ring, so a producer cannot lap its consumer. */
static uint64_t xhci_enqueue(xhci_ring_t* ring, uint64_t parameter,
                             uint32_t status, uint32_t control)
{
    if (ring->enqueue == RING_ENTRIES - 1) {
        xhci_barrier();
        ring->entries[RING_ENTRIES - 1].control = TRB_TYPE(6) | 2U | ring->cycle;
        ring->enqueue = 0;
        ring->cycle ^= 1;
    }
    uint16_t index = ring->enqueue++;
    ring->entries[index].parameter = parameter;
    ring->entries[index].status = status;
    xhci_barrier();
    ring->entries[index].control = control | ring->cycle;
    return ring->physical + index * sizeof(xhci_trb_t);
}

static void xhci_fail(xhci_controller_t* hc)
{
    hc->ready = false;
    xhci_write32(hc->operational, XHCI_USBCMD, 0);
    (void)xhci_wait(hc->operational, XHCI_USBSTS,
                    XHCI_HALTED, XHCI_HALTED, 100);
    /* Even if halt fails, retaining every page prevents late DMA into reuse. */
    con_write("xHCI: stopped after controller/transfer failure\n", 0x04);
}

static bool xhci_event(xhci_controller_t* hc, xhci_trb_t* event)
{
    volatile xhci_trb_t* entries = mem_phys_to_virt(hc->event_page);
    volatile xhci_trb_t* entry = &entries[hc->event_index];
    uint32_t control = entry->control;
    if ((control & 1U) != hc->event_cycle) return false;
    xhci_barrier();
    event->parameter = entry->parameter;
    event->status = entry->status;
    event->control = control;
    if (++hc->event_index == RING_ENTRIES) {
        hc->event_index = 0;
        hc->event_cycle ^= 1;
    }
    xhci_barrier();
    /* EHB is W1C; ERDP points at the next event the CPU will consume. */
    xhci_write_address(hc, hc->runtime, 0x38,
                       (hc->event_page + hc->event_index * 16) | 8U);
    return true;
}

static bool xhci_command(xhci_controller_t* hc, uint64_t parameter,
                          uint32_t control, uint8_t* slot)
{
    if (!hc->ready) return false;
    uint64_t physical = xhci_enqueue(&hc->commands, parameter, 0, control);
    xhci_barrier();
    xhci_write32(hc->doorbell, 0, 0);
    uint64_t start = hpet_millis();
    while (hpet_millis() - start < 1000) {
        xhci_trb_t event;
        bool available = xhci_event(hc, &event);
        if (available) xhci_keyboard_event(hc, &event);
        if (available && ((event.control >> 10) & 63U) == 33 &&
            (event.parameter & ~15ULL) == physical) {
            if (slot) *slot = event.control >> 24;
            if ((event.status >> 24) == 1) return true;
            con_write("xHCI: command completion error ", 0x04);
            print_uint64_dec(event.status >> 24);
            con_write("\n", 0x04);
            return false;
        }
        if (xhci_read32(hc->operational, XHCI_USBSTS) & ((1U << 2) | (1U << 12)))
            break;
        __asm__ volatile("pause");
    }
    xhci_fail(hc);
    return false;
}

static bool xhci_start(xhci_controller_t* hc)
{
    /* One 4 KiB scratchpad pointer array. Larger implementations are outside
     * this small driver's scope; all individual buffers are still 4 KiB. */
    if (hc->scratchpad_count > 512) return false;
    hc->dcbaa = xhci_dma_page(hc);
    hc->event_page = xhci_dma_page(hc);
    hc->erst = xhci_dma_page(hc);
    if (!hc->dcbaa || !hc->event_page || !hc->erst ||
        !xhci_ring_init(hc, &hc->commands)) return false;
    if (hc->scratchpad_count) {
        hc->scratch_array = xhci_dma_page(hc);
        if (!hc->scratch_array) return false;
        uint64_t* scratch = mem_phys_to_virt(hc->scratch_array);
        for (uint16_t i = 0; i < hc->scratchpad_count; i++) {
            scratch[i] = xhci_dma_page(hc);
            if (!scratch[i]) return false;
        }
        ((uint64_t*)mem_phys_to_virt(hc->dcbaa))[0] = hc->scratch_array;
    }
    uint64_t* erst = mem_phys_to_virt(hc->erst);
    erst[0] = hc->event_page;
    erst[1] = RING_ENTRIES; /* Segment size DWORD, followed by a reserved DWORD. */
    hc->event_cycle = 1;
    xhci_map_mmio((volatile uint8_t*)hc->runtime_phys, 0, 0x40);
    xhci_map_mmio((volatile uint8_t*)hc->doorbell_phys, 0, ((uint64_t)hc->max_slots + 1) * 4);
    xhci_map_mmio((volatile uint8_t*)hc->operational_phys, XHCI_PORT_BASE,
                   (uint64_t)hc->max_ports * XHCI_PORT_STRIDE);
    xhci_write32(hc->runtime, 0x20, 1); /* Clear IP, leave interrupt enable off. */
    xhci_write32(hc->runtime, 0x24, 0);
    xhci_write32(hc->runtime, 0x28, 1);
    xhci_write_address(hc, hc->runtime, 0x30, hc->erst);
    xhci_write_address(hc, hc->runtime, 0x38, hc->event_page);
    xhci_write_address(hc, hc->operational, 0x30, hc->dcbaa);
    xhci_write_address(hc, hc->operational, 0x18, hc->commands.physical | 1U);
    /* Hubs and children need independent slots. */
    xhci_write32(hc->operational, 0x38, hc->max_slots);
    xhci_barrier();
    xhci_write32(hc->operational, XHCI_USBCMD, XHCI_RUN);
    if (!xhci_wait(hc->operational, XHCI_USBSTS, XHCI_HALTED, 0, 100)) {
        xhci_fail(hc);
        return false;
    }
    hc->ready = true;
    if (!xhci_command(hc, 0, TRB_TYPE(23), 0)) { /* No-Op self-check. */
        if (hc->ready) xhci_fail(hc);
        return false;
    }
    con_write("xHCI: command/event rings running (polling)\n", 0x02);
    return true;
}

uint8_t xhci_port_count(xhci_controller_t* hc)
{
    return hc && hc->ready ? hc->max_ports : 0;
}

static uint32_t* xhci_input(xhci_device_t* dev, uint8_t index)
{
    return (uint32_t*)((uint8_t*)mem_phys_to_virt(dev->input_context) +
                      (uint32_t)index * dev->host->context_size);
}

bool xhci_close_device(xhci_device_t* dev)
{
    if (!dev) return true;
    xhci_controller_t* hc = dev->host;
    if (!hc->ready) return false;
    if (dev->slot && !xhci_command(hc, 0, TRB_TYPE(10) |
                                         ((uint32_t)dev->slot << 24), 0)) {
        xhci_fail(hc);
        return false;
    }
    if (dev->slot)
        ((uint64_t*)mem_phys_to_virt(hc->dcbaa))[dev->slot] = 0;
    /* Disable Slot completion establishes that these device pages are idle. */
    uint64_t pages[] = {dev->input_context, dev->output_context,
        dev->control_data, dev->control_ring.physical,
        dev->keyboard_data, dev->keyboard_ring.physical};
    for (uint32_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++)
        if (pages[i]) mem_free_page(pages[i]);
    hc->devices[dev->slot] = 0;
    mem_free_page(dev->allocation);
    return true;
}

/* Only software-defined fields; never echo output-only slot state. */
static void xhci_slot_context(xhci_device_t* dev, uint8_t entries)
{
    uint32_t* slot = xhci_input(dev, 1);
    slot[0] = dev->route | ((uint32_t)dev->speed << 20) |
              ((uint32_t)entries << 27) | (dev->hub_ports ? 1U << 26 : 0);
    slot[1] = ((uint32_t)dev->port << 16) | ((uint32_t)dev->hub_ports << 24);
    slot[2] = dev->tt_slot | ((uint32_t)dev->tt_port << 8) |
              ((uint32_t)dev->tt_think << 16);
}

static xhci_device_t* xhci_address_device(xhci_controller_t* hc,
    xhci_device_t* parent, uint8_t root_port, uint8_t parent_port, uint8_t speed)
{
    uint64_t allocation = mem_alloc_page();
    xhci_device_t* dev = mem_phys_to_virt(allocation);
    dev->allocation = allocation;
    dev->host = hc;
    dev->parent = parent;
    dev->port = root_port;
    dev->speed = speed;
    dev->parent_port = parent_port;
    if (parent) {
        dev->depth = parent->depth + 1;
        dev->route = parent->route |
            ((uint32_t)(parent_port > 15 ? 15 : parent_port) << (parent->depth * 4));
        if (speed != 3) {
            dev->tt_slot = parent->speed == 3 ? parent->slot : parent->tt_slot;
            dev->tt_port = parent->speed == 3 ? parent_port : parent->tt_port;
        }
    }
    dev->ep0_packet = dev->speed == 3 ? 64 : 8;
    if (!xhci_command(hc, 0, TRB_TYPE(9), &dev->slot)) {
        mem_free_page(dev->allocation);
        return 0;
    }
    if (!dev->slot || dev->slot > hc->max_slots) {
        xhci_fail(hc);
        return 0;
    }
    hc->devices[dev->slot] = dev;
    dev->input_context = xhci_dma_page(hc);
    dev->output_context = xhci_dma_page(hc);
    dev->control_data = xhci_dma_page(hc);
    if (!dev->input_context || !dev->output_context ||
        !dev->control_data || !xhci_ring_init(hc, &dev->control_ring)) {
        (void)xhci_close_device(dev);
        return 0;
    }
    ((uint64_t*)mem_phys_to_virt(hc->dcbaa))[dev->slot] = dev->output_context;
    xhci_input(dev, 0)[1] = 3; /* Add slot context and endpoint zero. */
    xhci_slot_context(dev, 1);
    uint32_t* ep = xhci_input(dev, 2);
    ep[1] = (3U << 1) | (4U << 3) | ((uint32_t)dev->ep0_packet << 16);
    ep[2] = (uint32_t)dev->control_ring.physical | 1U;
    ep[3] = dev->control_ring.physical >> 32;
    ep[4] = 8;
    if (!xhci_command(hc, dev->input_context, TRB_TYPE(11) |
                       ((uint32_t)dev->slot << 24), 0)) {
        (void)xhci_close_device(dev);
        return 0;
    }
    return dev;
}

xhci_device_t* xhci_open_port(xhci_controller_t* hc, uint8_t port)
{
    if (!hc || !hc->ready || !port || port > hc->max_ports)
        return 0;
    if (hc->protocol_ports && !hc->usb2_port[port]) return 0;
    uint32_t offset = XHCI_PORT_BASE + (uint32_t)(port - 1) * XHCI_PORT_STRIDE;
    uint32_t status = xhci_read32(hc->operational, offset);
    /* PORTSC has RW1C change bits and a RW1CS enable bit. Never echo them in
     * a read/modify/write. Only preserve ordinary RW fields when resetting. */
    uint32_t preserve = status & ((15U << 5) | XHCI_PORT_PP | (3U << 14) | (7U << 25));
    if (!(status & XHCI_PORT_PP)) {
        xhci_write32(hc->operational, offset, preserve | XHCI_PORT_PP);
        uint64_t start = hpet_millis();
        while (hpet_millis() - start < 20) __asm__ volatile("pause");
        status = xhci_read32(hc->operational, offset);
        preserve |= XHCI_PORT_PP;
    }
    if (!(status & XHCI_PORT_CCS)) return 0;
    /* Limit this implementation to standard USB2 speed IDs: full/low/high.
     * SuperSpeed endpoints and protocol-specific speed tables are unsupported. */
    uint8_t speed = (status >> 10) & 15U;
    if (speed > 3) return 0; /* Some hosts report speed only after reset. */
    uint64_t start = hpet_millis();
    while (hpet_millis() - start < 100) __asm__ volatile("pause"); /* Connect debounce. */
    xhci_write32(hc->operational, offset, preserve | (1U << 4));
    if (!xhci_wait(hc->operational, offset,
                    (1U << 4) | XHCI_PORT_PED, XHCI_PORT_PED, 1000)) return 0;
    status = xhci_read32(hc->operational, offset);
    if (!(status & XHCI_PORT_CCS)) return 0;
    xhci_write32(hc->operational, offset, preserve | (status & PORT_CHANGE_BITS));
    start = hpet_millis();
    while (hpet_millis() - start < 10) __asm__ volatile("pause"); /* Reset recovery. */
    speed = (status >> 10) & 15U;
    if (speed < 1 || speed > 3) return 0;
    return xhci_address_device(hc, 0, port, 0, speed);
}

xhci_device_t* xhci_open_hub_port(xhci_device_t* hub, uint8_t port, uint8_t speed)
{
    if (!hub || !hub->host->ready || !port || port > hub->hub_ports ||
        hub->depth >= 5 || speed < 1 || speed > 3 ||
        (hub->speed != 3 && speed == 3)) return 0;
    return xhci_address_device(hub->host, hub, hub->port, port, speed);
}

bool xhci_configure_hub(xhci_device_t* dev, uint8_t ports, uint8_t tt_think)
{
    if (!dev || !dev->host->ready || !ports || dev->speed == 2 || dev->depth >= 5)
        return false;
    xhci_controller_t* hc = dev->host;
    dev->hub_ports = ports;
    dev->tt_think = dev->speed == 3 ? tt_think & 3U : 0;
    memory_zero(mem_phys_to_virt(dev->input_context), PAGE_SIZE);
    xhci_input(dev, 0)[1] = 1;
    xhci_slot_context(dev, 1);
    /* Configure Endpoint initializes hub/port/TT fields; Evaluate Context does not. */
    return xhci_command(hc, dev->input_context, TRB_TYPE(12) |
                        ((uint32_t)dev->slot << 24), 0);
}

bool xhci_set_ep0_packet_size(xhci_device_t* dev, uint8_t packet_size)
{
    if (!dev) return false;
    xhci_controller_t* hc = dev->host;
    if (!hc->ready || !dev->slot ||
        (packet_size != 8 && packet_size != 16 && packet_size != 32 && packet_size != 64) ||
        (dev->speed == 2 && packet_size != 8) ||
        (dev->speed == 3 && packet_size != 64)) return false;
    if (packet_size == dev->ep0_packet) return true;
    memory_zero(mem_phys_to_virt(dev->input_context), PAGE_SIZE);
    xhci_input(dev, 0)[1] = 2;
    uint8_t* output = mem_phys_to_virt(dev->output_context);
    memory_copy(xhci_input(dev, 2), output + hc->context_size, hc->context_size);
    xhci_input(dev, 2)[1] = (xhci_input(dev, 2)[1] & 0xFFFFU) | ((uint32_t)packet_size << 16);
    if (!xhci_command(hc, dev->input_context, TRB_TYPE(13) |
                       ((uint32_t)dev->slot << 24), 0)) return false;
    dev->ep0_packet = packet_size;
    return true;
}

int xhci_control(xhci_device_t* dev, uint8_t request_type, uint8_t request, uint16_t value,
                 uint16_t index, void* data, uint16_t length)
{
    if (!dev) return -1;
    xhci_controller_t* hc = dev->host;
    if (!hc->ready || !dev->slot || length > PAGE_SIZE ||
        (length && !data)) return -1;
    bool input = (request_type & 0x80U) != 0;
    void* bounce = mem_phys_to_virt(dev->control_data);
    if (length && !input) memory_copy(bounce, data, length);
    uint64_t setup = request_type | ((uint64_t)request << 8) |
        ((uint64_t)value << 16) | ((uint64_t)index << 32) | ((uint64_t)length << 48);
    uint32_t transfer_type = length ? (input ? 3U : 2U) : 0;
    xhci_enqueue(&dev->control_ring, setup, 8,
                  TRB_TYPE(2) | (1U << 6) | (transfer_type << 16));
    uint64_t data_trb = 0;
    if (length)
        data_trb = xhci_enqueue(&dev->control_ring, dev->control_data,
                                 length, TRB_TYPE(3) | TRB_ISP | (input ? 1U << 16 : 0));
    uint64_t status_trb = xhci_enqueue(&dev->control_ring, 0, 0,
        TRB_TYPE(4) | TRB_IOC | ((!length || !input) ? 1U << 16 : 0));
    xhci_barrier();
    xhci_write32(hc->doorbell, (uint32_t)dev->slot * 4, 1);
    uint64_t start = hpet_millis();
    uint32_t actual = length;
    while (hpet_millis() - start < 1000) {
        xhci_trb_t event;
        bool available = xhci_event(hc, &event);
        if (available) xhci_keyboard_event(hc, &event);
        if (available && ((event.control >> 10) & 63U) == 32 &&
            (event.control >> 24) == dev->slot &&
            ((event.control >> 16) & 31U) == 1) {
            uint32_t code = event.status >> 24;
            uint64_t pointer = event.parameter & ~15ULL;
            if (code == 13 && pointer == data_trb) {
                uint32_t remaining = event.status & 0xFFFFFFU;
                if (remaining > length) { xhci_fail(hc); return -1; }
                actual = length - remaining;
            } else if (code != 1) {
                con_write("xHCI: control transfer error ", 0x04);
                print_uint64_dec(code);
                con_write("\n", 0x04);
                return -1; /* Caller must close the device, not reuse EP0. */
            } else if (pointer == status_trb) {
                if (length && input) memory_copy(data, bounce, actual);
                return (int)actual;
            }
        }
        __asm__ volatile("pause");
    }
    xhci_fail(hc);
    return -1;
}

static void xhci_keyboard_arm(xhci_device_t* dev)
{
    xhci_controller_t* hc = dev->host;
    dev->keyboard_trb = xhci_enqueue(&dev->keyboard_ring,
        dev->keyboard_data, dev->keyboard_packet,
        TRB_TYPE(1) | TRB_IOC | TRB_ISP);
    xhci_barrier();
    xhci_write32(hc->doorbell, (uint32_t)dev->slot * 4,
                  dev->keyboard_dci);
}

bool xhci_keyboard_endpoint(xhci_device_t* dev, uint8_t address,
                            uint16_t packet_size, uint8_t interval)
{
    if (!dev) return false;
    xhci_controller_t* hc = dev->host;
    if (!hc->ready || !dev->slot || dev->keyboard_dci ||
        !(address & 0x80U) || (address & 0x70U) || !(address & 15U) ||
        packet_size < 8 || packet_size > (dev->speed == 3 ? 1024 :
                                         dev->speed == 2 ? 8 : 64) ||
        !interval || (dev->speed == 3 && interval > 16)) return false;
    uint8_t exponent = 0;
    if (dev->speed == 3) exponent = interval - 1;
    else {
        for (uint8_t frames = interval; frames > 1; frames >>= 1) exponent++;
        exponent += 3; /* USB frames -> xHCI's 125 us microframes. */
    }
    dev->keyboard_data = xhci_dma_page(hc);
    if (!dev->keyboard_data || !xhci_ring_init(hc, &dev->keyboard_ring)) return false;
    uint8_t dci = (address & 15U) * 2 + 1;
    memory_zero(mem_phys_to_virt(dev->input_context), PAGE_SIZE);
    xhci_input(dev, 0)[1] = 1U | (1U << dci);
    xhci_slot_context(dev, dci);
    uint32_t* ep = xhci_input(dev, dci + 1);
    ep[0] = (uint32_t)exponent << 16;
    ep[1] = (3U << 1) | (7U << 3) | ((uint32_t)packet_size << 16);
    ep[2] = (uint32_t)dev->keyboard_ring.physical | 1U;
    ep[3] = dev->keyboard_ring.physical >> 32;
    ep[4] = packet_size | ((uint32_t)packet_size << 16);
    if (!xhci_command(hc, dev->input_context, TRB_TYPE(12) |
                       ((uint32_t)dev->slot << 24), 0)) return false;
    dev->keyboard_packet = packet_size;
    dev->keyboard_dci = dci;
    xhci_keyboard_arm(dev);
    return true;
}

/* Every consumer of a host event ring dispatches interrupt completions. A hub
 * status request must not discard a key arriving on another device's ring. */
static void xhci_keyboard_event(xhci_controller_t* hc, const xhci_trb_t* event)
{
    if (((event->control >> 10) & 63U) != 32) return;
    xhci_device_t* dev = hc->devices[event->control >> 24];
    if (!dev || !dev->keyboard_dci ||
        ((event->control >> 16) & 31U) != dev->keyboard_dci ||
        (event->parameter & ~15ULL) != dev->keyboard_trb) return;
    uint32_t code = event->status >> 24;
    uint32_t residual = event->status & 0xFFFFFFU;
    if ((code != 1 && code != 13) || residual > dev->keyboard_packet) {
        dev->keyboard_failed = true;
        return;
    }
    if (dev->keyboard_packet - residual < 8) {
        xhci_keyboard_arm(dev);
        return;
    }
    memory_copy(dev->report, mem_phys_to_virt(dev->keyboard_data), 8);
    dev->keyboard_pending = true; /* Rearm only after the report is consumed. */
}

bool xhci_keyboard_poll(xhci_device_t* dev, uint8_t report[8])
{
    if (!dev || !dev->host->ready || !dev->keyboard_dci || dev->keyboard_failed)
        return false;
    xhci_controller_t* hc = dev->host;
    if (xhci_read32(hc->operational, XHCI_USBSTS) & ((1U << 2) | (1U << 12))) {
        xhci_fail(hc);
        return false;
    }
    xhci_trb_t event;
    for (uint32_t i = 0; i < RING_ENTRIES && xhci_event(hc, &event); i++)
        xhci_keyboard_event(hc, &event);
    if (!dev->keyboard_pending) return false;
    memory_copy(report, dev->report, 8);
    dev->keyboard_pending = false;
    xhci_keyboard_arm(dev); /* A keyboard can NAK forever; no idle timeout. */
    return true;
}

bool xhci_keyboard_active(xhci_device_t* dev)
{
    if (!dev || !dev->host->ready || !dev->keyboard_dci || dev->keyboard_failed)
        return false;
    xhci_controller_t* hc = dev->host;
    uint32_t offset = XHCI_PORT_BASE + (uint32_t)(dev->port - 1) * XHCI_PORT_STRIDE;
    uint32_t status = xhci_read32(hc->operational, offset);
    return status != 0xFFFFFFFFU && (status & XHCI_PORT_CCS) && (status & XHCI_PORT_PED);
}

xhci_device_t* xhci_parent(xhci_device_t* dev) { return dev ? dev->parent : 0; }
uint8_t xhci_parent_port(xhci_device_t* dev) { return dev ? dev->parent_port : 0; }

/* Initialize each PCI host independently, including hosts after a failed one. */
static bool xhci_init_controller(xhci_controller_t* hc, uint32_t id)
{
    uint64_t bar = pcie_get_device_bar(id, 0);
    if (!bar) return false;
    hc->base_phys = bar;
    hc->base = (volatile uint8_t*)mem_phys_to_virt(bar);
    con_write("xHCI BAR0: ", 0x07);
    print_uint64_full(bar);
    con_write("\n", 0x07);
    if (!xhci_read_capabilities(hc)) {
        con_write("xHCI: invalid capabilities\n", 0x04);
        return false;
    }
    if (!xhci_take_ownership(hc)) {
        con_write("xHCI: ownership handoff failed\n", 0x04);
        return false;
    }
    if (!xhci_protocols(hc)) {
        con_write("xHCI: invalid supported protocol capabilities\n", 0x04);
        return false;
    }
    xhci_list_ports(hc);
    if (!xhci_reset(hc)) return false;
    uint32_t pages = xhci_read32(hc->operational, XHCI_PAGESIZE);
    con_write("xHCI PAGESIZE bitmap: ", 0x07);
    print_uint64_full(pages);
    con_write("\n", 0x07);
    if (pages == 0xFFFFFFFFU || !(pages & 1U)) {
        con_write("xHCI: 4 KiB controller pages unavailable\n", 0x04);
        return false;
    }
    return xhci_start(hc);
}

bool xhci_init(void)
{
    if (attempted) return controllers != 0;
    attempted = true;
    uint32_t id = PCIE_NO_DEVICE;
    xhci_controller_t** tail = &controllers;
    while ((id = pcie_find_device(0x0C, 0x03, 0x30, id)) != PCIE_NO_DEVICE) {
        uint64_t allocation = mem_alloc_page();
        xhci_controller_t* hc = mem_phys_to_virt(allocation);
        if (!xhci_init_controller(hc, id)) {
            /* A failed start may have published DMA addresses: retain pages. */
            con_write("xHCI: host initialization failed; trying next host\n", 0x04);
            continue;
        }
        *tail = hc;
        tail = &hc->next;
    }
    if (!controllers) con_write("xHCI: no usable controller found\n", 0x07);
    return controllers != 0;
}

xhci_controller_t* xhci_next_controller(xhci_controller_t* previous)
{
    return previous ? previous->next : controllers;
}
