/* Linked only by run_foundation.py; production kernels have no test hooks. */
#include "memory.h"
#include "paging.h"
#include "interrupts/interrupts.h"
#include "boot.h"

extern int test_register_frame(void);
extern void test_ud(void);
extern void test_gp(void);
extern void test_write(void* address);
extern void test_execute(void* address);
extern char test_ud_resume, test_gp_resume, test_write_resume, test_execute_resume;

static volatile uint64_t seen;
static volatile uint64_t irq_seen;
static uint64_t expected_address, expected_error, resume_address;
static console_state_t boot_console;

static void legacy_handler(void) { irq_seen++; }

static void debug(char* text)
{
    while (*text) __asm__ volatile("outb %0, $0xe9" :: "a"((uint8_t)*text++));
}
static void finish(uint8_t code)
{
    __asm__ volatile("outb %0, $0xf4" :: "a"(code));
    for (;;) __asm__ volatile("cli; hlt");
}
#define REQUIRE(condition) do { if (!(condition)) { debug("FAIL: " #condition "\n"); finish(0x11); } } while (0)

static void frame_handler(exception_frame_t* frame)
{
    REQUIRE(frame->vector == 3 && frame->error_code == 0);
    uint64_t* registers = &frame->r15;
    for (uint64_t i = 0; i < 15; i++) REQUIRE(registers[i] == 15 - i);
    REQUIRE((frame->rflags & 0x401) == 0x401);
    REQUIRE(frame->cs == 8 && frame->ss == 16 && frame->rsp != 0);
    REQUIRE(frame->cr0 & (1ULL << 16));
    REQUIRE(frame->cr3 && frame->cr4);
    REQUIRE(frame->fx_state != 0);
    REQUIRE(frame->fx_state->xmm[0][0] == 0xff);
    frame->fx_state->xmm[0][0] = 0xab;
    frame->rax = 101;
    seen++;
}
static void fault_handler(exception_frame_t* frame)
{
    REQUIRE(frame->error_code == expected_error);
    if (frame->vector == 14) REQUIRE(frame->cr2 == expected_address);
    frame->rip = resume_address;
    seen++;
}

static uint64_t entry_at(uint64_t address, bool* huge)
{
    *huge = false;
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    /* The static root belongs to the high kernel image, not PMM RAM. */
    uint64_t* table = mem_phys_to_virt(cr3 & ~0xfffULL);
    uint64_t entry = table[(address >> 39) & 511];
    if (!(entry & PAGE_PRESENT)) return 0;
    table = mem_phys_to_virt(entry & 0x000ffffffffff000ULL);
    entry = table[(address >> 30) & 511];
    if (!(entry & PAGE_PRESENT)) return 0;
    table = mem_phys_to_virt(entry & 0x000ffffffffff000ULL);
    entry = table[(address >> 21) & 511];
    if (!(entry & PAGE_PRESENT)) return 0;
    *huge = (entry & PAGE_PS) != 0;
    if (*huge) return entry;
    table = mem_phys_to_virt(entry & 0x000ffffffffff000ULL);
    return table[(address >> 12) & 511];
}

