#include "types.h"
#include "util.h"
#include "console.h"
#include "elf64.h"
#include "usermode.h"
#include "paging.h"

typedef struct {
    char magic[4];
    uint8_t mode;
    uint8_t endian;
    uint8_t header_ver;
    uint8_t os_abi;
    uint8_t abi_version;
    uint8_t unused[7];

    uint16_t type;
    uint16_t isa;
    uint32_t elf_version;

    uint64_t program_entry_address;
    uint64_t program_header_offset;
    uint64_t section_header_offset;

    uint32_t flags;

    uint16_t elf_header_size;
    uint16_t program_header_entry_size;
    uint16_t program_header_count;
    uint16_t section_header_entry_size;
    uint16_t section_header_count;
    uint16_t section_header_string_index;
} ELF_header_t;

_Static_assert(sizeof(ELF_header_t) == 64,
               "Invalid ELF64 header size");

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t file_offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} ELF_program_header_t;

_Static_assert(sizeof(ELF_program_header_t) == 56,
               "Invalid ELF64 program header size");

               #define PT_LOAD 1

#define PF_X 0x01
#define PF_W 0x02
#define PF_R 0x04

uint64_t elf_to_page_flags(uint32_t flags) {
    uint64_t result = PAGE_USER;

    if(flags & PF_W)
        result |= PAGE_RW;

    if(!(flags & PF_X))
        result |= PAGE_NX;

    return result;
}


static uint64_t last_top = 0;
bool load_segment(
    const char* data,
    const ELF_program_header_t* ph
) {
    if(ph->memory_size == 0)
        return true;
    uint64_t start = ph->virtual_address;
    uint64_t size = ph->memory_size;
    uint64_t file_offset = ph->file_offset;
    uint64_t flags = elf_to_page_flags(ph->flags);
    uint64_t page_aligned_start = start & ~(PAGE_SIZE_4K-1);

    if(page_aligned_start <= last_top){
        return false;
    }
    last_top = (start+size) & ~(PAGE_SIZE_4K-1);

    uint64_t padding = start-page_aligned_start;
    um_create_segment(page_aligned_start,(void*)(data+file_offset),size,flags,padding);
    return true;
}

bool load_program_headers(
    const char* data,
    uint64_t size,
    const ELF_header_t* header
) {
    if(header->program_header_entry_size !=
       sizeof(ELF_program_header_t))
        return false;
    
    uint64_t offset = header->program_header_offset;
    uint64_t count = header->program_header_count;

    if(offset > size || count == 0)
        return false;

    if(count > (size - offset) /
               sizeof(ELF_program_header_t))
        return false;

    bool valid_entry = false;

    for(uint64_t i = 0; i < count; i++) {
        ELF_program_header_t* ph = (ELF_program_header_t*)(data + offset + i*sizeof(ELF_program_header_t));
        if(ph->type == 2 || ph->type == 3 || ph->type == 7)
            return false;
        
        if(ph->type != PT_LOAD)
            continue;
        
        if(ph->file_size > ph->memory_size)
            return false;
        
        if(ph->file_offset > size ||
           ph->file_size > size - ph->file_offset)
            return false;
        
        const uint64_t user_limit = 0x0000800000000000ULL;

        if(ph->virtual_address < 0x1000 ||
           ph->virtual_address >= user_limit ||
           ph->memory_size >
               user_limit - ph->virtual_address)
            return false;
        
        if((ph->virtual_address & 0xFFF) !=
           (ph->file_offset & 0xFFF))
            return false;
        
        if(ph->alignment > 1) {
            if(ph->alignment & (ph->alignment - 1))
                return false;

            if((ph->virtual_address & (ph->alignment - 1)) !=
               (ph->file_offset & (ph->alignment - 1)))
                return false;
        }
        // Entry point must be inside executable memory.
        if((ph->flags & PF_X) &&
           header->program_entry_address >=
               ph->virtual_address &&
           header->program_entry_address <
               ph->virtual_address + ph->memory_size) {

            valid_entry = true;
        }

        if(!load_segment(data, ph))
            return false;
    }
    return valid_entry;
}

void parse_ELF(char* data,uint64_t filesize){
    last_top = 0;
    ELF_header_t* header = (ELF_header_t*)data;
    if(!byte_comp((char*)(&(header->magic)),"\x7F" "ELF",4)){
        con_write("ERROR: no a ELF\n",0x01);
        return;
    }
    if(header->mode != 2){
        con_write("ERROR: non 64 bit ELF not supported\n",0x01);
        return;
    }
    if(header->type != 2){
        con_write("ERROR: non executable ELF\n",0x01);
        print_uint64(header->type);
        con_write("\n",0x02);
        return;
    }
    if(header->isa != 0x3E){
        con_write("ERROR: incompatible instructionset\n",0x01);
        print_uint64(header->isa);
        con_write("\n",0x02);
        return;
    }
#ifdef ELF_DEBUG
    con_write("Entry: ",0x02);
    print_uint64(header->program_entry_address);
    con_write("\nProgram header: ",0x02);
    print_uint64(header->program_header_offset);
    con_write("\nProgram header entry size: ",0x02);
    print_uint64(header->program_header_entry_size);
    con_write("\nProgram header entry count: ",0x02);
    print_uint64(header->program_header_count);
    con_write("\nSection header: ",0x02);
    print_uint64(header->section_header_offset);
    con_write("\nSection header entry size: ",0x02);
    print_uint64(header->section_header_entry_size);
    con_write("\nSection header entry count: ",0x02);
    print_uint64(header->section_header_count);
    con_write("\nSection header strings index: ",0x02);
    print_uint64(header->section_header_string_index);
    con_write("\nELF header size: ",0x02);
    print_uint64(header->elf_header_size);
    con_write("\n",0x02);
#endif

    if(!load_program_headers(data,filesize,header)){
        con_write("ELF loading FAILED\n",0x01);
        um_dealloc();
    }else{
        um_eexecute(header->program_entry_address);
    }
}