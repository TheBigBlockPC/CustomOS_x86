#ifndef SYSCALL_H
#define SYSCALL_H
void syscall_init(void);
void syscall_set_stack(uint64_t stack_top);
#endif