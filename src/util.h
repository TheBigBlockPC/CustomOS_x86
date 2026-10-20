#ifndef UTIL_H
#define UTIL_H

#include "types.h"

void memory_zero(void* ptr, uint64_t size);
void memory_set(void* ptr, uint64_t size, uint8_t val);
void memory_copy(void* dst, const void* src, uint64_t size);
bool str_start_comp(char* a, char* b);
void str_copy(char* dest, char* src, bool add_null);

uint64_t int_div_round_up(uint64_t a, uint64_t b);
void print_uint64(uint64_t val);
void print_uint64_dec(uint64_t val);
#endif
