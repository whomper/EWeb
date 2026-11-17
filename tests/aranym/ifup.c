/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* IFUP.PRG, for tests/aranym/mintnet.py: ARAnyM's network card (nfeth.xif,
 * eth0) brought up under FreeMiNT 1.17.0, whose archive has no ifconfig or
 * route: the address 192.168.0.2/24 and a route to its network, as
 * MiNTnet's ioctls take them. Each step's result on NatFeats. */
#include <string.h>
#include <tos.h>

long nf_on = 1;
void nf_debug(const char *s);

#define S(n) (('S' << 8) | (n))
#define SIOCGIFFLAGS S(13)
#define SIOCSIFFLAGS S(14)
#define SIOCSIFADDR S(16)
#define SIOCSIFNETMASK S(22)
#define SIOCADDRT S(30)
#define IFF_UP 1
#define IFF_BROADCAST 2
#define RTF_UP 1

struct sin {			/* MiNT's sockaddr_in */
	short family;
	unsigned short port;
	unsigned long addr;
	char zero[8];
};

struct ifreq {
	char name[16];
	union {
		struct sin a;
		short flags;
		long l[4];
	} u;
};

struct rtentry {		/* the kernel's (sys/sockets/inet4/route.h) */
	unsigned long hash;
	struct sin dst, gateway;
	short flags;
	unsigned long metric;
	short refcnt;
	unsigned long use;
	void *ifp;
};

static void say(const char *what, long r)
{
	char m[80], *p = m;
	unsigned long v = r < 0 ? -r : r;
	char d[12];
	int n = 0;
	strcpy(m, "IFUP ");
	strcat(m, what);
	p = m + strlen(m);
	*p++ = ' ';
	if (r < 0)
		*p++ = '-';
	do
		d[n++] = '0' + v % 10;
	while ((v /= 10) && n < 11);
	while (n)
		*p++ = d[--n];
	*p++ = '\n';
	*p = 0;
	nf_debug(m);
}

int main(void)
{
	struct ifreq r;
	struct rtentry rt;
	long fd = Fsocket(2, 2, 0), e;	/* AF_INET, SOCK_DGRAM: any socket takes them */
	say("socket", fd);
	if (fd < 0)
		return 1;
	memset(&r, 0, sizeof(r));
	strcpy(r.name, "eth0");
	r.u.a.family = 2;
	r.u.a.addr = 0xc0a80002UL;	/* 192.168.0.2 */
	say("addr", Fcntl((short)fd, &r, SIOCSIFADDR));
	memset(&r.u, 0, sizeof(r.u));
	r.u.a.family = 2;
	r.u.a.addr = 0xffffff00UL;
	say("netmask", Fcntl((short)fd, &r, SIOCSIFNETMASK));
	memset(&r.u, 0, sizeof(r.u));
	e = Fcntl((short)fd, &r, SIOCGIFFLAGS);
	r.u.flags |= IFF_UP | IFF_BROADCAST;
	say("flags", e < 0 ? e : Fcntl((short)fd, &r, SIOCSIFFLAGS));
	memset(&rt, 0, sizeof(rt));
	rt.dst.family = 2;
	rt.dst.addr = 0xc0a80000UL;	/* 192.168.0.0, its network */
	rt.flags = RTF_UP;
	say("route", Fcntl((short)fd, &rt, SIOCADDRT));
	Fclose((short)fd);
	return 0;
}
