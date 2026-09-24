#include "types.h"
#include "console.h"
#include "util.h"
#include "drivers/nvme.h"
#include "filesystem.h"

static uint16_t read_le16(const uint8_t *p)
{
    return ((uint16_t)p[0]) |
           ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

typedef struct __attribute__((packed)) {
    char name[8];
    uint8_t ext[3];
    uint8_t attr;
    uint8_t nt;

    uint8_t creation_time_tenth;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;

    uint16_t first_cluster_high;
    
    uint16_t write_time;
    uint16_t write_date;

    uint16_t first_cluster_low;
    uint32_t size;
    /* data */
} FAT_entry_t;


void parse_entry(uint8_t* page, int index){
    FAT_entry_t* entry = (FAT_entry_t*)(page + index*32);
    char seq[] = {
        entry->name[0], entry->name[1], entry->name[2], entry->name[3],
        entry->name[4], entry->name[5], entry->name[6], entry->name[7],
        0
    };
    con_write((char*)seq,0x07);
    con_write(" ",0x07);
    if(entry->attr & 0x10){
        con_write("d",0x07);
    }else if(entry->attr & 0x08){
        con_write("v",0x07);
    }else{
        con_write("f",0x07);
    }
    uint32_t destination_clustor = (entry->first_cluster_high << 16) | entry->first_cluster_low;
    
    con_write(" ",0x07);
    print_uint64(destination_clustor);
    con_write(" ",0x07);
    print_uint64(entry->size);
    con_write("\n",0x07);
}

typedef struct {
    uint64_t first_data_sector;
    uint64_t sectors_per_cluster;
    uint32_t partition_start_lba;
} partition_dmetadata_t;

uint64_t get_cluster_LBA(
    partition_dmetadata_t* meta,
    uint64_t cluster
){
    uint64_t root_sector =
        meta->first_data_sector +
        (((uint64_t)cluster - 2ULL) *
        (uint64_t)(meta->sectors_per_cluster));

    uint64_t root_lba =
        (uint64_t)(meta->partition_start_lba) +
        root_sector;
    return root_lba;
}

void fs_init(){
    con_write("initializing filesystem\n",0x07);
    uint8_t* page = nvme_read_block(0, 0);
    if (!page) {
        //con_write("NVMe test read FAILED\n", 0x04);
        return;
    }
    uint32_t partition_start_lba = read_le32(page + 0x1C6);
    
    con_write("FAT Partition @ ",0x07);
    print_uint64_dec(partition_start_lba);
    con_write("\n",0x07);
    page = nvme_read_block(0, partition_start_lba);

    if (!page) {
        con_write("Failed to read FAT partition\n", 0x04);
        return;
    }

    uint16_t bytes_per_sector =
        read_le16(page + 0x0B);

    uint8_t sectors_per_cluster =
        page[0x0D];

    uint16_t reserved_sectors =
        read_le16(page + 0x0E);

    uint8_t number_of_fats =
        page[0x10];

    uint32_t fat_size =
        read_le32(page + 0x24);

    uint32_t root_cluster =
        read_le32(page + 0x2C);


    /* Debug first */

    con_write("BPS: ", 0x07);
    print_uint64_dec(bytes_per_sector);
    con_write("\nSPC: ", 0x07);
    print_uint64_dec(sectors_per_cluster);
    con_write("\nReserved: ", 0x07);
    print_uint64_dec(reserved_sectors);
    con_write("\nFATs: ", 0x07);
    print_uint64_dec(number_of_fats);
    con_write("\nFAT size: ", 0x07);
    print_uint64_dec(fat_size);
    con_write("\nRoot cluster: ", 0x07);
    print_uint64_dec(root_cluster);
    con_write("\n", 0x07);


    /* Validate */

    if (bytes_per_sector == 0 ||
        sectors_per_cluster == 0 ||
        reserved_sectors == 0 ||
        number_of_fats == 0 ||
        fat_size == 0 ||
        root_cluster < 2)
    {
        panic("Invalid FAT32 BPB\n", 0x04);
        return;
    }


    /* Everything below is 64-bit */

    uint64_t first_data_sector =
        (uint64_t)reserved_sectors +
        ((uint64_t)number_of_fats *
        (uint64_t)fat_size);
    /*
    uint64_t root_sector =
        first_data_sector +
        (((uint64_t)root_cluster - 2ULL) *
        (uint64_t)sectors_per_cluster);

    uint64_t root_lba =
        (uint64_t)partition_start_lba +
        root_sector;
    */
    partition_dmetadata_t metadata;
    metadata.first_data_sector = first_data_sector;
    metadata.partition_start_lba = partition_start_lba;
    metadata.sectors_per_cluster = sectors_per_cluster;

    uint64_t root_lba = get_cluster_LBA(&metadata,root_cluster);

    con_write("First data sector: ", 0x07);
    print_uint64_dec(first_data_sector);

    //con_write("\nRoot sector: ", 0x07);
    //print_uint64_dec(root_sector);

    con_write("\nRoot LBA: ", 0x07);
    print_uint64_dec(root_lba);

    con_write("\n", 0x07);

    page = nvme_read_block(0, root_lba);
    parse_entry(page,0);
    parse_entry(page,1);
    parse_entry(page,2);
}