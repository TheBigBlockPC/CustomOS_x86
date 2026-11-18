#ifndef USB_H
#define USB_H

#include "../../types.h"

/* First USB2 boot keyboard across all xHCI hosts, directly or through hubs.
 * Call after PCI/HPET initialization; initialization is idempotent. Poll in foreground. */
void usb_init(void);
typedef void (*usb_key_event_t)(uint8_t scancode, int down);
/* Physical Set-1-style positions: the input subsystem owns layout/ASCII. */
void usb_poll_keyboard(usb_key_event_t handler);

#endif
