/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ewrender - EWeb's page drawing on this computer: a saved page through
 * the document (src/doc), the layout (src/layout) and the canvas
 * (src/gfx) into a PNG, as the Falcon would show it, for checking the
 * layout by eye and for make test's comparisons. The page is loaded as
 * EWeb loads it (src/browse): a file, or a URL over the network.
 *
 *   ewrender [-w WIDTH] [-z ZOOM] [-X TEXT%] [-f EWEB.FNT] [-o OUT.PNG] [-d] [-t] [-v] [-c CACERT] [-y TOP] [-H HEIGHT] [-B] [-i] [-N URL] [-L WIDTH] [-e ENC]
 *            [-p COLOURS] PAGE.HTM|URL [SHEET.CSS...]
 *
 * -w the page's width in pixels (608), -a a box's scroll bars' width down
 * and height across, W or W,H (10; EWEB.PRG takes the AES's, 19 to 23), -z device pixels per CSS pixel
 * in 1024ths (819), -X the text's size in per cent (100: Larger text and
 * Smaller text, fonts only), -d the display list as text instead of a picture,
 * -t the document tree with each element's display first, -v what
 * the browser says while it loads (its status line), -c the root
 * certificates for https (binary/CACERT.PEM),
 * -y and -H the part of the page drawn (all of it); -p 2, 4, 16 or 256 draws
 * it as EWEB.PRG does on a screen of that many colours (pens, a byte a
 * pixel: monochrome's greys patterned, translucent items mixed through
 * the palette), with the ST's and Falcon's standard colours (256: the 16,
 * a 6 x 6 x 6 cube and 24 greys), rather than in true colour (5-6-5;
 * --deep: 32 bits, a byte a part, as a 24- or 32-bit screen). In -d's
 * list a colour less than solid is #rrggbbaa, a picture's and a
 * control's opacity "alpha N" (0-255; a control's colour is its
 * text's), and an item a click goes through (pointer-events: none)
 * "nohit". -N URL: then to URL,
 * Back, Forward, Back, the width 100 px less, Forward, Back, each step's
 * title and times printed (kept pages come back with no fetch or parse);
 * what is drawn or dumped is the page at the end. -B draws the page as
 * EWEB.PRG does on the screen (ui/screen.c), in bands of 32 rows at
 * every scroll position and in pieces across, at 8 and 16 bits, each into
 * a buffer of just its size: under AddressSanitizer a pixel drawn past a
 * band's edge stops it (on the Falcon it would land in ST RAM next to the
 * band, TOS's). -i draws the PNG in bands of 8 rows, each in the parts
 * cv_page_part stops at, as the kept page's are made on a stock Falcon
 * (ui/win.c): tests/layout/parts.py holds it to the page drawn whole.
 * The page's pictures are fetched and decoded before it is
 * drawn or dumped, as EWEB.PRG does after showing it (-n: none, their
 * alt text, as before pictures came). -F NAME=VALUE (again for more)
 * fills a form's field, ticks a box or radio button (NAME=VALUE too:
 * its value), chooses a list's option (by its value or text); -S
 * BUTTON then sends the form by that button ("-": Return in the first
 * field filled), and what comes is drawn or dumped. Back with -S
 * shows the form again, as it was filled. -k FILE keeps the cookies
 * that last in FILE (read when needed, written at the end), as EWEB.PRG
 * keeps COOKIES.TXT. -l FILE: the connections' log, as EWGET -l writes it.
 * -P PAGE: PAGE is loaded first and kept (history), as when the page is
 * reached from it: what one page's sheets did must not show in the next.
 * -s FILE saves the page shown at the end as it came (File > Save page
 * as), or a file EWeb doesn't show (a download); -g LINK saves that
 * link's file to FILE instead (Alternate-click), and so does a click
 * (-K) on a link to save (<a download>: "save:") or to a file EWeb
 * doesn't show. Each prints what it did and the name EWEB.PRG would
 * offer. -Z MS: a link's file is put where it goes as EWEB.PRG's file
 * selector puts it, once its answer has begun, and the selector takes
 * MS ms (the answer waits unread meanwhile). -m FILE keeps the bookmarks in
 * FILE (as BOOKMARK.HTM); -A adds the page shown at the end to them.
 * -V FILE keeps the history in FILE (as HISTORY.TXT: read when needed,
 * written at the end); the links to the pages in it are visited.
 * -G WORDS finds them on the page (Edit > Find; again for more: the next
 * after the last found, as ^G), -U WORDS the one before (Shift-^G),
 * "found:" printed with which of how many, its runs and bytes, and its
 * run's place (a box that scrolls brought to it); what is drawn has it
 * marked, as EWEB.PRG marks it.
 * -x X1,Y1,X2,Y2 selects the text from one point on the page to another
 * (a drag), -x X,Y the word there (a double click: on text only), -x all all of it
 * (Select all); "selected:" printed with its runs and bytes and its text
 * as ^C copies it (a line break as \n), what is drawn has it marked; -x
 * page prints the page's text as ^C copies it with nothing selected
 * ("copied:"). -j WIDTH (again for more): then the window made WIDTH px
 * wide, the selection placed again by its text ("selected again:").
 * -M X,Y clicks at that place on the page (a link, a control, a summary;
 * in a frame too, as EWEB.PRG's click), -M back is Back; both are taken in
 * turn with -K's, the pictures and frames fetched after each.
 * -I STAGE:N: the page's build (parse, style, layout or script: its
 * scripts compiled and run) stopped as by Esc
 * at its Nth poll in that stage (doc_poll, ec_poll: EWEB.PRG's window
 * gets its turn at each, ten times a second; here at every one), "polls:"
 * printed with how many each stage made, and "stopped:" with the page
 * that stays (-P's); -I count:0 only counts them.
 * -W HOST:PORT goes through AERA-Gateway's webgate (as Settings > Secure's
 * Pi gateway); -R then asks for pages through the Pi's browser (View >
 * Through the Pi's browser: webgate's snapshots).
 * -q SEARCH: words typed in the address go there, %s for them (as
 * EWEB.PRG's SEARCH=; none by default).
 * -O N:X:Y (again for more): the page's Nth box that scrolls (from 1) scrolled
 * to X, Y once the pictures are in (before -L), "scrolled:" printed with
 * where it went; -d lists each box's scroll bars ("bar": how far its box
 * scrolls, and is scrolled, and the bar's thumb).
 * -C DIR[,MB] keeps the disk cache in DIR, MB big (8 when not said) (as CACHE next to EWEB.PRG; its
 * index written at the end, as at quit).
 * -J runs the pages' scripts (src/js, every site's), printing what they
 * log, console.log and alert ("js ...", "console: ", "alert: ") and
 * answering OK; then -K ID clicks the element with that id (again for
 * more; "a script loaded" and the load's result when one sent the
 * browser on; a details' summary, or the details, opened or closed, in
 * the ST edition too; a link followed says where EWEB.PRG scrolls to,
 * "top", as "top:" does for an address with a #fragment, and "opened"
 * when a closed details was opened to show it), and -T MS lets the
 * page's timers run for MS ms, before the clicks and after each; -u
 * ID=TEXT, in turn with them, types TEXT into the field ID ("*": the
 * first one typed into) a key at a
 * time with its key events ("\\n" Return; the form isn't sent) (what a
 * click starts on a timer: a dialog's fade, a text that comes later).
 * -r N follows the page's refresh (<meta http-equiv=refresh>, the
 * Refresh header) when it is due, up to N times ("refresh:" where to,
 * "refreshed:" what came); -b then goes Back ("(refresh)" when the page
 * there has one waiting).
 * -S KB: the stack the cascade and the layout may take (ec_stack_floor;
 * all but a MB of the stack by default); "deep:" on stderr when a page was
 * cut there.
 * -L WIDTH (again for more): once the page is shown, the window made
 * WIDTH px wide (br_relayout, as EWEB.PRG does when it is sized: the
 * page's sheets added again when a media query of theirs goes the other
 * way), "resized:" printed; what is drawn or dumped is the page at the
 * last width. -Y WIDTH: the window sized so from inside the first
 * request the page's pictures make (as GEM's messages come in pf_idle
 * while one is under way): "sized in a request:" printed, and the
 * page's sheets come after it. -Y WIDTH@again: so inside the first
 * fetch of a picture again at the size shown (pens from the Pi).
 * -D BYTES: the memory pictures may take (img_max; EWEB.PRG's comes
 * from the machine's RAM), 8 MB by default.
 * -e NAME: once the page is shown, it is read again in that encoding
 * (View > Encoding; "auto": the page's own), "encoding:" printed with
 * what it was read in and its title.
 */
#define _DEFAULT_SOURCE		/* realpath */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>

#include "bookmark.h"
#include "history.h"
#include "browse.h"
#include "canvas.h"
#include "pagecol.h"
#include "conn.h"
#include "cookie.h"
#include "dcache.h"
#include "font.h"
#include "js.h"
#include "form.h"
#include "http.h"
#include "img.h"
#include "plat.h"
#include "ui.h"
#include "util.h"

EWOPT opt = { .falcon = 1, .dsp = 1 };

static char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *buf;
	long n;
	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(n + 1);
	if (buf == NULL || fread(buf, 1, n, f) != (size_t) n) {
		fclose(f);
		free(buf);
		return NULL;
	}
	fclose(f);
	buf[n] = 0;
	*len = (size_t) n;
	return buf;
}

/* ---------------------------------------------------------------- PNG, stored */

static unsigned long crc_table[256];

static unsigned long crc(unsigned long c, const unsigned char *p, size_t n)
{
	size_t i;
	if (crc_table[1] == 0) {
		unsigned long k, v;
		for (k = 0; k < 256; k++) {
			v = k;
			for (i = 0; i < 8; i++)
				v = v & 1 ? 0xedb88320UL ^ (v >> 1) : v >> 1;
			crc_table[k] = v;
		}
	}
	c ^= 0xffffffffUL;
	for (i = 0; i < n; i++)
		c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
	return c ^ 0xffffffffUL;
}

static void be32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char) (v >> 24);
	p[1] = (unsigned char) (v >> 16);
	p[2] = (unsigned char) (v >> 8);
	p[3] = (unsigned char) v;
}

static void chunk(FILE *f, const char *type, const unsigned char *data, size_t n)
{
	unsigned char h[8];
	unsigned long c;
	be32(h, (unsigned long) n);
	memcpy(h + 4, type, 4);
	fwrite(h, 1, 8, f);
	fwrite(data, 1, n, f);
	c = crc(0, h + 4, 4);
	c = crc(c, data, n);
	be32(h, c);
	fwrite(h, 1, 4, f);
}

