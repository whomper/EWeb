/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ewrender - EWeb's page drawing on this computer: a saved page through
 * the document (src/doc), the layout (src/layout) and the canvas
 * (src/gfx) into a PNG, as the Falcon would show it, for checking the
 * layout by eye and for make test's comparisons. The page is loaded as
 * EWeb loads it (src/browse): a file, or a URL over the network.
 *
 *   ewrender [-w WIDTH] [-z ZOOM] [-f EWEB.FNT] [-o OUT.PNG] [-d] [-t] [-v] [-c CACERT] [-y TOP] [-H HEIGHT] [-B] [-N URL]
 *            PAGE.HTM|URL [SHEET.CSS...]
 *
 * -w the page's width in pixels (608), -z device pixels per CSS pixel
 * in 1024ths (819), -d the display list as text instead of a picture,
 * -t the document tree with each element's display first, -v what
 * the browser says while it loads (its status line), -c the root
 * certificates for https (binary/CACERT.PEM),
 * -y and -H the part of the page drawn (all of it). -N URL: then to URL,
 * Back, Forward, Back, the width 100 px less, Forward, Back, each step's
 * title and times printed (kept pages come back with no fetch or parse);
 * what is drawn or dumped is the page at the end. -B draws the page as
 * EWEB.PRG does on the screen (ui/screen.c), in bands of 32 rows at
 * every scroll position and in pieces across, at 8 and 16 bits, each into
 * a buffer of just its size: under AddressSanitizer a pixel drawn past a
 * band's edge stops it (on the Falcon it would land in ST RAM next to the
 * band, TOS's). The page's pictures are fetched and decoded before it is
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
 * link's file to FILE instead (Alternate-click). Each prints what it
 * did and the name EWEB.PRG would offer. -m FILE keeps the bookmarks in
 * FILE (as BOOKMARK.HTM); -A adds the page shown at the end to them.
 * -W HOST:PORT goes through AERA-Gateway's webgate (as Settings > Secure's
 * Pi gateway); -R then asks for pages through the Pi's browser (View >
 * Through the Pi's browser: webgate's snapshots).
 * -C DIR keeps the disk cache in DIR (as CACHE next to EWEB.PRG; its
 * index written at the end, as at quit).
 */
#define _DEFAULT_SOURCE		/* realpath */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bookmark.h"
#include "browse.h"
#include "canvas.h"
#include "conn.h"
#include "cookie.h"
#include "dcache.h"
#include "font.h"
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
		for (x = 0; x < c->w; x++) {
			unsigned v = (unsigned) p[2 * x] << 8 | p[2 * x + 1];
			r[1 + 3 * x] = (unsigned char) ((v >> 8 & 0xf8) | (v >> 13));
			r[2 + 3 * x] = (unsigned char) ((v >> 3 & 0xfc) | (v >> 9 & 3));
			r[3 + 3 * x] = (unsigned char) ((v << 3 & 0xf8) | (v >> 2 & 7));
		}
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

/* -B: the page as ui/screen.c draws it, a band of 32 rows at a time, at
   every scroll position (each row the top of a band) and in pieces across
   (an area of the window uncovered), each into a buffer of just its size */
