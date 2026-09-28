#ifndef PCIE_H
#define PCIE_H

#include "types.h"

#define PCIE_NO_DEVICE 0xffffffffu

void pci_init(void);
/* Pass PCIE_NO_DEVICE for the first match, then the prior ID for the next. */
uint32_t pcie_find_device(uint8_t class_code, uint8_t subclass,
                          uint8_t prog_if, uint32_t after);
uint64_t pcie_get_device_bar(uint32_t id, uint8_t bar);

#endif
