/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * form.h - a page's forms (form.c): each control's state (what is typed
 * in a field, whether a box is ticked, the options chosen), kept with
 * the document so that it outlives laying the page out again and comes
 * back with the page for Back; and a form's submission as the HTML
 * Standard builds it (the entry list, then urlencoded, multipart or
 * plain text in the page's charset).
 */
#ifndef EW_FORM_H
#define EW_FORM_H

#include <stdint.h>

#include "doc.h"

/* what a control is */
enum {
	FC_TEXT,			/* <input> text, search, email, url, tel, number, date... */
	FC_PASSWORD,
	FC_HIDDEN,
	FC_CHECKBOX,
	FC_RADIO,
	FC_SUBMIT,			/* <input type=submit>, <button> (type submit, the default) */
	FC_RESET,
	FC_BUTTON,			/* does nothing without a script */
	FC_IMAGE,			/* <input type=image>: a submit button that is a picture */
	FC_FILE,			/* sent empty: EWeb doesn't upload files yet */
	FC_TEXTAREA,
	FC_SELECT
};

typedef struct fctl fctl;
struct fctl {
	fctl *next;			/* the next in document order */
	dnode *n;			/* the element */
	dnode *form;			/* its form, NULL: none */
	uint8_t kind;			/* FC_* */
	uint8_t checked;		/* a checkbox or radio button */
	uint8_t disabled, readonly;
	uint8_t multiple;		/* a <select multiple> */
	uint8_t focus;			/* the keyboard is here */
	uint8_t gone;			/* form_init again: no longer in the tree */
	char *v;			/* the value, UTF-8 (text, password, textarea, hidden...) */
	long len, cap;
	long maxlen;			/* maxlength, -1: none */
	long cur;			/* the caret: a byte in v */
	long first;			/* a field: its first byte shown; a textarea: its first line */
	int nopt;			/* a select's options, in order */
	dnode **opt;
	uint8_t *sel;			/* each chosen or not */
};

/* the document's controls, made once it is parsed (each element's ctl
   set), and again after a script changed the tree (those still in it
   keep their state); 0 only when out of memory */
int form_init(doc *d);
void form_free(doc *d);
/* every control of f (the one form) back as the page had it */
void form_reset(doc *d, dnode *f);

/* what a control shows: a button's label, a select's chosen option, a
   field's text (a password as bullets), into out (UTF-8, at most size) */
void form_label(const fctl *c, char *out, int size);
/* a field's placeholder when it is empty, else NULL */
const char *form_placeholder(const fctl *c);
/* is it a field one types into */
int form_typed(const fctl *c);

/* editing a field's text at the caret; 0 when nothing changed */
int form_insert(fctl *c, const char *s, long n);
int form_backspace(fctl *c);
int form_delete(fctl *c);
void form_set(fctl *c, const char *s, long n);	/* the whole value */
void form_retype(fctl *c);	/* its kind again, from its type attribute (a script set it) */
/* a click on a checkbox (toggled) or radio button (chosen, the rest of
   its group not) */
void form_check(doc *d, fctl *c);
/* option i of a select chosen (multiple: toggled) */
void form_choose(fctl *c, int i);
/* an option's text, white space collapsed */
void form_option_text(const dnode *o, char *out, int size);

/* the controls the keyboard goes to with Tab, from c (NULL: the first),
   back with dir -1; NULL past the last */
fctl *form_next(doc *d, fctl *c, int dir);

/* how a form is sent */
enum { FS_URLENCODED, FS_MULTIPART, FS_PLAIN };
typedef struct {
	int post;			/* 1: POST, 0: GET */
	int enctype;			/* FS_* */
	char *action;			/* the address as the page has it, "" for its own */
	char *body;			/* POST: the body; GET: the query (without '?') */
	long len;
	char ctype[96];			/* POST: Content-Type */
} FSUBMIT;

/* the form submitted: by the button submitter (NULL: the form's
   default, from Return in a field), (x, y) where a picture button was
   clicked; the page's charset for the text. 1 and *s (form_submit_free
   after), or 0 when nothing is sent (no form, a disabled default button,
   a form that says it is a dialog's) */
int form_submit(doc *d, fctl *from, fctl *submitter, int32_t x, int32_t y, FSUBMIT *s);
void form_submit_free(FSUBMIT *s);
/* the button Return in field c presses: the form's first submit button,
   or NULL (then the form is sent only when c is its one field) */
fctl *form_default(doc *d, fctl *c, int *send);

#endif
