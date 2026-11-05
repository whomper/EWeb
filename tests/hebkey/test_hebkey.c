/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* hebkey.c on the build machine: the table, the modifiers, the hook. */
#include <stdio.h>
#include "gem.h"
#include "hebkey.h"

static int fails, checks;
#define CK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static int hooked, tn_calls, vc_calls;
static char tn_s[8];
static short tn_x, tn_y, vc_col;
void vst_color(short h, short color) { (void)h; vc_calls++; vc_col = color; }
void text_n(short h, short x, short y, const char *s, long n)
{
	(void)h;
	tn_calls++;
	tn_x = x;
	tn_y = y;
	tn_s[0] = n > 0 ? s[0] : 0;
	tn_s[1] = n > 1 ? s[1] : 0;
	tn_s[2] = 0;
}


static void hook(void) { hooked++; }

/* the AES's key word: scancode << 8 | ascii */
#define KEY(scan, ascii) ((short)(((scan) << 8) | (ascii)))

int main(void)
{
	short s;
	/* off: every key is itself */
	CK(key_char(0, KEY(0x10, 'q')) == 'q');
	CK(key_char(0, KEY(0x1e, 'a')) == 'a');
	set_hebrew_kbd(1);
	CK(hebrew_kbd == 1);
	/* the SI-1452 rows: q w e r t y u i o p */
	{
		static const unsigned char row1[10] = { '/', '\'', 0xD4, 0xD5, 0xC2, 0xCA, 0xC7, 0xD8, 0xDA, 0xD2 };
		static const unsigned char row2[11] = { 0xD6, 0xC5, 0xC4, 0xCC, 0xD1, 0xCB, 0xC9, 0xCD, 0xD9, 0xDB, ',' };
		static const unsigned char row3[10] = { 0xC8, 0xD0, 0xC3, 0xC6, 0xCF, 0xCE, 0xD3, 0xD7, 0xDC, '.' };
		for (s = 0; s < 10; s++)
			CK(key_char(0, KEY(0x10 + s, 'x')) == row1[s]);
		for (s = 0; s < 11; s++)
			CK(key_char(0, KEY(0x1e + s, 'x')) == row2[s]);
		for (s = 0; s < 10; s++)
			CK(key_char(0, KEY(0x2c + s, 'x')) == row3[s]);
	}
	/* Shift, Control and Alt keep the Latin key; so do the keys outside the table */
	CK(key_char(K_LSHIFT, KEY(0x10, 'Q')) == 'Q');
	CK(key_char(K_RSHIFT, KEY(0x10, 'Q')) == 'Q');
	CK(key_char(0x08, KEY(0x10, 'q')) == 'q');
	CK(key_char(K_CTRL, KEY(0x10, 'q')) == 'q');
	CK(key_char(0, KEY(0x10, 0x11)) == 0x11);	/* ^Q typed while the program was busy: kstate 0 */
	CK(key_char(0, KEY(0x1c, 13)) == 13);		/* Return */
	CK(key_char(0, KEY(0x39, ' ')) == ' ');		/* Space */
	CK(key_char(0, KEY(0x0b, '0')) == '0');		/* digits */
	CK(key_char(0, KEY(0x1a, '[')) == '[');		/* unmapped keys inside the range */
	/* the hook: called when the keyboard is switched */
	hebrew_kbd_hook = hook;
	set_hebrew_kbd(0);
	CK(hooked == 1 && hebrew_kbd == 0);
	CK(key_char(0, KEY(0x10, 'q')) == 'q');
	set_hebrew_kbd(1);
	CK(hooked == 2 && hebrew_kbd == 1);
	/* the badge: only while the keyboard is on, the two letters ayin bet */
	set_hebrew_kbd(0);
	hebrew_badge(1, 100, 20, 1);
	CK(tn_calls == 0 && vc_calls == 0);
	set_hebrew_kbd(1);
	hebrew_badge(1, 100, 20, 3);
	CK(tn_calls == 1 && vc_calls == 1 && vc_col == 3);
	CK(tn_x == 100 && tn_y == 20 && (unsigned char)tn_s[0] == 0xD3 && (unsigned char)tn_s[1] == 0xC3);
	printf("hebkey: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