void foundation_selftest(void)
{
    debug("FOUNDATION_START\n");
    REQUIRE((uint64_t)&foundation_selftest >= 0xffffffff80000000ULL);
    REQUIRE(mem_kernel_to_phys(&foundation_selftest) < 0x100000000ULL);
    REQUIRE(mem_phys_to_virt(mem_kernel_to_phys(&foundation_selftest)) == (void*)&foundation_selftest);
    con_export_state(&boot_console);
    const char* loader_message = "Exiting boot services...";
    bool found_message = false;
    for (uint64_t i = 0; i < TERMINAL_W * TERMINAL_H; i++) {
        uint64_t j = 0;
        while (loader_message[j] && i + j < TERMINAL_W * TERMINAL_H &&
               boot_console.cells[i + j].character == loader_message[j]) j++;
        if (!loader_message[j]) found_message = true;
    }
    REQUIRE(found_message);
    uint64_t free_pages = mem_get_free_page_count();
    mem_free_page(mem_kernel_to_phys(&foundation_selftest) & ~0xfffULL);
    REQUIRE(mem_get_free_page_count() == free_pages);
    /* The framebuffer still uses low MMIO mappings, but the kernel's old
     * physical alias must be absent from the final tables. */
    bool low_huge;
    REQUIRE(!(entry_at(mem_kernel_to_phys(&foundation_selftest), &low_huge) & PAGE_PRESENT));
    debug("HIGH_KERNEL_HANDOFF_OK\n");
    exceptions_set_handler(3, frame_handler);
    REQUIRE(test_register_frame() == 1);
    REQUIRE(seen == 1);
    exceptions_set_handler(3, 0);
    interrupts_set_isr(64, legacy_handler);
    __asm__ volatile("int $0x40" ::: "memory");
    REQUIRE(irq_seen == 1);
    interrupts_set_isr(64, 0);
    debug("REGISTER_FRAME_OK\n");

    exceptions_set_handler(6, fault_handler);
    expected_error = 0;
    resume_address = (uint64_t)&test_ud_resume;
    test_ud();
    REQUIRE(seen == 2);
    exceptions_set_handler(6, 0);
    exceptions_set_handler(13, fault_handler);
    expected_error = 0xfff8;
    resume_address = (uint64_t)&test_gp_resume;
    test_gp();
    REQUIRE(seen == 3);
    exceptions_set_handler(13, 0);
    debug("EXCEPTION_ERROR_CODES_OK\n");

    uint64_t free_before = mem_get_free_page_count();
    uint64_t physical = mem_alloc_page();
    volatile uint8_t* data = mem_phys_to_virt(physical);
    REQUIRE(data[0] == 0 && data[4095] == 0);
    data[0] = 0xc3; /* RET instruction, used for execute-permission checks. */
    uint64_t virt = 0xffff900040000000ULL;
    mmu_map_2m(virt, 0x200000, PAGE_RW | PAGE_NX);
    /* Prime the huge-page TLB entry before replacing it with a 4K table. */
    (void)*(volatile uint8_t*)(virt + PAGE_SIZE);
    mmu_map_4k(virt + PAGE_SIZE, physical, PAGE_RW | PAGE_NX);
    REQUIRE(*(volatile uint8_t*)(virt + PAGE_SIZE) == 0xc3);
    bool huge;
    uint64_t entry = entry_at(virt + 2 * PAGE_SIZE, &huge);
    REQUIRE(!huge && (entry & 0x000ffffffffff000ULL) == 0x202000);
    REQUIRE((entry & (PAGE_RW | PAGE_NX)) == (PAGE_RW | PAGE_NX));
    virt += PAGE_SIZE;
    exceptions_set_handler(14, fault_handler);
    expected_address = virt;
    expected_error = 3; /* Present, write, supervisor. */
    resume_address = (uint64_t)&test_write_resume;
    mmu_map_4k(virt, physical, PAGE_NX);
    test_write((void*)virt);
    REQUIRE(seen == 4 && data[0] == 0xc3);
    expected_error = 0x11; /* Present, instruction fetch, supervisor. */
    resume_address = (uint64_t)&test_execute_resume;
    test_execute((void*)virt);
    REQUIRE(seen == 5);
    mmu_map_4k(virt, physical, 0); /* RO executable, invalidate former NX entry. */
    test_execute((void*)virt);
    REQUIRE(seen == 5);
    mmu_map_4k(virt, physical, PAGE_RW | PAGE_NX);
    test_write((void*)virt);
    REQUIRE(seen == 5 && data[0] == 1);
    expected_address = (uint64_t)&foundation_selftest;
    expected_error = 3;
    resume_address = (uint64_t)&test_write_resume;
    test_write((void*)expected_address);
    REQUIRE(seen == 6);
    expected_address = (uint64_t)&seen;
    expected_error = 0x11;
    resume_address = (uint64_t)&test_execute_resume;
    test_execute((void*)expected_address);
    REQUIRE(seen == 7);
    exceptions_set_handler(14, 0);
    /* Keep mapped physical page allocated: no unmap API exists yet. */
    REQUIRE(mem_get_free_page_count() < free_before);
    debug("MIXED_PAGES_WP_NX_OK\n");
    debug("KERNEL_SECTION_PERMISSIONS_OK\n");
}

void foundation_boot_complete(uint64_t root)
{
    REQUIRE(root >= 2);
    debug("STORAGE_BOOT_OK\n");
    finish(0x10);
}
