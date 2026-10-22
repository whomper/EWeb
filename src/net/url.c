/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * url.c - the WHATWG URL Standard's basic URL parser, for http and https
 * (url.h). The parser's states are followed in order, but written as one
 * pass that serialises as it goes: the URL is built in u->href, and the
 * offsets of its parts are kept next to it.
 */
#include <string.h>
#include "url.h"

typedef struct {
	char *s;
	int n, max, bad;		/* max: the size of s */
} OUT;

static void put(OUT *o, char c)
{
	if (o->n < o->max - 1)
		o->s[o->n++] = c;
	else
		o->bad = 1;
}

static void put_n(OUT *o, const char *s, int n)
{
	while (n-- > 0)
		put(o, *s++);
}

static const char hexd[] = "0123456789ABCDEF";

/* the percent-encode sets: each is the one before it and more */
enum { SET_FRAG, SET_QUERY, SET_SQUERY, SET_PATH, SET_USER };

static int encodes(int set, unsigned char c)
{
	if (c < 0x20 || c > 0x7e)
		return 1;		/* C0 controls, DEL and beyond ASCII */
	switch (set) {
	case SET_USER:
		if (strchr("/:;=@[\\]^|", c))
			return 1;
		/* fall through */
	case SET_PATH:
		if (strchr("?^`{}", c))
			return 1;
		/* fall through */
	case SET_SQUERY:
		if (set != SET_PATH && set != SET_USER && c == '\'')
			return 1;
		/* fall through */
	case SET_QUERY:
		if (c == '#')
			return 1;
		return strchr(" \"<>", c) != 0;
	default:	/* SET_FRAG */
		return strchr(" \"<>`", c) != 0;
	}
}

static void put_enc(OUT *o, const char *s, int n, int set)
{
	while (n-- > 0) {
		unsigned char c = (unsigned char)*s++;
		if (encodes(set, c)) {
			put(o, '%');
			put(o, hexd[c >> 4]);
			put(o, hexd[c & 15]);
		} else {
			put(o, (char)c);
		}
	}
}

/* the query's encoding for this parse (url_parse_enc), NULL UTF-8 */
static const URL_ENC *q_enc;

/* the query: percent-encoded after encoding (the URL Standard's query
   state, 4.4, and 1.3), in q_enc when there is one: its byte for each
   character, or "&#N;" percent-encoded for one it lacks */
static void put_query(OUT *o, const char *s, int n)
{
	if (q_enc == NULL) {
		put_enc(o, s, n, SET_SQUERY);
		return;
	}
	while (n > 0) {
		const unsigned char *b = (const unsigned char *)s;
		unsigned long c = b[0];
		int k = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc2 ? 2 : 1, i;
		if (c < 0x80 || k == 1 || c > 0xf4 || k > n) {
			put_enc(o, s, 1, SET_SQUERY);	/* ASCII, or not UTF-8: the byte */
			s++;
			n--;
			continue;
		}
		c &= 0x3f >> (k - 1);
		for (i = 1; i < k && (b[i] & 0xc0) == 0x80; i++)
			c = c << 6 | (b[i] & 0x3f);
		if (i < k) {
			put_enc(o, s, 1, SET_SQUERY);
			s++;
			n--;
			continue;
		}
		s += k;
		n -= k;
		/* its byte (U+FFFD has none: the table has it for each byte
		   the encoding leaves out) */
		i = 128;
		if (c != 0xfffd)
			for (i = 0; i < 128 && q_enc->hi[i] != c; i++)
				;
		if (i < 128) {
			put(o, '%');
			put(o, hexd[(0x80 + i) >> 4]);
			put(o, hexd[i & 15]);
		} else {
			char d[12];
			int l = 0;
			do
				d[l++] = (char)('0' + c % 10);
			while ((c /= 10) != 0);
			put_n(o, "%26%23", 6);
			while (l > 0)
				put(o, d[--l]);
			put_n(o, "%3B", 3);
		}
	}
}

