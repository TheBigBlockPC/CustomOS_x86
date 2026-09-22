#ifndef PCIE_H
#define PCIE_H

void pci_init(void);
uint8_t pcie_get_device_index(uint8_t class, uint8_t subclass, uint8_t prog_if);
uint64_t pcie_get_device_bar(uint8_t device_idd,uint8_t bar);
#endif