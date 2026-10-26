/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* The CSS tokenizer (CSS Syntax Level 3, section 4), on UTF-8 bytes as
 * they are: only ASCII has meaning to CSS's syntax, so nothing is decoded.
 * Tokens point into the text; escapes are undone only when a token's text
 * is asked for (ec_tok_text). Numbers become 22.10 fixed point here. */
#include "ec_int.h"

/* byte classes */
#define CW	1	/* whitespace */
#define CN	2	/* name start: letters, _, non-ASCII */
#define CD	4	/* digit */
#define CH	8	/* hex digit */
#define CB	16	/* special to ec_skip_block */

static uint8_t cls[256];
static int cls_ready;

static void cls_init(void)
{
	int i;

	for (i = 0; i < 256; i++) {
		uint8_t f = 0;
		if (i == ' ' || i == '\t' || i == '\n' || i == '\r' || i == '\f')
			f |= CW;
		if ((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z') || i == '_' || i >= 0x80)
			f |= CN;
		if (i >= '0' && i <= '9')
			f |= CD | CH;
		if ((i >= 'a' && i <= 'f') || (i >= 'A' && i <= 'F'))
			f |= CH;
		if (strchr("{}()[]\"'/\\", i) && i != 0)
			f |= CB;
		cls[i] = f;
	}
	cls_ready = 1;
}

#define IS(ch, f)	(cls[(uint8_t) (ch)] & (f))
#define NAMECH(ch)	(cls[(uint8_t) (ch)] & (CN | CD) || (ch) == '-')

void ec_lex_init(ec_lex *lx, const char *s, size_t len)
{
	if (!cls_ready)
		cls_init();
	lx->p = s;
	lx->end = s + len;
}

/* a backslash at p starts a valid escape */
static inline int valid_esc(const char *p, const char *end)
{
	return p < end && *p == '\\' && (p + 1 >= end || p[1] != '\n');
}

/* the three bytes at p would start an identifier */
static int starts_ident(const char *p, const char *end)
{
	if (p >= end)
		return 0;
	if (*p == '-') {
		if (p + 1 >= end)
			return 0;
		return IS(p[1], CN) || p[1] == '-' || valid_esc(p + 1, end);
	}
	return IS(*p, CN) || valid_esc(p, end);
}

static int starts_number(const char *p, const char *end)
{
	if (p >= end)
		return 0;
	if (*p == '+' || *p == '-')
		p++;
	if (p < end && IS(*p, CD))
		return 1;
	return p + 1 < end && *p == '.' && IS(p[1], CD);
}

/* past a name (identifier characters and escapes); sets TF_ESC */
static const char *name_end(const char *p, const char *end, uint8_t *flags)
{
	while (p < end) {
		if (NAMECH(*p)) {
			p++;
		} else if (valid_esc(p, end)) {
			*flags |= TF_ESC;
			p++;
			if (p < end && IS(*p, CH)) {
				int i = 0;
				while (p < end && i < 6 && IS(*p, CH)) {
					p++;
					i++;
				}
				if (p < end && IS(*p, CW))
					p += (*p == '\r' && p + 1 < end && p[1] == '\n') ? 2 : 1;
			} else if (p < end) {
				/* one character, which may be several bytes */
				p++;
				while (p < end && ((uint8_t) *p & 0xc0) == 0x80)
					p++;
			}
		} else {
			break;
		}
	}
	return p;
}

/* a number's value: fixed point, saturating at about +-2 million */
static const char *number(const char *p, const char *end, ec_tok *t)
{
	int neg = 0, digits = 0, fdigits = 0;
	uint32_t ip = 0, fp = 0, scale = 1;

	t->flags = TF_INT;
	if (*p == '+' || *p == '-') {
		neg = *p == '-';
		t->flags |= TF_SIGN;
		p++;
	}
	while (p < end && IS(*p, CD)) {
		if (ip < 0x200000)
			ip = ip * 10 + (*p - '0');
		digits++;
		p++;
	}
	if (p + 1 < end && *p == '.' && IS(p[1], CD)) {
		t->flags &= ~TF_INT;
		p++;
		while (p < end && IS(*p, CD)) {
			if (fdigits < 6) {
				fp = fp * 10 + (*p - '0');
				scale *= 10;
				fdigits++;
			}
			p++;
		}
	}
	if (ip > 0x1fffff)
		ip = 0x1fffff;
	t->num = (ec_fx) (ip << 10) + (ec_fx) ((fp * 1024 + scale / 2) / scale);
	if (p < end && (*p == 'e' || *p == 'E')) {
		const char *q = p + 1;
		int eneg = 0, e = 0;
		if (q < end && (*q == '+' || *q == '-'))
			eneg = *q++ == '-';
		if (q < end && IS(*q, CD)) {
			t->flags &= ~TF_INT;
			while (q < end && IS(*q, CD)) {
				if (e < 100)
					e = e * 10 + (*q - '0');
				q++;
			}
			p = q;
			/* rare: done slowly, digit by digit */
			while (e-- > 0) {
				if (eneg)
					t->num = (t->num + 5) / 10;
				else if (t->num < 0x7fffffff / 10)
					t->num *= 10;
				else
					t->num = 0x7fffffff;
			}
		}
	}
	if (neg)
		t->num = -t->num;
	return p;
}

/* a string token's end; sets TF_ESC, or makes it a bad string at a newline */
static const char *string_end(const char *p, const char *end, char q, ec_tok *t)
{
	const char *s = p;

	while (p < end) {
		char ch = *p;
		if (ch == q) {
			t->len = p - s;
			return p + 1;
		}
		if (ch == '\n') {
			t->type = T_BADSTR;
			t->len = p - s;
			return p;
		}
		if (ch == '\\') {
			t->flags |= TF_ESC;
			p += 2;
			if (p > end)
				p = end;
			continue;
		}
		p++;
	}
	t->len = end - s;
	return end;
}

/* url( already read, p after any whitespace */
static const char *url_rest(const char *p, const char *end, ec_tok *t)
{
	const char *s = p, *e;

	t->type = T_URL;
	while (p < end && *p != ')') {
		if (*p == '\\' && valid_esc(p, end)) {
			t->flags |= TF_ESC;
			p += 2;
			continue;
		}
		if (*p == '"' || *p == '\'' || *p == '(' || IS(*p, CW)) {
			/* whitespace may only end it; anything else is a bad url */
			if (IS(*p, CW)) {
				e = p;
				while (p < end && IS(*p, CW))
					p++;
				if (p >= end || *p == ')') {
					t->s = s;
					t->len = e - s;
					return p < end ? p + 1 : p;
				}
			}
			t->type = T_BADURL;
			while (p < end && *p != ')') {
				if (*p == '\\' && p + 1 < end)
					p++;
				p++;
			}
			t->s = s;
			t->len = p - s;
			return p < end ? p + 1 : p;
		}
		p++;
	}
	t->s = s;
	t->len = p - s;
	return p < end ? p + 1 : p;
}

int ec_next(ec_lex *lx, ec_tok *t)
{
	const char *p = lx->p, *end = lx->end;
	char ch;

	t->flags = 0;
again:
	if (p >= end) {
		lx->p = p;
		return t->type = T_EOF;
	}
	ch = *p;
	if (IS(ch, CW)) {
		t->s = p;
		t->len = 1;
		do
			p++;
		while (p < end && IS(*p, CW));
		/* comments after whitespace are part of it */
		while (p + 1 < end && p[0] == '/' && p[1] == '*') {
			const char *q = p + 2;
			while (q + 1 < end && !(q[0] == '*' && q[1] == '/'))
				q++;
			p = q + 1 < end ? q + 2 : end;
			while (p < end && IS(*p, CW))
				p++;
		}
		lx->p = p;
		return t->type = T_WS;
	}
	t->s = p;
	if (IS(ch, CN) || ch == '\\') {
		if (ch == '\\' && !valid_esc(p, end))
			goto delim;
ident:
		{
			const char *e = name_end(p, end, &t->flags);
			t->len = e - p;
			if (e < end && *e == '(') {
				e++;
				if (t->len == 3 && (p[0] | 32) == 'u' && (p[1] | 32) == 'r' &&
						(p[2] | 32) == 'l' && !(t->flags & TF_ESC)) {
					const char *q = e;
					while (q < end && IS(*q, CW))
						q++;
					if (q >= end || (*q != '"' && *q != '\'')) {
						lx->p = url_rest(q, end, t);
						return t->type;
					}
				}
				lx->p = e;
				return t->type = T_FUNC;
			}
			lx->p = e;
			return t->type = T_IDENT;
		}
	}
	if (IS(ch, CD))
		goto num;
	switch (ch) {
	case '/':
		if (p + 1 < end && p[1] == '*') {
			const char *q = p + 2;
			while (q + 1 < end && !(q[0] == '*' && q[1] == '/'))
				q++;
			p = q + 1 < end ? q + 2 : end;
			goto again;
		}
		goto delim;
	case '"':
	case '\'':
		t->type = T_STR;
		t->s = p + 1;
		lx->p = string_end(p + 1, end, ch, t);
		return t->type;
	case '#':
		if (p + 1 < end && (NAMECH(p[1]) || valid_esc(p + 1, end))) {
			const char *e;
			if (starts_ident(p + 1, end))
				t->flags |= TF_ID;
			e = name_end(p + 1, end, &t->flags);
			t->s = p + 1;
			t->len = e - (p + 1);
			lx->p = e;
			return t->type = T_HASH;
		}
		goto delim;
	case '(': lx->p = p + 1; return t->type = T_LPAREN;
	case ')': lx->p = p + 1; return t->type = T_RPAREN;
	case '[': lx->p = p + 1; return t->type = T_LBRACK;
	case ']': lx->p = p + 1; return t->type = T_RBRACK;
	case '{': lx->p = p + 1; return t->type = T_LBRACE;
	case '}': lx->p = p + 1; return t->type = T_RBRACE;
	case ',': lx->p = p + 1; return t->type = T_COMMA;
	case ':': lx->p = p + 1; return t->type = T_COLON;
	case ';': lx->p = p + 1; return t->type = T_SEMI;
	case '+':
	case '.':
		if (starts_number(p, end))
			goto num;
		goto delim;
	case '-':
		if (starts_number(p, end))
			goto num;
		if (p + 2 < end && p[1] == '-' && p[2] == '>') {
			lx->p = p + 3;
			return t->type = T_CDC;
		}
		if (starts_ident(p, end))
			goto ident;
		goto delim;
	case '<':
		if (p + 3 < end && p[1] == '!' && p[2] == '-' && p[3] == '-') {
			lx->p = p + 4;
			return t->type = T_CDO;
		}
		goto delim;
	case '@':
		if (starts_ident(p + 1, end)) {
			const char *e = name_end(p + 1, end, &t->flags);
			t->s = p + 1;
			t->len = e - (p + 1);
			lx->p = e;
			return t->type = T_AT;
		}
		goto delim;
	default:
		goto delim;
	}
num:
	{
		const char *e = number(p, end, t);
		t->s = p;
		t->len = e - p;
		if (starts_ident(e, end)) {
			const char *u = name_end(e, end, &t->flags);
			t->u = e;
			t->ulen = u - e;
			lx->p = u;
			return t->type = T_DIM;
		}
		if (e < end && *e == '%') {
			lx->p = e + 1;
			return t->type = T_PCT;
		}
		lx->p = e;
		return t->type = T_NUM;
	}
delim:
	t->s = p;
	t->len = 1;
	lx->p = p + 1;
	return t->type = T_DELIM;
}

static size_t put_utf8(char *d, uint32_t cp)
{
	if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
		cp = 0xfffd;
	if (cp < 0x80) {
		d[0] = cp;
		return 1;
	}
	if (cp < 0x800) {
		d[0] = 0xc0 | (cp >> 6);
		d[1] = 0x80 | (cp & 63);
		return 2;
	}
	if (cp < 0x10000) {
		d[0] = 0xe0 | (cp >> 12);
		d[1] = 0x80 | ((cp >> 6) & 63);
		d[2] = 0x80 | (cp & 63);
		return 3;
	}
	d[0] = 0xf0 | (cp >> 18);
	d[1] = 0x80 | ((cp >> 12) & 63);
	d[2] = 0x80 | ((cp >> 6) & 63);
	d[3] = 0x80 | (cp & 63);
	return 4;
}

const char *ec_tok_text(const ec_tok *t, char *buf, size_t cap, size_t *len)
{
	const char *p = t->s, *end = t->s + t->len;
	size_t n = 0;

	if (!(t->flags & TF_ESC)) {
		*len = t->len;
		return t->s;
	}
	while (p < end && n + 4 < cap) {
		if (*p != '\\') {
			buf[n++] = *p++;
			continue;
		}
		p++;
		if (p >= end)
			break;
		if (*p == '\n') {		/* a string's line continuation */
			p++;
			continue;
		}
		if (IS(*p, CH)) {
			uint32_t cp = 0;
			int i = 0;
			while (p < end && i < 6 && IS(*p, CH)) {
				char h = *p++;
				cp = cp * 16 + (h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
				i++;
			}
			if (p < end && IS(*p, CW))
				p++;
			n += put_utf8(buf + n, cp);
		} else {
			buf[n++] = *p++;
		}
	}
	*len = n;
	return buf;
}

int ec_tok_is(const ec_tok *t, const char *word)
{
	size_t i;

	for (i = 0; i < t->len; i++)
		if (word[i] == 0 || (t->s[i] | (t->s[i] >= 'A' && t->s[i] <= 'Z' ? 32 : 0)) != word[i])
			return 0;
	return word[i] == 0 && !(t->flags & TF_ESC);
}

/* (ec_skip_block_closed: *closed says whether its closer came before
   end; a sheet read as it comes stops before a rule not all here yet) */
static int skip_closed;

const char *ec_skip_block_closed(const char *p, const char *end, char open, int *closed)
{
	const char *r = ec_skip_block(p, end, open);
	*closed = skip_closed;
	return r;
}

const char *ec_skip_block(const char *p, const char *end, char open)
{
	char stack[64];
	int depth = 0;
	const char *start = p;

	if (!cls_ready)
		cls_init();
	stack[0] = open == '{' ? '}' : open == '[' ? ']' : ')';
	depth = 1;
	while (p < end) {
		char ch = *p;
		if (EC_LIKELY(!IS(ch, CB))) {
			p++;
			continue;
		}
		switch (ch) {
		case '(': {
			/* an unquoted url( is one token to its ): braces in it are
			 * not blocks (CSS Syntax 3, 4.3.6) */
			const char *q = p + 1;
			if (p - 3 >= start && ((p[-3] | 32) == 'u') && ((p[-2] | 32) == 'r') &&
					((p[-1] | 32) == 'l') && (p - 4 < start || !NAMECH(p[-4]))) {
				while (q < end && IS(*q, CW))
					q++;
				if (q < end && *q != '"' && *q != '\'') {
					while (q < end && *q != ')') {
						if (*q == '\\' && q + 1 < end)
							q++;
						q++;
					}
					p = q < end ? q + 1 : end;
					break;
				}
			}
		}
			/* fall through */
		case '{': case '[':
			if (depth < 64)
				stack[depth] = ch == '{' ? '}' : ch == '[' ? ']' : ')';
			depth++;
			p++;
			break;
		case '}': case ']': case ')':
			p++;
			if (depth > 64) {
				depth--;
			} else if (stack[depth - 1] == ch) {
				/* any other closer is only a token (CSS Syntax 5.4.8) */
				if (--depth == 0) {
					skip_closed = 1;
					return p;
				}
			}
			break;
		case '"': case '\'':
			p++;
			while (p < end && *p != ch && *p != '\n') {
				if (*p == '\\' && p + 1 < end)
					p++;
				p++;
			}
			if (p < end && *p == ch)
				p++;
			break;
		case '/':
			if (p + 1 < end && p[1] == '*') {
				p += 2;
				while (p + 1 < end && !(p[0] == '*' && p[1] == '/'))
					p++;
				p = p + 1 < end ? p + 2 : end;
			} else {
				p++;
			}
			break;
		case '\\':
			p += 2;
			break;
		default:
			p++;
		}
	}
	skip_closed = 0;
	return end;
}