static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_hex(int c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int hexval(int c) { return is_digit(c) ? c - '0' : (c | 0x20) - 'a' + 10; }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int is_slash(int c) { return c == '/' || c == '\\'; }

/* ---------------- hosts ---------------- */

/* an IPv4 part: decimal, 0x hex or 0 octal; -1 if it isn't a number,
   and values beyond 32 bits come out as 0x100000000 (too big) */
static long long ipv4_number(const char *s, int n)
{
	int radix = 10;
	long long v = 0;
	if (!n)
		return -1;
	if (n >= 2 && s[0] == '0' && (s[1] | 0x20) == 'x') {
		radix = 16;
		s += 2;
		n -= 2;
	} else if (n >= 2 && s[0] == '0') {
		radix = 8;
		s++;
		n--;
	}
	while (n-- > 0) {
		int c = *s++, d;
		if (radix == 16 ? !is_hex(c) : !is_digit(c) || (radix == 8 && c > '7'))
			return -1;
		d = radix == 16 ? hexval(c) : c - '0';
		v = v * radix + d;
		if (v > 0xffffffffLL)
			v = 0x100000000LL;
	}
	return v;
}

/* "ends in a number": the last label (a trailing dot aside) is digits,
   or 0x and hex digits */
static int ends_in_number(const char *h, int n)
{
	int e = n, s;
	if (e && h[e - 1] == '.')
		e--;
	for (s = e; s > 0 && h[s - 1] != '.'; s--)
		;
	if (s == e)
		return 0;
	{
		int i, digits = 1;
		for (i = s; i < e; i++)
			if (!is_digit(h[i]))
				digits = 0;
		if (digits)
			return 1;
	}
	if (e - s >= 2 && h[s] == '0' && (h[s + 1] | 0x20) == 'x') {
		int i;
		for (i = s + 2; i < e; i++)
			if (!is_hex(h[i]))
				return 0;
		return 1;
	}
	return 0;
}

static int ipv4(OUT *o, const char *h, int n)
{
	long long part[4];
	int np = 0, s = 0, i;
	unsigned long a;
	if (n && h[n - 1] == '.')
		n--;			/* a trailing dot is allowed */
	for (i = 0; i <= n; i++) {
		if (i == n || h[i] == '.') {
			if (np == 4)
				return 0;
			part[np] = ipv4_number(h + s, i - s);
			if (part[np] < 0)
				return 0;
			np++;
			s = i + 1;
		}
	}
	for (i = 0; i < np - 1; i++)
		if (part[i] > 255)
			return 0;
	if (part[np - 1] >= (1LL << (8 * (5 - np))))
		return 0;
	a = (unsigned long)part[np - 1];
	for (i = 0; i < np - 1; i++)
		a += (unsigned long)part[i] << (8 * (3 - i));
	for (i = 3; i >= 0; i--) {
		unsigned v = (a >> (8 * i)) & 255;
		if (v >= 100)
			put(o, (char)('0' + v / 100));
		if (v >= 10)
			put(o, (char)('0' + v / 10 % 10));
		put(o, (char)('0' + v % 10));
		if (i)
			put(o, '.');
	}
	return 1;
}

/* the IPv6 parser of the standard, and its serialiser (the longest run
   of zeros, if two or more, written as "::") */
static int ipv6(OUT *o, const char *s, int n)
{
	unsigned short a[8];
	int piece = 0, compress = -1, i = 0, k;
	memset(a, 0, sizeof(a));
	if (n && s[0] == ':') {
		if (n < 2 || s[1] != ':')
			return 0;
		i = 2;
		compress = ++piece;
	}
	while (i < n) {
		unsigned v = 0, len = 0;
		if (piece == 8)
			return 0;
		if (s[i] == ':') {
			if (compress >= 0)
				return 0;
			i++;
			compress = ++piece;
			continue;
		}
		while (len < 4 && i < n && is_hex(s[i])) {
			v = v * 16 + hexval(s[i]);
			i++;
			len++;
		}
		if (i < n && s[i] == '.') {
			/* an IPv4 address in the last two pieces */
			int seen = 0;
			if (!len)
				return 0;
			i -= len;
			if (piece > 6)
				return 0;
			while (i < n) {
				int val = -1;
				if (seen > 0) {
					if (s[i] == '.' && seen < 4)
						i++;
					else
						return 0;
				}
				if (i >= n || !is_digit(s[i]))
					return 0;
				while (i < n && is_digit(s[i])) {
					int d = s[i] - '0';
					if (val == -1)
						val = d;
					else if (val == 0)
						return 0;
					else
						val = val * 10 + d;
					if (val > 255)
						return 0;
					i++;
				}
				a[piece] = (unsigned short)(a[piece] * 256 + val);
				seen++;
				if (seen == 2 || seen == 4)
					piece++;
			}
			if (seen != 4)
				return 0;
			break;
		}
		if (i < n && s[i] == ':') {
			i++;
			if (i >= n)
				return 0;
		} else if (i < n) {
			return 0;
		}
		a[piece++] = (unsigned short)v;
	}
	if (compress >= 0) {
		int swaps = piece - compress;
		piece = 7;
		while (piece != 0 && swaps > 0) {
			unsigned short t = a[piece];
			a[piece] = a[compress + swaps - 1];
			a[compress + swaps - 1] = t;
			piece--;
			swaps--;
		}
	} else if (piece != 8) {
		return 0;
	}
	/* serialise */
	{
		int best = -1, bestlen = 1, run = -1;
		for (k = 0; k <= 8; k++) {
			if (k < 8 && !a[k]) {
				if (run < 0)
					run = k;
			} else if (run >= 0) {
				if (k - run > bestlen) {
					best = run;
					bestlen = k - run;
				}
				run = -1;
			}
		}
		put(o, '[');
		for (k = 0; k < 8; k++) {
			if (k == best) {
				put_n(o, k ? ":" : "::", k ? 1 : 2);
				k += bestlen - 1;
				continue;
			}
			{
				char hx[5];
				int m = 0, sh;
				for (sh = 12; sh >= 0; sh -= 4)
					if (m || (a[k] >> sh) & 15 || !sh)
						hx[m++] = "0123456789abcdef"[(a[k] >> sh) & 15];
				put_n(o, hx, m);
			}
			if (k < 7)
				put(o, ':');
		}
		put(o, ']');
	}
	return 1;
}

/* ---------------- names that aren't ASCII (IDNA) ---------------- */

/* UTS #46's mapping, the part of it pages meet: what is ignored (soft
   hyphen, zero-width characters), the other full stops, full-width
   ASCII, the other spaces (refused, as spaces are), lower case for Latin,
   Greek and Cyrillic, and what is disallowed (C1 controls, U+FFFD, the
   noncharacters). Hebrew, Arabic and the CJK scripts have no case and map
   to themselves. 0: ignored, ~0: disallowed */
static unsigned long idna_map(unsigned long c)
{
	if (c == 0xad || c == 0x34f || (c >= 0x200b && c <= 0x200d) || c == 0x2060 || c == 0xfeff ||
	    (c >= 0xfe00 && c <= 0xfe0f) || c == 0x180b || c == 0x180c || c == 0x180d)
		return 0;
	if (c == 0x3002 || c == 0xff0e || c == 0xff61)
		return '.';
	if (c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x202f || c == 0x205f ||
	    c == 0x3000)
		return ' ';			/* spaces: refused below */
	if ((c >= 0x80 && c <= 0x9f) || (c >= 0xfdd0 && c <= 0xfdef) || (c & 0xfffe) == 0xfffe ||
	    c == 0xfffd || c > 0x10ffff)
		return ~0UL;			/* disallowed */
	if (c >= 0xff01 && c <= 0xff5e)
		c -= 0xfee0;			/* full-width ASCII */
	if (c < 0x80)
		return (unsigned long)lower((int)c);
	if ((c >= 0xc0 && c <= 0xde && c != 0xd7) || (c >= 0x391 && c <= 0x3ab && c != 0x3a2) ||
	    (c >= 0x410 && c <= 0x42f))
		return c + 0x20;
	if (c >= 0x400 && c <= 0x40f)
		return c + 0x50;
	if ((c >= 0x100 && c <= 0x137) || (c >= 0x14a && c <= 0x177) || (c >= 0x460 && c <= 0x481) ||
	    (c >= 0x48a && c <= 0x4bf) || (c >= 0x4d0 && c <= 0x52f))
		return c | 1;
	if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17e))
		return (c & 1) ? c + 1 : c;
	if (c == 0x178)
		return 0xff;
	return c;
}

