#include "types.h"
#include "util.h"
#include "console.h"
#include "elf64.h"
#include "usermode.h"
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



void parse_ELF(char* data){
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

}