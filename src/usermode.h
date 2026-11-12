#ifndef USERMODE_H
#define USERMODE_H
void um_create_segment(uint64_t user_base, void* data, uint64_t size, uint64_t flags);
void um_eexecute(uint64_t entry);
void um_test();
#endif