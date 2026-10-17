/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* common/ui/dsp.c's calls on a computer with no DSP: BearSSL does RSA. */
#include "ui.h"

void falcon_dsp_init(void)
{
}

const char *falcon_dsp_state(void)
{
	return "CPU (no DSP)";
}
