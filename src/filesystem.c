#include "types.h"
#include "console.h"
#include "util.h"
#include "drivers/sata.h"
#include "filesystem.h"
#include "panic.h"

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

typedef struct {
    char name[8];
    uint8_t ext[3];
    uint8_t attr;
    uint32_t target_cluser;
} FS_fd_t;

FS_fd_t parse_entry(uint8_t* page, int index){
    FAT_entry_t* entry = (FAT_entry_t*)(page + index*32);
    uint32_t destination_clustor = ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
/*  
    con_write(" ",0x07);
    print_uint64(destination_clustor);
    con_write(" ",0x07);
    print_uint64(entry->size);
    con_write("\n",0x07);
*/
    FS_fd_t fd;
    memory_copy(&fd.name[0],&entry->name[0],8);
    memory_copy(&fd.ext[0],&entry->ext[0],3);
    fd.attr = entry->attr;
    fd.target_cluser = destination_clustor;
    return fd;
}

void print_fd(FS_fd_t *fd){
    char name[] = {
        fd->name[0], fd->name[1], fd->name[2], fd->name[3],
        fd->name[4], fd->name[5], fd->name[6], fd->name[7],
        0
    };
    
    char ext[] = {
        fd->ext[0], fd->ext[1], fd->ext[2],
        0
    };
    con_write((char*)name,0x07);
    con_write(".",0x07);
    con_write((char*)ext,0x07);
    con_write(" ",0x07);
    if(fd->attr == 0){
        con_write("n",0x07);
    }else if(fd->attr & 0x10){
        con_write("d",0x07);
    }else if(fd->attr & 0x08){
        con_write("v",0x07);
    }else{
        con_write("f",0x07);
    }
    con_write(" ",0x07);
    print_uint64(fd->target_cluser);
    con_write("\n",0x07);
}