/* RFC 3492: a label of code points as "xn--" and ASCII */
static int punycode(OUT *o, const unsigned long *cp, int n)
{
	unsigned long dn = 128, delta = 0, bias = 72, h, b = 0;
	int i;
	put_n(o, "xn--", 4);
	for (i = 0; i < n; i++)
		if (cp[i] < 0x80) {
			put(o, (char)cp[i]);
			b++;
		}
	if (b)
		put(o, '-');
	for (h = b; h < (unsigned long)n;) {
		unsigned long m = 0x10ffff + 1;
		for (i = 0; i < n; i++)
			if (cp[i] >= dn && cp[i] < m)
				m = cp[i];
		if ((m - dn) > (0xffffffffUL - delta) / (h + 1))
			return 0;
		delta += (m - dn) * (h + 1);
		dn = m;
		for (i = 0; i < n; i++) {
			if (cp[i] < dn && ++delta == 0)
				return 0;
			if (cp[i] == dn) {
				unsigned long q = delta, k;
				for (k = 36;; k += 36) {
					unsigned long t = k <= bias ? 1 : k >= bias + 26 ? 26 : k - bias, d;
					if (q < t)
						break;
					d = t + (q - t) % (36 - t);
					put(o, (char)(d < 26 ? 'a' + d : '0' + d - 26));
					q = (q - t) / (36 - t);
				}
				put(o, (char)(q < 26 ? 'a' + q : '0' + q - 26));
				/* adapt the bias */
				{
					unsigned long dl = h == b ? delta / 700 : delta / 2;
					dl += dl / (h + 1);
					for (k = 0; dl > 455; k += 36)
						dl /= 35;
					bias = k + 36 * dl / (dl + 38);
				}
				delta = 0;
				h++;
			}
		}
		delta++;
		dn++;
	}
	return 1;
}

