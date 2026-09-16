#include "types.h"
#include "drivers/keyboard_ps2.h"
#include "input_subsystem.h"

static char scan_to_ascii[128] = {
    [0x29] = '^',
    [0x02] = '1',
    [0x03] = '2',
    [0x04] = '3',
    [0x05] = '4',
    [0x06] = '5',
    [0x07] = '6',
    [0x08] = '7',
    [0x09] = '8',
    [0x0A] = '9',
    [0x0B] = '0',
    [0x0C] = '\xDF', //ß
    [0x0D] = '`',
    [0x0E] = '\b',
    [0x0F] = '\t',
    [0x10] = 'q',
    [0x11] = 'w',
    [0x12] = 'e',
    [0x13] = 'r',
    [0x14] = 't',
    [0x15] = 'z',
    [0x16] = 'u',
    [0x17] = 'i',
    [0x18] = 'o',
    [0x19] = 'p',
    [0x1A] = '\xFC',//ü
    [0x1B] = '+',
    [0x1C] = '\n',
    [0x1E] = 'a',
    [0x1F] = 's',
    [0x20] = 'd',
    [0x21] = 'f',
    [0x22] = 'g',
    [0x23] = 'h',
    [0x24] = 'j',
    [0x25] = 'k',
    [0x26] = 'l',
    [0x27] = '\xF6',//ö
    [0x28] = '\xE4',//ä
    [0x2B] = '#',
    [0x56] = '<',
    [0x2C] = 'y',
    [0x2D] = 'x',
    [0x2E] = 'c',
    [0x2F] = 'v',
    [0x30] = 'b',
    [0x31] = 'n',
    [0x32] = 'm',
    [0x33] = ',',
    [0x34] = '.',
    [0x35] = '-',
    [0x39] = ' '
};

static char scan_to_ascii_shift[128] = {
    [0x29] = '\xB0',//°
    [0x02] = '!',
    [0x03] = '"',
    [0x04] = '\xA7',//§
    [0x05] = '$',
    [0x06] = '%',
    [0x07] = '&',
    [0x08] = '/',
    [0x09] = '(',
    [0x0A] = ')',
    [0x0B] = '=',
    [0x0C] = '?',
    [0x0D] = '`',
    [0x0E] = '\b',
    [0x0F] = '\t',
    [0x10] = 'Q',
    [0x11] = 'W',
    [0x12] = 'E',
    [0x13] = 'R',
    [0x14] = 'T',
    [0x15] = 'Z',
    [0x16] = 'U',
    [0x17] = 'I',
    [0x18] = 'O',
    [0x19] = 'P',
    [0x1A] = '\xDC',//Ü
    [0x1B] = '*',
    [0x1C] = '\n',
    [0x1E] = 'A',
    [0x1F] = 'S',
    [0x20] = 'D',
    [0x21] = 'F',
    [0x22] = 'G',
    [0x23] = 'H',
    [0x24] = 'J',
    [0x25] = 'K',
    [0x26] = 'L',
    [0x27] = '\xD6',//ö
    [0x28] = '\xC4',//ä
    [0x2B] = '\'',
    [0x56] = '>',
    [0x2C] = 'Y',
    [0x2D] = 'X',
    [0x2E] = 'C',
    [0x2F] = 'V',
    [0x30] = 'B',
    [0x31] = 'N',
    [0x32] = 'M',
    [0x33] = ';',
    [0x34] = ':',
    [0x35] = '_',
    [0x39] = ' '
};

static char key_state[128];


static kb_event key_handler;

void set_kb_handler(kb_event handler){
    key_handler = handler;
}

void poll_events(){
    uint8_t scan_code = get_key();
    if(scan_code == 0) return;

    uint8_t normalized_scan = scan_code & 0b01111111;
    int is_down = !(scan_code & 0b10000000);
    if(is_down){
        key_state[normalized_scan] = 1;
    }else{
        key_state[normalized_scan] = 0;
    }

    if(key_state[0x2A] == 1){
        key_handler(scan_code,scan_to_ascii_shift[scan_code],is_down);
    }else{
        key_handler(scan_code,scan_to_ascii[scan_code],is_down);
    }
}