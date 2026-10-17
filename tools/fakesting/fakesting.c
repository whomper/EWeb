/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * FAKESTNG.PRG - a stand-in for the STinG TCP/IP stack, for testing EMail
 * in an emulator that has no network card (Hatari). It installs a "STiK"
 * cookie and a TRANSPORT_TCPIP table whose connections are tunnelled over
 * the serial port to tests/hatari/serial_bridge.py, which makes the real
 * TCP connections. Never install it on a real Atari.
 *
 * Based on Claude ST's FAKESTNG (whomper/atari_claude), which carries one
 * connection; this one multiplexes up to four, in frames:
 *
 *   Atari -> bridge   'O' cn ip[4] port[2]   open
 *                     'D' cn len[2] data      send
 *                     'C' cn                  close
 *   bridge -> Atari   'S' cn ok[1]            open result (1 = connected)
 *                     'D' cn len[2] data      received
 *                     'E' cn                  the server closed
 */
#include "tos.h"

#define NCONN  4
#define RXSIZE 16384

long get_dftab(void), TCP_open(void), TCP_close(void), TCP_send(void),
     TCP_wait_state(void), CNbyte_count(void), CNget_block(void),
     get_err_text(void), resolve(void), unsupported(void);

typedef struct {
	char magic[10];
	void *get_dftab, *etm_exec, *cfg, *basepage;
} DRV_LIST;

static DRV_LIST drivers = { "STiKmagic", get_dftab, unsupported, 0, 0 };

static struct {
	const char *module, *author, *version;
	void *fn[32];
} tpl = { "TRANSPORT_TCPIP", "fake", "01.00", {
	unsupported, unsupported, unsupported, unsupported, get_err_text, unsupported,
	unsupported, TCP_open, TCP_close, TCP_send, TCP_wait_state,
	unsupported, unsupported, unsupported, unsupported, unsupported,
	CNbyte_count, unsupported, unsupported, CNget_block, unsupported,
	resolve,
} };

static struct {
	short used, open, eof, refused;
	unsigned char buf[RXSIZE];
	long head, tail;	/* ring buffer of received bytes */
} conn[NCONN];

static u32 get_long(const short *a) { return ((u32)(u16)a[0] << 16) | (u16)a[1]; }

static void out(int c) { Bconout(1, c & 0xff); }

static int in_ready(void) { return (int)Bconstat(1); }

static int in_byte(void)
{
	return (int)(Bconin(1) & 0xff);
}

/* read and file one frame from the bridge, if one has started */
static void pump(void)
{
	while (in_ready()) {
		int type = in_byte(), cn = in_byte();
		if (cn < 0 || cn >= NCONN)
			continue;
		if (type == 'S') {
			int ok = in_byte();
			conn[cn].open = ok == 1;
			conn[cn].refused = ok != 1;
		} else if (type == 'E') {
			conn[cn].eof = 1;
		} else if (type == 'D') {
			int len = in_byte() << 8;
			len |= in_byte();
			while (len-- > 0) {
				int c = in_byte();
				long next = (conn[cn].head + 1) % RXSIZE;
				if (next != conn[cn].tail) {
					conn[cn].buf[conn[cn].head] = (unsigned char)c;
					conn[cn].head = next;
				}
			}
		}
	}
}

long c_unsupported(const short *a) { (void)a; return -32; }
long c_get_err_text(const short *a) { (void)a; return (long)"fake STinG error"; }
long c_resolve(const short *a) { (void)a; return -13; }	/* E_CANTRESOLVE: use IP addresses */

long c_get_dftab(const short *a)
{
	const char *name = (const char *)get_long(a);
	const char *t = "TRANSPORT_TCPIP";
	while (*name && *name == *t)
		name++, t++;
	return (*name || *t) ? 0 : (long)&tpl;
}

