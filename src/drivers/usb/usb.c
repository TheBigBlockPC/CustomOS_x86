#include "usb.h"
#include "xhci.h"
#include "../../console.h"
#include "../../hpet.h"
#include "../../util.h"

/* No HID report-descriptor parser: subclass 1 / protocol 1 keyboards implement
 * the standard eight-byte boot report after SET_PROTOCOL(0). */
static bool attempted, keyboard_ready;
static xhci_device_t* keyboard_device;
static uint64_t hub_check_at;
static uint8_t descriptor[4096];
static uint8_t held[128];
static uint8_t repeat_key;
static uint64_t repeat_at;

/* USB usage -> physical PS/2 Set 1 position. Layout/ASCII lives in input_subsystem. */
static const uint8_t usage_to_scan[128] = {
    [4]=0x1e, [5]=0x30, [6]=0x2e, [7]=0x20, [8]=0x12, [9]=0x21,
    [10]=0x22, [11]=0x23, [12]=0x17, [13]=0x24, [14]=0x25, [15]=0x26,
    [16]=0x32, [17]=0x31, [18]=0x18, [19]=0x19, [20]=0x10, [21]=0x13,
    [22]=0x1f, [23]=0x14, [24]=0x16, [25]=0x2f, [26]=0x11, [27]=0x2d,
    [28]=0x15, [29]=0x2c,
    [30]=0x02, [31]=0x03, [32]=0x04, [33]=0x05, [34]=0x06,
    [35]=0x07, [36]=0x08, [37]=0x09, [38]=0x0a, [39]=0x0b,
    [40]=0x1c, [41]=0x01, [42]=0x0e, [43]=0x0f, [44]=0x39,
    [45]=0x0c, [46]=0x0d, [47]=0x1a, [48]=0x1b, [49]=0x2b,
    [50]=0x2b, [51]=0x27, [52]=0x28, [53]=0x29, [54]=0x33,
    [55]=0x34, [56]=0x35, [57]=0x3a,
    [58]=0x3b, [59]=0x3c, [60]=0x3d, [61]=0x3e, [62]=0x3f,
    [63]=0x40, [64]=0x41, [65]=0x42, [66]=0x43, [67]=0x44,
    [68]=0x57, [69]=0x58, [100]=0x56
};

typedef struct {
    uint8_t configuration, interface, endpoint, interval;
    uint16_t packet;
} boot_keyboard_t;

static uint16_t usb_u16(const uint8_t* p) { return p[0] | ((uint16_t)p[1] << 8); }

static bool usb_find_keyboard(const uint8_t* bytes, uint16_t length,
                              boot_keyboard_t* keyboard)
{
    if (length < 9 || bytes[0] != 9 || bytes[1] != 2 || !bytes[5] ||
        usb_u16(bytes + 2) != length) return false;
    bool boot_interface = false;
    uint8_t interface = 0;
    for (uint32_t offset = 9; offset < length;) {
        if (length - offset < 2) return false;
        const uint8_t* d = bytes + offset;
        if (d[0] < 2 || d[0] > length - offset) return false;
        if (d[1] == 4) {
            if (d[0] < 9) return false;
            interface = d[2];
            boot_interface = d[3] == 0 && d[5] == 3 && d[6] == 1 && d[7] == 1;
        } else if (d[1] == 5 && boot_interface) {
            if (d[0] < 7) return false;
            uint16_t packet = usb_u16(d + 4);
            if ((d[2] & 0x80U) && !(d[2] & 0x70U) && (d[2] & 15U) &&
                (d[3] & 3U) == 3 && packet >= 8 && packet <= 1024 && d[6]) {
                keyboard->configuration = bytes[5];
                keyboard->interface = interface;
                keyboard->endpoint = d[2];
                keyboard->interval = d[6];
                keyboard->packet = packet;
                return true;
            }
        }
        offset += d[0];
    }
    return false;
}

