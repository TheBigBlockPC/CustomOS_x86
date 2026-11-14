#include "../types.h"
#include "../acpi.h"
#include "../paging.h"
#include "../panic.h"
#include "interrupts.h"
#include "apic.h"
#include "../console.h"
#include "../util.h"

/* 1. Hardware registers and driver state. */
#define IA32_APIC_BASE  0x1B
#define APIC_ENABLE     (1ULL << 11)
#define APIC_X2_MODE    (1ULL << 10)
#define APIC_BASE_MASK  0x000FFFFFFFFFF000ULL

#define LAPIC_ID        0x020
#define LAPIC_VERSION   0x030
#define LAPIC_TPR       0x080
#define LAPIC_EOI       0x0B0
#define LAPIC_SVR       0x0F0
#define LAPIC_ESR       0x280
#define LAPIC_LVT_CMCI  0x2F0
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_LVT_THERM 0x330
#define LAPIC_LVT_PERF  0x340
#define LAPIC_LVT_LINT0 0x350
#define LAPIC_LVT_LINT1 0x360
#define LAPIC_LVT_ERROR 0x370
#define LAPIC_TIMER_INIT 0x380

#define APIC_MASKED     (1U << 16)
#define LAPIC_SPURIOUS_VECTOR 0xFF
#define LAPIC_UNUSED_VECTOR   0xFE
#define IOAPIC_VERSION        0x01
#define IOAPIC_REDIR_BASE     0x10
#define IOAPIC_ACTIVE_LOW     (1U << 13)
#define IOAPIC_LEVEL          (1U << 15)
#define MAX_IOAPICS           8

static volatile uint8_t* lapic_base;
static bool lapic_x2;
static bool lapic_initialized;

typedef struct {
    volatile uint8_t* base;
    uint32_t gsi_base;
    uint32_t count;
} ioapic_t;

static ioapic_t ioapics[MAX_IOAPICS];
static uint32_t ioapic_count;

/* 2. CPU instructions and Local APIC register access. */
static uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr" :: "c"(msr), "a"((uint32_t)value),
                     "d"((uint32_t)(value >> 32)) : "memory");
}

static void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}

uint32_t lapic_read(uint32_t reg) {
    if (lapic_x2)
        return (uint32_t)rdmsr(0x800 + (reg >> 4));
    return *(volatile uint32_t*)(lapic_base + reg);
}

void lapic_write(uint32_t reg, uint32_t value) {
    if (lapic_x2) {
        wrmsr(0x800 + (reg >> 4), value);
        return;
    }
    *(volatile uint32_t*)(lapic_base + reg) = value;
}

bool lapic_is_ready(void) {
    return lapic_initialized;
}

uint32_t lapic_get_id(void) {
    uint32_t id = lapic_read(LAPIC_ID);
    return lapic_x2 ? id : id >> 24;
}

void lapic_eoi(void) {
    /* For level interrupts, clear the device's IRQ before this write. */
    lapic_write(LAPIC_EOI, 0);
}

/* 3. I/O APIC register access. Selector and window are shared.
 * All callers must serialize accesses (BSP only, IF=0 for now). */
static uint32_t ioapic_read(ioapic_t* apic, uint32_t reg) {
    *(volatile uint32_t*)(apic->base + 0x00) = reg;
    return *(volatile uint32_t*)(apic->base + 0x10);
}

static void ioapic_write(ioapic_t* apic, uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(apic->base + 0x00) = reg;
    *(volatile uint32_t*)(apic->base + 0x10) = value;
}

/* 4. ACPI MADT structures and discovery. */
typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint32_t lapic_address;
    uint32_t flags;
} madt_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
} madt_entry_t;

typedef struct __attribute__((packed)) {
    madt_entry_t header;
    uint8_t id;
    uint8_t reserved;
    uint32_t address;
    uint32_t gsi_base;
} madt_ioapic_t;

typedef struct __attribute__((packed)) {
    madt_entry_t header;
    uint16_t reserved;
    uint64_t address;
} madt_lapic_override_t;

_Static_assert(sizeof(madt_t) == 44, "MADT header size");
_Static_assert(sizeof(madt_ioapic_t) == 12, "MADT IOAPIC size");
_Static_assert(sizeof(madt_lapic_override_t) == 12, "MADT LAPIC override size");

