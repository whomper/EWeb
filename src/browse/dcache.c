/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dcache.c - EWeb's disk cache (dcache.h): the answers in a ring of
 * pieces on the disk, each piece with its list, and a small index of
 * them all.
 *
 * In pieces (Erez's choice, 29 Oct 2026): GEMDOS finds a place in a file
 * by walking its clusters from the start, so reading 8 KB 99 MB into one
 * 100 MB pack took 26 to 52 disk commands, against 2 at its start
 * (EmuTOS, Hatari's IDE trace; Erez's card about 48 ms a command); and
 * the one index of 3,000 answers, 800 KB, was read at the first page and
 * written at each quit. Now no file is longer than PIECE_MAX, and the
 * index holds 12 bytes an answer.
 *
 * CACHE\EWEB.IDX (two slots, slots.c): "EWDC 4 PIECES CUR AT SIZE\n",
 * then each piece's number (8 hex digits, a line), then 12 bytes an
 * answer: its key's hash (4), its piece (1), its place there (3) and its
 * length (4), big-endian. The whole of what is known of an answer (its
 * key, address, type, dates, validators) is in its piece's list.
 *
 * CACHE\EWEBnn.PAK (nn the piece, in hex): "EWDC" and the piece's number
 * in eight hex digits, CR LF twice (16 bytes), then answers, each its tag
 * (its key's hash and its length, 4 bytes each) and its bytes.
 *
 * CACHE\EWEBnn.LST (two slots): "EWDL 1 NUMBER\n", then a line for each
 * answer in the piece, "OFF LEN STORED FRESH USED FLAGS\tMIME\tCHARSET\t
 * SECURITY\tETAG\tLASTMOD\tKEY\tURL\n" (times in UTC seconds since 1970,
 * the address left out when it is the key). Read when one of its answers
 * is first asked for, written at quit when it changed.
 *
 * The ring: what a job brings goes in one write at AT in the current
 * piece; when the next answer wouldn't fit, the next piece is written over
 * from its start, and what it held is forgotten. A piece's number is new
 * each time it is begun, so an index or a list from before a reset doesn't
 * read another piece's answers; and each answer's tag is checked, so one
 * written over since isn't read.
 *
 * The pack of before (EWEB.PAK and its index, "EWDC 3") is removed, not
 * read (Erez: no migration, 29 Oct 2026).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cookie.h"
#include "dcache.h"
#include "plat.h"
#include "slots.h"
#include "util.h"

#define HEAD 16			/* a piece's first bytes */
#define TAG 8			/* before each answer in a piece */
#define REC 12			/* an answer in the index */
#define PIECE_MAX (4L << 20)	/* the most a piece holds */
#define PIECES_MAX 128		/* (512 MB) */

#define DCF_REVALIDATE	1	/* must-revalidate or no-cache: never shown stale */
#define DCF_IMMUTABLE	2	/* immutable (RFC 8246): Reload doesn't ask */
#define DCF_OWN		4	/* EWeb's own data (dc_put_own), not an answer */

/* what is known of an answer: from the index alone its hash, piece, place
   and length; with its piece's list (or made this run) the rest, its
   strings one after the other in meta */
enum { M_KEY, M_URL, M_ETAG, M_LM, M_MIME, M_CS, M_SEC, M_N };

typedef struct {
	unsigned long hash;
	long off, len;			/* off -1: in mem, not yet in a piece */
	long long stored, fresh, used;	/* (with meta) fresh: until when */
	char *meta;			/* NULL: its list not read yet */
	char *mem;
	unsigned char piece, flags;
} DCE;

static long long now_utc(void)
{
	return ck_now();
}

long long (*dc_now)(void) = now_utc;

static char dir[256];
static long max_size, piece_size;
static int pieces;			/* in the ring */
static DCE *ent;
static int nent, cap;
static int loaded;
static long pending;
static int cur;				/* the piece written now */
static long at;				/* where the next write goes in it */
static unsigned long pgen[PIECES_MAX];	/* each piece's number */
static unsigned char listed[PIECES_MAX];	/* its list read (or new) this run */
static unsigned char dirty[PIECES_MAX];	/* its list to be written at quit */
static unsigned char checked[PIECES_MAX];	/* its head checked this run */
static unsigned char fresh_piece[PIECES_MAX];	/* begun this run: its head to be written */
static unsigned char exists[PIECES_MAX];	/* its file (and list) on the disk (6.5: made at once if not) */
static int pak = -1, pak_piece = -1;
static int pak_wo;			/* made this job, open for writing only: opened again to read */
static int changed;			/* the index to be written */
static unsigned long seed;

static void flush_some(long limit);
#define flush() flush_some(0)
void (*dc_progress)(long done, long total);
/* how much gathers in memory before it is written while a page's parts
   come (dc_hold); the rest is written a step at a time when EWeb is idle
   (dc_write_step) */
static long hold_max = 1L << 20;
static int writing;		/* dc_write_step's under way: written to the end */
#define WRITE_STEP 32768L	/* a long write in pieces, dc_progress told after each */

static void put32(char *p, unsigned long v)
{
	p[0] = (char) (v >> 24);
	p[1] = (char) (v >> 16);
	p[2] = (char) (v >> 8);
	p[3] = (char) v;
}

static unsigned long get32(const char *p)
{
	const unsigned char *u = (const unsigned char *) p;
	return (unsigned long) u[0] << 24 | (unsigned long) u[1] << 16 | (unsigned long) u[2] << 8 | u[3];
}

static void path_of(char *out, int size, const char *name)
{
	snprintf(out, (size_t) size, "%s%c%s", dir, PF_SEP, name);
}

static void piece_path(char *out, int size, int p, const char *ext)
{
	char name[24];
	snprintf(name, sizeof name, "EWEB%02X.%.3s", p & 0xFF, ext);
	path_of(out, size, name);
}

static unsigned long hash_of(const char *s)
{
	unsigned long h = 2166136261UL;
	for (; *s; s++)
		h = ((h ^ (unsigned char) *s) * 16777619UL) & 0xFFFFFFFFUL;
	return h;
}

static unsigned long new_number(void)
{
	seed = ((unsigned long) dc_now() * 2654435761UL + seed * 69069UL + 1) & 0xFFFFFFFFUL;
	return seed | 1;	/* (0 is a piece never begun) */
}

/* ---------------------------------------------------------------- an answer's strings */

static const char *meta(const DCE *e, int i)
{
	const char *s = e->meta;
	while (i-- > 0)
		s += strlen(s) + 1;
	return s;
}

static char *meta_make(const char *const v[M_N])
{
	long n = 0;
	int i;
	char *m, *p;
	for (i = 0; i < M_N; i++)
		n += (long) strlen(v[i]) + 1;
	if ((m = malloc((size_t) n)) == NULL)
		return NULL;
	for (p = m, i = 0; i < M_N; i++) {
		strcpy(p, v[i]);
		p += strlen(p) + 1;
	}
	return m;
}

/* one of its strings changed (an ETag a 304 brought) */
static void meta_set(DCE *e, int which, const char *s)
{
	const char *v[M_N];
	char *m;
	int i;
	for (i = 0; i < M_N; i++)
		v[i] = i == which ? s : meta(e, i);
	if ((m = meta_make(v)) != NULL) {
		free(e->meta);
		e->meta = m;
	}
}

/* ---------------------------------------------------------------- the answers in memory */

static void ent_free(DCE *e)
{
	free(e->meta);
	free(e->mem);
}

static void forget_all(void)
{
	int i;
	for (i = 0; i < nent; i++)
		ent_free(&ent[i]);
	free(ent);
	ent = NULL;
	nent = cap = 0;
	pending = 0;
	cur = 0;
	at = HEAD;
	memset(listed, 0, sizeof listed);
	memset(dirty, 0, sizeof dirty);
	memset(checked, 0, sizeof checked);
	memset(fresh_piece, 0, sizeof fresh_piece);
	memset(exists, 0, sizeof exists);
	memset(pgen, 0, sizeof pgen);
}

static void close_pak(void)
{
	if (pak >= 0)
		pf_close(pak);
	pak = -1;
	pak_piece = -1;
	pak_wo = 0;
}

static void drop_at(int i)
{
	DCE *e = &ent[i];
	if (e->off < 0)
		pending -= e->len;
	else
		dirty[e->piece] = 1;
	ent_free(e);
	*e = ent[--nent];
	changed = 1;
}

static void drop(DCE *e)
{
	drop_at((int) (e - ent));
}

static DCE *add(void)
{
	DCE *e;
	if (nent == cap) {
		int c = cap ? cap * 2 : 64;
		DCE *n = realloc(ent, (size_t) c * sizeof *n);
		if (n == NULL)
			return NULL;
		ent = n;
		cap = c;
	}
	e = &ent[nent++];
	memset(e, 0, sizeof *e);
	return e;
}

/* a piece begun again: what it held forgotten, a new number, its head
   to be written */
static void begin_piece(int p)
{
	int i;
	if (pak_piece == p)
		close_pak();
	for (i = nent - 1; i >= 0; i--)
		if (ent[i].off >= 0 && ent[i].piece == p)
			drop_at(i);
	pgen[p] = new_number();
	if (!exists[p]) {
		char path[300];
		piece_path(path, sizeof path, p, "LST");
		slot_none(path);	/* its list made at once at quit */
	}
	listed[p] = 1;		/* its list: what goes in it now */
	dirty[p] = 1;
	checked[p] = 1;
	fresh_piece[p] = 1;
	changed = 1;
}

/* every file gone: the pieces, their lists, the index, the pack of before */
static void remove_files(int n)
{
	char p[300];
	int i;
	for (i = 0; i < n; i++) {
		piece_path(p, sizeof p, i, "PAK");
		pf_remove(p);
		piece_path(p, sizeof p, i, "LST");
		slot_remove(p);
	}
	path_of(p, sizeof p, "EWEB.IDX");
	slot_remove(p);
}

void dc_clear(void)
{
	close_pak();
	forget_all();
	if (dir[0])
		remove_files(pieces);
	loaded = 1;
	changed = 0;
	begin_piece(0);
	changed = 0;
	dirty[0] = 0;
}

void dc_dir(const char *d, long max)
{
	close_pak();
	forget_all();
	str_copy(dir, d, sizeof dir);
	max_size = max;
	/* a quarter of the cache at most, so that the ring going round
	   forgets a quarter at a time; 4 MB at most (deeper in a file,
	   GEMDOS takes many disk commands to find its place) */
	piece_size = max / 4 + HEAD + TAG < PIECE_MAX ? max / 4 + HEAD + TAG : PIECE_MAX;
	if (piece_size < 4096)
		piece_size = 4096;
	pieces = (int) ((max + piece_size - 1) / piece_size);
	if (pieces > PIECES_MAX)
		pieces = PIECES_MAX;
	if (pieces < 1)
		pieces = 1;
	loaded = 0;
	changed = 0;
}

/* a number at *p (the freestanding Atari build has no sscanf), and *p
   past it and the space after it; 1 when there was one */
static int read_num(char **p, long long *v)
{
	char *s = *p;
	int neg = *s == '-', n = 0;
	*v = 0;
	if (neg)
		s++;
	for (; *s >= '0' && *s <= '9' && n < 19; s++, n++)
		*v = *v * 10 + (*s - '0');
	if (neg)
		*v = -*v;
	while (*s == ' ')
		s++;
	*p = s;
	return n > 0;
}

/* the next tab-separated field of *p, cut off */
static char *field(char **p)
{
	char *s = *p, *t = strchr(s, '\t');
	if (t != NULL) {
		*t = 0;
		*p = t + 1;
	} else {
		*p = s + strlen(s);
	}
	return s;
}

/* the index, read the first time the cache is needed (6.5: once) */
static void load(void)
{
	char p[300], *d, *s;
	long len, n;
	long long v[4];
	int i, ok;
	if (loaded)
		return;
	loaded = 1;
	seed = new_number();
	if (dir[0] == 0)
		return;
	path_of(p, sizeof p, "EWEB.IDX");
	d = slot_load(p, &len);		/* its newer slot (slots.c) */
	if (d != NULL && strncmp(d, "EWDC 3 ", 7) == 0) {
		/* the one pack of before: removed, not read */
		path_of(p, sizeof p, "EWEB.PAK");
		pf_remove(p);
	}
	s = d != NULL && strncmp(d, "EWDC 4 ", 7) == 0 ? d + 7 : NULL;
	ok = s != NULL;
	for (i = 0; ok && i < 4; i++)
		ok = read_num(&s, &v[i]);
	ok = ok && *s == '\n' && v[0] >= 1 && v[0] <= PIECES_MAX && v[1] >= 0 && v[1] < v[0] &&
	     v[2] >= HEAD && v[2] <= v[3] && s + 1 + v[0] * 9 <= d + len;
	if (ok && (v[0] != pieces || v[3] != piece_size)) {
		/* the cache's size changed: begun again */
		remove_files((int) v[0]);
		ok = 0;
	}
	if (!ok) {
		free(d);
		begin_piece(0);
		changed = 0;
		dirty[0] = 0;
		return;
	}
	cur = (int) v[1];
	at = (long) v[2];
	s++;
	for (i = 0; i < pieces; i++, s += 9) {
		pgen[i] = strtoul(s, NULL, 16);
		exists[i] = pgen[i] != 0;	/* (begun in a run before: its file is there) */
	}
	for (n = (long) (s - d); n + REC <= len; n += REC) {
		DCE *e;
		const char *r = d + n;
		long off = (long) get32(r + 4) & 0xFFFFFFL, l = (long) get32(r + 8);
		int pc = (unsigned char) r[4];
		if (pc >= pieces || off < HEAD || l < 0 || off + TAG + l > piece_size)
			continue;
		if ((e = add()) == NULL)
			break;
		e->hash = get32(r);
		e->piece = (unsigned char) pc;
		e->off = off;
		e->len = l;
	}
	free(d);
}

/* a piece's list read: what the index knows of it gets the rest; what
   the list hasn't (an index and a list of different runs) is forgotten */
static void load_list(int p)
{
	char path[300], want[32], *d, *s, *nl;
	long len;
	int i;
	if (listed[p])
		return;
	listed[p] = 1;
	piece_path(path, sizeof path, p, "LST");
	d = slot_load(path, &len);
	snprintf(want, sizeof want, "EWDL 1 %08lx\n", pgen[p]);
	if (d != NULL && strncmp(d, want, strlen(want)) == 0) {
		for (s = d + strlen(want); *s; s = nl + 1) {
			long long o, n, st, fr, us, fl = 0;
			char *rest = s;
			const char *v[M_N];
			DCE *e = NULL;
			unsigned long h;
			nl = strchr(s, '\n');
			if (nl == NULL)
				break;
			*nl = 0;
			if (!read_num(&rest, &o) || !read_num(&rest, &n) || !read_num(&rest, &st) || !read_num(&rest, &fr) ||
			    !read_num(&rest, &us) || (*rest != '\t' && !read_num(&rest, &fl)) || *rest != '\t')
				continue;
			rest++;
			v[M_MIME] = field(&rest);
			v[M_CS] = field(&rest);
			v[M_SEC] = field(&rest);
			v[M_ETAG] = field(&rest);
			v[M_LM] = field(&rest);
			v[M_KEY] = field(&rest);
			v[M_URL] = field(&rest);
			if (!v[M_KEY][0])
				continue;
			if (!v[M_URL][0])
				v[M_URL] = v[M_KEY];
			h = hash_of(v[M_KEY]);
			for (i = 0; i < nent; i++)
				if (ent[i].meta == NULL && ent[i].off == (long) o && ent[i].piece == p &&
				    ent[i].len == (long) n && ent[i].hash == h) {
					e = &ent[i];
					break;
				}
			if (e == NULL || (e->meta = meta_make(v)) == NULL)
				continue;
			e->stored = st;
			e->fresh = fr;
			e->used = us;
			e->flags = (unsigned char) fl;
		}
	}
	free(d);
	for (i = nent - 1; i >= 0; i--)
		if (ent[i].off >= 0 && ent[i].piece == p && ent[i].meta == NULL)
			drop_at(i);
}

static DCE *find(const char *key)
{
	unsigned long h = hash_of(key);
	int i, unread;
	for (;;) {
		unread = -1;
		for (i = 0; i < nent; i++)
			if (ent[i].hash == h) {
				if (ent[i].meta == NULL) {
					if (listed[ent[i].piece]) {
						drop_at(i);	/* (its list read and without it) */
						i = -1;
						continue;
					}
					unread = ent[i].piece;
					break;
				}
				if (strcmp(meta(&ent[i], M_KEY), key) == 0)
					return &ent[i];
			}
		if (unread < 0)
			return NULL;
		load_list(unread);	/* (each answer of it has its strings now, or is gone) */
	}
}

/* the piece open, its head checked the first time this run: 1, or 0
   (there is none, or it isn't the index's: its answers forgotten) */
static int open_piece(int p, int write)
{
	char path[300], head[HEAD + 1], want[HEAD + 1];
	if (pak >= 0 && pak_piece == p && (write || !pak_wo))
		return 1;
	close_pak();
	piece_path(path, sizeof path, p, "PAK");
	snprintf(want, sizeof want, "EWDC%08lx\r\n\r\n", pgen[p]);
	if (fresh_piece[p]) {
		if (!write)
			return 0;
		/* begun: its head first, in place when the file is there, else
		   made at once (6.5: no look that fails) */
		pak = exists[p] ? pf_open(path, PF_UPDATE) : -1;
		if (pak < 0) {
			pf_mkdir(dir);
			pak = pf_open(path, PF_WRITE);
			pak_wo = pak >= 0;	/* (written only: opened again to read) */
		}
		if (pak >= 0 && pf_write(pak, want, HEAD) != HEAD) {
			close_pak();
			pf_remove(path);
		}
		if (pak < 0)
			return 0;
		exists[p] = 1;
		fresh_piece[p] = 0;
		pak_piece = p;
		return 1;
	}
	pak = pf_open(path, PF_UPDATE);
	if (pak >= 0 && !checked[p]) {
		head[HEAD] = 0;
		checked[p] = 1;
		if (pf_read(pak, head, HEAD) != HEAD || strcmp(head, want) != 0 ||
		    (p == cur && pf_open_size(pak) < at)) {
			pf_close(pak);
			pak = -1;
		}
	}
	if (pak < 0) {
		/* lost or another's: what the index said was in it forgotten */
		begin_piece(p);
		if (p == cur)
			at = HEAD;
		return write ? open_piece(p, 1) : 0;
	}
	pak_piece = p;
	return 1;
}

/* ---------------------------------------------------------------- HTTP's rules */

/* every header line called name, their values joined with ", " */
static void header(const char *h, const char *name, char *out, int size)
{
	size_t n = strlen(name);
	out[0] = 0;
	while (h != NULL && *h) {
		const char *nl = strchr(h, '\n'), *e = nl ? nl : h + strlen(h);
		size_t i;
		for (i = 0; i < n && h + i < e; i++)
			if ((h[i] | 0x20) != (name[i] | 0x20))
				break;
		if (i == n && h + n < e && h[n] == ':') {
			const char *v = h + n + 1;
			size_t k = strlen(out);
			while (v < e && (*v == ' ' || *v == '\t'))
				v++;
			while (e > v && (e[-1] == '\r' || e[-1] == ' '))
				e--;
			if (k > 0 && (int) k + 2 < size) {
				strcpy(out + k, ", ");
				k += 2;
			}
			if ((int) (k + (size_t) (e - v)) < size) {
				memcpy(out + k, v, (size_t) (e - v));
				out[k + (size_t) (e - v)] = 0;
			}
		}
		h = nl ? nl + 1 : NULL;
	}
}

/* the header lines have one of that name, of any length */
static int has_header(const char *h, const char *name)
{
	size_t n = strlen(name);
	while (h != NULL && *h) {
		if (!strncasecmp(h, name, n) && h[n] == ':')
			return 1;
		h = strchr(h, '\n');
		if (h != NULL)
			h++;
	}
	return 0;
}

/* a Cache-Control directive: 1 (and its number in *v when there is one) */
static int directive(const char *cc, const char *name, long *v)
{
	size_t n = strlen(name);
	const char *p = cc;
	while (*p) {
		size_t i;
		while (*p == ' ' || *p == ',')
			p++;
		for (i = 0; i < n; i++)
			if ((p[i] | 0x20) != name[i])
				break;
		if (i == n && (p[n] == 0 || p[n] == ',' || p[n] == ' ' || p[n] == '=')) {
			if (v != NULL) {
				const char *q = p + n;
				*v = -1;
				if (*q == '=') {
					q++;
					if (*q == '"')
						q++;
					if (*q >= '0' && *q <= '9')
						*v = strtol(q, NULL, 10);
				}
			}
			return 1;
		}
		while (*p && *p != ',')
			p++;
	}
	return 0;
}

/* Vary may name what EWeb asks with the same way each time (RFC 9111,
   4.1): what it sends always so (User-Agent, Accept, Accept-Language,
   Accept-Encoding), and what it never sends (Origin, Cache-Control,
   Access-Control-Request-...: absent in every request, so always the
   same; iamexpat.de's pictures say "Vary: Origin, Cache-Control", and
   none was kept, 29 Oct 2026). An answer that varies by what EWeb sends
   differently (Cookie, Referer) or by anything ("*") isn't kept, rather
   than shown for a request it wasn't the answer to. */
static int vary_ok(const char *v)
{
	static const char *const differs[] = { "*", "cookie", "referer", "authorization", NULL };
	const char *p = v;
	while (*p) {
		const char *w;
		size_t n;
		int i;
		while (*p == ' ' || *p == ',')
			p++;
		w = p;
		while (*p && *p != ',' && *p != ' ')
			p++;
		n = (size_t) (p - w);
		if (n == 0)
			continue;
		for (i = 0; differs[i]; i++)
			if (strlen(differs[i]) == n) {
				size_t k;
				for (k = 0; k < n; k++)
					if ((w[k] | 0x20) != differs[i][k])
						break;
				if (k == n)
					return 0;
			}
	}
	return 1;
}

static long long lifetime(const char *h, unsigned char *flags)
{
	char cc[256], v[128];
	long n;
	long long date, t, life = 0;
	header(h, "Cache-Control", cc, sizeof cc);
	*flags = 0;
	if (directive(cc, "no-store", NULL))
		return -1;
	header(h, "Vary", v, sizeof v);
	if (!vary_ok(v))
		return -1;
	if (directive(cc, "must-revalidate", NULL) || directive(cc, "no-cache", NULL) ||
	    directive(cc, "proxy-revalidate", NULL))
		*flags |= DCF_REVALIDATE;
	if (directive(cc, "immutable", NULL))
		*flags |= DCF_IMMUTABLE;
	header(h, "Date", v, sizeof v);
	if (!ck_date(v, &date))
		date = dc_now();
	if (directive(cc, "no-cache", NULL)) {
		life = 0;
	} else if (directive(cc, "max-age", &n) && n >= 0) {
		life = n;
	} else if (header(h, "Expires", v, sizeof v), v[0]) {
		life = ck_date(v, &t) && t > date ? t - date : 0;
	} else if (header(h, "Last-Modified", v, sizeof v), v[0] && ck_date(v, &t) && t < date) {
		life = (date - t) / 10;
		if (life > 86400)
			life = 86400;
	}
	header(h, "Age", v, sizeof v);
	if (v[0] >= '0' && v[0] <= '9')
		life -= strtol(v, NULL, 10);
	if (life > 31536000)		/* a year at most */
		life = 31536000;
	return life > 0 ? life : 0;
}

void dc_put(const char *key, const char *url, const char *headers, const char *mime, const char *charset,
	    const char *security, const char *data, long len)
{
	char etag[96], lm[48];
	const char *v[M_N];
	long long life;
	unsigned char flags;
	DCE *e;
	if (dir[0] == 0)
		return;
	load();
	e = find(key);
	if (e != NULL)
		drop(e);
	life = lifetime(headers, &flags);
	header(headers, "ETag", etag, sizeof etag);
	header(headers, "Last-Modified", lm, sizeof lm);
	/* not kept: what HTTP says not to keep, what would never be fresh
	   and can't be asked about, a page that sets a cookie (one person's;
	   a picture or a stylesheet that does is kept, as browsers' own
	   caches keep them), one with a Refresh header (the index keeps no
	   headers: from the cache it wouldn't go on), and what would take a
	   quarter of the cache (or more than a piece holds) */
	if (life < 0 || (life == 0 && !etag[0] && !lm[0]) || (has_header(headers, "Set-Cookie") && !strcmp(mime, "text/html")) ||
	    has_header(headers, "Refresh") || len > max_size / 4 || len > piece_size - HEAD - TAG ||
	    strchr(etag, '\t') || strchr(lm, '\t') || strchr(key, '\t') || strchr(url, '\t') || strchr(security, '\t') ||
	    strchr(key, '\n') || strchr(url, '\n') || strchr(etag, '\n') || strchr(lm, '\n') || strchr(security, '\n'))
		return;
	v[M_KEY] = key;
	v[M_URL] = url;
	v[M_ETAG] = etag;
	v[M_LM] = lm;
	v[M_MIME] = mime;
	v[M_CS] = charset;
	v[M_SEC] = security;
	if ((e = add()) == NULL)
		return;
	e->meta = meta_make(v);
	e->mem = malloc((size_t) len + 1);
	if (e->meta == NULL || e->mem == NULL) {
		ent_free(e);
		nent--;
		return;
	}
	memcpy(e->mem, data, (size_t) len);
	e->hash = hash_of(key);
	e->stored = e->used = dc_now();
	e->fresh = e->stored + life;
	e->flags = flags;
	e->off = -1;
	e->len = len;
	pending += len;
	changed = 1;
	/* held while the page's parts come (a write meanwhile stopped the
	   pictures' download: Erez's Falcon on yaary.music, 31 Oct 2026),
	   written at idle (dc_write_step); into the ring now only when what
	   has gathered is more than the memory may hold */
	if (pending >= hold_max)
		flush();
}

void dc_fresh(const char *key, const char *headers)
{
	char v[96];
	long long life;
	unsigned char flags;
	DCE *e;
	if (dir[0] == 0)
		return;
	load();
	e = find(key);
	if (e == NULL)
		return;
	life = lifetime(headers, &flags);
	if (life < 0) {
		drop(e);
		return;
	}
	e->stored = e->used = dc_now();
	e->fresh = e->stored + life;
	header(headers, "Cache-Control", v, sizeof v);
	if (v[0])		/* a 304 with its own Cache-Control: what it says now */
		e->flags = flags;
	header(headers, "ETag", v, sizeof v);
	if (v[0] && !strchr(v, '\t') && !strchr(v, '\n'))
		meta_set(e, M_ETAG, v);
	if (e->off >= 0)
		dirty[e->piece] = 1;	/* (its list; the index is the same) */
}

void dc_drop(const char *key)
{
	DCE *e;
	if (dir[0] == 0)
		return;
	load();
	if ((e = find(key)) != NULL)
		drop(e);
}

long dc_own_max(void)
{
	long m = max_size / 8;
	if (dir[0] == 0)
		return 0;
	return m < piece_size - HEAD - TAG ? m : piece_size - HEAD - TAG;
}

void dc_put_own(const char *key, const char *what, const char *data, long len)
{
	const char *v[M_N];
	DCE *e;
	if (dir[0] == 0)
		return;
	load();
	if ((e = find(key)) != NULL)
		drop(e);
	if (len > dc_own_max() || strchr(key, '\t') || strchr(key, '\n') || strchr(what, '\t') || strchr(what, '\n'))
		return;
	v[M_KEY] = key;
	v[M_URL] = what;
	v[M_ETAG] = v[M_LM] = v[M_CS] = v[M_SEC] = "";
	v[M_MIME] = "application/x-eweb";
	if ((e = add()) == NULL)
		return;
	e->meta = meta_make(v);
	e->mem = malloc((size_t) len + 1);
	if (e->meta == NULL || e->mem == NULL) {
		ent_free(e);
		nent--;
		return;
	}
	memcpy(e->mem, data, (size_t) len);
	e->hash = hash_of(key);
	e->stored = e->used = dc_now();
	e->fresh = e->stored + 31536000;	/* its key is its contents: never stale */
	e->flags = DCF_OWN;
	e->off = -1;
	e->len = len;
	pending += len;
	changed = 1;
	if (pending >= hold_max)
		flush();
}

int dc_has(const char *key)
{
	if (dir[0] == 0)
		return 0;
	load();
	return find(key) != NULL;
}

int dc_get(const char *key, DC_GOT *g)
{
	DCE *e;
	char *d;
	if (dir[0] == 0)
		return 0;
	load();
	e = find(key);
	if (e == NULL)
		return 0;
	d = malloc((size_t) e->len + TAG + 1);
	if (d == NULL)
		return 0;
	if (e->off < 0) {
		memcpy(d, e->mem, (size_t) e->len);
	} else if (!open_piece(e->piece, 0) || (e = find(key)) == NULL) {	/* a piece that wasn't the index's */
		free(d);
		return 0;
	} else {
		char tag[TAG];
		put32(tag, e->hash);
		put32(tag + 4, (unsigned long) e->len);
		if (pf_seek(pak, e->off) != e->off || pf_read(pak, d, e->len + TAG) != e->len + TAG ||
		    memcmp(d, tag, TAG) != 0) {
			free(d);
			drop(e);	/* the piece lost it, or it was written over (a reset) */
			return 0;
		}
		memmove(d, d + TAG, (size_t) e->len);
	}
	d[e->len] = 0;
	memset(g, 0, sizeof *g);
	g->data = d;
	g->len = e->len;
	str_copy(g->url, meta(e, M_URL), sizeof g->url);
	str_copy(g->mime, meta(e, M_MIME), sizeof g->mime);
	str_copy(g->charset, meta(e, M_CS), sizeof g->charset);
	str_copy(g->security, meta(e, M_SEC), sizeof g->security);
	e->used = dc_now();
	g->fresh = e->used < e->fresh;
	g->stale_ok = !(e->flags & DCF_REVALIDATE);
	g->immutable = (e->flags & DCF_IMMUTABLE) != 0;
	if (meta(e, M_ETAG)[0])
		snprintf(g->validate, sizeof g->validate, "If-None-Match: %.150s\r\n", meta(e, M_ETAG));
	else if (meta(e, M_LM)[0])
		snprintf(g->validate, sizeof g->validate, "If-Modified-Since: %.60s\r\n", meta(e, M_LM));
	/* (used is kept for the list, not a change: nothing reads it, the
	   ring being a ring; a list written for it alone would cost a quit) */
	return 1;
}

/* ---------------------------------------------------------------- the ring */

static long told_done, told_total;

/* a write of len bytes at the piece's place, in steps, dc_progress told
   after each */
static int write_told(const char *buf, long len)
{
	long done = 0, n;
	while (done < len) {
		n = len - done > WRITE_STEP ? WRITE_STEP : len - done;
		if (pf_write(pak, buf + done, n) != n)
			return 0;
		done += n;
		if (dc_progress != NULL)
			dc_progress(told_done + done, told_total);
	}
	told_done += len;
	return 1;
}

/* what has gathered, into the ring: as many answers as the current piece
   holds in one write, then the next piece begun (what it held forgotten)
   for the rest. A piece's list is read first, so that it can be written
   whole at quit. */
static void flush_some(long limit)
{
	DCE *pend;
	int np = 0, i, k, bad = 0;
	long wrote = 0;
	if (pending <= 0)
		return;
	/* the answers waiting taken out, in the order they came */
	for (i = 0; i < nent; i++)
		np += ent[i].off < 0;
	pend = malloc((size_t) np * sizeof *pend);
	if (pend == NULL)
		return;
	for (np = 0, i = 0; i < nent; i++)
		if (ent[i].off < 0)
			pend[np++] = ent[i];
		else
			ent[i - np] = ent[i];
	nent -= np;
	told_done = 0;
	told_total = pending + (long) np * TAG;
	pending = 0;
	changed = 1;
	k = 0;
	while (k < np && (limit <= 0 || wrote < limit)) {
		long sum = 0, o;
		int j;
		char *buf;
		if (!fresh_piece[cur])
			load_list(cur);
		for (j = k; j < np && at + sum + TAG + pend[j].len <= piece_size &&
		     (limit <= 0 || j == k || wrote + sum + TAG + pend[j].len <= limit); j++)
			sum += TAG + pend[j].len;
		if (j == k) {
			/* it doesn't fit: the next piece */
			close_pak();
			cur = (cur + 1) % pieces;
			begin_piece(cur);
			at = HEAD;
			continue;
		}
		buf = malloc((size_t) sum);
		if (buf == NULL || !open_piece(cur, 1) || pf_seek(pak, at) != at) {
			free(buf);
			bad = 1;
			break;
		}
		for (o = 0, i = k; i < j; i++) {
			put32(buf + o, pend[i].hash);
			put32(buf + o + 4, (unsigned long) pend[i].len);
			memcpy(buf + o + TAG, pend[i].mem, (size_t) pend[i].len);
			o += TAG + pend[i].len;
		}
		if (!write_told(buf, sum)) {
			free(buf);
			bad = 1;
			break;
		}
		free(buf);
		for (i = k; i < j; i++) {
			pend[i].piece = (unsigned char) cur;
			pend[i].off = at;
			at += TAG + pend[i].len;
			free(pend[i].mem);
			pend[i].mem = NULL;
		}
		dirty[cur] = 1;
		wrote += sum;
		k = j;
	}
	/* back with the rest; what the disk wouldn't take let go, what a
	   step left for the next kept */
	for (i = 0; i < np; i++) {
		if (pend[i].off < 0 && (limit <= 0 || bad)) {
			ent_free(&pend[i]);
			continue;
		}
		if (pend[i].off < 0)
			pending += pend[i].len;
		ent[nent++] = pend[i];
	}
	free(pend);
}

void dc_hold(long bytes)
{
	hold_max = bytes < (1L << 20) ? 1L << 20 : bytes;
}

/* written at idle once this much has gathered (a sixteenth of the cache,
   a megabyte at most); less stays in memory until quit, and a few pages
   touch no file (the guidelines' 6.5) */
static long idle_min(void)
{
	return max_size / 16 < (1L << 20) ? max_size / 16 : 1L << 20;
}

int dc_write_due(void)
{
	return loaded && dir[0] && pending > 0 && (writing || pending >= idle_min());
}

void dc_write_step(void)
{
	if (!dc_write_due())
		return;
	writing = 1;		/* (begun: on to the end, below the mark too) */
	flush_some(64L << 10);
	if (pending <= 0) {
		writing = 0;
		close_pak();
	}
}

void dc_job_end(void)
{
	close_pak();
}

void dc_unsaved(long *bytes, long *answers)
{
	*bytes = loaded && dir[0] ? pending : 0;
	*answers = loaded && dir[0] && changed ? nent : 0;
}

void dc_flush(void)
{
	flush();
	close_pak();
}

/* the index's write, told as the second half of dc_close's work */
static void index_told(long done, long total)
{
	dc_progress(500 + (done >> 6) * 500 / ((total >> 6) + 1), 1000);
}

/* a piece's list, whole */
static void save_list(int p)
{
	SBUF s;
	char path[300];
	int i;
	sb_init(&s);
	sb_printf(&s, "EWDL 1 %08lx\n", pgen[p]);
	for (i = 0; i < nent; i++) {
		DCE *e = &ent[i];
		if (e->off < 0 || e->piece != p || e->meta == NULL)
			continue;
		/* times as long: the Atari's printf has no %lld (2038 is far
		   enough for a cache) */
		sb_printf(&s, "%ld %ld %ld %ld %ld %d\t", e->off, e->len, (long) e->stored, (long) e->fresh,
			  (long) e->used, (int) e->flags);
		sb_adds(&s, meta(e, M_MIME));
		sb_addc(&s, '\t');
		sb_adds(&s, meta(e, M_CS));
		sb_addc(&s, '\t');
		sb_adds(&s, meta(e, M_SEC));
		sb_addc(&s, '\t');
		sb_adds(&s, meta(e, M_ETAG));
		sb_addc(&s, '\t');
		sb_adds(&s, meta(e, M_LM));
		sb_addc(&s, '\t');
		sb_adds(&s, meta(e, M_KEY));
		sb_addc(&s, '\t');
		/* an address that is its key is left out (read back as the key) */
		if (strcmp(meta(e, M_URL), meta(e, M_KEY)) != 0)
			sb_adds(&s, meta(e, M_URL));
		sb_addc(&s, '\n');
	}
	piece_path(path, sizeof path, p, "LST");
	if (s.s != NULL && slot_save(path, s.s, s.len) == 0)
		dirty[p] = 0;
	sb_free(&s);
}

void dc_close(void)
{
	SBUF s;
	char p[300];
	int i, k, n = 0;
	flush();
	close_pak();
	if (dir[0] == 0 || !loaded) {
		forget_all();
		return;
	}
	/* the lists of the pieces that changed (the one being written, and
	   one whose answer a 304 made fresh), then the index */
	for (i = 0; i < pieces; i++)
		n += dirty[i] && listed[i];
	for (i = 0, k = 0; i < pieces; i++)
		if (dirty[i] && listed[i]) {
			save_list(i);
			if (dc_progress != NULL)
				dc_progress((long) ++k * 500 / n, 1000);	/* the lists: the first half */
		}
	if (changed) {
		sb_init(&s);
		sb_printf(&s, "EWDC 4 %d %d %ld %ld\n", pieces, cur, at, piece_size);
		for (i = 0; i < pieces; i++)
			sb_printf(&s, "%08lx\n", pgen[i]);
		if (sb_room(&s, (long) nent * REC)) {
			for (i = 0; i < nent; i++) {
				char r[REC];
				DCE *e = &ent[i];
				if (e->off < 0)
					continue;
				put32(r, e->hash);
				put32(r + 4, (unsigned long) e->off);
				r[4] = (char) e->piece;	/* (the place in 3 bytes: a piece is 4 MB at most) */
				put32(r + 8, (unsigned long) e->len);
				sb_add(&s, r, REC);
			}
		}
		path_of(p, sizeof p, "EWEB.IDX");
		/* written over in place: as a new file, Erez's quit took 24 s
		   on his disk (slots.c) */
		slot_progress = dc_progress != NULL ? index_told : NULL;
		if (s.s == NULL || slot_save(p, s.s, s.len) != 0)
			dc_clear();
		slot_progress = NULL;
		sb_free(&s);
	}
	forget_all();
	loaded = 0;
	changed = 0;
}

long dc_count(long *bytes)
{
	long b = 0;
	int i;
	load();
	for (i = 0; i < nent; i++)
		b += ent[i].len;
	if (bytes != NULL)
		*bytes = b;
	return nent;
}
