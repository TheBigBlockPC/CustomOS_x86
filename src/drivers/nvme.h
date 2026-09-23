#ifndef NVME_H
#define NVME_H

#include "../types.h"

void nvme_driver_init(void);

/* Index 0 is the current controller. LBA is the first logical block of a
 * 4096-byte read. Returns NULL on failure. The page is overwritten by the
 * next read and must not be freed or used concurrently with another read. */
uint8_t* nvme_read_block(uint8_t nvme_index, uint64_t lba);

#endif
