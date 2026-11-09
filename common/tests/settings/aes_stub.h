/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* what the host test's stand-in AES (aes_stub.c) shares with the test */
#ifndef AES_STUB_H
#define AES_STUB_H
#include <setjmp.h>

typedef struct {
	short kind, a, b;
	const short *p;			/* S_OBJ: the object's number, in a variable of the test */
	void (*fn)(void);		/* S_CALL: a check made while the dialog is up */
} STEP;
enum { S_END, S_KEY, S_MSG, S_TAB, S_OK, S_CANCEL, S_OBJ, S_XY, S_CALL };
#define SMAXTAB 12
#define END     { S_END, 0, 0, 0, 0 }
#define KEY(k)  { S_KEY, (k), 0, 0, 0 }
#define TAB(n)  { S_TAB, (n), 0, 0, 0 }
#define OKB     { S_OK, 0, 0, 0, 0 }
#define CANCELB { S_CANCEL, 0, 0, 0, 0 }
#define OBJ(v)  { S_OBJ, 0, 0, &(v), 0 }
#define XY(x, y) { S_XY, (x), (y), 0, 0 }
#define MSG(t)  { S_MSG, (t), 0, 0, 0 }
#define CALL(f) { S_CALL, 0, 0, 0, (f) }

#define KEY_RETURN 0x1c0d
#define KEY_ESC    0x011b
#define KEY_UNDO   0x6100
#define K(c)       ((short)(0x1e00 | (c)))

extern const STEP *script;
extern jmp_buf script_out;
extern void (*refs_hook)(void);	/* called before each event: the test refreshes ref_* */
extern short ref_tab[SMAXTAB + 1], ref_ok, ref_cancel;
extern short cell_w, cell_h, ap_count, pre_key;
extern int draws, bells, updates, update_low, dial_start, dial_finish, alerts, win_made, win_open, timers, popups, pop_new;
extern short last_draw[4];
extern char win_name[40];
extern short pop_x, pop_y, pop_n, pop_answer, pop_kept;
extern char pop_lab[20][40];
#endif