static uint64_t parse_madt(uint32_t* flags) {
    const madt_t* madt = (const madt_t*)acpi_find_table("APIC");
    if (!madt || madt->header.length < sizeof(madt_t))
        panic("Invalid MADT");

    uint64_t address = madt->lapic_address;
    *flags = madt->flags;

    const uint8_t* ptr = (const uint8_t*)madt + sizeof(madt_t);
    const uint8_t* end = (const uint8_t*)madt + madt->header.length;

    while (ptr < end) {
        if ((uint64_t)(end - ptr) < sizeof(madt_entry_t))
            panic("Truncated MADT entry");
        const madt_entry_t* entry = (const madt_entry_t*)ptr;
        if (entry->length < sizeof(*entry) ||
            entry->length > (uint64_t)(end - ptr))
            panic("Invalid MADT entry length");

        if (entry->type == 1) {
            if (entry->length < sizeof(madt_ioapic_t) ||
                ioapic_count >= MAX_IOAPICS)
                panic("Invalid or too many IOAPICs");

            const madt_ioapic_t* info = (const madt_ioapic_t*)ptr;
            if (!info->address || (info->address & 0xFFF))
                panic("Invalid IOAPIC address");

            ioapic_t* apic = &ioapics[ioapic_count++];
            apic->base = (volatile uint8_t*)(uint64_t)info->address;
            apic->gsi_base = info->gsi_base;

            mmu_identity_map_range(info->address, 4096,
                                   PAGE_RW | PAGE_NX | PAGE_UC);
            uint32_t version = ioapic_read(apic, IOAPIC_VERSION);
            apic->count = ((version >> 16) & 0xFF) + 1;
            /* Register selector is eight bits; last entry must fit. */
            if (apic->count > 120)
                panic("Invalid IOAPIC size");
        } else if (entry->type == 5) {
            if (entry->length < sizeof(madt_lapic_override_t))
                panic("Invalid LAPIC override");
            const madt_lapic_override_t* info =
                (const madt_lapic_override_t*)ptr;
            address = info->address;
        }
        /* Other entries (ISA overrides, CPUs, NMIs) are not used yet. */
        ptr += entry->length;
    }
    return address;
}

/* 5. Local APIC initialization. */
static void lapic_spurious_isr(void) {
    /* A spurious interrupt has no in-service bit: do not send EOI. */
}

static void lapic_init(uint64_t address) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                             : "a"(1), "c"(0));
    if (!(edx & (1U << 9)))
        panic("CPU has no Local APIC");

    uint64_t msr = rdmsr(IA32_APIC_BASE);
    if (!(msr & APIC_ENABLE)) {
        msr |= APIC_ENABLE;
        wrmsr(IA32_APIC_BASE, msr);
    }
    lapic_x2 = (msr & APIC_X2_MODE) != 0;

    if (!lapic_x2) {
        uint64_t base = msr & APIC_BASE_MASK;
        if (!base || base != address)
            panic("LAPIC address mismatch");
        mmu_identity_map_range(base, 4096, PAGE_RW | PAGE_NX | PAGE_UC);
        lapic_base = (volatile uint8_t*)base;
    }

    uint32_t max_lvt = (lapic_read(LAPIC_VERSION) >> 16) & 0xFF;
    if (max_lvt < 3)
        panic("Unsupported Local APIC");

    /* Mask error source first, then all unused local interrupt sources.
     * Nonzero vector avoids an illegal-vector configuration. */
    uint32_t masked = APIC_MASKED | LAPIC_UNUSED_VECTOR;
    lapic_write(LAPIC_LVT_ERROR, masked);
    lapic_write(LAPIC_LVT_TIMER, masked);
    lapic_write(LAPIC_LVT_LINT0, masked);
    lapic_write(LAPIC_LVT_LINT1, masked);
    if (max_lvt >= 4) lapic_write(LAPIC_LVT_PERF, masked);
    if (max_lvt >= 5) lapic_write(LAPIC_LVT_THERM, masked);
    if (max_lvt >= 6) lapic_write(LAPIC_LVT_CMCI, masked);
    lapic_write(LAPIC_TIMER_INIT, 0);

    lapic_write(LAPIC_ESR, 0);
    (void)lapic_read(LAPIC_ESR);
    lapic_write(LAPIC_TPR, 0);

    interrupts_set_isr(LAPIC_SPURIOUS_VECTOR, lapic_spurious_isr);
    /* Bit 8 enables the LAPIC in software; bit 12 stays zero so
     * level-triggered EOI broadcasts are not suppressed. */
    lapic_write(LAPIC_SVR, (1U << 8) | LAPIC_SPURIOUS_VECTOR);
    lapic_initialized = true;
}