typedef struct {
    uint64_t first_data_sector;
    uint64_t fat_start;
    uint64_t sectors_per_cluster;
    uint32_t partition_start_lba;
    uint64_t root_cluster;
    uint16_t bytes_per_sector;
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


void list_files(
    partition_dmetadata_t* meta,
    uint64_t cluster
){
    if(cluster == 0){
        con_write("INVALLID CLUISTER\n",0x01);
        return;
    }
    uint64_t lba = get_cluster_LBA(meta,cluster);
    uint8_t* page = block_storage_read_block(0, lba);

    for(int i=0;i<64;i++){
        FS_fd_t fd = parse_entry(page,i);
        if(fd.name[0] == 0) break;
        if(fd.attr != 0x0F){
            print_fd(&fd);
        }
    }
}

uint64_t change_directory(
    partition_dmetadata_t* meta,
    uint64_t cluster,
    char* name
){
    uint64_t lba = get_cluster_LBA(meta,cluster);
    uint8_t* page = block_storage_read_block(0, lba);
    char prepared_name[8];
    memory_set(&prepared_name[0],8,' ');
    for (int i = 0; name[i]; i++) {
        if (i == 8) return 0; /* Short names only; never overwrite the stack. */
        prepared_name[i] = name[i];
    }
    for(int entry_index=0;entry_index<64;entry_index++){
        FS_fd_t fd = parse_entry(page,entry_index);
        if(fd.name[0] == 0) break;
        
        bool matched = true;
        for(int i=0;i<8;i++){
            if(fd.name[i] != prepared_name[i]){
                matched = false;
            }
        }
        if(matched){
            return fd.target_cluser;
        }
    }
    return 0;
}


uint32_t fat_next_cluster(
    partition_dmetadata_t* meta,
    uint32_t cluster
){
    uint64_t fat_start_lba = meta->partition_start_lba + meta->fat_start;
    uint64_t cluster_index_offset = cluster*4;
    uint64_t cluster_lba_offset = cluster_index_offset/512;
    uint64_t fat_block_lba=fat_start_lba+cluster_lba_offset;
    uint64_t index_offset = cluster_index_offset-(cluster_lba_offset*512);
    uint8_t* page = block_storage_read_block(0, fat_block_lba);

    return (*((uint32_t*)(page+index_offset))) & 0x0FFFFFFF;
}

void read_file(
    partition_dmetadata_t* meta,
    uint64_t cluster
){
    uint64_t target = cluster;
    while(true){
        //con_write("Cluister: ",0x02);
        //print_uint64(target);
        //con_write("\n",0x07);
        uint64_t lba = get_cluster_LBA(meta,target);
        uint64_t data_LBAs = meta->sectors_per_cluster * (meta->bytes_per_sector / 512);
        for(int i = 0;i<data_LBAs;i++){
            con_write("reading sector",0x07);
            uint8_t* page = block_storage_read_block(0, lba + i);
            for(int byte=0;byte < meta->bytes_per_sector;byte++){
                char seq[] = {page[byte],0};
                con_write(seq,0x07);
            }
        }
        //uint8_t* page = block_storage_read_block(0, lba);
        //con_write(page,0x07);
        //con_write("\n",0x07);
        target = fat_next_cluster(meta,target);
        if(target >= 0x0FFFFFF8) break;
    }
}

partition_dmetadata_t metadata;

uint64_t fs_cd(uint64_t cluster, char *name){
    return change_directory(&metadata,cluster,name);
}

void fs_ls(uint64_t cluster){
    list_files(&metadata,cluster);
}

void fs_cat(uint64_t cluster){
    read_file(&metadata,cluster);
}

uint64_t fs_root(){
    return metadata.root_cluster;
}

void fs_init(){
    con_write("initializing filesystem\n",0x07);
    uint8_t* page = block_storage_read_block(0, 0);
    if (!page) {
        //con_write("NVMe test read FAILED\n", 0x04);
        return;
    }
    uint32_t partition_start_lba = read_le32(page + 0x1C6);
    
    con_write("FAT Partition @ ",0x07);
    print_uint64_dec(partition_start_lba);
    con_write("\n",0x07);
    page = block_storage_read_block(0, partition_start_lba);

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
    /*
    uint64_t root_sector =
        first_data_sector +
        (((uint64_t)root_cluster - 2ULL) *
        (uint64_t)sectors_per_cluster);

    uint64_t root_lba =
        (uint64_t)partition_start_lba +
        root_sector;
    */
    
    metadata.first_data_sector = first_data_sector;
    metadata.fat_start = (uint64_t)reserved_sectors;
    metadata.partition_start_lba = partition_start_lba;
    metadata.sectors_per_cluster = sectors_per_cluster;
    metadata.bytes_per_sector = bytes_per_sector;
    if((sectors_per_cluster * bytes_per_sector) > 4096){
        con_write("cluster > 4096 bytes\n",0x01);
    }
    if((bytes_per_sector) != 512){
        panic("unsupported sector size, sectors must be 512 bytes");
    }
    

    uint64_t root_lba = get_cluster_LBA(&metadata,root_cluster);

    con_write("First data sector: ", 0x07);
    print_uint64_dec(first_data_sector);

    //con_write("\nRoot sector: ", 0x07);
    //print_uint64_dec(root_sector);

    con_write("\nRoot LBA: ", 0x07);
    print_uint64_dec(root_lba);

    con_write("\n", 0x07);
    metadata.root_cluster = root_cluster;
    //list_files(&metadata,root_cluster);
    //uint64_t efi_cluster = change_directory(&metadata,root_cluster,"EFI");
    //con_write("\n", 0x07);
    //list_files(&metadata,efi_cluster);
    //uint64_t prev_cluster = change_directory(&metadata,efi_cluster,"BOOT");
    //con_write("\n", 0x07);
    //list_files(&metadata,prev_cluster);
    /* File-chain debugging belongs in an explicit shell/test command. */
}
