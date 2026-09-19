#ifndef PAGING_H
#define PAGING_H

void mmu_map_2m(uint64_t virtual_addr,uint64_t physical_addr);
void identitymap_2m(uint64_t physical_addr);
void set_mmu();
int get_mmu_active();

#endif