#include <Lemon/Core/Input.h>
#include <Lemon/Core/Keyboard.h>

#include "fterm.h"

int keymap_us[128] =
{
	0,  27, '1', '2', '3', '4', '5', '6', '7', '8',	/* 9 */
	'9', '0', '-', '=', '\b',	/* Backspace */
	'\t',			/* Tab */
	'q', 'w', 'e', 'r',	/* 19 */
	't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',	/* Enter key */
	KEY_CONTROL,			/* 29   - Control */
	'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',	/* 39 */
	'\'', '`',   KEY_SHIFT,		/* Left shift */
	'\\', 'z', 'x', 'c', 'v', 'b', 'n',			/* 49 */
	'm', ',', '.', '/',   0,				/* Right shift */
	'*',
	KEY_ALT,	/* Alt */
	' ',	/* Space bar */
	KEY_CAPS,	/* Caps lock */
	KEY_F1,	/* 59 - F1 key ... > */
	KEY_F2,   KEY_F3,   KEY_F4,   KEY_F5,   KEY_F6,   KEY_F7,   KEY_F8,   KEY_F9,
	KEY_F10,	/* < ... F10 */
	0,	/* 69 - Num lock*/
	0,	/* Scroll Lock */
	KEY_HOME,	/* Home key */
	KEY_ARROW_UP,	/* Up Arrow */
	0,	/* Page Up */
	'-',
	KEY_ARROW_LEFT,	/* Left Arrow */
	0,
	KEY_ARROW_RIGHT,	/* Right Arrow */
	'+',
	KEY_END,	/* 79 - End key*/
	KEY_ARROW_DOWN,	/* Down Arrow */
	0,	/* Page Down */
	0,	/* Insert Key */
	KEY_DELETE,	/* Delete Key */
	0,   0,   0,
	0,	/* F11 Key */
	0,	/* F12 Key */
	0,	/* All other keys are undefined */
	0,
	0,
	0,  /* 90 */
	KEY_GUI,  /* Left GUI key */
	KEY_GUI,  /* Right GUI key */
};

int keymap_us_shift[128] =
{
	0,  27, '!', '@', '#', '$', '%', '^', '&', '*',	/* 9 */
	'(', ')', '_', '+', '\b',	/* Backspace */
	'\t',			/* Tab */
	'Q', 'W', 'E', 'R',	/* 19 */
	'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',	/* Enter key */
	KEY_CONTROL,			/* 29   - Control */
	'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',	/* 39 */
	'\"', '~', KEY_SHIFT,		/* Left shift */
	'|', 'Z', 'X', 'C', 'V', 'B', 'N',			/* 49 */
	'M', '<', '>', '?',   0,				/* Right shift */
	'*',
	KEY_ALT,	/* Alt */
	' ',	/* Space bar */
	KEY_CAPS,	/* Caps lock */
	KEY_F1,	/* 59 - F1 key ... > */
	KEY_F2,   KEY_F3,   KEY_F4,   KEY_F5,   KEY_F6,   KEY_F7,   KEY_F8,   KEY_F9,
	KEY_F10,	/* < ... F10 */
	0,	/* 69 - Num lock*/
	0,	/* Scroll Lock */
	0,	/* Home key */
	KEY_ARROW_UP,	/* Up Arrow */
	0,	/* Page Up */
	'-',
	KEY_ARROW_LEFT,	/* Left Arrow */
	0,
	KEY_ARROW_RIGHT,	/* Right Arrow */
	'+',
	0,	/* 79 - End key*/
	KEY_ARROW_DOWN,	/* Down Arrow */
	0,	/* Page Down */
	0,	/* Insert Key */
	KEY_DELETE,	/* Delete Key */
	0,   0,   0,
	0,	/* F11 Key */
	0,	/* F12 Key */
	0,	/* All other keys are undefined */
	0,
	0,
	0,  /* 90 */
	KEY_GUI,  /* Left GUI key */
	KEY_GUI,  /* Right GUI key */
};

void InputManager::Poll(){
    uint8_t buf[16];
    ssize_t count = Lemon::PollKeyboard(buf, 16);

    for(ssize_t i = 0; i < count; i++){
        uint8_t code = buf[i] & 0x7F;
        bool isPressed = !((buf[i] >> 7) & 1);
        int key = 0;
        
        if(keyboard.shift){
            key = keymap_us_shift[code];
        } else {
            key = keymap_us[code];
        }

        switch (key)
        {
        case KEY_SHIFT:
            keyboard.shift = isPressed;
            break;
        case KEY_CONTROL:
            keyboard.control = isPressed;
            break;
        case KEY_ALT:
            keyboard.alt = isPressed;
            break;
        case KEY_CAPS:
            if(isPressed) keyboard.caps = !keyboard.caps;
            break;
        }

        if(isPressed){
			OnKey(key);
		}
    }
}   