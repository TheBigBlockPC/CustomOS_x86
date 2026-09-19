#include "types.h"
#include "util.h"

uint64_t int_div_round_up(uint64_t a, uint64_t b){
    return a / b + (a % b != 0);
}