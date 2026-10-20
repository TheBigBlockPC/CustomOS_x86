#ifndef PANIC_H
#define PANIC_H

__attribute__((noreturn)) void panic(char* message);
void setup_default_exceptionhanders();
#endif