static int write_png(const char *path, const canvas *c)
{
	FILE *f = fopen(path, "wb");
	size_t row = 1 + 3 * (size_t) c->w, raw = row * c->h, i, z = 0, nblocks = (raw + 65534) / 65535;
	unsigned char *rawbuf = malloc(raw), *zbuf = malloc(raw + 5 * nblocks + 6), head[13];
	unsigned long a = 1, b = 0;
	int32_t x, y;

	if (f == NULL || rawbuf == NULL || zbuf == NULL)
		return 0;
	for (y = 0; y < c->h; y++) {
		unsigned char *r = rawbuf + y * row;
		const unsigned char *p = c->pix + y * c->stride;
		r[0] = 0;
		for (x = 0; c->bpp == 8 && x < c->w; x++)
			memcpy(r + 1 + 3 * x, c->pal[p[x]], 3);
		for (x = 0; c->bpp == 16 && x < c->w; x++) {
			unsigned v = (unsigned) p[2 * x] << 8 | p[2 * x + 1];
			r[1 + 3 * x] = (unsigned char) ((v >> 8 & 0xf8) | (v >> 13));
			r[2 + 3 * x] = (unsigned char) ((v >> 3 & 0xfc) | (v >> 9 & 3));
			r[3 + 3 * x] = (unsigned char) ((v << 3 & 0xf8) | (v >> 2 & 7));
		}
		for (x = 0; c->bpp == 32 && x < c->w; x++)
			memcpy(r + 1 + 3 * x, p + 4 * x + 1, 3);
	}
	zbuf[z++] = 0x78;
	zbuf[z++] = 0x01;
	for (i = 0; i < raw; i += 65535) {
		size_t n = raw - i < 65535 ? raw - i : 65535;
		zbuf[z++] = i + n >= raw;
		zbuf[z++] = (unsigned char) n;
		zbuf[z++] = (unsigned char) (n >> 8);
		zbuf[z++] = (unsigned char) ~n;
		zbuf[z++] = (unsigned char) (~n >> 8);
		memcpy(zbuf + z, rawbuf + i, n);
		z += n;
	}
	for (i = 0; i < raw; i++) {
		a = (a + rawbuf[i]) % 65521;
		b = (b + a) % 65521;
	}
	be32(zbuf + z, b << 16 | a);
	z += 4;
	fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
	be32(head, (unsigned long) c->w);
	be32(head + 4, (unsigned long) c->h);
	head[8] = 8;
	head[9] = 2;
	head[10] = head[11] = head[12] = 0;
	chunk(f, "IHDR", head, 13);
	chunk(f, "IDAT", zbuf, z);
	chunk(f, "IEND", head, 0);
	fclose(f);
	free(rawbuf);
	free(zbuf);
	return 1;
}

/* a picture's opacity in -d's list: nothing when it is opaque */
static const char *alpha_of(const dl_item *it)
{
	static char b[16];
	if (it->rgb >> 24 == 255)
		return "";
	snprintf(b, sizeof b, " alpha %u", (unsigned) (it->rgb >> 24));
	return b;
}

/* a gradient as the display list has it (device px, rounded): its kind,
   then linear: its angle or to its side or corner; radial: circle or
   ellipse, its extent or radii, at its centre; conic: from its angle at
   its centre; then its stops, a colour less than solid as #rrggbbaa,
   each position px+pct% (a conic one's pct of the turn), and its hints */
static const char *grad_of(const ec_grad *g)
{
	static char b[1024];
	static const char *const kind[] = { "linear", "radial", "conic" };
	static const char *const ext[] = { "", " closest-side", " closest-corner", " farthest-side", " farthest-corner" };
	size_t n;
	int i;
	n = (size_t) snprintf(b, sizeof b, "%s%s", g->repeat ? "repeating-" : "", kind[g->type]);
	if (g->approx)
		n += snprintf(b + n, sizeof b - n, " averaged");	/* (its stops can't be placed) */
	else if (g->type == EC_GRAD_LINEAR) {
		if (g->to)
			n += snprintf(b + n, sizeof b - n, " to%s%s%s%s", g->to & EC_TO_TOP ? " top" : "",
				      g->to & EC_TO_BOTTOM ? " bottom" : "", g->to & EC_TO_LEFT ? " left" : "",
				      g->to & EC_TO_RIGHT ? " right" : "");
		else
			n += snprintf(b + n, sizeof b - n, " %ddeg", (int) (g->angle >> 10));
	} else {
		if (g->type == EC_GRAD_RADIAL) {
			n += snprintf(b + n, sizeof b - n, " %s", g->circle ? "circle" : "ellipse");
			if (g->extent)
				n += snprintf(b + n, sizeof b - n, "%s", ext[g->extent]);
			else
				n += snprintf(b + n, sizeof b - n, " %d+%d%% %d+%d%%", (int) (g->rx.px >> 10),
					      (int) (g->rx.pct >> 10), (int) (g->ry.px >> 10), (int) (g->ry.pct >> 10));
		} else {
			n += snprintf(b + n, sizeof b - n, " from %ddeg", (int) (g->angle >> 10));
		}
		n += snprintf(b + n, sizeof b - n, " at %d+%d%% %d+%d%%", (int) (g->x.px >> 10), (int) (g->x.pct >> 10),
			      (int) (g->y.px >> 10), (int) (g->y.pct >> 10));
	}
	for (i = 0; i < g->n && n < sizeof b - 40; i++) {
		const ec_gstop *s = &g->s[i];
		if (s->flags & EC_GS_HINT)
			n += snprintf(b + n, sizeof b - n, ", hint");
		else if (s->argb >> 24 == 255)
			n += snprintf(b + n, sizeof b - n, ", #%06lx", (unsigned long) (s->argb & 0xffffff));
		else
			n += snprintf(b + n, sizeof b - n, ", #%06lx%02lx", (unsigned long) (s->argb & 0xffffff),
				      (unsigned long) (s->argb >> 24));
		if (s->flags & EC_GS_AT)
			n += snprintf(b + n, sizeof b - n, " %d+%d%%", (int) (s->at.px >> 10), (int) (s->at.pct >> 10));
	}
	return b;
}

/* ---------------------------------------------------------------- the tree, with each element's display */

static void tree(const dnode *n, int depth)
{
	const dnode *c;
	for (c = n->first; c != NULL; c = c->next) {
		if (c->type == DN_ELEMENT) {
			size_t len;
			const char *name = ec_atom_text(c->e.name, &len);
			const ec_style *st = c->e.style;
			printf("%*s<%.*s> %s\n", 2 * depth, "", (int) len, name,
			       st == NULL ? "(no style)" : ec_kw_name(st->v[EP_DISPLAY].i));
			tree(c, depth + 1);
		} else if (c->type == DN_TEXT) {
			printf("%*s\"%.*s\"\n", 2 * depth, "", (int) (c->len > 40 ? 40 : c->len), c->text);
		}
	}
}

/* ---------------------------------------------------------------- the page */

static uint8_t pen8(void *ud, uint32_t rgb)
{
	(void) ud;
	return (uint8_t) ((rgb >> 16 ^ rgb >> 8 ^ rgb) & 0xff);
}

static int shade8(void *ud, uint32_t rgb, uint8_t *dark, uint8_t *light)
{
	(void) ud;
	*dark = 1;
	*light = 0;
	return (int) (rgb & 15);
}

static int picture(void *ud, void *img, int32_t w, int32_t h, const PIC **pic, const uint8_t **pens)
{
	(void) ud;
	return img_picture(img, w, h, pic, pens);
}

/* the Falcon's 16 standard colours, for the pictures' pens at 8 bits */
static const unsigned char falcon16[16][3] = {
	{ 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 }, { 255, 255, 0 }, { 0, 0, 255 }, { 255, 0, 255 },
	{ 0, 255, 255 }, { 170, 170, 170 }, { 85, 85, 85 }, { 170, 0, 0 }, { 0, 170, 0 }, { 170, 170, 0 },
	{ 0, 0, 170 }, { 170, 0, 170 }, { 0, 170, 170 }, { 0, 0, 0 }
};

/* -p: the screen's colours, and its pens as ui/screen.c finds them */
static unsigned char scr_pal[256][3];
static int scr_n;

static void make_pal(int n)
{
	static const unsigned char mono[2][3] = { { 255, 255, 255 }, { 0, 0, 0 } };
	static const unsigned char med[4][3] = { { 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 0 } };
	int k;
	scr_n = n;
	if (n == 2)
		memcpy(scr_pal, mono, sizeof mono);
	else if (n == 4)
		memcpy(scr_pal, med, sizeof med);
	else
		memcpy(scr_pal, falcon16, sizeof falcon16);
	if (n == 256) {
		for (k = 0; k < 216; k++) {
			scr_pal[16 + k][0] = (unsigned char) (k / 36 * 51);
			scr_pal[16 + k][1] = (unsigned char) (k / 6 % 6 * 51);
			scr_pal[16 + k][2] = (unsigned char) (k % 6 * 51);
		}
		for (k = 232; k < 256; k++)
			scr_pal[k][0] = scr_pal[k][1] = scr_pal[k][2] = (unsigned char) ((k - 232) * 11);
	}
}

static uint8_t pen_n(void *ud, uint32_t rgb)
{
	int r = (int) (rgb >> 16 & 255), g = (int) (rgb >> 8 & 255), b = (int) (rgb & 255), i, pick = 0;
	long best = 0x7fffffffL;
	(void) ud;
	if (scr_n == 2)
		return 299L * r + 587L * g + 114L * b >= 127500L ? 0 : 1;
	for (i = 0; i < scr_n; i++) {
		long dr = r - scr_pal[i][0], dg = g - scr_pal[i][1], db = b - scr_pal[i][2];
		long d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
		if (d < best) {
			best = d;
			pick = i;
		}
	}
	return (uint8_t) pick;
}

static int shade_n(void *ud, uint32_t rgb, uint8_t *dark, uint8_t *light)
{
	long y = 299L * (rgb >> 16 & 255) + 587L * (rgb >> 8 & 255) + 114L * (rgb & 255);
	(void) ud;
	if (scr_n != 2)
		return -1;
	*light = 0;
	*dark = 1;
	return (int) ((y * 16 + 127500L) / 255000L);
}

/* -B: the page as ui/screen.c draws it, a band of 32 rows at a time, at
   every scroll position (each row the top of a band) and in pieces across
   (an area of the window uncovered), each into a buffer of just its size */
