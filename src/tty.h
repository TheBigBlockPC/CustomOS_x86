#ifndef TTY_H
#define TTY_H

typedef void (*send_event)(char* string);

void tty_set_send_event(send_event on_send_callback);
void tty_init();
void tty_allow_input();
void tty_print(char* string);
void tty_print_err(char* string);
#endif