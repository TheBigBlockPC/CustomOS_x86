#include "../types.h"
#include "keyboard_ps2.h"
static inline uint8_t inb(uint16_t port) {
    uint8_t data;
    __asm__ volatile("inb %1, %0" : "=a"(data) : "Nd"(port));
    return data;
}

uint8_t get_key(){
    uint8_t status = inb(0x64);
    /* An absent i8042 reads as 0xff (e.g. a USB-only QEMU machine). */
    if(status == 0xff || !(status & 1)){
        return 0;
    }
    return inb(0x60);
}
