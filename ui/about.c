/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * about.c - EWeb's About box, the AERA programs' shared one
 * (common/ui/aboutbox.c), with the network, the DSP, the screen and the
 * memory it runs in; the keys; the iconified window.
 */
#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "aboutbox.h"
#include "iconify.h"
#include "plat.h"
#include "icon.h"
#include "icon16.h"
#include "icon256.h"

#ifndef EWEB_VERSION
#define EWEB_VERSION ""
#endif

void dlg_about(void)
{
	static char net[80], where[80];
	/* what the program takes from others: the ST edition has no TLS
	   and no scripts, and TJpgDec in place of libjpeg-turbo */
#ifdef EW_ST
	ABOUT a = { "EWeb", EWEB_VERSION, "A web browser for the Atari ST and STE", { net, where, 0 },
		    { "Fonts drawn from DejaVu. From NetSurf (MIT):",
		      "libparserutils, libnsgif. JPEG: TJpgDec",
		      "(ChaN's licence)." },
		    icon_bits, icon16_px, icon16_rgb, icon256_px, icon256_rgb };
#else
	ABOUT a = { "EWeb", EWEB_VERSION, "A web browser for the Atari Falcon", { net, where, 0 },
		    { "DejaVu fonts. MIT: BearSSL, QuickJS, musl's",
		      "maths, NetSurf's libparserutils and libnsgif.",
		      "JPEG: libjpeg-turbo (IJG's and BSD licences)." },
		    icon_bits, icon16_px, icon16_rgb, icon256_px, icon256_rgb };
#endif
	snprintf(net, sizeof net, "%s  \xF9  secure sites: %s",
		 strcmp(net_stack(), "none") ? net_stack() : "no network",
		 !opt.falcon ? (opt.gate[0] ? "the Pi gateway" : "none") :
		 strcmp(falcon_dsp_ec_state(), "DSP") ? "68030" : "DSP");
	snprintf(where, sizeof where, "%s%s%s", scr_mode(), about_ram()[0] ? "  \xF9  " : "", about_ram());
	about_show(&a);
}

void dlg_keys(void)
{
	text_box("EWeb's keys",
		 "^L       the address: type one, Return goes\n"
		 "^O       open a page on the disk\n"
		 "Undo     back (^Z); Shift-Undo forward\n"
		 "^R       reload; Esc stops a page loading\n"
		 "Up/Down  scroll; with Shift a page\n"
		 "Space    a page down; Shift-Space up\n"
		 "ClrHome  the top; Shift-ClrHome the end\n"
		 "^+ ^-    larger and smaller text; ^0 as it was\n"
		 "^C       copy the page's text (or the address)\n"
		 "Tab      between the page and the address\n"
		 "Right click: a link's or the page's commands\n"
		 "^N       a new window; ^W closes one\n"
		 "^,       the Settings\n"
		 "^Q       quit");
}

void icon_window(const GRECT *area)
{
	icon_redraw(cur->h, area, icon_bits, icon16_px, loading ? "loading" : "");
}