static long bands(const page *p, int32_t width)
{
	static const int32_t across[][2] = { { 0, 0 }, { 0, 100 }, { 37, 211 }, { 300, 0 }, { 599, 0 } };
	int32_t y, k, bpp;
	long n = 0;
	for (bpp = 8; bpp <= 32; bpp *= 2) {
		for (k = 0; k < (int32_t) (sizeof across / sizeof across[0]); k++) {
			int32_t x = across[k][0], w = across[k][1] ? across[k][1] : width - across[k][0];
			if (w <= 0 || x >= width)
				continue;
			for (y = -40; y < p->height + 40; y += k ? 13 : 1) {
				canvas c;
				memset(&c, 0, sizeof c);
				c.w = w;
				c.h = 32;
				c.bpp = (int) bpp;
				c.stride = (long) w * (bpp / 8);
				c.ox = x;
				c.oy = y;
				c.pen = pen8;
				c.shade = k & 1 ? shade8 : NULL;
				c.pal = (const uint8_t (*)[3]) scr_pal;
				c.picture = picture;
				c.pix = malloc(c.stride * c.h);
				if (c.pix == NULL)
					return -1;
				cv_page(&c, p);
				free(c.pix);
				n++;
			}
		}
	}
	return n;
}

/* -1 (with -p 2): the page as the ST edition draws its monochrome screen,
   each band of 16 rows at many scroll places and parts across, both a
   byte a pixel (its pens' low bit, as ui/screen.c's to_planes takes it)
   and, where cv_page_planes says it can, straight into one plane (bpp
   1): the same bits, or the band's place said. *planar: how many bands
   were drawn in one plane; the bands that differ */
static long planes_check(const page *p, int32_t width, long *planar)
{
	static const int32_t across[][2] = { { 0, 0 }, { 3, 200 }, { 37, 211 }, { 300, 0 }, { 599, 0 } };
	int32_t y, k;
	long bad = 0, n = 0;
	*planar = 0;
	for (k = 0; k < (int32_t) (sizeof across / sizeof across[0]); k++) {
		int32_t x = across[k][0], w = across[k][1] ? across[k][1] : width - across[k][0];
		long words = (w + 15) / 16;
		if (w <= 0 || x >= width)
			continue;
		for (y = -8; y < p->height + 8; y += k ? 23 : 7) {
			canvas a, b;
			int32_t i, j;
			memset(&a, 0, sizeof a);
			a.w = w;
			a.h = 16;
			a.bpp = 8;
			a.stride = w;
			a.ox = x;
			a.oy = y;
			a.pen = pen_n;
			a.shade = shade_n;
			a.pal = (const uint8_t (*)[3]) scr_pal;
			a.picture = picture;
			b = a;
			b.bpp = 1;
			b.stride = words * 2;
			n++;
			if (!cv_page_planes(&b, p))
				continue;
			(*planar)++;
			a.pix = malloc((size_t) (a.stride * a.h));
			b.pix = calloc(1, (size_t) (b.stride * b.h));
			if (a.pix == NULL || b.pix == NULL)
				return -1;
			cv_page(&a, p);
			cv_page(&b, p);
			for (j = 0; j < a.h; j++) {
				for (i = 0; i < w; i++)
					if ((a.pix[j * a.stride + i] & 1) != (b.pix[j * b.stride + i / 8] >> (7 - i % 8) & 1))
						break;
				if (i < w) {
					if (bad < 10)
						printf("planes: band at x %d y %d (width %d): row %d differs from column %d\n",
						       (int) x, (int) y, (int) w, (int) j, (int) i);
					bad++;
					break;
				}
			}
			free(a.pix);
			free(b.pix);
		}
	}
	printf("planes: %ld bands, %ld in one plane, %ld differ\n", n, *planar, bad);
	return bad;
}

/* -k's: every part as short as cv_page_part allows */
static int stop_now(void *ud)
{
	(void) ud;
	return 1;
}

static void said(void *ud, const char *what)
{
	if (*(int *) ud)
		fprintf(stderr, "%s\n", what);
}

static int scripts_on;

static void logged(void *ud, const char *line)
{
	if (scripts_on && strncmp(line, "js ", 3) == 0)
		printf("%s\n", line);
	else
		said(ud, line);
}

static void console_line(void *ud, const char *line)
{
	(void) ud;
	printf("console: %s\n", line);
}

static int asked(void *ud, int kind, const char *text, char *out, int size)
{
	(void) ud;
	printf("%s: %s\n", kind == JS_ALERT ? "alert" : kind == JS_CONFIRM ? "confirm" : "prompt", text);
	(void) out, (void) size;
	return 1;
}

static dnode *by_id(dnode *n, const char *id)
{
	dnode *c, *f;
	const char *v;
	if (n->type == DN_ELEMENT && (v = dn_attr(n, "id")) != NULL && strcmp(v, id) == 0)
		return n;
	for (c = n->first; c != NULL; c = c->next)
		if ((f = by_id(c, id)) != NULL)
			return f;
	return NULL;
}

/* -s FILE, and whether a click saved to it already (the page isn't then) */
static const char *save_to;
static int click_saved;
/* -Z: how long the file selector takes, ms (-1: none is shown) */
static long selector_ms = -1;
static int stream_files;

/* browser.save_as with -Z: the file selector, MS ms, choosing -s's file */
static int selector(void *ud, const char *name, char *path, int size)
{
	(void) ud;
	printf("selector: %s\n", name);
	fflush(stdout);
	usleep((useconds_t) selector_ms * 1000);
	str_copy(path, save_to, size);
	return 1;
}

/* url's file saved to save_to as it comes (br_save_url), given: its
   link's download attribute or NULL; what it did, and the name EWEB.PRG
   would offer */
static int save_url(browser *b, const char *url, const char *given)
{
	char path[1024], name[16], err[200];
	int r;
	str_copy(path, selector_ms >= 0 ? "" : save_to, sizeof path);
	if (selector_ms >= 0)
		b->save_as = selector;
	r = br_save_url(b, url, given, path, sizeof path, err, sizeof err);
	br_file_name(b->dl.url, b->dl.name, b->dl.mime, name, sizeof name);
	printf("saved link: %d %ld %s %s\n", r, br_saved, name, err);
	printf("save size: %ld\n", br_save_total);
	return r;
}

/* a file a click came to (r: BR_DOWNLOAD, what came; BR_SAVE, a link to
   save as it comes), saved to -s's file as EWEB.PRG saves it */
static void click_file(browser *b, int r)
{
	if (r == BR_DOWNLOAD) {
		char name[16];
		br_file_name(b->dl.url, b->dl.name, b->dl.mime, name, sizeof name);
		printf("download: %s %ld %s\n", b->dl.mime, b->dl.len, name);
		if (save_to != NULL) {
			br_dl_save(b, save_to);
			click_saved = 1;
		}
		br_dl_free(b);
	} else if (r == BR_SAVE) {
		/* a link to save (<a download>): as EWEB.PRG saves it */
		char url[1024], given[256];
		str_copy(url, b->dl.url, sizeof url);
		str_copy(given, b->dl.name, sizeof given);
		printf("save: %s \"%s\"\n", url, given);
		if (save_to != NULL) {
			save_url(b, url, given);
			click_saved = 1;
		}
	} else if (r == BR_WINDOW) {
		/* for another window (_blank, a name): shown here, in its
		   stead, as EWEB.PRG shows it in that window */
		char url[1024], name[64];
		str_copy(url, b->elsewhere, sizeof url);
		str_copy(name, b->window, sizeof name);
		r = br_go(b, url);
		printf("window \"%s\": %s, %d \"%s\"\n", name, url, r, b->title);
	}
}

/* where a details' summary is on the page (its DL_SUM item): n the
   summary, or the details (a click on a details is one on its summary,
   one of the layout's own too) */
static int summary_rect(const page *p, const dnode *n, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
	long k;
	for (k = 0; k < p->nitems; k++) {
		const dl_item *it = &p->items[k];
		if (it->type == DL_SUM && (it->det == n || (DN_IS(n, T_SUMMARY) && it->det == n->parent))) {
			*x = it->x;
			*y = it->y;
			*w = it->w;
			*h = it->h;
			return 1;
		}
	}
	return 0;
}

/* a click on the element with that id, as EWEB.PRG does one: its click
   event, then (unless a script said no) its link, control or details'
   summary; then the page's tasks due at once (a toggle event), as
   EWEB.PRG's loop runs them. The ST edition (no scripts, no element's
   place kept) clicks only a summary */
/* -T MS: the page's timers for MS ms (none when 0) */
static void run_timers(browser *b, int ms)
{
	unsigned long t0 = pf_ms();
	long due;
	while (ms > 0 && (due = br_js_due(b)) >= 0 && (long) (pf_ms() - t0) + due <= ms) {
		int load = 0, k;
		if (due > 0)
			usleep((useconds_t) due * 1000);
		k = br_js_step(b, &load);
		if (k == BRJ_LOAD) {
			printf("load: %d \"%s\" %s\n", load, b->title, b->url);
			click_file(b, load);
		}
	}
}

#ifndef EW_ST
/* -u ID=TEXT: TEXT typed into the field ID (or @NAME: the field of that
   name; *: the first one types into) as EWEB.PRG types it, a key
   at a time (keydown, a character's keypress, the character put in and
   input, keyup); "\n" is Return, whose keypress the page's scripts see
   (the form isn't sent: no site's form goes anywhere from the tests) */
