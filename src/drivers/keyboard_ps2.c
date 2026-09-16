#include "../types.h"
#include "keyboard_ps2.h"
static inline uint8_t inb(uint16_t port) {
    uint8_t data;
    __asm__ volatile("inb %1, %0" : "=a"(data) : "Nd"(port));
    return data;
}

uint8_t get_key(){
    if(!(inb(0x64) & 1)){
        return 0;
    }
    return inb(0x60);
}