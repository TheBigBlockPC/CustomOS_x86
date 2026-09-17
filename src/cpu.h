#ifndef CPU_H
#define CPU_H

typedef void (*kernel_entry_t)(void);

void cpu_enter_kernel(kernel_entry_t entry);

#endif