static void typed(browser *b, const char *spec)
{
	char id[128];
	const char *eq = strchr(spec, '='), *t;
	dnode *n;
	fctl *c = NULL;
	int cancelled, load, r = BRJ_NONE, keys = 0, stopped = 0;
	if (eq == NULL || (size_t) (eq - spec) >= sizeof id) {
		printf("type %s: ID=TEXT\n", spec);
		return;
	}
	memcpy(id, spec, (size_t) (eq - spec));
	id[eq - spec] = 0;
	n = b->d ? by_id(b->d->root, id) : NULL;
	if (n != NULL)
		for (c = b->d->ctls; c != NULL && c->n != n; c = c->next)
			;
	else if (b->d != NULL && strcmp(id, "*") == 0)	/* the first field one types into */
		for (c = b->d->ctls; c != NULL && !form_typed(c); c = c->next)
			;
	else if (b->d != NULL && id[0] == '@')	/* @NAME: the field of that name */
		for (c = b->d->ctls; c != NULL; c = c->next) {
			const char *nm = c->n ? dn_attr(c->n, "name") : NULL;
			if (nm != NULL && strcmp(nm, id + 1) == 0 && (form_typed(c) || c->kind == FC_FILE))
				break;
		}
	if (c == NULL) {
		printf("type %s: no such field\n", id);
		return;
	}
	if (c->kind == FC_FILE) {
		/* a file control: TEXT the file chosen, as the file selector
		   gives it (win.c's choose_file), then input and change */
		form_set_file(c, eq + 1);
		r = br_js_event(b, c, "input", &cancelled, &load);
		if (r != BRJ_LOAD)
			r = br_js_event(b, c, "change", &cancelled, &load);
		printf("file %s: \"%s\" chosen\n", id, form_file_name(c));
		if (r == BRJ_LOAD) {
			printf("load: %d \"%s\" %s\n", load, b->title, b->url);
			click_file(b, load);
		}
		return;
	}
	for (t = eq + 1; *t; t++) {
		char ch[2] = { *t, 0 };
		int nl = t[0] == '\\' && t[1] == 'n';
		const char *name = nl ? "Enter" : ch;
		long kc = nl ? 13 : (*t >= 'a' && *t <= 'z' ? *t - 32 : (unsigned char) *t), cc = nl ? 13 : (unsigned char) *t;
		if (nl)
			t++;
		keys++;
		r = br_js_key(b, c, "keydown", name, kc, 0, &cancelled, &load);
		if (!cancelled)
			r = br_js_key(b, c, "keypress", name, cc, cc, &cancelled, &load);
		if (cancelled)
			stopped++;
		else if (!nl && form_insert(c, ch, 1))
			r = br_js_event(b, c, "input", &cancelled, &load);
		r = br_js_key(b, c, "keyup", name, kc, 0, &cancelled, &load);
		if (r == BRJ_LOAD) {
			printf("load: %d \"%s\" %s\n", load, b->title, b->url);
			click_file(b, load);
			return;
		}
	}
	printf("typed %s: %d keys%s, the field \"%.*s\"\n", id, keys, stopped ? " (some cancelled)" : "",
	       (int) c->len, c->v ? c->v : "");
}
#endif

#ifndef EW_ST
/* what is under (x, y) before the page's scripts run: its control, and
   its link's address (as win.c's click: what they move there takes no
   click) */
static char *under(browser *b, int32_t x, int32_t y, fctl **c, int *l)
{
	*c = b->p ? page_ctl_at(b->p, x, y, NULL) : NULL;
	*l = b->p ? page_link_at(b->p, x, y) : -1;
	return *l >= 0 && b->p->links[*l] != NULL ? strdup(b->p->links[*l]) : NULL;
}
#endif

static int click(browser *b, const char *id)
{
	dnode *n, *det;
	int32_t x, y, w, h;
	int cancelled = 0, load = 0, r = BRJ_NONE, k, px, py;
	char *h0 = NULL;
#ifndef EW_ST
	int l0 = -1;
	fctl *c0 = NULL;
#endif
#ifndef EW_ST
	if (strncmp(id, "type:", 5) == 0) {
		typed(b, id + 5);
		return 1;
	}
#endif
#ifndef EW_ST
	{
		/* the mouse beyond a click (as win.c gives it): -K hover:ID
		   (or hover:X,Y) the mouse come over it, dbl:ID a double click,
		   down:ID and up:ID a press and its release (a click held),
		   drag:ID:DX,DY a press, the mouse moved by DX, DY in four
		   steps and let go; tap:ID a click as the page's :hover */
		static const char *const verbs[] = { "hover:", "dbl:", "down:", "up:", "drag:", "tap:" };
		int v;
		for (v = 0; v < 6 && strncmp(id, verbs[v], strlen(verbs[v])) != 0; v++)
			;
		if (v < 6) {
			char name[128], *colon;
			int32_t dx = 0, dy = 0;
			const char *arg = id + strlen(verbs[v]);
			str_copy(name, arg, sizeof name);
			char to[128] = "";
			if (v == 4 && (colon = strchr(name, ':')) != NULL) {
				long a, c;
				if (colon[1] == '@')
					str_copy(to, colon + 2, sizeof to);	/* drag:ID:@TO, to TO's middle */
				else if (sscanf(colon + 1, "%ld,%ld", &a, &c) == 2) {
					dx = (int32_t) a;
					dy = (int32_t) c;
				}
				*colon = 0;
			}
			if (sscanf(name, "%d,%d", &px, &py) == 2) {
				x = px;
				y = py;
			} else {
				n = b->d ? by_id(b->d->root, name) : NULL;
				if (n == NULL || !page_node_rect(b->p, n, &x, &y, &w, &h)) {
					printf("%s%s: not shown\n", verbs[v], name);
					return 0;
				}
				x += w / 2;
				y += h / 2;
			}
			if (*to) {
				int32_t tx, ty, tw, th;
				n = b->d ? by_id(b->d->root, to) : NULL;
				if (n == NULL || !page_node_rect(b->p, n, &tx, &ty, &tw, &th)) {
					printf("drag to %s: not shown\n", to);
					return 0;
				}
				dx = tx + tw / 2 - x;
				dy = ty + th / 2 - y;
			}
			if (v == 5) {
				/* tap:ID, EWeb's hover by a click (win.c's, before the
				   click itself): :hover's element */
				k = br_hover_click(b, x, y);
				printf("tap %s: %s, height %d\n", name, k ? "laid out again" : "nothing", b->p ? (int) b->p->height : 0);
				goto due;
			}
			if (v == 0) {
				r = br_js_hover(b, x, y, &load);
				printf("hover %s: %s\n", name, r == BRJ_DRAWN ? "laid out again" : r == BRJ_LOAD ? "a script loaded" : "nothing");
			} else if (v == 1) {
				br_js_click(b, x, y, 1, 1, &cancelled, &load);
				r = br_js_click(b, x, y, 2, 1, &cancelled, &load);
				printf("double click %s: %s%s\n", name, r == BRJ_DRAWN ? "laid out again" : r == BRJ_LOAD ? "a script loaded" : "nothing",
				       cancelled ? ", cancelled" : "");
			} else if (v == 2 || v == 3) {
				r = br_js_mouse(b, v == 2 ? "mousedown" : "mouseup", x, y, 1, &cancelled, &load);
				printf("%s %s: %s%s\n", v == 2 ? "press" : "release", name,
				       r == BRJ_DRAWN ? "laid out again" : r == BRJ_LOAD ? "a script loaded" : "nothing", cancelled ? ", cancelled" : "");
			} else {
				int s;
				r = br_js_mouse(b, "mousedown", x, y, 1, &cancelled, &load);
				if (!cancelled && r != BRJ_LOAD && br_js_draggable(b, x, y)) {
					/* HTML's drag and drop, as win.c's html_drag */
					int over = 0, drawn;
					r = br_js_mouse(b, "dragstart", x, y, 1, &cancelled, &load);
					drawn = r == BRJ_DRAWN;
					if (cancelled) {
						printf("drag %s: its dragstart cancelled\n", name);
						goto due;
					}
					for (s = 1; s <= 4 && r != BRJ_LOAD; s++) {
						r = br_js_mouse(b, "dragover", x + dx * s / 4, y + dy * s / 4, 1, &cancelled, &load);
						over = cancelled;
						drawn |= r == BRJ_DRAWN;
					}
					if (r != BRJ_LOAD && over)
						drawn |= (r = br_js_mouse(b, "drop", x + dx, y + dy, 1, &cancelled, &load)) == BRJ_DRAWN;
					if (r != BRJ_LOAD)
						drawn |= (r = br_js_mouse(b, "dragend", x + dx, y + dy, 1, &cancelled, &load)) == BRJ_DRAWN;
					printf("HTML drag %s by %ld,%ld: %s%s\n", name, (long) dx, (long) dy, over ? "dropped, " : "not taken, ",
					       r == BRJ_LOAD ? "a script loaded" : drawn ? "laid out again" : "nothing");
					if (r == BRJ_LOAD)
						click_file(b, load);
					goto due;
				}
				if (!cancelled) {
					printf("drag %s: its press not taken by a script (a selection)\n", name);
					return 1;
				}
				for (s = 1; s <= 4 && r != BRJ_LOAD; s++)
					r = br_js_mouse(b, "mousemove", x + dx * s / 4, y + dy * s / 4, 1, &cancelled, &load);
				if (r != BRJ_LOAD)
					r = br_js_mouse(b, "mouseup", x + dx, y + dy, 1, &cancelled, &load);
				printf("drag %s by %ld,%ld: %s\n", name, (long) dx, (long) dy,
				       r == BRJ_DRAWN ? "laid out again" : r == BRJ_LOAD ? "a script loaded" : "nothing");
			}
			if (r == BRJ_LOAD)
				click_file(b, load);
			goto due;
		}
	}
#endif
	if (strcmp(id, "back") == 0) {
		/* -M back: the window's Back */
		r = br_back(b);
		printf("back: %d \"%s\" %s\n", r, b->title, b->url);
		return 1;
	}
	if (sscanf(id, "%d,%d", &px, &py) == 2) {
		/* -M X,Y: a click at a place on the page (in a frame too) */
		x = px;
		y = py;
#ifndef EW_ST
		/* (the page's scripts told first, as a click by id is) */
		h0 = under(b, x, y, &c0, &l0);
		r = br_js_click(b, x, y, 1, 1, &cancelled, &load);
		printf("click %d,%d: %s%s\n", px, py, r == BRJ_DRAWN ? "laid out again" : r == BRJ_LOAD ? "a script loaded" : "nothing",
		       cancelled ? ", cancelled" : "");
#endif
		goto at;
	}
	n = b->d ? by_id(b->d->root, id) : NULL;
#ifdef EW_ST
	if (n == NULL || !summary_rect(b->p, n, &x, &y, &w, &h)) {
		printf("click %s: not shown\n", id);
		return 0;
	}
	x += w / 2;
	y += h / 2;
#else
	/* a summary where its DL_SUM is, as in the ST edition */
	k = n != NULL && (DN_IS(n, T_DETAILS) || DN_IS(n, T_SUMMARY)) && summary_rect(b->p, n, &x, &y, &w, &h);
	if (!k && n != NULL && !DN_IS(n, T_DETAILS))
		k = page_node_rect(b->p, n, &x, &y, &w, &h);
	if (!k) {
		printf("click %s: not shown\n", id);
		return 0;
	}
	x += w / 2;
	y += h / 2;
	h0 = under(b, x, y, &c0, &l0);
	r = br_js_click(b, x, y, 1, 1, &cancelled, &load);
	printf("click %s: %s%s\n", id, r == BRJ_DRAWN ? "laid out again" : r == BRJ_LOAD ? "a script loaded" : "nothing",
	       cancelled ? ", cancelled" : "");
#endif
at:
	if (r == BRJ_LOAD) {
		printf("load: %d \"%s\" %s\n", load, b->title, b->url);
		click_file(b, load);
	} else if (!cancelled) {
		int link = page_link_at(b->p, x, y);
		fctl *c = page_ctl_at(b->p, x, y, NULL);
#ifndef EW_ST
		if (c != c0 || (link >= 0) != (l0 >= 0) ||
		    (link >= 0 && (h0 == NULL || b->p->links[link] == NULL || strcmp(h0, b->p->links[link]) != 0))) {
			printf("moved under the mouse by the scripts: no click\n");
			free(h0);
			h0 = NULL;
			goto due;
		}
#endif
		if (c != NULL && (c->kind == FC_SUBMIT || c->kind == FC_IMAGE)) {
			r = br_submit(b, NULL, c, 0, 0);
			printf("sent: %d \"%s\" %s\n", r, b->title, b->url);
			click_file(b, r);
		} else if (c != NULL && (c->kind == FC_CHECKBOX || c->kind == FC_RADIO)) {
			form_check(br_ctl_doc(b, c), c);
			br_js_event(b, c, "change", &cancelled, &load);
		} else if (c != NULL && c->kind == FC_RESET) {
			/* (as win.c's click on one) */
			form_reset(br_ctl_doc(b, c), c->form);
			printf("reset\n");
		} else if (link >= 0) {
			r = br_follow(b, link);
			printf("followed: %d \"%s\" %s top %d%s\n", r, b->title, b->url, (int) b->scroll_to,
			       r == BR_SAME && b->relaid ? " opened" : "");
			click_file(b, r);
		} else if (c == NULL && (det = page_summary_at(b->p, x, y)) != NULL) {
			r = br_details(b, det);
			printf("details %s: %s, height %d\n", dn_attr(det, "id") ? dn_attr(det, "id") : "-",
			       !r ? "failed" : dn_attr(det, "open") ? "opened" : "closed", b->p ? (int) b->p->height : 0);
		}
	}
#ifndef EW_ST
due:
#endif
	free(h0);
	for (k = 0; k < 64 && br_js_due(b) == 0; k++) {
		if (br_js_step(b, &load) == BRJ_LOAD) {
			printf("load: %d \"%s\" %s\n", load, b->title, b->url);
			break;
		}
	}
	return 1;
}

