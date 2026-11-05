/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* The Hebrew keyboard (hebkey.h). Weak definitions: see there. */
#include "gem.h"
#include "hebkey.h"

#ifndef K_ALT
#define K_ALT 0x08
#endif

short hebrew_kbd __attribute__((weak));
void (*hebrew_kbd_hook)(void) __attribute__((weak));

/* Israeli SI-1452 layout by key position (scancode 0x10-0x35), giving
   Atari character codes; 0 = not remapped. The table ECal, EMail, EFTP
   and Claude ST each had a copy of. */
static const unsigned char hebrew_keys[0x36 - 0x10] = {
	/* 10 q..p */ '/', '\'', 0xD4, 0xD5, 0xC2, 0xCA, 0xC7, 0xD8, 0xDA, 0xD2,
	/* 1a [ ] ret ctrl */ 0, 0, 0, 0,
	/* 1e a..' */ 0xD6, 0xC5, 0xC4, 0xCC, 0xD1, 0xCB, 0xC9, 0xCD, 0xD9, 0xDB, ',',
	/* 29 ` lshift \ */ 0, 0, 0,
	/* 2c z../ */ 0xC8, 0xD0, 0xC3, 0xC6, 0xCF, 0xCE, 0xD3, 0xD7, 0xDC, '.'
};

unsigned char key_char(short kstate, short key) __attribute__((weak));
unsigned char key_char(short kstate, short key)
{
	short scan = (key >> 8) & 0xff;
	unsigned char ascii = (unsigned char)(key & 0xff);
	if (hebrew_kbd && !(kstate & (K_LSHIFT | K_RSHIFT | K_ALT)) && !key_ctrl(kstate, key) &&
	    scan >= 0x10 && scan < 0x36 && hebrew_keys[scan - 0x10])
		ascii = hebrew_keys[scan - 0x10];
	return ascii;
}

void set_hebrew_kbd(short on) __attribute__((weak));
void set_hebrew_kbd(short on)
{
	hebrew_kbd = on;
	if (hebrew_kbd_hook)
		hebrew_kbd_hook();
}
