/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ui.h - EWeb's GEM layer. There is no GEM program yet: this is what
 * common/ui/dsp.c and dsp_ec.c take from it, the settings that say
 * whether the DSP may do the handshake's arithmetic.
 */
#ifndef UI_H
#define UI_H

typedef struct {
	int falcon;			/* TLS on the Atari itself (always, in EWeb) */
	int dsp;			/* RSA and elliptic curves on the DSP when there is one */
} EWOPT;
extern EWOPT opt;

/* common/ui/dsp.c, common/ui/dsp_ec.c */
void falcon_dsp_init(void);
const char *falcon_dsp_state(void);	/* "DSP" or "68030" */
void falcon_dsp_ec_init(void);
const char *falcon_dsp_ec_state(void);

#endif