/* -I: a page's build polled (doc_poll, ec_poll: the window's turn, in
   pf_idle, at every poll), and stopped as by Esc at the Nth while it is at
   one stage */
static int z_stage = -1, z_at;
static long z_polls[5];

static void build_idle(void)
{
	if (br_building > 0 && br_building < 5 && ++z_polls[br_building] == z_at && br_building == z_stage)
		conn_stop = 1;
}

/* -Y: the window sized inside a request (pf_idle), once */
static browser *in_req_b;
static int32_t in_req_w;
static int in_req_again;	/* (W@again: in a picture's fetch at its size) */

static void in_request(void)
{
	if (in_req_w > 0 && (!in_req_again || br_pic_again)) {
		int32_t w = in_req_w;
		in_req_w = 0;
		printf("sized in a request: %d\n", br_relayout(in_req_b, w));
	}
}

/* every picture of the page shown, fetched and decoded */
/* -x: a stretch's text (the page's: r NULL) as ^C copies it, a line
   break as \n */
static void selected(const page *p, const pg_range *r, const char *what)
{
	long n, k;
	char *t = page_copy_text(p, r, &n);
	printf("%s: ", what);
	if (r != NULL)
		printf("item %ld %u to item %ld %u ", r->a.item, r->a.off, r->b.item, r->b.off);
	putchar('"');
	for (k = 0; t != NULL && k < n; k++) {
		if (t[k] == '\r')
			continue;
		if (t[k] == '\n')
			fputs("\\n", stdout);
		else if (t[k] == '"' || t[k] == '\\')
			printf("\\%c", t[k]);
		else
			putchar(t[k]);
	}
	printf("\"\n");
	free(t);
}

static void pictures(browser *b)
{
	int32_t y0, y1;
	while (br_pictures_pending(b)) {
		int r = br_pictures_step(b, 0, 400, &y0, &y1);
		if (r == BRP_NONE || r == BRP_STOPPED)
			break;
	}
}

