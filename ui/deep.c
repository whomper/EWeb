/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * deep.c - a graphics card's screen, seen (ui/screen.c's use of it: a
 * screen deeper than the Falcon's, DESIGN.md). Linked after everything
 * else with ui/tcpix.c, so the code a Falcon runs keeps its place: the
 * canvas moved 0x1090 bytes made it 7% slower on the stock Falcon (its
 * 256-byte caches), though not one instruction of it changed.
 */
#include <string.h>

#include "ui.h"
#include "tcpix.h"

/* what the screen shows for pure red, green and blue: each drawn by the
   VDI in its top left pixel and read back (vro_cpyfm, as fVDI's notes
   ask a program to find a card's pixels: fVDI's vq_scrninfo in 24 bits
   names red's bits where blue's are, 11 Nov 2026), the pixel and the
   pen put back */
int scr_seen_form(short *info)
{
	static const short pure[3][3] = { { 1000, 0, 0 }, { 0, 1000, 0 }, { 0, 0, 1000 } };
	unsigned char keep[64], got[64], seen[3][4];
	short pxy[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }, was[3], c, ok;
	MFDB scr, mem;
	memset(&scr, 0, sizeof scr);
	memset(&mem, 0, sizeof mem);
	mem.fd_w = 16;
	mem.fd_h = 1;
	mem.fd_wdwidth = 1;
	mem.fd_nplanes = planes;
	wind_update(BEG_UPDATE);
	v_hide_c(vdi_h);
	vs_clip(vdi_h, 0, pxy);
	mem.fd_addr = keep;
	vro_cpyfm(vdi_h, 3, pxy, &scr, &mem);
	vq_color(vdi_h, 2, was);
	vswr_mode(vdi_h, 1);
	vsf_interior(vdi_h, 1);
	vsf_perimeter(vdi_h, 0);
	vsf_color(vdi_h, 2);
	mem.fd_addr = got;
	for (c = 0; c < 3; c++) {
		vs_color(vdi_h, 2, pure[c]);
		vr_recfl(vdi_h, pxy);
		vro_cpyfm(vdi_h, 3, pxy, &scr, &mem);
		memcpy(seen[c], got, 4);
	}
	vs_color(vdi_h, 2, was);
	mem.fd_addr = keep;
	vro_cpyfm(vdi_h, 3, pxy, &mem, &scr);
	v_show_c(vdi_h, 1);
	wind_update(END_UPDATE);
	ok = (short) tc_seen(info, planes, (const uint8_t (*)[4]) seen);
	log_event("screen: red, green and blue read back %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x%s",
		  seen[0][0], seen[0][1], seen[0][2], seen[0][3], seen[1][0], seen[1][1], seen[1][2], seen[1][3],
		  seen[2][0], seen[2][1], seen[2][2], seen[2][3], ok ? "" : ": no form in them");
	return ok;
}
