/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * TOPACC.ACC - another program's window over EWeb's, for gui_test.py's
 * check of the guidelines' 5.2 (EWeb's own 256 colours given back while
 * another program's window is on top; decided 24 Nov 2026). A test
 * accessory, not a program of the family (EWeb's tests run EWeb alone:
 * the guidelines' 1.2): when C:\TOPON.TXT appears it opens a plain white
 * window over the middle of the screen, and when C:\TOPOFF.TXT appears it
 * closes it. It says each on the emulator's console through NatFeats
 * ("TOPACC open", "TOPACC closed").
 *
 *   make build/hatari/TOPACC.ACC
 */
#include <string.h>
#include "tos.h"
#include "gem.h"

long nf_on = 1;		/* a test program: NatFeats always (nf.S's is 0) */

static short vh, win = -1;

static void say(const char *s)
{
	char line[80];
	strcpy(line, s);
	strcat(line, "\n");
	nf_debug(line);
}

static void redraw(short x, short y, short w, short h)
{
	short wx, wy, ww, wh, r[4], clip[4], p[4];
	wind_get(win, WF_WORKXYWH, &wx, &wy, &ww, &wh);
	wind_update(BEG_UPDATE);
	v_hide_c(vh);
	wind_get(win, WF_FIRSTXYWH, &r[0], &r[1], &r[2], &r[3]);
	while (r[2] > 0 && r[3] > 0) {
		short x1 = r[0] > x ? r[0] : x, y1 = r[1] > y ? r[1] : y;
		short x2 = r[0] + r[2] < x + w ? r[0] + r[2] : x + w;
		short y2 = r[1] + r[3] < y + h ? r[1] + r[3] : y + h;
		if (x2 > x1 && y2 > y1) {
			clip[0] = x1;
			clip[1] = y1;
			clip[2] = x2 - 1;
			clip[3] = y2 - 1;
			vs_clip(vh, 1, clip);
			vsf_interior(vh, FIS_SOLID);
			vsf_color(vh, 0);
			p[0] = wx;
			p[1] = wy;
			p[2] = wx + ww - 1;
			p[3] = wy + wh - 1;
			vr_recfl(vh, p);
			vs_clip(vh, 0, clip);
		}
		wind_get(win, WF_NEXTXYWH, &r[0], &r[1], &r[2], &r[3]);
	}
	v_show_c(vh, 1);
	wind_update(END_UPDATE);
}

int main(void)
{
	short msg[8], work_out[57], d, id = appl_init();
	AEVENT ev;
	menu_register(id, "  Top window (test)");
	vh = graf_handle(&d, &d, &d, &d);
	vh = v_opnvwk_(vh, work_out);
	for (;;) {
		evnt_multi_(MU_MESAG | MU_TIMER, 0, 0, 0, 200, msg, &ev);
		if ((ev.which & MU_MESAG) && msg[0] == WM_REDRAW && msg[3] == win)
			redraw(msg[4], msg[5], msg[6], msg[7]);
		if ((ev.which & MU_MESAG) && msg[0] == WM_TOPPED && msg[3] == win)
			wind_set(win, WF_TOP, 0, 0, 0, 0);
		if (win < 0 && Fattrib("C:\\TOPON.TXT", 0, 0) >= 0) {
			Fdelete("C:\\TOPON.TXT");
			win = wind_create(NAME, 0, 20, 640, 460);
			wind_set_str(win, WF_NAME, " Another program ");
			wind_open(win, 160, 120, 320, 200);
			say("TOPACC open");
		} else if (win >= 0 && Fattrib("C:\\TOPOFF.TXT", 0, 0) >= 0) {
			Fdelete("C:\\TOPOFF.TXT");
			wind_close(win);
			wind_delete(win);
			win = -1;
			say("TOPACC closed");
		}
	}
}
