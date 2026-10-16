#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Counts the instructions a Linux 68030 program runs (make bench68k):
#   tools/count68k.sh build/m68k/nsbench -q PAGE.HTM SHEET.CSS
# prints the program's own output, then "insns N". QEMU with plugins comes
# from tools/build-qemu.sh; QEMU_DIR says where it is (default build/qemu).
# The count is exact for the code; time on a real 68030 also depends on its
# caches and memory (docs/DESIGN.md has the figures used).
q=${QEMU_DIR:-build/qemu}/out
"$q/qemu-m68k" -cpu m68030 -plugin "$q/tests/tcg/plugins/libinsn.so" -d plugin -D /dev/stderr "$@" \
    2> /tmp/count68k.$$ 
rc=$?
sed -n 's/^total insns: /insns /p' /tmp/count68k.$$
rm -f /tmp/count68k.$$
exit $rc
