/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * plat_posix.c - plat.h for Linux: EWeb's network code in the tests and
 * in ewget, against real servers.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include "../../src/plat.h"

void (*pf_idle)(void);

int pf_open(const char *path, int mode)
{
	int fl = mode == PF_READ ? O_RDONLY : mode == PF_WRITE ? O_WRONLY | O_CREAT | O_TRUNC
		: mode == PF_UPDATE ? O_RDWR : O_WRONLY | O_CREAT | O_APPEND;
	return open(path, fl, 0644);
}

long pf_read(int h, void *buf, long n) { return read(h, buf, n); }
long pf_write(int h, const void *buf, long n) { return write(h, buf, n); }
void pf_close(int h) { close(h); }
long pf_seek(int h, long off) { return (long)lseek(h, off, SEEK_SET); }

long pf_size(const char *path)
{
	struct stat st;
	if (stat(path, &st) < 0 || !S_ISREG(st.st_mode))
		return -1;
	return st.st_size;
}

unsigned long pf_stamp(const char *path)
{
	struct stat st;
	if (stat(path, &st) < 0 || !S_ISREG(st.st_mode))
		return 0;
	return (unsigned long)st.st_mtime ^ (unsigned long)st.st_mtim.tv_nsec;
}

unsigned long pf_info(const char *path, long *size)
{
	*size = pf_size(path);
	return pf_stamp(path);
}

int pf_exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0;
}

int pf_mkdir(const char *path)
{
	return (mkdir(path, 0755) == 0 || errno == EEXIST) ? 0 : -1;
}

int pf_remove(const char *path) { return unlink(path); }
int pf_rename(const char *from, const char *to) { return rename(from, to); }

int pf_list(const char *dir, int (*cb)(const char *, long, void *), void *ud)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int n = 0;
	char path[1024];
	if (!d)
		return 0;
	while ((e = readdir(d))) {
		long sz;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		sz = pf_size(path);
		if (sz < 0)
			continue;
		n++;
		if (cb(e->d_name, sz, ud))
			break;
	}
	closedir(d);
	return n;
}

void pf_now(PFTIME *t)
{
	time_t now = time(0);
	struct tm tm;
	localtime_r(&now, &tm);
	t->year = tm.tm_year + 1900;
	t->mon = tm.tm_mon + 1;
	t->day = tm.tm_mday;
	t->hour = tm.tm_hour;
	t->min = tm.tm_min;
	t->sec = tm.tm_sec;
}

void pf_entropy(unsigned char *buf, int n)
{
	int fd = open("/dev/urandom", O_RDONLY), got = 0;
	if (fd >= 0) {
		got = (int)read(fd, buf, n);
		close(fd);
	}
	if (got != n)
		abort();
}

unsigned long pf_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void pf_debug(const char *s)
{
	if (getenv("EW_DEBUG"))
		fprintf(stderr, "%s\n", s);
}

/* ---- TCP ---- (the fuzz targets have their own: common/tests/fuzz/fuzz_net.c) */

#ifndef FUZZ_NET
int net_init(void) { return 1; }
const char *net_stack(void) { return "POSIX"; }

int net_open(const char *host, unsigned short port, char *err, int errlen)
{
	struct addrinfo hints, *res, *ai;
	char ps[8];
	int fd = -1, r;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(ps, sizeof(ps), "%u", port);
	r = getaddrinfo(host, ps, &hints, &res);
	if (r) {
		snprintf(err, errlen, "can't find %s", host);
		return -1;
	}
	for (ai = res; ai; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	if (fd < 0) {
		snprintf(err, errlen, "can't connect to %s:%u", host, port);
		return -1;
	}
	fcntl(fd, F_SETFL, O_NONBLOCK);
	return fd;
}

long net_write(int h, const void *buf, long n)
{
	const char *p = buf;
	long done = 0;
	while (done < n) {
		ssize_t r = send(h, p + done, n - done, MSG_NOSIGNAL);
		if (r < 0 && (errno == EAGAIN || errno == EINTR)) {
			struct timespec ts = { 0, 1000000 };
			nanosleep(&ts, 0);
			continue;
		}
		if (r <= 0)
			return -1;
		done += r;
	}
	return done;
}

long net_read(int h, void *buf, long max)
{
	ssize_t r = recv(h, buf, max, 0);
	if (r < 0 && (errno == EAGAIN || errno == EINTR)) {
		struct timespec ts = { 0, 1000000 };
		nanosleep(&ts, 0);
		return 0;
	}
	return r <= 0 ? -1 : r;
}

void net_close(int h) { close(h); }
void net_close_wait(int h, int seconds) { (void)seconds; close(h); }
#endif