static void usb_delay(uint64_t milliseconds)
{
    uint64_t start = hpet_millis();
    while (hpet_millis() - start < milliseconds) __asm__ volatile("pause");
}

/* Hubs use alternate setting zero (single TT, even on multi-TT hardware). */
static bool usb_find_hub(const uint8_t* bytes, uint16_t length)
{
    if (length < 9 || bytes[0] != 9 || bytes[1] != 2 || !bytes[5] ||
        usb_u16(bytes + 2) != length) return false;
    bool found = false;
    for (uint32_t offset = 9; offset < length;) {
        const uint8_t* d = bytes + offset;
        if (length - offset < 2 || d[0] < 2 || d[0] > length - offset) return false;
        if (d[1] == 4) {
            if (d[0] < 9) return false;
            if (!d[3] && d[5] == 9 && !d[6] && d[7] <= 1) found = true;
        }
        offset += d[0];
    }
    return found;
}

static bool usb_hub_status(xhci_device_t* hub, uint8_t port,
                            uint16_t* status, uint16_t* changes)
{
    uint8_t bytes[4];
    if (xhci_control(hub, 0xA3, 0, 0, port, bytes, 4) != 4) return false;
    *status = usb_u16(bytes);
    *changes = usb_u16(bytes + 2);
    return true;
}

static bool usb_hub_clear_changes(xhci_device_t* hub, uint8_t port, uint16_t changes)
{
    for (uint8_t bit = 0; bit < 5; bit++)
        if ((changes & (1U << bit)) &&
            xhci_control(hub, 0x23, 1, 16 + bit, port, 0, 0) < 0) return false;
    return true;
}

/* -1: hub control pipe failed; 0: no usable child; 1..3: xHCI speed ID. */
static int usb_hub_reset_port(xhci_device_t* hub, uint8_t port)
{
    uint16_t status, changes;
    if (!usb_hub_status(hub, port, &status, &changes) ||
        !usb_hub_clear_changes(hub, port, changes)) return -1;
    if (!(status & 1U) || (status & (1U << 3))) return 0;
    usb_delay(100); /* Connect debounce. */
    if (!usb_hub_status(hub, port, &status, &changes)) return -1;
    if (!(status & 1U) || (status & (1U << 3))) return 0;
    if (xhci_control(hub, 0x23, 3, 4, port, 0, 0) < 0) return -1; /* PORT_RESET. */
    uint64_t start = hpet_millis();
    do {
        usb_delay(10);
        if (!usb_hub_status(hub, port, &status, &changes)) return -1;
        if (!(status & 1U) || (status & (1U << 3))) break;
        if (!(status & (1U << 4)) && (status & (1U << 1)) && (changes & (1U << 4))) {
            if (!usb_hub_clear_changes(hub, port, changes)) return -1;
            usb_delay(10); /* Reset recovery before Address Device. */
            return (status & (1U << 9)) ? 2 : (status & (1U << 10)) ? 3 : 1;
        }
    } while (hpet_millis() - start < 1000);
    /* Don't leave an unaddressed, enabled child beside the next reset port. */
    return xhci_control(hub, 0x23, 1, 1, port, 0, 0) < 0 ? -1 : 0;
}

static bool usb_enumerate_device(xhci_device_t* dev);

