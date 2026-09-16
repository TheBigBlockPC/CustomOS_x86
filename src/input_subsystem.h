#ifndef INPUT_SUBSYSTEM_H
#define INPUT_SUBSYSTEM_H
typedef void (*kb_event)(uint8_t scancode, char ascii, int k_down);
void set_kb_handler(kb_event handler);
void poll_events();
#endif