/* the name as UTF-8 (percent-decoded), mapped, each label that isn't
   ASCII in punycode, into d: its length, or -1 */
static int idna(const unsigned char *s, int n, char *d, int size)
{
	static unsigned long cp[256];
	OUT o;
	int m = 0, i = 0, start = 0;
	while (i < n) {
		unsigned long c = s[i++];
		int more = c < 0x80 ? 0 : c >= 0xc2 && c < 0xe0 ? 1 : c >= 0xe0 && c < 0xf0 ? 2 : c >= 0xf0 && c < 0xf5 ? 3 : -1;
		if (more < 0)
			return -1;
		c &= more == 0 ? 0x7f : more == 1 ? 0x1f : more == 2 ? 0x0f : 0x07;
		while (more--) {
			if (i >= n || (s[i] & 0xc0) != 0x80)
				return -1;
			c = (c << 6) | (s[i++] & 0x3f);
		}
		if (c >= 0xd800 && c <= 0xdfff)
			return -1;
		c = idna_map(c);
		if (c == ~0UL)
			return -1;
		if (c && m < (int)(sizeof(cp) / sizeof(cp[0])))
			cp[m++] = c;
		else if (c)
			return -1;
	}
	o.s = d;
	o.n = 0;
	o.max = size;
	o.bad = 0;
	for (i = 0; i <= m; i++) {
		if (i == m || cp[i] == '.') {
			int j, ascii = 1;
			for (j = start; j < i; j++)
				if (cp[j] >= 0x80)
					ascii = 0;
			if (ascii) {
				for (j = start; j < i; j++)
					put(&o, (char)cp[j]);
			} else if (!punycode(&o, cp + start, i - start)) {
				return -1;
			}
			if (i < m)
				put(&o, '.');
			start = i + 1;
		}
	}
	if (o.bad || o.n >= size)
		return -1;
	return o.n;
}

