/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* inttypes.h for QuickJS: EWeb's, and the 64-bit formats its dumps name */
#ifndef QJS_INTTYPES_H
#define QJS_INTTYPES_H
#include_next <inttypes.h>
#define PRId64 "lld"
#define PRIu64 "llu"
#define PRIx64 "llx"
#endif
