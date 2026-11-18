#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# tests/coldfire/run.sh: EWEBJSFB.OVL's numbers (jsmath.c: musl's maths as
# the overlay builds them, and atari/cfmath.c) as ColdFire code under
# qemu-m68k -cpu cfv4e, against the host's build of the same sources; and
# EWEBJS.OVL's (the 68030 with its 68881, built with the Makefile's
# TOSJS_FPU: atari/js_fpu.h) under qemu-m68k -cpu m68030, against the same.
# Needs the m68k cross compiler and qemu-user (qemu-m68k).
set -e
cd "$(dirname "$0")/../.."
B=build/cftest
CC=m68k-linux-gnu-gcc
GCCINC=$($CC -print-file-name=include)
RT=common/tests/coldfire
LIBM=third_party/libm
mkdir -p $B/cf $B/host $B/030
F030="-m68030 -m68881 -DJS_FPU_DOUBLE -U__FLT_EVAL_METHOD__ -D__FLT_EVAL_METHOD__=0 -O2 -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-tree-loop-distribute-patterns -nostdinc -isystem $GCCINC -Iatari -Iatari/include -Icommon/atari/include"
CF="-mcpu=5475 -O2 -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-tree-loop-distribute-patterns -nostdinc -isystem $GCCINC -Iatari/include -Icommon/atari/include"
M="-I$LIBM/eweb -include $LIBM/eweb/features.h -I$LIBM/src -w"
# (on the host musl's headers but features.h, the C library's own)
mkdir -p $B/hinc
for h in math.h fenv.h endian.h; do cp $LIBM/eweb/$h $B/hinc/; done
HOSTM="-O2 -fno-builtin -include $LIBM/eweb/features.h -I$B/hinc -I$LIBM/src -w"
$CC -mcpu=5475 -c -o $B/cf/rt.o $RT/rt.S
$CC $CF -I$RT -Icommon/src -c -o $B/cf/libc.o $RT/libc.c
$CC -m68030 -c -o $B/030/rt.o $RT/rt.S
$CC $F030 -I$RT -Icommon/src -c -o $B/030/libc.o $RT/libc.c
objs="$B/cf/rt.o $B/cf/libc.o"
o030="$B/030/rt.o $B/030/libc.o"
for f in $LIBM/src/*.c; do
	o=$B/cf/$(basename "$f" .c).o
	$CC $CF $M -c -o "$o" "$f"
	objs="$objs $o"
	o=$B/030/$(basename "$f" .c).o
	$CC $F030 $M -c -o "$o" "$f"
	o030="$o030 $o"
	cc $HOSTM -c -o "$B/host/$(basename "$f" .c).o" "$f"
done
$CC $CF -c -o $B/cf/cfmath.o atari/cfmath.c
$CC $CF -c -o $B/cf/ll64.o common/atari/ll64.c
$CC $CF -Iatari -I$RT -c -o $B/cf/jsmath.o tests/coldfire/jsmath.c
$CC -mcpu=5475 -nostdlib -static -no-pie -Wl,-z,noexecstack -o $B/jsmath $B/cf/jsmath.o $B/cf/cfmath.o $objs $B/cf/ll64.o
$CC $F030 -c -o $B/030/cfmath.o atari/cfmath.c
$CC $F030 -I$RT -c -o $B/030/jsmath.o tests/coldfire/jsmath.c
$CC -m68030 -nostdlib -static -no-pie -Wl,-z,noexecstack -o $B/jsmath030 $B/030/jsmath.o $B/030/cfmath.o $o030 -lgcc
cc -O2 -fno-builtin -DHOST -Iatari -o $B/jsmath_host tests/coldfire/jsmath.c atari/cfmath.c $B/host/*.o
./$B/jsmath_host > $B/host.txt
qemu-m68k -cpu cfv4e $B/jsmath > $B/cf.txt
cat $B/cf.txt
cmp $B/host.txt $B/cf.txt && echo "jsmath: the ColdFire's maths give the host's every answer"
qemu-m68k -cpu m68030 $B/jsmath030 > $B/030.txt
cat $B/030.txt
cmp $B/host.txt $B/030.txt && echo "jsmath: the 68030's with its 68881 give the host's every answer"
# EWEB.FNT through font.c and inflate.c as EWEBFB.PRG has them (build/cf's
# objects: make binary/EWEBFB.PRG first) against the host's build: every
# face, every block, every glyph (a FireBee tester's crash on scrolling
# from 1.17, 18 Nov 2026; tests/coldfire/fonttest.c)
if [ -f build/cf/ewb/gfx/font.o ] && [ -f binary/EWEB.FNT ]; then
	$CC $CF -I$RT -Isrc -Isrc/gfx -Icommon/src -c -o $B/cf/fonttest.o tests/coldfire/fonttest.c
	$CC -mcpu=5475 -nostdlib -static -no-pie -Wl,-z,noexecstack -o $B/fonttest $B/cf/rt.o $B/cf/libc.o \
		$B/cf/fonttest.o build/cf/ewb/gfx/font.o build/cf/net/inflate.o
	cc -O2 -std=gnu99 -Isrc -Isrc/gfx -Icommon/src -o $B/fonttest_host tests/coldfire/fonttest.c \
		src/gfx/font.c common/src/inflate.c common/src/util.c
	./$B/fonttest_host binary/EWEB.FNT > $B/font_host.txt
	qemu-m68k -cpu cfv4e $B/fonttest binary/EWEB.FNT > $B/font_cf.txt
	tail -n 1 $B/font_cf.txt
	cmp $B/font_host.txt $B/font_cf.txt && echo "fonttest: EWEBFB.PRG's fonts give the host's every glyph"
else
	echo "fonttest: no build/cf objects (make binary/EWEBFB.PRG) or binary/EWEB.FNT: not run"
fi