/* 6. Configure a hardware GSI to reach the current CPU and IDT vector. */
bool ioapic_route_irq(uint32_t gsi, uint8_t vector,
                      bool level_triggered, bool active_low) {
    if (vector < 32 || vector >= LAPIC_UNUSED_VECTOR)
        return false;

    uint32_t apic_id = lapic_get_id();
    /* Direct physical routing has an eight-bit destination field. */
    if (apic_id >= 255)
        return false;

    for (uint32_t i = 0; i < ioapic_count; i++) {
        ioapic_t* apic = &ioapics[i];
        if (gsi < apic->gsi_base)
            continue;
        uint32_t pin = gsi - apic->gsi_base;
        if (pin >= apic->count)
            continue;

        uint32_t reg = IOAPIC_REDIR_BASE + pin * 2;
        uint32_t low = vector;           /* Fixed, physical destination. */
        uint32_t high = apic_id << 24;
        if (level_triggered) low |= IOAPIC_LEVEL;
        if (active_low) low |= IOAPIC_ACTIVE_LOW;

        /* Mask old route before changing destination. Then unmask new route.
         * Only route unused/inactive pins; no live rerouting yet. */
        ioapic_write(apic, reg, ioapic_read(apic, reg) | APIC_MASKED);
        ioapic_write(apic, reg + 1, high);
        ioapic_write(apic, reg, low);
        return true;
    }
    return false;
}

/* 7. Driver initialization: discover, mask old sources, enable LAPIC.
 * This is called once on the BSP after ACPI, paging and IDT are ready. */
void apic_init(void) {
    __asm__ volatile("cli" ::: "memory");

    uint32_t flags;
    uint64_t address = parse_madt(&flags);
    if (ioapic_count == 0)
        panic("No IOAPIC found");

    /* PCAT_COMPAT: legacy 8259 PICs exist and must be masked. */
    if (flags & 1) {
        outb(0x21, 0xFF);
        outb(0xA1, 0xFF);
    }

    /* Prevent unregistered devices from interrupting the CPU. */
    for (uint32_t i = 0; i < ioapic_count; i++) {
        ioapic_t* apic = &ioapics[i];
        for (uint32_t pin = 0; pin < apic->count; pin++) {
            uint32_t reg = IOAPIC_REDIR_BASE + pin * 2;
            ioapic_write(apic, reg, ioapic_read(apic, reg) | APIC_MASKED);
        }
    }
    lapic_init(address);
    /* Caller executes STI only after configuring device routes and handlers. */
}

void ioapic_debug(uint32_t gsi) {
    for (uint32_t i = 0; i < ioapic_count; i++) {
        ioapic_t* apic = &ioapics[i];

        if (gsi < apic->gsi_base)
            continue;

        uint32_t pin = gsi - apic->gsi_base;

        if (pin >= apic->count)
            continue;

        uint32_t reg = 0x10 + pin * 2;

        con_write("IOAPIC LOW: ", 0x07);
        print_uint64_full(ioapic_read(apic, reg));

        con_write("\nIOAPIC HIGH: ", 0x07);
        print_uint64_full(ioapic_read(apic, reg + 1));

        con_write("\n", 0x07);
        return;
    }

    con_write("GSI not found\n", 0x01);
}

void lapic_debug(uint8_t vector) {
    uint32_t offset = (vector / 32) * 0x10;
    uint32_t mask = 1U << (vector % 32);

    uint32_t irr = lapic_read(0x200 + offset);
    uint32_t isr = lapic_read(0x100 + offset);

    con_write("LAPIC ID: ", 0x07);
    print_uint64_full(lapic_get_id());

    con_write("\nLAPIC SVR: ", 0x07);
    print_uint64_full(lapic_read(0xF0));

    con_write("\nLAPIC TPR: ", 0x07);
    print_uint64_full(lapic_read(0x80));

    con_write("\nLAPIC PPR: ", 0x07);
    print_uint64_full(lapic_read(0xA0));

    con_write("\nPending: ", 0x07);
    print_uint64_dec((irr & mask) != 0);

    con_write("\nIn service: ", 0x07);
    print_uint64_dec((isr & mask) != 0);

    con_write("\n", 0x07);
}
