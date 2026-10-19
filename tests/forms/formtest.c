/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * formtest - EWeb's forms (src/doc/form.c) against cases worked out
 * from the HTML Standard (form submission: constructing the entry list,
 * the urlencoded, multipart and plain text encodings, implicit
 * submission) and what browsers send: formtest CASES.TXT. Each case is
 *
 *   case NAME
 *   html TEXT             the page (more html lines go on, with \n)
 *   charset NAME          what the page was read as (utf-8)
 *   set|type NAME TEXT    a control's value set, or typed at its caret
 *   back NAME N           Backspace N times
 *   check NAME [VALUE]    a checkbox or radio button clicked
 *   choose NAME I         a select's option I chosen
 *   reset                 the first form reset
 *   label NAME TEXT       what the control shows must be TEXT
 *   submit BUTTON [X Y]   sent by the button of that name ("-": Return
 *                         in the first field of the form)
 *   expect none | get|post ACTION [CTYPE]
 *   body TEXT             what it sends (\r \n \\ \x41 escapes; a
 *                         multipart boundary as {B})
 *
 * Prints each failure, then how many passed, on stderr (Hubbub's
 * debugging build prints its states on stdout).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doc.h"
#include "form.h"

static int failed, passed;

static void unescape(const char *s, char *out, size_t size)
{
	size_t o = 0;
	while (*s && o < size - 1) {
		if (*s == '\\' && s[1]) {
			s++;
			if (*s == 'r')
				out[o++] = '\r';
			else if (*s == 'n')
				out[o++] = '\n';
			else if (*s == 'x' && s[1] && s[2]) {
				char h[3] = { s[1], s[2], 0 };
				out[o++] = (char) strtol(h, NULL, 16);
				s += 2;
			} else
				out[o++] = *s;
			s++;
		} else {
			out[o++] = *s++;
		}
	}
	out[o] = 0;
}

/* the control of that name (and value, for radio buttons) */
static fctl *named(doc *d, const char *name, const char *value)
{
	fctl *c;
	for (c = d->ctls; c != NULL; c = c->next) {
		const char *n = dn_attr(c->n, "name"), *v = dn_attr(c->n, "value");
		if (n != NULL && strcmp(n, name) == 0 && (value == NULL || (v != NULL && strcmp(v, value) == 0)))
			return c;
	}
	return NULL;
}

static void fail(const char *name, const char *what, const char *want, const char *got)
{
	fprintf(stderr, "FAIL %s: %s\n  want: ", name, what);
	for (; *want; want++)
		fprintf(stderr, *want == '\r' ? "\\r" : *want == '\n' ? "\\n" : "%c", *want);
	fprintf(stderr, "\n  got:  ");
	for (; *got; got++)
		fprintf(stderr, *got == '\r' ? "\\r" : *got == '\n' ? "\\n" : "%c", *got);
	fprintf(stderr, "\n");
	failed++;
}

