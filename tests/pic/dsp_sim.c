/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dsp_sim.c - dsp/jpeg.a56 itself, loaded through the bootstrap of
 * Hatari's DSP56001 core (../ec/fetch-hatari.sh), behind the Falcon's host
 * port: a PIC_DSP for test_pic_dsp. It counts the DSP's cycles, and the
 * words each way.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "dsp_core.h"
#include "dsp_cpu.h"
#include "pic.h"
#include "../../dsp/jpeg_prog.h"

/* what the core calls in the rest of Hatari */
void DSP_SsiTransmit_SC1(uint32_t v) { (void)v; }
void DSP_SsiTransmit_SC2(uint32_t v) { (void)v; }
void dsp56k_disasm_init(void) {}
uint16_t dsp56k_disasm(int mode, FILE *f) { (void)mode; (void)f; return 0; }
const char *dsp56k_getInstructionText(void) { return ""; }
static void hint(int s) { (void)s; }

unsigned long long sim_cycles;		/* DSP clock cycles (32 MHz on the Falcon) */
unsigned long sim_in, sim_out;		/* words to the DSP, and from it */
int sim_busy;				/* begin() says the DSP is taken */

static int step(void)
{
	if (!dsp_core.running)
		return 0;
	dsp56k_execute_instruction();
	sim_cycles += dsp_core.instr_cycle;
	return 1;
}

static void put1(unsigned long w)
{
	long n = 0;
	while (!(dsp_core_read_host(CPU_HOST_ISR) & 2))
		if (!step() || ++n > 10000000) {
			pic_dsp_failed = 1;
			return;
		}
	dsp_core_write_host(CPU_HOST_TRXH, (w >> 16) & 255);
	dsp_core_write_host(CPU_HOST_TRXM, (w >> 8) & 255);
	dsp_core_write_host(CPU_HOST_TRXL, w & 255);
	sim_in++;
}

static unsigned long get1(void)
{
	unsigned long w;
	long n = 0;
	while (!(dsp_core_read_host(CPU_HOST_ISR) & 1))
		if (!step() || ++n > 10000000) {
			pic_dsp_failed = 1;
			return 0;
		}
	w = (unsigned long)dsp_core_read_host(CPU_HOST_RXH) << 16;
	w |= (unsigned long)dsp_core_read_host(CPU_HOST_RXM) << 8;
	w |= dsp_core_read_host(CPU_HOST_RXL);
	sim_out++;
	return w;
}

static void put(const unsigned long *w, long n)
{
	while (n-- > 0 && !pic_dsp_failed)
		put1(*w++);
}

static void get16(unsigned short *d, int cols, int rows, long pitch)
{
	int r, c;
	for (r = 0; r < rows; r++, d += pitch)
		for (c = 0; c < cols && !pic_dsp_failed; c++)
			d[c] = (unsigned short)get1();
}

static void get24(unsigned char *d, int cols, int rows, long pitch)
{
	int r, c;
	for (r = 0; r < rows; r++, d += pitch)
		for (c = 0; c < cols && !pic_dsp_failed; c++) {
			unsigned long v = get1();
			d[3 * c] = (unsigned char)(v >> 16);
			d[3 * c + 1] = (unsigned char)(v >> 8);
			d[3 * c + 2] = (unsigned char)v;
		}
}

static int begin(void)
{
	static int inited;
	if (sim_busy)
		return 0;
	if (!inited) {
		dsp_core_init(hint);
		dsp56k_init_cpu();
		inited = 1;
	}
	dsp_core_reset();
	pic_dsp_failed = 0;
	put(dsp_jpeg_prog, 512);
	return 1;
}

/* PIC_DSP_DUMP=ADDR,N: the DSP's Y memory from ADDR at the end, for
   debugging jpeg.a56 */
static void end(int clean)
{
	(void)clean;
	const char *e = getenv("PIC_DSP_DUMP");
	unsigned a, n, i;
	if (e == NULL || sscanf(e, "%x,%u", &a, &n) != 2)
		return;
	for (i = 0; i < n; i++)
		fprintf(stderr, "%06lx%c", (unsigned long)dsp_core.ramext[(a + i) & 0x3fff], i % 8 == 7 ? '\n' : ' ');
}
static const PIC_DSP port = { begin, end, put, get16, get24 };

const PIC_DSP *sim_dsp(void)
{
	return &port;
}
