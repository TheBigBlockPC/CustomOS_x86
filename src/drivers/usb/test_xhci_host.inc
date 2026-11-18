/* Synthetic DMA completions exercise topology/context rules that QEMU's
 * full-speed-only hub cannot cover. Never linked into the kernel. */
#include "xhci.c"
#include "../../util.c"

extern int printf(const char*, ...);
extern void exit(int) __attribute__((noreturn));
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static uint8_t pages[256][4096] __attribute__((aligned(4096)));
static bool used[256];
static uint64_t now;
void* mem_phys_to_virt(uint64_t p) { return (void*)p; }
uint64_t mem_alloc_page(void) {
    for (unsigned i = 0; i < 256; i++) if (!used[i]) {
        used[i] = true; memory_zero(pages[i], 4096); return (uint64_t)pages[i];
    }
    CHECK(false); return 0;
}
void mem_free_page(uint64_t p) {
    for (unsigned i = 0; i < 256; i++) if (p == (uint64_t)pages[i]) {
        CHECK(used[i]); used[i] = false; return;
    }
    CHECK(false);
}
uint64_t hpet_millis(void) { return now++; }
void con_write(char* p, uint8_t c) { (void)p; (void)c; }
void mmu_identity_map_range(uint64_t p, uint64_t n, uint64_t f) { (void)p; (void)n; (void)f; }
uint32_t pcie_find_device(uint8_t c, uint8_t s, uint8_t p, uint32_t a)
{ (void)c; (void)s; (void)p; (void)a; return PCIE_NO_DEVICE; }
uint64_t pcie_get_device_bar(uint32_t id, uint8_t b) { (void)id; (void)b; return 0; }

static void setup(xhci_controller_t* hc, uint8_t context_size) {
    memory_zero(hc, sizeof(*hc));
    hc->ready = hc->dma64 = true;
    hc->context_size = context_size;
    hc->max_slots = 64;
    hc->dcbaa = mem_alloc_page();
    hc->event_page = mem_alloc_page();
    hc->runtime = mem_phys_to_virt(mem_alloc_page());
    hc->operational = mem_phys_to_virt(mem_alloc_page());
    hc->doorbell = mem_phys_to_virt(mem_alloc_page());
    hc->event_cycle = 1;
    CHECK(xhci_ring_init(hc, &hc->commands));
}

static void event_at(xhci_controller_t* hc, unsigned ahead, uint64_t pointer,
                      uint32_t status, uint32_t control) {
    unsigned index = hc->event_index + ahead;
    xhci_trb_t* entries = mem_phys_to_virt(hc->event_page);
    entries[index % RING_ENTRIES] = (xhci_trb_t){pointer, status,
        control | (hc->event_cycle ^ (index >= RING_ENTRIES))};
}

static void command_completion(xhci_controller_t* hc, unsigned ahead,
                                 uint8_t slot, uint8_t code) {
    event_at(hc, ahead, hc->commands.physical + (hc->commands.enqueue + ahead) * 16,
             (uint32_t)code << 24, TRB_TYPE(33) | ((uint32_t)slot << 24));
}

static xhci_device_t* root(xhci_controller_t* hc, uint8_t slot, uint8_t speed) {
    command_completion(hc, 0, slot, 1);
    command_completion(hc, 1, slot, 1);
    xhci_device_t* dev = xhci_address_device(hc, 0, 5, 0, speed);
    CHECK(dev && dev->slot == slot && hc->devices[slot] == dev);
    return dev;
}

static void hub(xhci_device_t* dev, uint8_t ports, uint8_t think) {
    command_completion(dev->host, 0, dev->slot, 1);
    CHECK(xhci_configure_hub(dev, ports, think));
    uint32_t* context = xhci_input(dev, 1);
    CHECK(context[0] & (1U << 26));
    CHECK((context[1] >> 24) == ports);
    CHECK(((context[2] >> 16) & 3U) == (dev->speed == 3 ? think : 0));
}

static xhci_device_t* child(xhci_device_t* parent, uint8_t port,
                             uint8_t slot, uint8_t speed) {
    command_completion(parent->host, 0, slot, 1);
    command_completion(parent->host, 1, slot, 1);
    xhci_device_t* dev = xhci_open_hub_port(parent, port, speed);
    CHECK(dev && dev->slot == slot);
    CHECK(xhci_parent(dev) == parent && xhci_parent_port(dev) == port);
    CHECK(dev->port == 5);
    return dev;
}