int main(int argc, char **argv)
{
	const char *out = "out.png", *fnt = "build/EWEB.FNT", *page_arg, *cacert = "binary/CACERT.PEM";
	int32_t width = 608, zoom = 819, top = 0, height = -1, text = 100, sbar = 0, sbar_h = 0;
	const char *search = NULL, *boxes[8];
	int nboxes = 0;
	int dump = 0, show_tree = 0, verbose = 0, band = 0, parts = 0, nopics = 0, snapshot = 0, pens = -1, penlist = 0, i, r;
	int colours = 0, own_pal = 0, deep = 0, planes = 0;
	static PIC_PAL pal;
	const char *next = NULL, *prev = NULL, *xl_arg = NULL, *gw_to = NULL;
	char where[4096];
	browser b;
	page *p;
	SBUF extra;
	const char *fill[32], *send = NULL;
	unsigned long shows;
	int rc = 0;
	int nfill = 0, back_after = 0, save_link = -1, bookmark = 0, nclick = 0, timers = 0, refreshes = 0;
	const char *finds[16];
	int nfind = 0, find_back[16];
	pg_pos found[2] = { { -1, 0 }, { -1, 0 } };
	const char *select = NULL;
	int32_t sel_w[8];
	int nsel_w = 0, sel_on = 0;
	pg_range sel;
	const page *marked_p = NULL;	/* the layout the words found or the selection are on */
	int32_t resize[8];
	int nresize = 0;
	const char *clicks[16];
	const char *enc = NULL;

	{
		/* the stack's floor, as EWEB.PRG has one (ui/main.c): all of the
		   stack but a MB */
		struct rlimit rl;
		long size = 8L << 20;
		if (getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY)
			size = (long) rl.rlim_cur;
		ec_stack_floor = (char *) __builtin_frame_address(0) - size + (1L << 20);
		for (i = 1; i + 1 < argc && argv[i][0] == '-'; i++)
			if (!strcmp(argv[i], "-S"))
				ec_stack_floor = (char *) __builtin_frame_address(0) - atol(argv[i + 1]) * 1024;
	}
	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (!strcmp(argv[i], "-w") && i + 1 < argc)
			width = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-z") && i + 1 < argc)
			zoom = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-X") && i + 1 < argc)
			text = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-f") && i + 1 < argc)
			fnt = argv[++i];
		else if (!strcmp(argv[i], "-o") && i + 1 < argc)
			out = argv[++i];
		else if (!strcmp(argv[i], "-y") && i + 1 < argc)
			top = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-H") && i + 1 < argc)
			height = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-d"))
			dump = 1;
		else if (!strcmp(argv[i], "-B"))
			band = 1;
		else if (!strcmp(argv[i], "-1"))
			planes = 1;	/* each band in one plane and a byte a pixel alike (with -p 2) */
		else if (!strcmp(argv[i], "-i"))
			parts = 1;
		else if (!strcmp(argv[i], "-E"))
			penlist = 1;	/* each picture's pens for 16 colours, summed */
		else if (!strcmp(argv[i], "--deep"))
			deep = 1;	/* 32 bits a pixel */
		else if (!strcmp(argv[i], "--own"))
			own_pal = 1;	/* -p 256 with EWeb's own colours and the page's (pagecol.h) */
		else if (!strcmp(argv[i], "-p") && i + 1 < argc)
			colours = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-N") && i + 1 < argc)
			next = argv[++i];
		else if (!strcmp(argv[i], "-L") && i + 1 < argc && nresize < 8)
			resize[nresize++] = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-D") && i + 1 < argc)
			img_max = atol(argv[++i]);
		else if (!strcmp(argv[i], "-Y") && i + 1 < argc) {
			in_req_w = atoi(argv[++i]);
			in_req_again = strstr(argv[i], "@again") != NULL;
		}
		else if (!strcmp(argv[i], "-P") && i + 1 < argc)
			prev = argv[++i];
		else if (!strcmp(argv[i], "-t"))
			show_tree = 1;
		else if (!strcmp(argv[i], "-v"))
			verbose = 1;
		else if (!strcmp(argv[i], "-n"))
			nopics = 1;
		else if (!strcmp(argv[i], "-c") && i + 1 < argc)
			cacert = argv[++i];
		else if (!strcmp(argv[i], "-F") && i + 1 < argc && nfill < 32)
			fill[nfill++] = argv[++i];
		else if (!strcmp(argv[i], "-S") && i + 1 < argc)
			send = argv[++i];
		else if (!strcmp(argv[i], "-b"))
			back_after = 1;
		else if (!strcmp(argv[i], "-m") && i + 1 < argc)
			bm_file(argv[++i]);
		else if (!strcmp(argv[i], "-V") && i + 1 < argc)
			hg_file(argv[++i]);
		else if (!strcmp(argv[i], "-Q") && i + 1 < argc)
			pens = atoi(argv[++i]);	/* pictures as the Pi's pens: 1 always, 0 never */
		else if (!strcmp(argv[i], "-S") && i + 1 < argc)
			i++;			/* the stack's floor: above */
		else if (!strcmp(argv[i], "-W") && i + 1 < argc)
			str_copy(http_gateway, argv[++i], sizeof http_gateway);
		else if (!strcmp(argv[i], "-C") && i + 1 < argc) {
			/* DIR, or DIR,MB (8 MB when not said) */
			char d[400], *c;
			long mb = 8;
			str_copy(d, argv[++i], sizeof d);
			if ((c = strrchr(d, ',')) != NULL) {
				*c = 0;
				mb = atol(c + 1);
			}
			dc_dir(d, mb << 20);
		}
		else if (!strcmp(argv[i], "-R"))
			snapshot = 1;
		else if (!strcmp(argv[i], "-q") && i + 1 < argc)
			search = argv[++i];
		else if (!strcmp(argv[i], "-O") && i + 1 < argc && nboxes < 8)
			boxes[nboxes++] = argv[++i];
		else if (!strcmp(argv[i], "-A"))
			bookmark = 1;
		else if (!strcmp(argv[i], "-J"))
			scripts_on = 1;
		else if (!strcmp(argv[i], "-Jn"))
			scripts_on = 2;	/* (scripts could run, on no site: "needs its scripts") */
		else if ((!strcmp(argv[i], "-K") || !strcmp(argv[i], "-M")) && i + 1 < argc && nclick < 16)
			clicks[nclick++] = argv[++i];
		else if (!strcmp(argv[i], "-u") && i + 1 < argc && nclick < 16) {
			/* (in turn with the clicks) */
			static char tp[16][300];
			snprintf(tp[nclick], sizeof tp[nclick], "type:%s", argv[++i]);
			clicks[nclick] = tp[nclick];
			nclick++;
		}
		else if ((!strcmp(argv[i], "-G") || !strcmp(argv[i], "-U")) && i + 1 < argc && nfind < 16) {
			find_back[nfind] = argv[i][1] == 'U';
			finds[nfind++] = argv[++i];
		}
		else if (!strcmp(argv[i], "-x") && i + 1 < argc)
			select = argv[++i];
		else if (!strcmp(argv[i], "-a") && i + 1 < argc) {
			const char *c = strchr(argv[++i], ',');
			sbar = sbar_h = atoi(argv[i]);
			if (c != NULL)
				sbar_h = atoi(c + 1);
		}
		else if (!strcmp(argv[i], "-I") && i + 1 < argc) {
			const char *z = argv[++i], *c = strchr(z, ':');
			z_stage = !strncmp(z, "parse", 5) ? BRB_PARSE : !strncmp(z, "style", 5) ? BRB_STYLE :
				  !strncmp(z, "layout", 6) ? BRB_LAYOUT : !strncmp(z, "script", 6) ? BRB_SCRIPT : 0;
			z_at = c != NULL ? atoi(c + 1) : 0;
		}
		else if (!strcmp(argv[i], "-j") && i + 1 < argc && nsel_w < 8)
			sel_w[nsel_w++] = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-T") && i + 1 < argc)
			timers = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-r") && i + 1 < argc)
			refreshes = atoi(argv[++i]);	/* refreshes followed, each when it is due */
		else if (!strcmp(argv[i], "-s") && i + 1 < argc)
			save_to = argv[++i];
		else if (!strcmp(argv[i], "-e") && i + 1 < argc)
			enc = argv[++i];	/* View > Encoding */
		else if (!strcmp(argv[i], "-g") && i + 1 < argc)
			save_link = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-stream"))
			stream_files = 1;	/* a file a link opens: BR_SAVE, as EWEB.PRG has it */
		else if (!strcmp(argv[i], "-Z") && i + 1 < argc)
			selector_ms = atol(argv[++i]);	/* the file selector's time */
		else if (!strcmp(argv[i], "--gwtranslate") && i + 1 < argc)
			gw_to = argv[++i];	/* Translate on through the Gateway (-W): the page asked for translated */
		else if (!strcmp(argv[i], "--translate") && i + 1 < argc)
			xl_arg = argv[++i];	/* HOST:PORT:TO: translated Direct (xlate.c), as EWEB.PRG */
		else if (!strcmp(argv[i], "-l") && i + 1 < argc)
			str_copy(conn_logfile, argv[++i], sizeof conn_logfile);	/* the HTTP dialogue, as EWGET -l */
		else if (!strcmp(argv[i], "-k") && i + 1 < argc) {
			/* the key beside it, as eweb.key beside cookies.txt */
			static char key[1024];
			const char *f = argv[++i], *sl = strrchr(f, '/');
			snprintf(key, sizeof key, "%.*seweb.key", sl ? (int) (sl - f + 1) : 0, f);
			ck_file(f, key);
		}
		else
			break;
	}
	if (i >= argc) {
		fprintf(stderr, "ewrender [-w W] [-z ZOOM] [-X TEXT%%] [-f FNT] [-o OUT.PNG] [-d] [-t] [-v] [-c CACERT] [-y TOP] [-H H] [-B] [-i] [-E] [-Q 0|1] [-Jn] [--deep] [-N URL] [-L WIDTH] [-Y WIDTH] [-D BYTES] [-e ENCODING] "
			"[-p 2|4|16|256] PAGE|URL [SHEET...]\n");
		return 2;
	}
	if (colours != 0 && colours != 2 && colours != 4 && colours != 16 && colours != 256) {
		fprintf(stderr, "ewrender: -p takes 2, 4, 16 or 256\n");
		return 2;
	}
	page_arg = argv[i++];
	if (!font_init(fnt)) {
		fprintf(stderr, "ewrender: can't read %s\n", fnt);
		return 1;
	}
	/* a file named as it is here is made absolute: an address typed
	   without a scheme would be taken for a host */
	if (strstr(page_arg, "://") == NULL && strncmp(page_arg, "about:", 6) != 0 && page_arg[0] != '/') {
		if (realpath(page_arg, where) == NULL) {
			perror(page_arg);
			return 1;
		}
	} else {
		snprintf(where, sizeof where, "%s", page_arg);
	}
	sb_init(&extra);
	for (; i < argc; i++) {
		size_t n;
		char *css = slurp(argv[i], &n);
		if (css != NULL) {
			sb_add(&extra, css, (long) n);
			sb_addc(&extra, '\n');
			free(css);
		}
	}
	net_init();
	str_copy(conn_cacert, cacert, sizeof conn_cacert);
	br_init(&b, width, zoom);
	b.stream_files = stream_files;
	b.translate = gw_to;	/* (--gwtranslate: NULL off, as EWEB.PRG with Translate off) */
	b.height = height > 0 ? height : 400;	/* (the window's: a frameset is as tall) */
	b.text = (int32_t) (1024L * text / 100);
	b.sbar = sbar;
	b.sbar_h = sbar_h;
	b.say = said;
	b.log = logged;
	b.ud = &verbose;
	b.scripts = scripts_on != 0;
	b.script_sites = scripts_on == 2 ? "" : "*";
	b.console = console_line;
	b.ask = asked;
	b.extra_css = extra.s;
	b.pictures = !nopics;
	b.snapshot = snapshot;
	b.search = search;
	b.pens = pens;
	make_pal(band ? 256 : colours ? colours : 16);
	if (own_pal && colours == 256) {
		for (i = 0; i < EW_OWN; i++) {
			uint8_t q[3];
			ew_own_colour(i, q);
			memcpy(scr_pal[16 + i], q, 3);
		}
		scr_pal[255][0] = scr_pal[255][1] = scr_pal[255][2] = 0;
	}
	if (band || penlist || colours) {
		pic_pal_init(&pal, own_pal && colours == 256 ? EW_PIC_PENS : colours ? colours : 16,
			     (const unsigned char (*)[3]) scr_pal);
		img_pal = &pal;
	}
	if (prev != NULL) {
		char pw[4096];
		if (strstr(prev, "://") == NULL && realpath(prev, pw) != NULL)
			prev = pw;
		if (br_go(&b, prev) != BR_SHOWN) {
			fprintf(stderr, "ewrender: %s not shown\n", prev);
			return 1;
		}
		pictures(&b);
	}
	shows = b.shows;
	if (z_stage >= 0) {
		pf_idle = build_idle;
		br_poll_ms = 0;
	}
	r = br_go(&b, where);
	if (z_stage >= 0) {
		pf_idle = NULL;
		br_poll_ms = 100;
		printf("polls: parse %ld style %ld layout %ld script %ld\n", z_polls[BRB_PARSE], z_polls[BRB_STYLE],
		       z_polls[BRB_LAYOUT], z_polls[BRB_SCRIPT]);
		if (r == BR_STOPPED && b.p != NULL) {
			printf("stopped: \"%s\" %s\n", b.title, b.url);	/* (the page before stays) */
			r = BR_SHOWN;
		}
	}
	if ((r == BR_SAVE || r == BR_DOWNLOAD) && b.shows != shows && b.p != NULL) {
		/* a page shown on the way, whose script then clicked a download
		   link as it loaded: its file saved, then the page drawn, as
		   EWEB.PRG does */
		printf("shown on the way: %d \"%s\"\n", r, b.title);
		click_file(&b, r);
		r = BR_SHOWN;
	}
	if (r == BR_SAVE) {
		/* a file opened, saved as it comes (-stream) */
		click_file(&b, r);
		br_free(&b);
		return rc;
	}
	if (r == BR_DOWNLOAD) {
		char name[16];
		br_file_name(b.dl.url, b.dl.name, b.dl.mime, name, sizeof name);
		printf("download: %s %ld %s\n", b.dl.mime, b.dl.len, name);
		if (save_to != NULL && !br_dl_save(&b, save_to))
			rc = 1;
		br_dl_free(&b);
		br_free(&b);
		return rc;
	}
	if (r != BR_SHOWN || b.p == NULL) {
		fprintf(stderr, "ewrender: nothing shown (%d)\n", r);
		return 1;
	}
	if (b.p->deep)
		fprintf(stderr, "ewrender: deep: nested deeper than the stack holds, the rest left out\n");
	if (in_req_w > 0) {
		in_req_b = &b;
		pf_idle = in_request;
	}
	if (strchr(b.url, '#') != NULL)
		printf("top: %d\n", (int) b.scroll_to);	/* where EWEB.PRG scrolls to */
	pictures(&b);
	pf_idle = NULL;
	if (gw_to != NULL)
		printf("gateway translation: %d note \"%s\" runs %lu\n", b.trg_shows == b.shows ? b.tr_gate : 0, b.note,
		       br_translation_runs);
	if (xl_arg != NULL) {
		/* the page translated as EWEB.PRG does it: the screen (TOP, H)
		   first, a piece at a time */
		static char host[104], to[8];
		unsigned port = 0;
		char err[160];
		int32_t h = height > 0 ? height : 400;
		int n = 0;
		if (sscanf(xl_arg, "%103[^:]:%u:%7s", host, &port, to) != 3) {
			fprintf(stderr, "ewrender: --translate HOST:PORT:TO\n");
			return 2;
		}
		b.translate = to;
		r = br_translate_start(&b, host, (unsigned short) port);
		printf("translate: %d\n", r);
		while (r > 0 && br_translate_pending(&b, top, h, 0)) {
			int screen = br_translate_pending(&b, top, h, 1);
			r = br_translate_step(&b, top, h, err, sizeof err);
			printf("piece %d: %d%s%s%s\n", ++n, r, screen ? " (screen)" : "", err[0] ? " " : "", err);
		}
		{
			long done, total;
			char from[8];
			br_translate_progress(&b, &done, &total, from, sizeof from);
			printf("translated: %ld of %ld from %s \"%s\" runs %lu\n", done, total, from[0] ? from : "?",
			       b.title, br_translation_runs);
		}
		if (r < 0)
			br_translate_stop(&b);
	}
	if (enc != NULL) {
		r = br_encoding(&b, strcmp(enc, "auto") == 0 ? "" : enc);
		pictures(&b);
		printf("encoding: %d %s \"%s\"\n", r, b.d != NULL ? b.d->charset : "", b.title);
	}
	for (i = 0; i < nboxes && b.p != NULL; i++) {
		int n = 0;
		long sx = 0, sy = 0;
		int32_t x = 0, y = 0, w = 0, h = 0;
		sscanf(boxes[i], "%d:%ld:%ld", &n, &sx, &sy);
		r = page_scroll(b.p, n - 1, (int32_t) sx, (int32_t) sy, &x, &y, &w, &h);
		printf("scrolled: %d box %d to %d,%d (%d,%d %dx%d)\n", r, n, n > 0 && n <= b.p->nscr ?
		       (int) b.p->scr[n - 1].sx : 0, n > 0 && n <= b.p->nscr ? (int) b.p->scr[n - 1].sy : 0,
		       (int) x, (int) y, (int) w, (int) h);
	}
	for (i = 0; i < nresize; i++) {
		r = br_relayout(&b, resize[i]);
		pictures(&b);
		printf("resized: %d \"%s\" width %d\n", r, b.title, b.p ? (int) b.p->opt.width : 0);
	}
	if (nfill > 0 || send != NULL) {
		fctl *first = NULL, *by = NULL;
		int k;
		for (k = 0; k < nfill; k++) {
			char name[256];
			const char *eq = strchr(fill[k], '=');
			fctl *c = NULL;
			snprintf(name, sizeof name, "%.*s", eq ? (int) (eq - fill[k]) : (int) strlen(fill[k]), fill[k]);
			for (c = b.d->ctls; c != NULL; c = c->next) {
				const char *n = dn_attr(c->n, "name"), *v = dn_attr(c->n, "value");
				if (n == NULL || strcmp(n, name) != 0)
					continue;
				if (c->kind == FC_RADIO && eq != NULL && (v == NULL || strcmp(v, eq + 1) != 0))
					continue;
				break;
			}
			if (c == NULL) {
				fprintf(stderr, "ewrender: no field %s\n", name);
				return 1;
			}
			if (form_typed(c) || c->kind == FC_HIDDEN) {
				form_set(c, eq ? eq + 1 : "", eq ? (long) strlen(eq + 1) : 0);
				if (first == NULL)
					first = c;
			} else if (c->kind == FC_FILE) {
				form_set_file(c, eq ? eq + 1 : NULL);	/* -F NAME=PATH: the file chosen */
			} else if (c->kind == FC_CHECKBOX || c->kind == FC_RADIO) {
				form_check(b.d, c);
			} else if (c->kind == FC_SELECT && eq != NULL) {
				int o;
				for (o = 0; o < c->nopt; o++) {
					char t[256];
					const char *ov = dn_attr(c->opt[o], "value");
					form_option_text(c->opt[o], t, sizeof t);
					if ((ov && !strcmp(ov, eq + 1)) || (!ov && !strcmp(t, eq + 1)))
						form_choose(c, o);
				}
			}
		}
		if (send != NULL) {
			if (strcmp(send, "-") == 0) {
				int ok;
				by = form_default(b.d, first, &ok);
				if (!ok)
					first = NULL;
			} else {
				for (by = b.d->ctls; by != NULL; by = by->next)
					if (dn_attr(by->n, "name") && !strcmp(dn_attr(by->n, "name"), send))
						break;
				first = NULL;
			}
			r = br_submit(&b, first, by, 0, 0);
			printf("sent: %d \"%s\" %s\n", r, b.title, b.url);
			if (back_after) {
				r = br_back(&b);
				printf("back: %d \"%s\" %s\n", r, b.title, b.url);
			}
			pictures(&b);
		}
	}
	if (next != NULL) {
		static const char *const step[] = { "to", "back", "forward", "back", "narrower", "forward", "back" };
		int k;
		for (k = 0; k < 7; k++) {
			if (k == 0)
				r = br_go(&b, next);
			else if (k == 1 || k == 3 || k == 6)
				r = br_back(&b);
			else if (k == 4)
				r = br_relayout(&b, width - 100) ? BR_SHOWN : BR_STOPPED;
			else
				r = br_forward(&b);
			pictures(&b);
			printf("%s: %d \"%s\" %s width %d fetch %lu parse %lu layout %lu\n", step[k], r, b.title, b.url,
			       b.p ? (int) b.p->opt.width : 0, b.ms_fetch, b.ms_parse, b.ms_layout);
		}
		if (b.p == NULL)
			return 1;
	}
	run_timers(&b, timers);
	for (i = 0; i < nclick; i++) {
		click(&b, clicks[i]);
		pictures(&b);
		run_timers(&b, timers);
	}
	for (i = 0; i < nfind; i++) {
		/* Edit > Find: from the top, then on from (back: before) the
		   words found last, as EWEB.PRG's ^G and Shift-^G */
		pg_pos from = found[0], a, e;
		long nth, n;
		if (found[0].item >= 0 && !find_back[i])
			from.off++;
		n = page_find(b.p, finds[i], found[0].item >= 0 ? &from : NULL, find_back[i], &a, &e, &nth);
		if (n > 0) {
			int moved = page_reveal_item(b.p, a.item, 0);
			found[0] = a;
			found[1] = e;
			marked_p = b.p;
			const dl_item *d = &b.p->items[a.item];
			printf("found: %ld of %ld item %ld %u to item %ld %u at %d,%d %dx%d%s\n", nth, n, a.item, a.off,
			       e.item, e.off, (int) d->x, (int) d->y, (int) d->w, (int) d->h,
			       moved ? " (a box scrolled)" : "");
		} else {
			printf("found: none (%ld)\n", n);
		}
	}
	if (select != NULL && b.p != NULL && !strcmp(select, "page")) {
		selected(b.p, NULL, "copied");
	} else if (select != NULL && b.p != NULL) {
		/* as EWEB.PRG selects (ui/win.c): a drag from one place to
		   another, a double click's word, Select all */
		pg_pos x, y, at;
		int x1 = 0, y1 = 0, x2 = 0, y2 = 0, n = sscanf(select, "%d,%d,%d,%d", &x1, &y1, &x2, &y2);
		if (!strcmp(select, "all"))
			sel_on = page_text_place(b.p, 0, 0, &x) && page_text_place(b.p, LONG_MAX, 1, &y);
		else if (n == 4)
			sel_on = page_pos_at(b.p, x1, y1, &x) && page_pos_at(b.p, x2, y2, &y);
		else if (n == 2 && page_pos_at(b.p, x1, y1, &at) == 2) {
			page_word(b.p, &at, &x, &y);	/* (on text only, as EWEB.PRG's) */
			sel_on = 1;
		}
		if (sel_on) {
			long ix[2];
			int k;
			page_range(b.p, &x, &y, &sel);
			marked_p = b.p;
			selected(b.p, &sel, "selected");
			ix[0] = page_text_index(b.p, &x);
			ix[1] = page_text_index(b.p, &y);
			for (k = 0; k < nsel_w; k++) {
				int back = ix[0] > ix[1];
				r = br_relayout(&b, sel_w[k]);
				pictures(&b);
				if (b.p == NULL || !page_text_place(b.p, ix[0], back, &x) ||
				    !page_text_place(b.p, ix[1], !back, &y)) {
					printf("selected again: none\n");
					sel_on = 0;
					break;
				}
				page_range(b.p, &x, &y, &sel);
				marked_p = b.p;
				selected(b.p, &sel, "selected again");
			}
		} else {
			printf("selected: none\n");
		}
	}
	if (refreshes > 0) {
		/* the page's refresh (<meta http-equiv=refresh>, the Refresh
		   header), waited for and followed, as EWEB.PRG's idle loop does;
		   -b then goes Back */
		long due;
		int k;
		for (k = 0; k < refreshes && (due = br_refresh_due(&b)) >= 0; k++) {
			printf("refresh: %lu s %s\n", b.refresh_s, b.refresh);
			if (due > 0)
				usleep((useconds_t) due * 1000);
			r = br_refresh_go(&b);
			pictures(&b);
			printf("refreshed: %d \"%s\" %s\n", r, b.title, b.url);
		}
		if (back_after && send == NULL) {
			r = br_back(&b);
			printf("back: %d \"%s\" %s%s\n", r, b.title, b.url, br_refresh_due(&b) >= 0 ? " (refresh)" : "");
		}
	}
	if (b.note[0])
		printf("note: %s\n", b.note);
	if (bookmark) {
		int k = br_bookmark(&b);
		printf("bookmarked: %d %d\n", k, bm_n);
	}
	if (save_to != NULL && !click_saved) {
		char name[16];
		if (save_link >= 0) {
			char url[4096], given[256];
			const char *g = br_link_download(&b, save_link);
			br_link_url(&b, save_link, url, sizeof url);
			if (g != NULL)
				str_copy(given, g, sizeof given);
			save_url(&b, url, g != NULL ? given : NULL);
		} else {
			br_file_name(b.url, NULL, b.mime, name, sizeof name);
			r = br_save_page(&b, save_to);
			printf("saved page: %d %ld %s\n", r, b.src_len, name);
		}
	}
	p = b.p;
	if (show_tree)
		tree(b.d->root, 0);
	if (penlist) {
		/* each picture shown, its pens (and where it is shown) summed:
		   the same through the Pi's gateway as decoded here */
		long k, j;
		for (k = 0; k < p->nitems; k++) {
			const dl_item *it = &p->items[k];
			const PIC *pic;
			const uint8_t *pens;
			unsigned long h = 2166136261UL;
			if (it->type != DL_IMAGE || (it->flags & DLF_ALT) ||
			    !img_picture(it->img, DL_PIC_W(it), DL_PIC_H(it), &pic, &pens) || pens == NULL)
				continue;
			for (j = 0; j < (long) pic->w * pic->h; j++) {
				h = ((h ^ pens[j]) * 16777619UL) & 0xFFFFFFFFUL;
				h = ((h ^ (pic->alpha == NULL || pic->alpha[j] >= 128)) * 16777619UL) & 0xFFFFFFFFUL;
			}
			printf("pens %s %dx%d %08lx\n", img_url(it->img), pic->w, pic->h, h);
		}
	}
	if (planes) {
		long planar;
		rc = planes_check(p, width, &planar) != 0;
	} else if (band) {
		long n = bands(p, width);
		printf("bands: %ld drawn, page %d x %d\n", n, (int) p->width, (int) p->height);
		rc = n < 0;
	} else if (dump) {
		long k;
		printf("page %d x %d, background %06lx, %ld items, %d links, title \"%s\"\n", (int) p->width,
		       (int) p->height, (unsigned long) p->bg, p->nitems, p->nlinks, b.d->title);
		if (p->right > p->width)
			printf("wider: to %d (the window scrolls sideways)\n", (int) p->right);
		if (getenv("EWR_CLIPS") != NULL)	/* (each item's clip, for debugging) */
			for (k = 0; k < p->nitems; k++)
				if (p->items[k].clip)
					printf("item %ld type %d at %d,%d in clip %d\n", k, p->items[k].type,
					       (int) p->items[k].x, (int) p->items[k].y, p->items[k].clip);
		if (p->seg != NULL) {
			/* (the order drawn in, when it isn't the list's: its
			   ranges, first to last item) */
			printf("drawn:");
			for (k = 0; k < p->nseg; k++)
				printf(" %ld-%ld", p->seg[k].k0, p->seg[k].k1 - 1);
			printf("\n");
		}
		if (verbose)
			printf("address %s, security \"%s\"\n", b.url, b.security);
		for (k = 0; k < p->nitems; k++) {
			const dl_item *it = &p->items[k];
			char col[16];
			/* a colour less than solid as CSS's #rrggbbaa (a control's:
			   its text's, and its opacity as a picture's) */
			uint32_t rgb = it->type == DL_CTL ? it->ink : it->rgb;
			const char *nohit = it->flags & DLF_NOHIT ? " nohit" : "";
			if (rgb >> 24 == 255)
				snprintf(col, sizeof col, "#%06lx", (unsigned long) (rgb & 0xffffff));
			else
				snprintf(col, sizeof col, "#%06lx%02lx", (unsigned long) (rgb & 0xffffff),
					 (unsigned long) (rgb >> 24));
			if (it->type == DL_FILL && it->link)	/* (a block link's box, for clicks) */
				printf("fill %d,%d %dx%d %s link %d\n", (int) it->x, (int) it->y, (int) it->w, (int) it->h,
				       col, it->link);
			else if (it->type == DL_FILL)
				printf("fill %d,%d %dx%d %s\n", (int) it->x, (int) it->y, (int) it->w, (int) it->h, col);
			else if (it->type == DL_IMAGE && (it->flags & DLF_TILE))
				printf("background %d,%d %dx%d%s%s tile %d,%d %dx%d%s%s %s%s\n", (int) it->x, (int) it->y,
				       (int) it->w, (int) it->h, it->flags & DLF_NOSIZE ? " unsized" : "",
				       it->tile == NULL && !(it->flags & DLF_NOSIZE) ? " none" : "",
				       it->tile ? (int) it->tile->dx : 0, it->tile ? (int) it->tile->dy : 0,
				       it->tile ? (int) it->tile->w : 0, it->tile ? (int) it->tile->h : 0,
				       it->tile && it->tile->rx ? " across" : "", it->tile && it->tile->ry ? " down" : "",
				       img_url(it->img), alpha_of(it));
			else if (it->type == DL_CLIP) {
				/* (a box hiding what overflows it; * an axis it doesn't clip) */
				char cx[32], cy[32];
				if (it->w > (1L << 28))
					snprintf(cx, sizeof cx, "*");
				else
					snprintf(cx, sizeof cx, "%d+%d", (int) it->x, (int) it->w);
				if (it->h > (1L << 28))
					snprintf(cy, sizeof cy, "*");
				else
					snprintf(cy, sizeof cy, "%d+%d", (int) it->y, (int) it->h);
				printf("clip x %s y %s in %d\n", cx, cy, (int) it->clip);
			} else if (it->type == DL_SBAR) {
				/* (a box's scroll bar: how far the box scrolls, and is
				   scrolled; not one it doesn't need, nor one not shown) */
				const struct pg_scroll *sc;
				int32_t t0, t1;
				if (it->len == 0 || it->len > (uint32_t) p->nscr || (it->rgb >> 24) == 0 || it->w <= 0)
					continue;
				sc = &p->scr[it->len - 1];
				page_bar_thumb(p, it, &t0, &t1);
				printf("bar %s %d,%d %dx%d box %d reach %d,%d at %d,%d thumb %d-%d\n", it->base ? "x" : "y",
				       (int) it->x, (int) it->y, (int) it->w, (int) it->h, (int) it->len, (int) sc->mx,
				       (int) sc->my, (int) sc->sx, (int) sc->sy, (int) t0, (int) t1);
			} else if (it->type == DL_SUM)
				/* (a details' summary: where a click opens and closes it) */
				printf("summary %d,%d %dx%d %s %s\n", (int) it->x, (int) it->y, (int) it->w, (int) it->h,
				       dn_attr(it->det, "open") ? "open" : "closed",
				       dn_attr(it->det, "id") ? dn_attr(it->det, "id") : "-");
			else if (it->type == DL_GRAD)
				printf("gradient %d,%d %dx%d tile %d,%d %dx%d%s%s%s %s%s\n", (int) it->x, (int) it->y,
				       (int) it->w, (int) it->h, it->tile ? (int) it->tile->dx : 0,
				       it->tile ? (int) it->tile->dy : 0, it->tile ? (int) it->tile->w : 0,
				       it->tile ? (int) it->tile->h : 0, it->tile == NULL ? " none" : "",
				       it->tile && it->tile->rx ? " across" : "", it->tile && it->tile->ry ? " down" : "",
				       grad_of(it->img), alpha_of(it));
			else if (it->type == DL_IMAGE)
				printf("image %d,%d %dx%d%s%s%s link %d \"%.*s\" %s%s\n", (int) it->x, (int) it->y,
				       (int) it->w, (int) it->h, it->flags & DLF_NOSIZE ? " guessed" : "",
				       it->flags & DLF_ALT ? " alt" : "", nohit, it->link, (int) it->len, it->text,
				       img_url(it->img), alpha_of(it));
			else if (it->type == DL_CTL) {
				static const char *const kinds[] = { "text", "password", "hidden", "checkbox", "radio",
								     "submit", "reset", "button", "image", "file",
								     "textarea", "select" };
				const fctl *c = p->ctls[it->ctl - 1];
				char lab[256], *q;
				form_label(c, lab, sizeof lab);
				for (q = lab; *q; q++)
					if (*q == '\n')
						*q = '|';	/* a textarea's lines on one line */
				/* (a field's text centred or at the right: text-align) */
				int fld = c->kind == FC_TEXT || c->kind == FC_PASSWORD || c->kind == FC_TEXTAREA;
				printf("control %d,%d %dx%d %s %s%s%s%s \"%s\"%s%s%s\n", (int) it->x, (int) it->y,
				       (int) it->w, (int) it->h, col, kinds[c->kind],
				       fld && (it->flags & DLF_CENTRE) ? " centred" : fld && (it->flags & DLF_RIGHT) ? " right" : "",
				       c->disabled ? " disabled" : "", nohit,
				       lab, form_placeholder(c) ? " placeholder " : "",
				       form_placeholder(c) ? form_placeholder(c) : "", alpha_of(it));
			} else
				printf("text %d,%d %dx%d %s %s%d%s%s%s%s link %d \"%.*s\"\n", (int) it->x,
				       (int) (it->y + it->base), (int) it->w, (int) it->h, col,
				       it->font->family == FF_SANS ? "sans" : it->font->family == FF_SERIF ? "serif" : "mono",
				       it->font->px, it->font->style & FS_BOLD ? " bold" : "",
				       it->font->style & FS_ITALIC ? " italic" : "", it->flags & DLF_RTL ? " rtl" : "", nohit,
				       it->link, (int) it->len, it->text);
		}
		for (k = 0; k < p->nlinks; k++) {
			char u[4096];
			br_link_url(&b, (int) k, u, sizeof u);
			printf("link %ld %s\n", k + 1, u);
		}
	} else {
		canvas c;
		if (own_pal && colours == 256) {
			/* the page's own colours on EWeb's last pens, as
			   ui/screen.c sets them for the page in front */
			uint32_t pc[EW_PAGE];
			int np = page_colours(p, (const uint8_t (*)[3]) scr_pal, EW_PIC_PENS, pc, EW_PAGE), q;
			for (q = 0; q < np; q++) {
				scr_pal[EW_PIC_PENS + q][0] = (unsigned char) (pc[q] >> 16);
				scr_pal[EW_PIC_PENS + q][1] = (unsigned char) (pc[q] >> 8);
				scr_pal[EW_PIC_PENS + q][2] = (unsigned char) pc[q];
			}
			fprintf(stderr, "page colours: %d\n", np);
		}
		memset(&c, 0, sizeof c);
		c.w = width;
		c.h = height > 0 ? height : (p->height > 0 ? p->height : 1);
		if (c.h > 30000)
			c.h = 30000;
		c.bpp = colours ? 8 : deep ? 32 : 16;
		c.stride = (long) c.w * (c.bpp / 8);
		c.oy = top;
		c.pen = pen_n;
		c.shade = shade_n;
		c.pal = (const uint8_t (*)[3]) scr_pal;
		c.picture = picture;
		c.pix = malloc(c.stride * c.h);
		if (c.pix == NULL)
			return 1;
		if (p != marked_p) {
			/* (another page or layout since: none marked) */
		} else if (sel_on) {
			c.marked = 1;
			c.mark = sel;
			c.mark_rgb = 0x99ccff;	/* (ui/win.c's SEL_RGB) */
		} else if (found[0].item >= 0) {
			c.marked = 1;
			page_range(p, &found[0], &found[1], &c.mark);
			c.mark_rgb = 0xffff00;
		}
		if (parts) {
			/* -i: in bands of 8 rows, each in parts (cv_page_part
			   stopped as often as it asks), as the kept page's are
			   made on a stock Falcon (ui/win.c) */
			int32_t y;
			for (y = 0; y < c.h; y += 8) {
				canvas s = c;
				long pos = 0;
				s.pix = c.pix + (long) y * c.stride;
				s.oy = c.oy + y;
				s.h = c.h - y < 8 ? c.h - y : 8;
				do
					pos = cv_page_part(&s, p, pos, stop_now, NULL);
				while (pos >= 0);
			}
		} else
			cv_page(&c, p);
		if (!write_png(out, &c)) {
			perror(out);
			return 1;
		}
		free(c.pix);
	}
	br_free(&b);
	br_cache_clear();
	http_close_all();
	sb_free(&extra);
	font_free();
	ck_save();
	hg_save();
	hg_free();
	ck_file(NULL, NULL);
	ck_clear();
	dc_close();
	return rc;
}
