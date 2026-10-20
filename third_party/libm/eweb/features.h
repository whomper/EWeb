/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* features.h - what musl's libm sources take from musl's own (given
   to each with -include) */
#ifndef FEATURES_H
#define FEATURES_H
#define hidden
#define weak __attribute__((__weak__))
#define weak_alias(old, new) extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))
#endif