int main(int argc, char **argv)
{
	FILE *f;
	static char line[8192], html[65536], name[128], cs[40], buf[8192], want[8192];
	doc *d = NULL;
	FSUBMIT s;
	int have = 0, ok = 1;
	ec_media m;

	if (argc != 2 || (f = fopen(argv[1], "r")) == NULL) {
		fprintf(stderr, "formtest CASES.TXT\n");
		return 2;
	}
	memset(&m, 0, sizeof m);
	m.width = 640;
	m.height = 400;
	memset(&s, 0, sizeof s);
	html[0] = 0;
	strcpy(cs, "utf-8");
	while (fgets(line, sizeof line, f) != NULL) {
		char *arg;
		line[strcspn(line, "\n")] = 0;
		if (line[0] == '#' || line[0] == 0)
			continue;
		arg = strchr(line, ' ');
		arg = arg ? arg + 1 : line + strlen(line);
		if (strncmp(line, "case ", 5) == 0) {
			if (d != NULL) {
				if (ok)
					passed++;
				doc_free(d);
				d = NULL;
			}
			form_submit_free(&s);
			have = 0;
			ok = 1;
			snprintf(name, sizeof name, "%s", arg);
			html[0] = 0;
			strcpy(cs, "utf-8");
			continue;
		}
		if (strncmp(line, "html ", 5) == 0) {
			if (html[0])
				strcat(html, "\n");
			strncat(html, arg, sizeof html - strlen(html) - 2);
			continue;
		}
		if (strncmp(line, "charset ", 8) == 0) {
			snprintf(cs, sizeof cs, "%s", arg);
			continue;
		}
		if (d == NULL) {
			unescape(html, buf, sizeof buf);
			d = doc_parse(buf, strlen(buf), cs, &m);
			if (d == NULL) {
				fprintf(stderr, "FAIL %s: out of memory\n", name);
				return 1;
			}
		}
		if (strncmp(line, "set ", 4) == 0 || strncmp(line, "type ", 5) == 0) {
			char *sp = strchr(arg, ' ');
			fctl *c;
			*sp = 0;
			c = named(d, arg, NULL);
			unescape(sp + 1, buf, sizeof buf);
			if (c == NULL) {
				fprintf(stderr, "FAIL %s: no control %s\n", name, arg);
				failed++;
				ok = 0;
			} else if (line[0] == 's') {
				form_set(c, buf, (long) strlen(buf));
			} else {
				form_insert(c, buf, (long) strlen(buf));
			}
		} else if (strncmp(line, "back ", 5) == 0) {
			char *sp = strchr(arg, ' ');
			int k;
			fctl *c;
			*sp = 0;
			c = named(d, arg, NULL);
			for (k = atoi(sp + 1); c != NULL && k > 0; k--)
				form_backspace(c);
		} else if (strncmp(line, "check ", 6) == 0) {
			char *sp = strchr(arg, ' ');
			fctl *c;
			if (sp != NULL)
				*sp = 0;
			c = named(d, arg, sp ? sp + 1 : NULL);
			if (c != NULL)
				form_check(d, c);
		} else if (strncmp(line, "choose ", 7) == 0) {
			char *sp = strchr(arg, ' ');
			fctl *c;
			*sp = 0;
			c = named(d, arg, NULL);
			if (c != NULL)
				form_choose(c, atoi(sp + 1));
		} else if (strcmp(line, "reset") == 0) {
			fctl *c;
			for (c = d->ctls; c != NULL && c->form == NULL; c = c->next)
				;
			if (c != NULL)
				form_reset(d, c->form);
		} else if (strncmp(line, "label ", 6) == 0) {
			char *sp = strchr(arg, ' '), lab[256];
			fctl *c;
			*sp = 0;
			c = named(d, arg, NULL);
			unescape(sp + 1, want, sizeof want);
			if (c != NULL)
				form_label(c, lab, sizeof lab);
			else
				strcpy(lab, "(no control)");
			if (strcmp(lab, want) != 0) {
				fail(name, "label", want, lab);
				ok = 0;
			}
		} else if (strncmp(line, "submit ", 7) == 0) {
			char bn[128];
			int x = 0, y = 0;
			fctl *from = NULL, *by = NULL, *c;
			bn[0] = 0;
			sscanf(arg, "%127s %d %d", bn, &x, &y);
			if (strcmp(bn, "-") == 0) {
				int send;
				for (c = d->ctls; c != NULL && !(form_typed(c) && c->form != NULL); c = c->next)
					;
				from = c;
				by = form_default(d, c, &send);
				if (!send)
					from = NULL;
			} else {
				by = named(d, bn, NULL);
			}
			form_submit_free(&s);
			have = (from != NULL || by != NULL) && form_submit(d, from, by, x, y, &s);
		} else if (strncmp(line, "expect ", 7) == 0) {
			char how[16], act[1024], ct[256];
			ct[0] = act[0] = 0;
			sscanf(arg, "%15s %1023s %255s", how, act, ct);
			if (strcmp(act, "''") == 0)
				act[0] = 0;
			if (strcmp(how, "none") == 0) {
				if (have) {
					fail(name, "nothing sent", "", s.body);
					ok = 0;
				}
			} else if (!have) {
				fail(name, "something sent", how, "nothing");
				ok = 0;
			} else {
				if ((strcmp(how, "post") == 0) != s.post) {
					fail(name, "method", how, s.post ? "post" : "get");
					ok = 0;
				}
				if (strcmp(act, s.action) != 0) {
					fail(name, "action", act, s.action);
					ok = 0;
				}
				if (ct[0] && strncmp(s.ctype, ct, strlen(ct)) != 0) {
					fail(name, "content type", ct, s.ctype);
					ok = 0;
				}
			}
		} else if (strncmp(line, "body ", 5) == 0) {
			char got[8192];
			const char *bd = strstr(s.ctype, "boundary=");
			unescape(arg, want, sizeof want);
			snprintf(got, sizeof got, "%.*s", have ? (int) s.len : 0, have ? s.body : "");
			if (bd != NULL) {
				/* the boundary as {B} */
				char *p;
				size_t bl = strlen(bd + 9);
				while ((p = strstr(got, bd + 9)) != NULL) {
					memmove(p + 3, p + bl, strlen(p + bl) + 1);
					memcpy(p, "{B}", 3);
				}
			}
			if (strcmp(got, want) != 0) {
				fail(name, "body", want, got);
				ok = 0;
			}
		} else {
			fprintf(stderr, "formtest: what is \"%s\"?\n", line);
			return 2;
		}
	}
	if (d != NULL) {
		if (ok)
			passed++;
		doc_free(d);
	}
	form_submit_free(&s);
	fclose(f);
	fprintf(stderr, "forms: %d cases passed, %d checks failed\n", passed, failed);
	return failed != 0;
}
