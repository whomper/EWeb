/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* share.h - EWeb's own page for shared files (DESIGN.md, Shared files:
   the look decided 26 Nov 2026): a file or a folder a cloud drive shares
   (Google Drive, Dropbox, Box, iCloud Drive), whose own page is made by
   scripts far bigger than EWeb runs, shown as a list EWeb draws, in
   AERA's look: each file with its type, name, size and Download, each
   folder with Open, and Download all where the service gives one. */
#ifndef EW_SHARE_H
#define EW_SHARE_H

#include "util.h"

typedef struct share_item {
	char name[160];			/* as the service names it, UTF-8 */
	char type[8];			/* ZIP, PDF, DIR for a folder ("" unknown) */
	char size[24];			/* "1.2 MB", "3 files", "" unknown */
	char url[640];			/* where Download (or Open) goes */
	unsigned char folder;
} share_item;

#define SHARE_MAX 200

typedef struct share {
	const char *service;		/* "Google Drive" */
	char host[64];			/* drive.google.com */
	char title[256];		/* the folder's or the file's name */
	char page[1024];		/* the service's own page (the address shown) */
	char all[640];			/* Download all's address, "" none */
	const char *note;		/* a line under the list, NULL none */
	const char *empty;		/* said in place of the list when it has none, NULL: the default */
	int n;
	share_item it[SHARE_MAX];
} share;

/* the page into s: AERA's look (black, light copper labels, a copper
   ruler), a row an item. gateway: the Gateway is set (its browser shows
   the service's own page) */
void share_page(SBUF *s, const share *sh, int gateway);

/* a size in bytes as the page shows it ("720 KB", "8.4 MB") */
void share_size(char *out, int size, long bytes);

/* the type a file's name says: its extension in capitals, at most 4
   letters ("" none) */
void share_type(char *out, int size, const char *name);

#endif
