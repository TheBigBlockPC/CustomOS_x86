#include "types.h"
#include "console.h"
#include "panic.h"

void panic(char* message){
    __asm__ volatile("cli" ::: "memory");
    con_write("Kernel panic\n",0x01);
    con_write(message,0x07);
    while(1){
        __asm__ __volatile__("hlt");
    }
}