static long bands(const page *p, int32_t width)
{
	static const int32_t across[][2] = { { 0, 0 }, { 0, 100 }, { 37, 211 }, { 300, 0 }, { 599, 0 } };
	int32_t y, k, bpp;
	long n = 0;
	for (bpp = 8; bpp <= 16; bpp += 8) {
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
				c.stride = bpp == 16 ? 2L * w : w;
				c.ox = x;
				c.oy = y;
				c.pen = pen8;
				c.shade = k & 1 ? shade8 : NULL;
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

static void said(void *ud, const char *what)
{
	if (*(int *) ud)
		fprintf(stderr, "%s\n", what);
}

/* every picture of the page shown, fetched and decoded */
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
	int32_t width = 608, zoom = 819, top = 0, height = -1;
	int dump = 0, show_tree = 0, verbose = 0, band = 0, nopics = 0, snapshot = 0, i, r;
	PIC_PAL pal;
	const char *next = NULL, *prev = NULL;
	char where[4096];
	browser b;
	page *p;
	SBUF extra;
	const char *fill[32], *send = NULL;
	int rc = 0;
	int nfill = 0, back_after = 0, save_link = -1, bookmark = 0;
	const char *save_to = NULL;

	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (!strcmp(argv[i], "-w") && i + 1 < argc)
			width = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-z") && i + 1 < argc)
			zoom = atoi(argv[++i]);
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
		else if (!strcmp(argv[i], "-N") && i + 1 < argc)
			next = argv[++i];
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
		else if (!strcmp(argv[i], "-W") && i + 1 < argc)
			str_copy(http_gateway, argv[++i], sizeof http_gateway);
		else if (!strcmp(argv[i], "-C") && i + 1 < argc)
			dc_dir(argv[++i], 8L << 20);
		else if (!strcmp(argv[i], "-R"))
			snapshot = 1;
		else if (!strcmp(argv[i], "-A"))
			bookmark = 1;
		else if (!strcmp(argv[i], "-s") && i + 1 < argc)
			save_to = argv[++i];
		else if (!strcmp(argv[i], "-g") && i + 1 < argc)
			save_link = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-l") && i + 1 < argc)
			str_copy(conn_logfile, argv[++i], sizeof conn_logfile);	/* the HTTP dialogue, as EWGET -l */
		else if (!strcmp(argv[i], "-k") && i + 1 < argc) {
			/* the key beside it, as EWEB.KEY beside COOKIES.TXT */
			static char key[1024];
			const char *f = argv[++i], *sl = strrchr(f, '/');
			snprintf(key, sizeof key, "%.*sEWEB.KEY", sl ? (int) (sl - f + 1) : 0, f);
			ck_file(f, key);
		}
		else
			break;
	}
	if (i >= argc) {
		fprintf(stderr, "ewrender [-w W] [-z ZOOM] [-f FNT] [-o OUT.PNG] [-d] [-t] [-v] [-c CACERT] [-y TOP] [-H H] [-B] [-N URL] "
			"PAGE|URL [SHEET...]\n");
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
	b.say = said;
	b.log = said;
	b.ud = &verbose;
	b.extra_css = extra.s;
	b.pictures = !nopics;
	b.snapshot = snapshot;
	if (band) {
		pic_pal_init(&pal, 16, falcon16);
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
	r = br_go(&b, where);
	if (r == BR_DOWNLOAD) {
		char name[16];
		br_file_name(b.dl.url, b.dl.mime, name, sizeof name);
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
	pictures(&b);
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
	if (bookmark) {
		int k = br_bookmark(&b);
		printf("bookmarked: %d %d\n", k, bm_n);
	}
	if (save_to != NULL) {
		char name[16];
		if (save_link >= 0) {
			char url[4096], err[200];
			br_link_url(&b, save_link, url, sizeof url);
			br_file_name(url, "", name, sizeof name);
			r = br_save_url(&b, url, save_to, err, sizeof err);
			printf("saved link: %d %ld %s %s\n", r, br_saved, name, err);
		} else {
			br_file_name(b.url, b.mime, name, sizeof name);
			r = br_save_page(&b, save_to);
			printf("saved page: %d %ld %s\n", r, b.src_len, name);
		}
	}
	p = b.p;
	if (show_tree)
		tree(b.d->root, 0);
	if (band) {
		long n = bands(p, width);
		printf("bands: %ld drawn, page %d x %d\n", n, (int) p->width, (int) p->height);
		rc = n < 0;
	} else if (dump) {
		long k;
		printf("page %d x %d, background %06lx, %ld items, %d links, title \"%s\"\n", (int) p->width,
		       (int) p->height, (unsigned long) p->bg, p->nitems, p->nlinks, b.d->title);
		if (verbose)
			printf("address %s, security \"%s\"\n", b.url, b.security);
		for (k = 0; k < p->nitems; k++) {
			const dl_item *it = &p->items[k];
			if (it->type == DL_FILL)
				printf("fill %d,%d %dx%d #%06lx\n", (int) it->x, (int) it->y, (int) it->w, (int) it->h,
				       (unsigned long) it->rgb);
			else if (it->type == DL_IMAGE)
				printf("image %d,%d %dx%d%s%s link %d \"%.*s\" %s\n", (int) it->x, (int) it->y, (int) it->w,
				       (int) it->h, it->flags & DLF_NOSIZE ? " guessed" : "", it->flags & DLF_ALT ? " alt" : "",
				       it->link, (int) it->len, it->text, img_url(it->img));
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
				printf("control %d,%d %dx%d #%06lx %s%s \"%s\"%s%s\n", (int) it->x, (int) it->y, (int) it->w,
				       (int) it->h, (unsigned long) it->rgb, kinds[c->kind], c->disabled ? " disabled" : "",
				       lab, form_placeholder(c) ? " placeholder " : "",
				       form_placeholder(c) ? form_placeholder(c) : "");
			} else
				printf("text %d,%d %dx%d #%06lx %s%d%s%s%s link %d \"%.*s\"\n", (int) it->x,
				       (int) (it->y + it->base), (int) it->w, (int) it->h, (unsigned long) it->rgb,
				       it->font->family == FF_SANS ? "sans" : it->font->family == FF_SERIF ? "serif" : "mono",
				       it->font->px, it->font->style & FS_BOLD ? " bold" : "",
				       it->font->style & FS_ITALIC ? " italic" : "", it->flags & DLF_RTL ? " rtl" : "",
				       it->link, (int) it->len, it->text);
		}
		for (k = 0; k < p->nlinks; k++) {
			char u[4096];
			br_link_url(&b, (int) k, u, sizeof u);
			printf("link %ld %s\n", k + 1, u);
		}
	} else {
		canvas c;
		memset(&c, 0, sizeof c);
		c.w = width;
		c.h = height > 0 ? height : (p->height > 0 ? p->height : 1);
		if (c.h > 30000)
			c.h = 30000;
		c.bpp = 16;
		c.stride = 2L * c.w;
		c.oy = top;
		c.picture = picture;
		c.pix = malloc(c.stride * c.h);
		if (c.pix == NULL)
			return 1;
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
	ck_file(NULL, NULL);
	ck_clear();
	dc_close();
	return rc;
}