/* a host of http or https: percent-decoded, lower case, checked; an
   address in its usual form */
static int host(OUT *o, const char *h, int n)
{
	unsigned char raw[256];
	char d[256];
	int m = 0, i, wide = 0;
	if (n && h[0] == '[') {
		if (h[n - 1] != ']')
			return 0;
		return ipv6(o, h + 1, n - 2);
	}
	if (!n || n >= (int)sizeof(raw))
		return 0;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)h[i];
		if (c == '%' && i + 2 < n && is_hex(h[i + 1]) && is_hex(h[i + 2])) {
			c = (unsigned char)(hexval(h[i + 1]) * 16 + hexval(h[i + 2]));
			i += 2;
		}
		wide |= c >= 0x80;
		raw[m++] = c;
	}
	if (wide) {
		m = idna(raw, m, d, sizeof(d));
		if (m <= 0)
			return 0;
	} else {
		for (i = 0; i < m; i++)
			d[i] = (char)lower(raw[i]);
	}
	for (i = 0; i < m; i++) {
		unsigned char c = (unsigned char)d[i];
		if (c <= 0x20 || c == 0x7f || strchr("#%/:<>?@[\\]^|", c))
			return 0;		/* the forbidden domain code points */
	}
	if (ends_in_number(d, m))
		return ipv4(o, d, m);
	put_n(o, d, m);
	return 1;
}

/* ---------------- the parts after the host ---------------- */

static int single_dot(const char *s, int n)
{
	return (n == 1 && s[0] == '.') || (n == 3 && s[0] == '%' && s[1] == '2' && (s[2] | 0x20) == 'e');
}

static int double_dot(const char *s, int n)
{
	if (n == 2)
		return s[0] == '.' && s[1] == '.';
	if (n == 4)
		return (s[0] == '.' && single_dot(s + 1, 3)) || (single_dot(s, 3) && s[3] == '.');
	if (n == 6)
		return single_dot(s, 3) && single_dot(s + 3, 3);
	return 0;
}

/* drop the last segment of the path written from start */
static void shorten(OUT *o, int start)
{
	while (o->n > start && o->s[o->n - 1] != '/')
		o->n--;
	if (o->n > start)
		o->n--;
}

/* the path, from p to '?', '#' or the end, after what is already
   written from start; returns where it stopped */
static const char *path(OUT *o, int start, const char *p)
{
	for (;;) {
		const char *e = p;
		int mark = o->n, len;
		while (*e && !is_slash(*e) && *e != '?' && *e != '#')
			e++;
		/* the segment, encoded, decides what it is */
		put(o, '/');
		put_enc(o, p, (int)(e - p), SET_PATH);
		len = o->n - mark - 1;
		if (double_dot(o->s + mark + 1, len)) {
			o->n = mark;
			shorten(o, start);
			if (!is_slash(*e))
				put(o, '/');
		} else if (single_dot(o->s + mark + 1, len)) {
			o->n = mark;
			if (!is_slash(*e))
				put(o, '/');
		}
		if (!is_slash(*e))
			return e;
		p = e + 1;
	}
}

