#include "types.h"
#include "util.h"
#include "console.h"

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