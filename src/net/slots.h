/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * slots.h - a file EWeb writes again and again (the cache's index, the
 * cookies, the history) kept in two slots, each written over in place.
 */
#ifndef EW_SLOTS_H
#define EW_SLOTS_H

/* the newer whole slot's data (malloc'ed, NUL-terminated), or NULL */
char *slot_load(const char *path, long *len);
/* written over the other slot: 0, or < 0 (the newer one is left as it was) */
int slot_save(const char *path, const void *data, long len);
/* known to be on the disk in neither slot (a file EWeb hasn't made
   yet): the first save makes NAME at once, without looking for it */
void slot_none(const char *path);
/* both slots gone */
void slot_remove(const char *path);
/* told how much of a slot_save's data is on the disk after each piece
   (the quit's bar; NULL: nothing told) */
extern void (*slot_progress)(long done, long total);
/* the other slot's name (NAME.TX2 for NAME.TXT) */
void slot_twin(const char *path, char *out, int size);

#endif
