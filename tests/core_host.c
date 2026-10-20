#include "../src/util.c"
#include "../src/input_subsystem.c"

extern int printf(const char*, ...);
extern void exit(int) __attribute__((noreturn));
static int checks;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    printf("FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)
static char output[80];
void con_write(char* text, uint8_t color)
{
    (void)color;
    uint64_t index = 0;
    while (*text && index < sizeof(output) - 1) output[index++] = *text++;
    output[index] = 0;
}
static bool equal(char* a, char* b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static uint8_t scancode;
uint8_t get_key(void) { return scancode; }
static uint8_t received_code;
static char received_ascii;
static int received_down;
static void key(uint8_t code, char ascii, int down)
{
    received_code = code;
    received_ascii = ascii;
    received_down = down;
}
int main(void)
{
    struct { char buffer[4]; char guard; } copy = {{'?', '?', '?', '?'}, '!'};
    str_copy(copy.buffer, "abc", true);
    CHECK(equal(copy.buffer, "abc") && copy.guard == '!');
    str_copy(copy.buffer, "", true);
    CHECK(copy.buffer[0] == 0);
    CHECK(!str_start_comp("", "cd "));
    CHECK(!str_start_comp("c", "cd "));
    CHECK(str_start_comp("cd EFI", "cd "));
    CHECK(str_start_comp("fs", "fs"));
    print_uint64_dec(0);
    CHECK(equal(output, "0"));
    print_uint64_dec(1);
    CHECK(equal(output, "1"));
    print_uint64_dec(1234567890);
    CHECK(equal(output, "1234567890"));
    print_uint64_dec(~0ULL);
    CHECK(equal(output, "18446744073709551615"));
    set_kb_handler(key);
    scancode = 0x1e;
    poll_events();
    CHECK(received_ascii == 'a' && received_down);
    scancode = 0x9e; /* Key release must not index beyond the 128-entry map. */
    poll_events();
    CHECK(received_code == 0x9e && received_ascii == 'a' && !received_down);
    scancode = 0x36; /* Right shift. */
    poll_events();
    scancode = 0x1e;
    poll_events();
    CHECK(received_ascii == 'A');
    scancode = 0xb6;
    poll_events();
    scancode = 0x1e;
    poll_events();
    CHECK(received_ascii == 'a');
    set_kb_handler(0);
    poll_events();
    printf("PASS: %d string/formatting/keyboard checks\n", checks);
    return 0;
}
