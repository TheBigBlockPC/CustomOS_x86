#ifndef XHCI_H
#define XHCI_H

#include "../../types.h"

/* BSP/foreground only, after PCI, paging, PMM and HPET. One controller and one
 * directly attached USB 1.x/2.0 device at a time; no external hubs or IRQs. */
bool xhci_init(void);
uint8_t xhci_port_count(void);
bool xhci_open_port(uint8_t port);
bool xhci_close_device(void);
/* Returns bytes transferred, or -1. Uses an internal one-page DMA bounce buffer. */
int xhci_control(uint8_t request_type, uint8_t request, uint16_t value,
                 uint16_t index, void* data, uint16_t length);
bool xhci_set_ep0_packet_size(uint8_t packet_size);
bool xhci_keyboard_endpoint(uint8_t address, uint16_t packet_size,
                            uint8_t interval);
/* Nonblocking: copies an eight-byte boot report when one is available. */
bool xhci_keyboard_poll(uint8_t report[8]);
bool xhci_keyboard_active(void);

#endif
