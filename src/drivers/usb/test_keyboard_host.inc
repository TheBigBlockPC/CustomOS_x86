/* Compiled only by test_keyboard.py, never linked into the kernel. */
#include "usb.c"
#include "../../input_subsystem.c"
#include "../../util.c"

extern int printf(const char*, ...);
extern void exit(int) __attribute__((noreturn));
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint64_t now;
static uint8_t ps2_scan, pending_report[8];
static bool pending, active = true;
static struct { uint8_t scan, ascii; int down; } events[128];
static unsigned event_count;
void con_write(char* text, uint8_t color) { (void)text; (void)color; }
uint64_t hpet_millis(void) { return now; }
uint8_t get_key(void) { uint8_t scan = ps2_scan; ps2_scan = 0; return scan; }
bool xhci_init(void) { return false; }
xhci_controller_t* xhci_next_controller(xhci_controller_t* p) { (void)p; return 0; }
uint8_t xhci_port_count(xhci_controller_t* h) { (void)h; return 0; }
xhci_device_t* xhci_open_port(xhci_controller_t* h, uint8_t p) { (void)h; (void)p; return 0; }
xhci_device_t* xhci_open_hub_port(xhci_device_t* d, uint8_t p, uint8_t s)
{ (void)d; (void)p; (void)s; return 0; }
bool xhci_configure_hub(xhci_device_t* d, uint8_t p, uint8_t t)
{ (void)d; (void)p; (void)t; return false; }
bool xhci_close_device(xhci_device_t* d) { (void)d; return true; }
xhci_device_t* xhci_parent(xhci_device_t* d) { (void)d; return 0; }
uint8_t xhci_parent_port(xhci_device_t* d) { (void)d; return 0; }
bool xhci_set_ep0_packet_size(xhci_device_t* d, uint8_t n) { (void)d; (void)n; return false; }
int xhci_control(xhci_device_t* dev, uint8_t t, uint8_t r, uint16_t v, uint16_t i, void* d, uint16_t n)
{ (void)dev; (void)t; (void)r; (void)v; (void)i; (void)d; (void)n; return -1; }
bool xhci_keyboard_endpoint(xhci_device_t* d, uint8_t a, uint16_t n, uint8_t i)
{ (void)d; (void)a; (void)n; (void)i; return false; }
bool xhci_keyboard_active(xhci_device_t* d) { (void)d; return active; }
bool xhci_keyboard_poll(xhci_device_t* d, uint8_t report[8]) {
    (void)d;
    if (!pending) return false;
    memory_copy(report, pending_report, 8); pending = false; return true;
}
static void event(uint8_t scan, char ascii, int down) {
    CHECK(event_count < 128);
    events[event_count].scan = scan;
    events[event_count].ascii = (uint8_t)ascii;
    events[event_count++].down = down;
}
static void report(uint8_t modifiers, uint8_t first, uint8_t second) {
    memory_zero(pending_report, 8);
    pending_report[0] = modifiers; pending_report[2] = first; pending_report[3] = second;
    pending = true; poll_events();
}
static bool has(uint8_t scan, uint8_t ascii, int down) {
    for (unsigned i = 0; i < event_count; i++)
        if (events[i].scan == scan && events[i].ascii == ascii && events[i].down == down) return true;
    return false;
}
int main(void) {
    uint8_t config[] = {9,2,34,0,1,1,0,0x80,50, 9,4,0,0,1,3,1,1,0,
                       9,0x21,0x11,1,0,1,0x22,63,0, 7,5,0x81,3,8,0,10};
    boot_keyboard_t keyboard;
    CHECK(usb_find_keyboard(config, sizeof(config), &keyboard));
    CHECK(keyboard.configuration == 1 && keyboard.interface == 0 &&
          keyboard.endpoint == 0x81 && keyboard.packet == 8 && keyboard.interval == 10);
    for (uint16_t n = 0; n < sizeof(config); n++) CHECK(!usb_find_keyboard(config, n, &keyboard));
    config[9] = 0; CHECK(!usb_find_keyboard(config, sizeof(config), &keyboard)); config[9] = 9;
    config[16] = 2; CHECK(!usb_find_keyboard(config, sizeof(config), &keyboard)); config[16] = 1;
    config[12] = 1; CHECK(!usb_find_keyboard(config, sizeof(config), &keyboard)); config[12] = 0;
    config[29] = 1; CHECK(!usb_find_keyboard(config, sizeof(config), &keyboard)); config[29] = 0x81;
    config[29] = 0x80; CHECK(!usb_find_keyboard(config, sizeof(config), &keyboard)); config[29] = 0x81;
    config[30] = 2; CHECK(!usb_find_keyboard(config, sizeof(config), &keyboard)); config[30] = 3;
    config[31] = 7; CHECK(!usb_find_keyboard(config, sizeof(config), &keyboard)); config[31] = 8;
    config[33] = 0; CHECK(!usb_find_keyboard(config, sizeof(config), &keyboard)); config[33] = 10;
    config[27] = 255; CHECK(!usb_find_keyboard(config, sizeof(config), &keyboard)); config[27] = 7;
    config[18] = 1; CHECK(!usb_find_keyboard(config, sizeof(config), &keyboard)); config[18] = 9;
    uint8_t hub_config[] = {9,2,25,0,1,1,0,0x80,0, 9,4,0,0,1,9,0,0,0,
                            7,5,0x81,3,1,0,255};
    CHECK(usb_find_hub(hub_config, sizeof(hub_config)));
    for (uint16_t n = 0; n < sizeof(hub_config); n++) CHECK(!usb_find_hub(hub_config, n));
    hub_config[16] = 1; CHECK(usb_find_hub(hub_config, sizeof(hub_config)));
    hub_config[16] = 2; CHECK(!usb_find_hub(hub_config, sizeof(hub_config))); hub_config[16] = 0;
    hub_config[12] = 1; CHECK(!usb_find_hub(hub_config, sizeof(hub_config))); hub_config[12] = 0;
    hub_config[18] = 0; CHECK(!usb_find_hub(hub_config, sizeof(hub_config)));
    /* Bounded descriptor fuzzing: hostile lengths must never escape the buffer. */
    uint32_t random = 1;
    uint8_t fuzz[256];
    for (unsigned trial = 0; trial < 10000; trial++) {
        for (unsigned i = 0; i < sizeof(fuzz); i++) {
            random = random * 1664525U + 1013904223U; fuzz[i] = random >> 24;
        }
        uint16_t n = random & 255U;
        if (n >= 9) { fuzz[0] = 9; fuzz[1] = 2; fuzz[2] = n; fuzz[3] = 0; fuzz[5] = 1; }
        (void)usb_find_keyboard(fuzz, n, &keyboard);
        (void)usb_find_hub(fuzz, n);
    }
    keyboard_ready = true;
    set_kb_handler(event);
    report(0, 4, 0); CHECK(has(0x1e, 'a', 1));
    event_count = 0; report(0, 4, 4); CHECK(event_count == 0); /* Duplicates/unchanged. */
    report(0, 1, 1); CHECK(held[0x1e] && event_count == 0); /* Rollover is not release. */
    report(0, 0, 0); CHECK(has(0x9e, 'a', 0));
    event_count = 0; report(0x22, 4, 0); CHECK(has(0x1e, 'A', 1)); /* Both Shifts. */
    report(0x20, 4, 0); CHECK(usb_key_state[0x36] && !usb_key_state[0x2a]);
    event_count = 0; report(0, 0, 0); CHECK(has(0x9e, 'a', 0));
    event_count = 0; report(0, 28, 0); CHECK(has(0x15, 'z', 1)); report(0, 0, 0);
    event_count = 0; report(0, 255, 127); CHECK(event_count == 0); /* Unknown usages. */
    report(0, 0, 0);
    ps2_scan = 0x2a; poll_events(); report(0, 4, 0); CHECK(has(0x1e, 'A', 1));
    report(0x02, 4, 0); ps2_scan = 0xaa; poll_events();
    CHECK(usb_key_state[0x2a] && !key_state[0x2a]); /* Independent keyboard states. */
    report(0, 0, 0);
    event_count = 0; now = 0; report(0, 5, 0); CHECK(event_count == 1);
    now = 399; poll_events(); CHECK(event_count == 1);
    now = 400; poll_events(); CHECK(event_count == 2 && has(0x30, 'b', 1));
    now = 439; poll_events(); CHECK(event_count == 2);
    now = 440; poll_events(); CHECK(event_count == 3);
    report(0, 0, 0); unsigned released = event_count;
    now = 1000; poll_events(); CHECK(event_count == released);
    report(0x02, 4, 0); event_count = 0; active = false; poll_events();
    CHECK(has(0x9e, 'a', 0) && !repeat_key && !keyboard_ready);
    CHECK(!usb_key_state[0x2a] && !usb_key_state[0x1e]);
    printf("PASS: %d keyboard/parser checks and 10000 malformed descriptor trials\n", checks);
    return 0;
}
