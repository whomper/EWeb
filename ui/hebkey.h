/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * hebkey.h - the Israeli (SI-1452) Hebrew keyboard, one for the family.
 *
 * F10 (or the app's menu entry) switches it: while hebrew_kbd is set,
 * key_char() gives a letter key's Hebrew Atari code (0xC2-0xDC, logical
 * order) in place of its Latin one. Shift, Alt and Control keys are left as
 * they are. The table is by key position (scancode 0x10-0x35).
 *
 *   hebrew_kbd              1 while it is on
 *   set_hebrew_kbd(on)      sets it and calls hebrew_kbd_hook, if any (the
 *                           app's info line or badge shows it)
 *   key_char(kstate, key)   the character a key types: the AES's key word
 *                           (scancode << 8 | ascii) and kstate
 *
 * The definitions are weak: an app that still has its own copies keeps
 * them (no clash) until it drops them. dlg_do (dialog.c) types through
 * key_char when it is linked, so every dialog takes Hebrew.
 */
#ifndef AERA_HEBKEY_H
#define AERA_HEBKEY_H

extern short hebrew_kbd;
extern void (*hebrew_kbd_hook)(void);
void set_hebrew_kbd(short on);
unsigned char key_char(short kstate, short key);

#endif