static bool usb_enumerate_hub(xhci_device_t* hub, uint8_t configuration)
{
    uint8_t hub_descriptor[71]; /* USB2: header plus two 256-bit port bitmaps. */
    if (xhci_control(hub, 0, 9, configuration, 0, 0, 0) < 0 ||
        xhci_control(hub, 0xA0, 6, 0x2900, 0, hub_descriptor, 7) != 7 ||
        hub_descriptor[1] != 0x29 || !hub_descriptor[2]) return false;
    uint8_t ports = hub_descriptor[2];
    uint16_t length = hub_descriptor[0];
    /* The obsolete PortPwrCtrlMask is unused. Some hubs (including QEMU's
     * eight-port hub) omit its reserved hub bit; require the removable bitmap
     * and read the advertised length rather than assuming equal bitmap sizes. */
    if (length < 7 + (ports + 1U + 7U) / 8U || length > sizeof(hub_descriptor) ||
        xhci_control(hub, 0xA0, 6, 0x2900, 0, hub_descriptor, length) != length ||
        hub_descriptor[0] != length || hub_descriptor[1] != 0x29 ||
        hub_descriptor[2] != ports) return false;
    uint16_t characteristics = usb_u16(hub_descriptor + 3);
    if (!xhci_configure_hub(hub, ports, (characteristics >> 5) & 3U)) return false;
    /* Power every port before waiting; ganged and always-powered hubs accept
     * PORT_POWER as well. bPwrOn2PwrGood is expressed in two-millisecond units. */
    for (uint16_t port = 1; port <= ports; port++)
        if (xhci_control(hub, 0x23, 3, 8, port, 0, 0) < 0) return false;
    usb_delay((uint64_t)hub_descriptor[5] * 2 + 20);
    con_write("USB: scanning hub ports: ", 0x07);
    print_uint64_dec(ports);
    con_write("\n", 0x07);
    for (uint16_t port = 1; port <= ports; port++) {
        int speed = usb_hub_reset_port(hub, (uint8_t)port);
        if (speed < 0) return false;
        if (!speed) continue;
        xhci_device_t* child = xhci_open_hub_port(hub, (uint8_t)port, (uint8_t)speed);
        if (child && usb_enumerate_device(child)) return true;
        if (!xhci_close_device(child)) return false;
        /* A rejected device must not remain enabled at the default address
         * while another port is reset. Keep the hub alive for later siblings. */
        if (xhci_control(hub, 0x23, 1, 1, port, 0, 0) < 0) return false;
    }
    return false;
}

static bool usb_enumerate_device(xhci_device_t* dev)
{
    /* Address Device already sent SET_ADDRESS; do not send it a second time. */
    if (xhci_control(dev, 0x80, 6, 0x0100, 0, descriptor, 8) != 8 ||
        descriptor[0] != 18 || descriptor[1] != 1 ||
        !xhci_set_ep0_packet_size(dev, descriptor[7])) return false;
    if (xhci_control(dev, 0x80, 6, 0x0100, 0, descriptor, 18) != 18 ||
        descriptor[0] != 18 || descriptor[1] != 1) return false;
    uint8_t configurations = descriptor[17];
    for (uint16_t i = 0; i < configurations; i++) {
        if (xhci_control(dev, 0x80, 6, 0x0200 | i, 0, descriptor, 9) != 9 ||
            descriptor[0] != 9 || descriptor[1] != 2) return false;
        uint16_t length = usb_u16(descriptor + 2);
        if (length < 9 || length > sizeof(descriptor)) continue;
        if (xhci_control(dev, 0x80, 6, 0x0200 | i, 0, descriptor, length) != length) return false;
        if (usb_find_hub(descriptor, length))
            return usb_enumerate_hub(dev, descriptor[5]);
        boot_keyboard_t keyboard;
        if (!usb_find_keyboard(descriptor, length, &keyboard)) continue;
        if (xhci_control(dev, 0, 9, keyboard.configuration, 0, 0, 0) < 0 ||
            xhci_control(dev, 0x21, 11, 0, keyboard.interface, 0, 0) < 0 || /* Boot protocol. */
            xhci_control(dev, 0x21, 10, 0, keyboard.interface, 0, 0) < 0)  /* Idle: changes only. */
            return false;
        if (!xhci_keyboard_endpoint(dev, keyboard.endpoint, keyboard.packet, keyboard.interval))
            return false;
        keyboard_device = dev;
        return true;
    }
    return false;
}

