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
        con_write("Invalid FAT32 BPB\n", 0x04);
        return;
    }


    /* Everything below is 64-bit */

    uint64_t first_data_sector =
        (uint64_t)reserved_sectors +
        ((uint64_t)number_of_fats *
        (uint64_t)fat_size);

    uint64_t root_sector =
        first_data_sector +
        (((uint64_t)root_cluster - 2ULL) *
        (uint64_t)sectors_per_cluster);

    uint64_t root_lba =
        (uint64_t)partition_start_lba +
        root_sector;


    con_write("First data sector: ", 0x07);
    print_uint64_dec(first_data_sector);

    con_write("\nRoot sector: ", 0x07);
    print_uint64_dec(root_sector);

    con_write("\nRoot LBA: ", 0x07);
    print_uint64_dec(root_lba);

    con_write("\n", 0x07);

    page = nvme_read_block(0, root_lba);
    con_write(page,0x03);
    con_write("\n", 0x07);
}