long c_TCP_open(const short *a)
{
	u32 ip = get_long(a);
	u16 port = (u16)a[2];
	short cn, i;
	for (cn = 0; cn < NCONN && conn[cn].used; cn++)
		;
	if (cn == NCONN)
		return -9;		/* E_NOCCB */
	conn[cn].used = 1;
	conn[cn].open = conn[cn].eof = conn[cn].refused = 0;
	conn[cn].head = conn[cn].tail = 0;
	out('O');
	out(cn);
	for (i = 3; i >= 0; i--)
		out((int)(ip >> (i * 8)));
	out(port >> 8);
	out(port);
	return cn;
}

long c_TCP_wait_state(const short *a)
{
	short cn = a[0];
	u32 start = tos_hz200();
	if (cn < 0 || cn >= NCONN || !conn[cn].used)
		return -9;
	while (!conn[cn].open && !conn[cn].refused) {
		pump();
		if (tos_hz200() - start > 200UL * a[2] + 400)
			return -16;	/* E_CNTIMEOUT */
	}
	return conn[cn].open ? 0 : -7;	/* E_REFUSE */
}

long c_TCP_send(const short *a)
{
	short cn = a[0], len = a[3], i;
	const unsigned char *buf = (const unsigned char *)get_long(a + 1);
	if (cn < 0 || cn >= NCONN || !conn[cn].open)
		return -9;
	out('D');
	out(cn);
	out(len >> 8);
	out(len);
	for (i = 0; i < len; i++)
		out(buf[i]);
	return 0;
}

long c_CNbyte_count(const short *a)
{
	short cn = a[0];
	long n;
	if (cn < 0 || cn >= NCONN || !conn[cn].used)
		return -9;
	pump();
	n = (conn[cn].head - conn[cn].tail + RXSIZE) % RXSIZE;
	if (n > 32000)
		n = 32000;
	if (n)
		return n;
	return conn[cn].eof ? -3 : -2;	/* E_EOF : E_NODATA */
}

long c_CNget_block(const short *a)
{
	short cn = a[0], len = a[3], n = 0;
	unsigned char *buf = (unsigned char *)get_long(a + 1);
	if (cn < 0 || cn >= NCONN || !conn[cn].used)
		return -9;
	pump();
	while (n < len && conn[cn].tail != conn[cn].head) {
		buf[n++] = conn[cn].buf[conn[cn].tail];
		conn[cn].tail = (conn[cn].tail + 1) % RXSIZE;
	}
	return n ? n : (conn[cn].eof ? -3 : -2);
}

long c_TCP_close(const short *a)
{
	short cn = a[0];
	if (cn < 0 || cn >= NCONN || !conn[cn].used)
		return -9;
	out('C');
	out(cn);
	conn[cn].used = 0;
	return 0;
}

/* --- install --- */

static char rxbuf[16384];
static IOREC *io;

static void sup_install(void)
{
	long *jar;
	short sr;
	__asm__ volatile("move.l 0x5a0.w,%0" : "=a"(jar));
	if (jar) {
		long i = 0;
		while (jar[i * 2])
			i++;
		/* the terminator's value is the jar size; append before it */
		jar[i * 2 + 2] = 0;
		jar[i * 2 + 3] = jar[i * 2 + 1];
		jar[i * 2] = 0x5354694bL;	/* 'STiK' */
		jar[i * 2 + 1] = (long)&drivers;
	}
	__asm__ volatile("move.w %%sr,%0\n\tor.w #0x0700,%%sr" : "=d"(sr) :: "memory");
	io->ibuf = rxbuf;
	io->ibufsiz = sizeof(rxbuf);
	io->ibufhd = io->ibuftl = 0;
	io->ibuflow = 4096;
	io->ibufhi = 12288;
	__asm__ volatile("move.w %0,%%sr" :: "d"(sr) : "memory");
}

long fake_install(long *basepage)
{
	/* a Falcon's AUX port is its SCC; Hatari wires the MFP port (6) */
	if ((get_cookie(0x5f4d4348L) >> 16) == 3)	/* '_MCH' */
		trap14_ww(44, 6);			/* Bconmap(6) */
	io = Iorec(0);
	Supexec(sup_install);
	Cconws("Fake STinG for EMail installed (TCP over serial)\r\n");
	return 0x100 + basepage[3] + basepage[5] + basepage[7];
}
