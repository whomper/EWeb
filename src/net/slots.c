/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * slots.c - a file EWeb writes again and again, in two slots written
 * over in place (slots.h).
 *
 * pf_save makes NAME.IN$, writes it, removes NAME and renames: a new
 * file each time, whose clusters GEMDOS finds by reading the FAT from
 * its start, and the FAT and the folder written again. On a FAT16 disk
 * half full that is 60 disk commands for 644 bytes (EmuTOS, counted
 * from Hatari's IDE trace, 29 Oct 2026); Erez's quit after Wikipedia
 * took 2.9 s for the cookies, 2.9 s for the history and 24 s for the
 * cache's index on his SD card. Written over in place, the same file is
 * 4 commands: no cluster to find, no FAT, the folder's entry once.
 *
 * So each such file is two: NAME and its twin (NAME.TX2 for NAME.TXT).
 * Each starts with "EWS1 GEN LEN\r\n" (hex, 24 bytes) and its data;
 * what lies past LEN is an older, longer copy's end. A save writes over
 * the slot that isn't the newer one: the data first, its head last, so
 * until the save is whole the newer slot is still the one read (the
 * guidelines' 2.3: a reset never leaves a half-written file read). A
 * file of before (no head) is the oldest slot. A slot that has grown
 * far past what it holds is made again, once.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat.h"
#include "slots.h"

#define HEAD 24
#define PIECE 16384L	/* written a piece at a time, slot_progress told after each */

void (*slot_progress)(long done, long total);

/* what was read or written last, per file: the newer slot (0: NAME,
   1: the twin), its generation, and which slots are on the disk (a save
   makes a missing one at once, without a look that fails: the
   guidelines' 6.5) */
static struct {
	char path[200];
	int newer, there[2];
	unsigned long gen;
} known[4];

void slot_twin(const char *path, char *out, int size)
{
	const char *base = path, *dot = NULL, *p;
	int n = (int) strlen(path);
	for (p = path; *p; p++)
		if (*p == '\\' || *p == '/' || *p == ':') {
			base = p + 1;
			dot = NULL;
		} else if (*p == '.' && p > base) {
			dot = p;
		}
	if (n + 3 > size) {
		out[0] = 0;
		return;
	}
	strcpy(out, path);
	if (dot == NULL)
		strcat(out, ".2");
	else if (dot[1] == 0)
		strcat(out, "2");
	else
		out[n - 1] = out[n - 1] == '2' ? '3' : '2';
}

static int find(const char *path, int add)
{
	int i, free_one = -1;
	for (i = 0; i < 4; i++) {
		if (strcmp(known[i].path, path) == 0)
			return i;
		if (known[i].path[0] == 0 && free_one < 0)
			free_one = i;
	}
	if (!add)
		return -1;
	i = free_one >= 0 ? free_one : 0;
	snprintf(known[i].path, sizeof known[i].path, "%s", path);
	return i;
}

static unsigned long hex(const char *s, int n, int *ok)
{
	unsigned long v = 0;
	for (; n > 0; n--, s++) {
		int d = *s >= '0' && *s <= '9' ? *s - '0' : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10 : -1;
		if (d < 0)
			*ok = 0;
		v = v << 4 | (unsigned long) (d & 15);
	}
	return v;
}

/* a slot open, its head read: 1 with *gen, *len (*data where its data
   starts); 0 when it is missing (*there 0) or isn't whole. A file with
   no head (before slots) is generation 0, all of it data. */
static int look(const char *path, int legacy, int *h, unsigned long *gen, long *len, long *data, int *there)
{
	char head[HEAD];
	long size;
	int ok = 1;
	*h = pf_open(path, PF_READ);
	*there = *h >= 0;
	if (*h < 0)
		return 0;
	size = pf_open_size(*h);
	if (size >= HEAD && pf_read(*h, head, HEAD) == HEAD && memcmp(head, "EWS1 ", 5) == 0) {
		*gen = hex(head + 5, 8, &ok);
		*len = (long) hex(head + 14, 8, &ok);
		*data = HEAD;
		if (ok && head[13] == ' ' && head[22] == '\r' && head[23] == '\n' && *len <= size - HEAD)
			return 1;
	} else if (legacy && size >= 0 && (size < 3 || memcmp(head, "EWS", 3) != 0)) {
		*gen = 0;
		*len = size;
		*data = 0;
		return 1;
	}
	pf_close(*h);
	*h = -1;
	return 0;
}

char *slot_load(const char *path, long *len)
{
	char twin[220];
	int ha, hb, a, b, k, use, ta = 0, tb = 0;
	unsigned long ga = 0, gb = 0;
	long la = 0, lb = 0, da = 0, db = 0, n, got;
	char *d = NULL;
	*len = 0;
	slot_twin(path, twin, sizeof twin);
	a = look(path, 1, &ha, &ga, &la, &da, &ta);
	b = twin[0] && look(twin, 0, &hb, &gb, &lb, &db, &tb);
	use = b && (!a || gb > ga);
	k = find(path, 1);
	known[k].there[0] = ta;
	known[k].there[1] = tb;
	known[k].newer = use;
	known[k].gen = use ? gb : a ? ga : 0;
	if (a || b) {
		int h = use ? hb : ha;
		n = use ? lb : la;
		if (pf_seek(h, use ? db : da) >= 0 && (d = malloc((size_t) n + 1)) != NULL) {
			for (got = 0; got < n; got += k) {
				k = (int) (n - got > 32768L ? 32768L : n - got);
				if (pf_read(h, d + got, k) != k)
					break;
			}
			if (got < n) {
				free(d);
				d = NULL;
			} else {
				d[n] = 0;
				*len = n;
			}
		}
	}
	if (a)
		pf_close(ha);
	if (b)
		pf_close(hb);
	return d;
}

int slot_save(const char *path, const void *data, long len)
{
	char twin[220], head[HEAD + 1];
	const char *to;
	int k = find(path, 0), h, ok;
	long size;
	if (k < 0) {
		/* not read this run: which slot is newer, from their heads */
		int ha, hb, a, b, ta = 0, tb = 0;
		unsigned long ga = 0, gb = 0;
		long la, lb, da, db;
		slot_twin(path, twin, sizeof twin);
		a = look(path, 1, &ha, &ga, &la, &da, &ta);
		b = twin[0] && look(twin, 0, &hb, &gb, &lb, &db, &tb);
		if (a)
			pf_close(ha);
		if (b)
			pf_close(hb);
		k = find(path, 1);
		known[k].there[0] = ta;
		known[k].there[1] = tb;
		known[k].newer = b && (!a || gb > ga);
		known[k].gen = known[k].newer ? gb : ga;
	}
	slot_twin(path, twin, sizeof twin);
	if (twin[0] == 0)
		return -1;
	to = known[k].newer ? path : twin;
	h = known[k].there[!known[k].newer] ? pf_open(to, PF_UPDATE) : -1;
	if (h >= 0 && (size = pf_open_size(h)) > 2 * (len + HEAD) + 32768L) {
		pf_close(h);		/* far too big: made again, once */
		h = -1;
	}
	if (h < 0)
		h = pf_open(to, PF_WRITE);
	if (h < 0)
		return -1;
	/* a head that is no slot's first (GEMDOS can't seek past the end),
	   the data, then the head */
	memcpy(head, "EWS0                  \r\n", HEAD);
	ok = pf_write(h, head, HEAD) == HEAD;
	{
		long done = 0, n;
		while (ok && done < len) {
			n = len - done > PIECE ? PIECE : len - done;
			ok = pf_write(h, (const char *) data + done, n) == n;
			done += n;
			if (slot_progress != NULL)
				slot_progress(done, len);
		}
	}
	snprintf(head, sizeof head, "EWS1 %08lx %08lx\r\n", (known[k].gen + 1) & 0xFFFFFFFFUL, (unsigned long) len);
	ok = ok && pf_seek(h, 0) == 0 && pf_write(h, head, HEAD) == HEAD;
	pf_close(h);
	known[k].there[!known[k].newer] = 1;
	if (!ok)
		return -1;
	known[k].newer = !known[k].newer;
	known[k].gen = (known[k].gen + 1) & 0xFFFFFFFFUL;
	return 0;
}

void slot_none(const char *path)
{
	int k = find(path, 1);
	known[k].there[0] = known[k].there[1] = 0;
	known[k].newer = 1;	/* (so the save goes to NAME) */
	known[k].gen = 0;
}

void slot_remove(const char *path)
{
	char twin[220];
	int k = find(path, 0);
	slot_twin(path, twin, sizeof twin);
	pf_remove(path);
	if (twin[0])
		pf_remove(twin);
	if (k >= 0)
		known[k].path[0] = 0;
}
