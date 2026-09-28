#ifndef SATA_H
#define SATA_H

#include "../types.h"

void sata_driver_init(void);
/* LBA is a device logical sector. Each successful read returns 4096 bytes.
 * The static buffer is overwritten by the next read. */
uint8_t* sata_read_block(uint8_t sata_index, uint64_t lba);

void block_storage_init(void);
uint8_t* block_storage_read_block(uint8_t drive_index, uint64_t lba);

#endif