int main(void) {
    xhci_controller_t first, second;
    setup(&first, 32); setup(&second, 64);
    xhci_device_t* high = root(&first, 1, 3);
    CHECK(high->route == 0 && high->tt_slot == 0 && high->tt_port == 0);
    hub(high, 24, 3);
    xhci_device_t* full = child(high, 20, 2, 1);
    CHECK(full->route == 15 && full->depth == 1);
    CHECK(full->tt_slot == 1 && full->tt_port == 20);
    CHECK(xhci_input(full, 1)[0] == (15U | (1U << 20) | (1U << 27)));
    CHECK(xhci_input(full, 1)[2] == (1U | (20U << 8)));
    hub(full, 8, 2); /* Full-speed hubs inherit the high-speed ancestor's TT. */
    xhci_device_t* low = child(full, 7, 3, 2);
    CHECK(low->route == 0x7f && low->tt_slot == 1 && low->tt_port == 20);
    command_completion(&first, 0, 3, 1);
    CHECK(xhci_keyboard_endpoint(low, 0x81, 8, 10));
    CHECK(xhci_input(low, 1)[0] == (0x7fU | (2U << 20) | (3U << 27)));
    CHECK(xhci_input(low, 1)[2] == (1U | (20U << 8))); /* Configure preserves TT. */
    xhci_device_t* high_child = child(high, 3, 4, 3);
    CHECK(high_child->tt_slot == 0 && high_child->tt_port == 0);
    hub(high_child, 4, 1);
    xhci_device_t* translated = child(high_child, 2, 5, 1);
    CHECK(translated->route == 0x23 && translated->tt_slot == 4 && translated->tt_port == 2);

    xhci_device_t* other = root(&second, 1, 1); /* Same slot ID on a different HC. */
    hub(other, 4, 0);
    xhci_device_t* other_key = child(other, 2, 2, 2);
    CHECK(other_key->route == 2 && other_key->tt_slot == 0);
    CHECK(first.devices[1] == high && second.devices[1] == other);
    CHECK((uint8_t*)xhci_input(other_key, 2) - (uint8_t*)mem_phys_to_virt(other_key->input_context) == 128);

    /* A keyboard completion arriving before a synchronous hub command must be
     * saved, not discarded, and the DMA buffer must not be reused prematurely. */
    uint8_t* report_data = mem_phys_to_virt(low->keyboard_data);
    report_data[2] = 4;
    event_at(&first, 0, low->keyboard_trb, 1U << 24,
             TRB_TYPE(32) | (3U << 24) | (3U << 16));
    event_at(&first, 1, first.commands.physical + first.commands.enqueue * 16,
             1U << 24, TRB_TYPE(33));
    CHECK(xhci_command(&first, 0, TRB_TYPE(23), 0));
    CHECK(low->keyboard_pending);
    uint16_t enqueue = low->keyboard_ring.enqueue;
    uint8_t report[8];
    CHECK(xhci_keyboard_poll(low, report) && report[2] == 4);
    CHECK(!low->keyboard_pending && low->keyboard_ring.enqueue == enqueue + 1);
    CHECK(!xhci_keyboard_poll(low, report));

    /* A failure on one host must leave the other's devices and rings alive. */
    xhci_write32(first.operational, XHCI_USBSTS, XHCI_HALTED);
    xhci_fail(&first);
    CHECK(!first.ready && second.ready);
    command_completion(&second, 0, 0, 1);
    CHECK(xhci_command(&second, 0, TRB_TYPE(23), 0));
    CHECK(first.devices[3] == low); /* Failed host retains DMA allocations. */
    command_completion(&second, 0, 2, 1);
    CHECK(xhci_close_device(other_key));
    CHECK(!second.devices[2] && second.devices[1] == other);

    /* MMIO address width and DMA limits are properties of each host. */
    uint32_t registers[4] = {0, 0xdeadbeefU, 0, 0};
    first.dma64 = false;
    xhci_write_address(&first, (volatile uint8_t*)registers, 0, 0x12345678U);
    CHECK(registers[0] == 0x12345678U && registers[1] == 0xdeadbeefU);
    xhci_write_address(&second, (volatile uint8_t*)registers, 8, 0x123456789abcdef0ULL);
    CHECK(registers[2] == 0x9abcdef0U && registers[3] == 0x12345678U);
    CHECK(!xhci_dma_page(&first)); /* Host test pages reside above 4 GiB. */
    uint64_t dma = xhci_dma_page(&second);
    CHECK(dma != 0); mem_free_page(dma);

    /* Reject topology overflows and impossible speed combinations. */
    CHECK(!xhci_open_hub_port(full, 1, 3));
    CHECK(!xhci_open_hub_port(other, 0, 1));
    CHECK(!xhci_open_hub_port(other, 5, 1));
    other->depth = 5;
    CHECK(!xhci_open_hub_port(other, 1, 1));
    CHECK(!xhci_configure_hub(other, 4, 0));
    printf("PASS: %d xHCI topology, TT, controller isolation, and event dispatch checks\n", checks);
    return 0;
}
