/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dirlist.h - a folder's entries for the family's file dialog (ui/filepick.c;
 * from EPlay's Add files, Erez, 4 Nov 2026: Selectric's ways, a selection,
 * no check boxes): its folders and files with their sizes and dates, as
 * many as memory holds, sorted (folders first) by name, date, size or
 * type, and the view of them a type and a mask (* and ?) let through, each
 * file selected or not. The types are the app's: a table of names and the
 * extensions each lets through. No TOS here: the dialog reads the folder
 * and hands each entry in, so the host can test it all (tests/dirlist).
 */
#ifndef AERA_DIRLIST_H
#define AERA_DIRLIST_H

typedef struct {
	char name[14];		/* 8.3, as GEMDOS gives it */
	long size;
	unsigned short date;	/* GEMDOS's: year - 1980 << 9 | month << 5 | day */
	unsigned short time;
	unsigned char dir;	/* a folder */
	unsigned char sel;	/* selected (files only) */
} DENT;

/* the Type pop-up's entries, the app's: its name ("Music files") and the
   extensions it lets through, a space between ("MP3 WAV AIF AIFF"), ""
   for every file. The first is the dialog's own at its start */
typedef struct {
	const char *name;
	const char *exts;
} DL_TYPE;
#define DL_TYPES 12		/* the most a pop-up holds */

enum { DS_NAME, DS_DATE, DS_SIZE, DS_TYPE, DS_N };		/* the Sort pop-up */

typedef struct {
	DENT *e;
	int n, cap;		/* all the folder's entries, sorted */
	int *view, nview;	/* those shown: indexes into e */
	int type, sort;		/* type: an index into types */
	char mask[14];		/* "" or "*": everything */
	const DL_TYPE *types;	/* dl_init: one, "All files" */
	int ntypes;
} DLIST;

void dl_init(DLIST *d);
void dl_types(DLIST *d, const DL_TYPE *t, int n);	/* the app's; type 0 then */
void dl_free(DLIST *d);
void dl_clear(DLIST *d);	/* its entries gone (another folder), the type, sort and mask kept */
/* an entry of the folder read ("." and ".." are left out): 0 when there's
   no memory for it */
int dl_add(DLIST *d, const char *name, long size, unsigned short date, unsigned short time, int dir);
/* sorted (folders first, then the files, each as d->sort has it), and
   the view made again from the type and the mask (folders always) */
void dl_sort(DLIST *d);
void dl_filter(DLIST *d);
DENT *dl_at(DLIST *d, int row);		/* the view's row: 0 past it */

int dl_match(const char *mask, const char *name);	/* * and ?, case aside */
int dl_type_ok(const DLIST *d, int type, const char *name);
const char *dl_type_name(const DLIST *d, int type);	/* its name: "?" past the table */
const char *dl_sort_name(int sort);	/* "By name", ... */

/* the selection: in the view's rows */
void dl_select_only(DLIST *d, int row);
void dl_toggle(DLIST *d, int row);
void dl_select_range(DLIST *d, int a, int b);	/* a to b, and only them */
void dl_select_all(DLIST *d, int on);	/* on: every file (not the folders: a drive's all at once) */
int dl_selected_dirs(const DLIST *d);	/* of those, the folders (added with their subfolders) */
int dl_selected(const DLIST *d, long *bytes);	/* how many rows (files and folders), the files' bytes */

/* "7.1M", "64M", "512K", "12"; "03.11.26" */
void dl_size_text(long size, char *out);
void dl_date_text(unsigned short date, char *out);

#endif
