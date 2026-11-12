#include "syscall.h"
int main(void) {
    sys_write("Hello from C!\n", 14);
    return 0;
}