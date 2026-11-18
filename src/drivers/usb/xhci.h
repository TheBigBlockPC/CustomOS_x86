#ifndef XHCI_H
#define XHCI_H

#include "../../types.h"

/* BSP/foreground only, after PCI, paging, PMM and HPET. Polling USB 1.x/2.0
 * hosts, with independent device slots for root devices and external hubs. */
typedef struct xhci_controller xhci_controller_t;
typedef struct xhci_device xhci_device_t;
bool xhci_init(void);
xhci_controller_t* xhci_next_controller(xhci_controller_t* previous);
uint8_t xhci_port_count(xhci_controller_t* host);
xhci_device_t* xhci_open_port(xhci_controller_t* host, uint8_t port);
xhci_device_t* xhci_open_hub_port(xhci_device_t* hub, uint8_t port, uint8_t speed);
/* Hub uses its default single-TT interface. Call after SET_CONFIGURATION. */
bool xhci_configure_hub(xhci_device_t* hub, uint8_t ports, uint8_t tt_think);
/* Close children before their parent. DMA pages are freed after Disable Slot. */
bool xhci_close_device(xhci_device_t* device);
xhci_device_t* xhci_parent(xhci_device_t* device);
uint8_t xhci_parent_port(xhci_device_t* device);
/* Returns bytes transferred, or -1. Uses a per-device one-page DMA buffer. */
int xhci_control(xhci_device_t* device, uint8_t request_type, uint8_t request,
                 uint16_t value, uint16_t index, void* data, uint16_t length);
bool xhci_set_ep0_packet_size(xhci_device_t* device, uint8_t packet_size);
bool xhci_keyboard_endpoint(xhci_device_t* device, uint8_t address,
                            uint16_t packet_size, uint8_t interval);
/* Nonblocking: copies an eight-byte boot report when one is available. */
bool xhci_keyboard_poll(xhci_device_t* device, uint8_t report[8]);
bool xhci_keyboard_active(xhci_device_t* device);

#endif
