#include "types.h"
#include "console.h"
#include "util.h"
#include "drivers/nvme.h"
#include "filesystem.h"

void fs_init(){
    con_write("initializing filesystem\n",0x07);
    uint8_t* page = nvme_read_block(0, 0);
    if (!page) {
        //con_write("NVMe test read FAILED\n", 0x04);
        return;
    }
    uint32_t fat_part_lba = *((uint32_t*)(page+0x1C6));
    
    con_write("FAT Partition @ ",0x07);
    print_uint64_dec(fat_part_lba);
    con_write("\n",0x07);
    page = nvme_read_block(0, fat_part_lba);

}