/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ui/people_file.c on the build machine: names from EContact's
 * CONTACTS.TXT through people_ask.c when EContact isn't running (the
 * guidelines, 4.1, an option), the file read in pieces, and not again
 * while it is unchanged.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "tos.h"
#include "gem.h"
#include "av.h"
#include "plat.h"
#include "people_ask.h"
#include "people_file.h"

static u32 now = 1000;
static int fails, opens, names_calls;
void (*av_people_hook)(const char *s, short from);

u32 tos_ms(void) { return now; }
short appl_find(const char *n) { (void)n; return -1; }	/* no EContact */
const char *av_name(void) { return "EMAIL"; }
int av_people_ready(void) { return 1; }
int av_people(short id, const char *text) { (void)id; (void)text; return -1; }

static FILE *files[64];
int pf_open(const char *path, int mode)
{
	FILE *f = fopen(path, "rb");
	(void)mode;
	opens++;
	if (!f)
		return -1;
	files[fileno(f)] = f;
	return fileno(f);
}
long pf_read(int h, void *buf, long n)
{
	return (long)fread(buf, 1, (size_t)n, files[h]);
}
void pf_close(int h) { if (files[h]) fclose(files[h]); files[h] = 0; }
unsigned long pf_info(const char *path, long *size)
{
	struct stat st;
	if (stat(path, &st)) {
		*size = -1;
		return 0;
	}
	*size = (long)st.st_size;
	return (unsigned long)st.st_mtime + 1;
}
char *pf_load(const char *path, long *len) { (void)path; *len = 0; return 0; }

#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s (line %d)\n", what, __LINE__); fails++; } } while (0)

static void names_came(void) { names_calls++; }

static void write_file(const char *path, int extra)
{
	FILE *f = fopen(path, "wb");
	int i;
	fprintf(f, "; CONTACTS.TXT - written by EContact\r\n; made from 1A2B3C4D\r\n");
	fprintf(f, "P\tAvi Levi\tavi@example.org\thome\r\n");
	/* a group longer than a piece's spare: passed over whole */
	fprintf(f, "G\tEveryone\t");
	for (i = 0; i < 300; i++)
		fprintf(f, "%sPerson %d <person%d@example.org>", i ? ", " : "", i, i);
	fprintf(f, "\r\n");
	/* enough lines that the file is read in several pieces */
	for (i = 0; i < 300; i++)
		fprintf(f, "P\tPerson %d\tperson%d@example.org\twork\r\n", i, i);
	fprintf(f, "P\tGrace Baker\tgrace@x.org\t\r\n");
	fprintf(f, "Pbad line\r\n");
	if (extra)
		fprintf(f, "P\tNew One\tnew@x.org\tother\r\n");
	fprintf(f, "P\tThe Last\tlast@x.org\thome");	/* no line end */
	fclose(f);
}

int main(void)
{
	char dir[] = "/tmp/peoplefileXXXXXX", path[128];
	const char *s;
	if (!mkdtemp(dir))
		return 1;
	snprintf(path, sizeof(path), "%s/CONTACTS.TXT", dir);
	write_file(path, 0);
	people_init(16, 0, names_came, 0);

	/* the option off: addresses show as they are */
	CHECK(people_name("grace@x.org") == 0, "off: being asked");
	people_poll();
	CHECK(people_name("grace@x.org") && !*people_name("grace@x.org"), "off: no EContact, no name");
	CHECK(opens == 0, "off: the file isn't read");

	/* on: the names come from the file */
	people_file(dir, 0);
	now += 31000;			/* the absent one is asked again */
	people_name("grace@x.org");
	people_poll();
	s = people_name("grace@x.org");
	CHECK(s && !strcmp(s, "Grace Baker"), "on: Grace's name from the file");
	CHECK(opens == 1, "on: one open");
	CHECK(people_name("GRACE@X.ORG") && !strcmp(people_name("GRACE@X.ORG"), "Grace Baker"), "any case");
	people_name("person0@example.org");
	people_name("person299@example.org");
	people_name("last@x.org");
	people_name("nobody@x.org");
	people_poll();
	CHECK(!strcmp(people_name("person0@example.org"), "Person 0"), "the line after a long group line");
	CHECK(!strcmp(people_name("person299@example.org"), "Person 299"), "a line past the first piece");
	CHECK(!strcmp(people_name("last@x.org"), "The Last"), "the last line, without its end");
	CHECK(people_name("nobody@x.org") && !*people_name("nobody@x.org"), "not in the file: no name");
	CHECK(opens == 2, "a new address: the file read again (2 opens)");
	CHECK(names_calls >= 2, "names_came told");

	/* unchanged: the one not there isn't looked for again */
	now += 31000;
	people_name("nobody@x.org");
	people_poll();
	CHECK(opens == 2, "unchanged file: not read again for the one not there");

	/* EContact wrote it again: read again */
	sleep(1);
	write_file(path, 1);
	now += 31000;
	people_name("new@x.org");
	people_poll();
	CHECK(!strcmp(people_name("new@x.org"), "New One"), "a new line after EContact wrote the file");

	/* no file: as without the option */
	remove(path);
	now += 31000;
	people_name("other@x.org");
	people_poll();
	CHECK(people_name("other@x.org") && !*people_name("other@x.org"), "no file: no name, no error");
	people_file(0, 0);
	rmdir(dir);
	printf("%s\n", fails ? "people_file: FAILED" : "people_file: ok");
	return fails != 0;
}