static void finish(URL *u, OUT *o, const char *p)
{
	u->query = (unsigned short)o->n;
	if (*p == '?') {
		const char *e = strchr(p, '#');
		if (!e)
			e = p + strlen(p);
		put(o, '?');
		put_query(o, p + 1, (int)(e - p - 1));
		p = e;
	}
	u->frag = (unsigned short)o->n;
	if (*p == '#') {
		put(o, '#');
		put_enc(o, p + 1, (int)strlen(p + 1), SET_FRAG);
	}
}

/* the authority (userinfo, host, port) at p, then the rest */
static int authority(URL *u, OUT *o, const char *p)
{
	const char *e = p, *at = 0, *hp, *colon = 0, *q;
	int br = 0;
	unsigned long port = 0;
	while (*e && !is_slash(*e) && *e != '?' && *e != '#')
		e++;
	for (q = p; q < e; q++)
		if (*q == '@')
			at = q;
	if (at) {
		const char *c = memchr(p, ':', at - p);
		const char *ue = c ? c : at;
		if (at + 1 == e)
			return 0;		/* credentials and no host */
		if (ue > p || (c && at > c + 1)) {
			put_enc(o, p, (int)(ue - p), SET_USER);
			if (c && at > c + 1) {
				put(o, ':');
				put_enc(o, c + 1, (int)(at - c - 1), SET_USER);
			}
			put(o, '@');
		}
		hp = at + 1;
	} else {
		hp = p;
	}
	for (q = hp; q < e; q++) {
		if (*q == '[')
			br = 1;
		else if (*q == ']')
			br = 0;
		else if (*q == ':' && !br) {
			colon = q;
			break;
		}
	}
	u->host = (unsigned short)o->n;
	if (!host(o, hp, (int)((colon ? colon : e) - hp)))
		return 0;
	u->hostlen = (unsigned short)(o->n - u->host);
	u->port = u->tls ? 443 : 80;
	if (colon && colon + 1 < e) {
		for (q = colon + 1; q < e; q++) {
			if (!is_digit(*q))
				return 0;
			port = port * 10 + (unsigned long)(*q - '0');
			if (port > 65535)
				return 0;
		}
		if (port != u->port) {
			char d[6];
			int m = 0;
			unsigned long v = port;
			do
				d[m++] = (char)('0' + v % 10);
			while (v /= 10);
			put(o, ':');
			while (m)
				put(o, d[--m]);
			u->port = (unsigned short)port;
		}
	}
	u->path = (unsigned short)o->n;
	if (is_slash(*e))
		e++;
	finish(u, o, path(o, u->path, e));
	return 1;
}

/* ---------------- the parser ---------------- */

