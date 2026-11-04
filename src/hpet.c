#include "types.h"
#include "acpi.h"
#include "paging.h"
#include "util.h"
#include "console.h"
#include "hpet.h"

struct address_structure
{
    uint8_t address_space_id;    // 0 - system memory, 1 - system I/O
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t reserved;
    uint64_t address;
} __attribute__((packed));

typedef struct address_structure address_structure ;

struct hpet
{
    uint8_t hardware_rev_id;
    uint8_t comparator_couhpet_nt:5;
    uint8_t counter_size:1;
    uint8_t reserved:1;
    uint8_t legacy_replacement:1;
    uint16_t pci_vendor_id;
    address_structure address;
    uint8_t hpet_number;
    uint16_t minimum_tick;
    uint8_t page_protection;
} __attribute__((packed));

typedef struct hpet hpet;
static hpet* HPET_base;
static uint64_t frequency;

void set_register_64(uint32_t offset, uint64_t value){
    *((uint64_t*)(HPET_base->address.address + offset)) = value;
}

uint64_t get_time(){
    return *((uint64_t*)(HPET_base->address.address + 0xF0));
}
uint64_t hpet_millis(){
    return get_time() / (frequency/1000);
}
void print_timer(){
    con_write("Time: ", 0x07);
    uint64_t time = hpet_millis();
    print_uint64_full(time);
    con_write("\n", 0x07);
}
void sleep(uint64_t time){
    uint64_t end = hpet_millis() + time;
    while (hpet_millis() < end) {}
}

void hpet_init(){
    HPET_base = (hpet*)(((void*)acpi_find_table("HPET")) + sizeof(acpi_sdt_header_t));
    con_write("HEPT_addr: ", 0x03);
    print_uint64_full(HPET_base->address.address);
    con_write("\n", 0x07);
    mmu_identity_map_range(HPET_base->address.address, 4096, PAGE_NX | PAGE_RW);
    
    uint32_t period = *((uint32_t*)(HPET_base->address.address + 0x04));
    frequency = 1000000000000000 / period;
    con_write("frequency: ", 0x03);
    print_uint64_dec(frequency);
    con_write("Hz\n", 0x03);
    set_register_64(0xF0,0);
    set_register_64(0x10,1);
    con_write("getting time\n", 0x07);
    while(get_time() == 0){}
}