void usb_init(void)
{
    if (attempted) return;
    attempted = true;
    if (!xhci_init()) return;
    xhci_controller_t* host = 0;
    uint32_t host_number = 0;
    while ((host = xhci_next_controller(host))) {
        host_number++;
        uint8_t ports = xhci_port_count(host);
        for (uint16_t port = 1; port <= ports; port++) {
            xhci_device_t* dev = xhci_open_port(host, (uint8_t)port);
            if (dev && usb_enumerate_device(dev)) {
                keyboard_ready = true;
                hub_check_at = hpet_millis() + 100;
                con_write("USB: boot keyboard ready on host ", 0x02);
                print_uint64_dec(host_number);
                con_write(" root port ", 0x02);
                print_uint64_dec(port);
                con_write("\n", 0x02);
                return;
            }
            if (!xhci_close_device(dev)) break;
        }
    }
    con_write("USB: no supported boot keyboard found\n", 0x07);
}

/* Check the whole path: a hub can disconnect while its root port stays live.
 * This also catches a replaced device even if CONNECTION is already set again. */
static bool usb_keyboard_path_active(void)
{
    if (!xhci_keyboard_active(keyboard_device)) return false;
    if (hpet_millis() < hub_check_at) return true;
    hub_check_at = hpet_millis() + 100;
    xhci_device_t* path[5];
    uint8_t count = 0;
    for (xhci_device_t* child = keyboard_device; xhci_parent(child); child = xhci_parent(child)) {
        if (count == sizeof(path) / sizeof(path[0])) return false;
        path[count++] = child;
    }
    /* Check from root to leaf, so a detached hub is never itself queried. */
    while (count) {
        xhci_device_t* child = path[--count];
        uint16_t status, changes;
        if (!usb_hub_status(xhci_parent(child), xhci_parent_port(child), &status, &changes) ||
            (status & 3U) != 3U || (status & (1U << 3)) || (changes & 1U)) return false;
    }
    return true;
}

static bool usb_modifier(uint8_t scan)
{
    return scan == 0x1d || scan == 0x2a || scan == 0x36 || scan == 0x38;
}

static void usb_keyboard_report(const uint8_t report[8], usb_key_event_t handler)
{
    uint8_t next[128];
    memory_zero(next, sizeof(next));
    static const uint8_t modifiers[8] = {0x1d, 0x2a, 0x38, 0, 0x1d, 0x36, 0x38, 0};
    /* ErrorRollOver/POSTFail/ErrorUndefined do not describe released keys. */
    for (uint8_t i = 2; i < 8; i++)
        if (report[i] >= 1 && report[i] <= 3) return;
    for (uint8_t i = 0; i < 8; i++)
        if ((report[0] & (1U << i)) && modifiers[i]) next[modifiers[i]] = 1;
    for (uint8_t i = 2; i < 8; i++)
        if (report[i] < sizeof(usage_to_scan) && usage_to_scan[report[i]])
            next[usage_to_scan[report[i]]] = 1;
    /* Modifiers first, including press + release in the same report as a key. */
    for (uint8_t pass = 0; pass < 2; pass++) {
        for (uint8_t scan = 1; scan < 128; scan++) {
            if (usb_modifier(scan) != (pass == 0) || next[scan] == held[scan]) continue;
            handler(scan, next[scan]);
            if (next[scan] && !usb_modifier(scan) && scan != 0x3a) {
                repeat_key = scan;
                repeat_at = hpet_millis() + 400;
            }
        }
    }
    memory_copy(held, next, sizeof(held));
    if (repeat_key && !held[repeat_key]) repeat_key = 0;
}

void usb_poll_keyboard(usb_key_event_t handler)
{
    if (!keyboard_ready || !handler) return;
    uint8_t report[8];
    if (xhci_keyboard_poll(keyboard_device, report)) usb_keyboard_report(report, handler);
    if (!usb_keyboard_path_active()) {
        memory_zero(report, sizeof(report));
        usb_keyboard_report(report, handler);
        keyboard_ready = false;
        return; /* Release held keys on detach/failure; reconnect needs a reboot. */
    }
    if (repeat_key && hpet_millis() >= repeat_at) {
        handler(repeat_key, 1);
        repeat_at = hpet_millis() + 40;
    }
}