static int parse(URL *u, const char *in, const URL *base)
{
	OUT o;
	const char *p = in;
	int scheme = 0;
	o.s = u->href;
	o.n = 0;
	o.max = URL_MAX;
	o.bad = 0;
	u->host = u->hostlen = 0;
	u->special = u->tls = 0;
	/* the scheme */
	if (is_alpha(*p)) {
		const char *q = p;
		while (is_alpha(*q) || is_digit(*q) || *q == '+' || *q == '-' || *q == '.')
			q++;
		if (*q == ':')
			scheme = (int)(q - p);
	}
	if (scheme) {
		int i;
		for (i = 0; i < scheme; i++)
			put(&o, (char)lower(p[i]));
		u->scheme = (unsigned short)scheme;
		u->special = (scheme == 4 && !strncmp(o.s, "http", 4)) || (scheme == 5 && !strncmp(o.s, "https", 5));
		u->tls = scheme == 5 && u->special;
		p += scheme + 1;
		put(&o, ':');
		if (!u->special) {
			/* kept as it is, for the browser to recognise */
			const char *h = strchr(p, '#');
			put_n(&o, p, (int)strlen(p));
			u->path = (unsigned short)(scheme + 1);
			u->query = u->frag = (unsigned short)(h ? scheme + 1 + (h - p) : o.n);
			u->port = 0;
			goto done;
		}
		if (!base || !base->special || base->tls != u->tls) {
			while (is_slash(*p))
				p++;
			put_n(&o, "//", 2);
			if (!authority(u, &o, p))
				return 0;
			goto done;
		}
		/* "http:foo" (and "http:/foo") against an http base: relative */
	} else {
		if (!base)
			return 0;
		if (!base->special) {
			/* only "#..." on an address like mailto: */
			if (*p != '#')
				return 0;
			put_n(&o, base->href, base->frag);
			u->scheme = base->scheme;
			u->path = base->path;
			u->query = base->query;
			u->frag = (unsigned short)o.n;
			put(&o, '#');
			put_enc(&o, p + 1, (int)strlen(p + 1), SET_FRAG);
			u->port = 0;
			goto done;
		}
		u->scheme = base->scheme;
		u->special = 1;
		u->tls = base->tls;
		put_n(&o, base->href, base->scheme + 1);
	}
	/* relative to base, whose scheme this is */
	if (is_slash(p[0]) && is_slash(p[1])) {
		while (is_slash(*p))
			p++;
		put_n(&o, "//", 2);
		if (!authority(u, &o, p))
			return 0;
		goto done;
	}
	/* the base's authority */
	put_n(&o, base->href + o.n, base->path - o.n);
	u->host = base->host;
	u->hostlen = base->hostlen;
	u->port = base->port;
	u->path = base->path;
	if (is_slash(*p)) {
		finish(u, &o, path(&o, u->path, p + 1));
	} else if (*p == '?') {
		put_n(&o, base->href + base->path, base->query - base->path);
		finish(u, &o, p);
	} else if (*p == '#') {
		put_n(&o, base->href + base->path, base->frag - base->path);
		u->query = base->query;
		u->frag = (unsigned short)o.n;
		put(&o, '#');
		put_enc(&o, p + 1, (int)strlen(p + 1), SET_FRAG);
	} else if (!*p) {
		put_n(&o, base->href + base->path, base->frag - base->path);
		u->query = base->query;
		u->frag = (unsigned short)o.n;
	} else {
		/* the base's path without its last segment, then p */
		put_n(&o, base->href + base->path, base->query - base->path);
		shorten(&o, u->path);
		finish(u, &o, path(&o, u->path, p));
	}
done:
	if (o.bad)
		return 0;
	o.s[o.n] = 0;
	return 1;
}

int url_parse(URL *u, const char *s, const URL *base)
{
	return url_parse_enc(u, s, base, 0);
}

int url_parse_enc(URL *u, const char *s, const URL *base, const URL_ENC *enc)
{
	static char in[URL_MAX];
	static URL b;
	int n = 0, ok;
	/* the spaces and controls around it go, and tabs and newlines in it */
	while (*s && (unsigned char)*s <= 0x20)
		s++;
	for (; *s; s++) {
		if (*s == '\t' || *s == '\n' || *s == '\r')
			continue;
		if (n == URL_MAX - 1)
			return 0;
		in[n++] = *s;
	}
	while (n && (unsigned char)in[n - 1] <= 0x20)
		n--;
	in[n] = 0;
	if (base == u) {
		b = *base;
		base = &b;
	}
	q_enc = enc;
	ok = parse(u, in, base);
	q_enc = 0;
	if (!ok)
		u->href[0] = 0;
	return ok;
}

void url_target(const URL *u, char *out, int size)
{
	int n = u->frag - u->path;
	if (n >= size)
		n = size - 1;
	memcpy(out, u->href + u->path, n);
	out[n] = 0;
}

void url_hostport(const URL *u, char *out, int size)
{
	int n = u->path - u->host;
	if (n >= size)
		n = size - 1;
	memcpy(out, u->href + u->host, n);
	out[n] = 0;
}

void url_host(const URL *u, char *out, int size)
{
	const char *h = u->href + u->host;
	int n = u->hostlen;
	if (n >= 2 && h[0] == '[') {
		h++;
		n -= 2;
	}
	if (n >= size)
		n = size - 1;
	memcpy(out, h, n);
	out[n] = 0;
}
