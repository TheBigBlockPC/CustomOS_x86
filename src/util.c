#include "types.h"
#include "util.h"
#include "console.h"

void memory_zero(void* ptr, uint64_t size)
{
    volatile uint8_t* bytes = (volatile uint8_t*)ptr;
    for (uint64_t i = 0; i < size; i++) {
        bytes[i] = 0;
    }
}

void memory_set(void* ptr, uint64_t size, uint8_t val)
{
    volatile uint8_t* bytes = (volatile uint8_t*)ptr;
    for (uint64_t i = 0; i < size; i++) {
        bytes[i] = val;
    }
}

void memory_copy(void* dst, const void* src, uint64_t size)
{
    volatile uint8_t* out = (volatile uint8_t*)dst;
    volatile const uint8_t* in = (volatile const uint8_t*)src;
    for (uint64_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
}

void str_copy(char* dest, char* src, bool add_null){
    int i=0;
    while(true){
        char value = src[i];
        if(value == 0) break;
        dest[i] = value;
        i++;
    }
    if(add_null){
        dest[i+1] = 0;
    }
}

uint64_t int_div_round_up(uint64_t a, uint64_t b){
    return a / b + (a % b != 0);
}
void print_uint64(uint64_t val){
    char hex_chars[16] = {
        '0','1','2','3','4','5','6','7',
        '8','9','A','B','C','D','E','F'
    };

    con_write("0x", 0x07);

    int start = 0;
    if(val <= 0xFFFFFFFF){
        start = 16-8;
    }
    if(val <= 0xFFFF){
        start = 16-4;
    }
    if(val <= 0xFF){
        start = 16-2;
    }

    for(int i = start; i < 16; i++){
        int shift = (15 - i) * 4;
        uint8_t digit = (val >> shift) & 0x0F;

        char seq[2] = {
            hex_chars[digit],
            '\0'
        };

        con_write(seq, 0x07);
    }
}

void print_uint64_dec(uint64_t val){
    char dec_chars[10] = {'0','1','2','3','4','5','6','7','8','9'};
    int digits = 0;
    char buf[32];
    if(val == 0){
        con_write("0",0x07);
    }
    uint64_t val_temp = val;
    for(int i =0;i<32;i++){
        buf[i] = 0;
    }
    while(val_temp != 0){
        uint64_t digit = val_temp % 10;
        buf[31-digits] = dec_chars[digit];
        digits++;
        val_temp /= 10;
    }
    con_write((char*)(&buf[32-digits]),0x07);
    con_write("\b",0x07);
}
