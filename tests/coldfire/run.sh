#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# tests/coldfire/run.sh: EWEBJSFB.OVL's numbers (jsmath.c: musl's maths as
# the overlay builds them, and atari/cfmath.c) as ColdFire code under
# qemu-m68k -cpu cfv4e, against the host's build of the same sources.
# Needs the m68k cross compiler and qemu-user (qemu-m68k).
set -e
cd "$(dirname "$0")/../.."
B=build/cftest
CC=m68k-linux-gnu-gcc
GCCINC=$($CC -print-file-name=include)
RT=common/tests/coldfire
LIBM=third_party/libm
mkdir -p $B/cf $B/host
CF="-mcpu=5475 -O2 -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-tree-loop-distribute-patterns -nostdinc -isystem $GCCINC -Iatari/include -Icommon/atari/include"
M="-I$LIBM/eweb -include $LIBM/eweb/features.h -I$LIBM/src -w"
# (on the host musl's headers but features.h, the C library's own)
mkdir -p $B/hinc
for h in math.h fenv.h endian.h; do cp $LIBM/eweb/$h $B/hinc/; done
HOSTM="-O2 -fno-builtin -include $LIBM/eweb/features.h -I$B/hinc -I$LIBM/src -w"
$CC -mcpu=5475 -c -o $B/cf/rt.o $RT/rt.S
$CC $CF -I$RT -Icommon/src -c -o $B/cf/libc.o $RT/libc.c
objs="$B/cf/rt.o $B/cf/libc.o"
for f in $LIBM/src/*.c; do
	o=$B/cf/$(basename "$f" .c).o
	$CC $CF $M -c -o "$o" "$f"
	objs="$objs $o"
	cc $HOSTM -c -o "$B/host/$(basename "$f" .c).o" "$f"
done
$CC $CF -c -o $B/cf/cfmath.o atari/cfmath.c
$CC $CF -c -o $B/cf/ll64.o common/atari/ll64.c
$CC $CF -I$RT -c -o $B/cf/jsmath.o tests/coldfire/jsmath.c
$CC -mcpu=5475 -nostdlib -static -no-pie -Wl,-z,noexecstack -o $B/jsmath $B/cf/jsmath.o $B/cf/cfmath.o $objs $B/cf/ll64.o
cc -O2 -fno-builtin -DHOST -o $B/jsmath_host tests/coldfire/jsmath.c atari/cfmath.c $B/host/*.o
./$B/jsmath_host > $B/host.txt
qemu-m68k -cpu cfv4e $B/jsmath > $B/cf.txt
cat $B/cf.txt
cmp $B/host.txt $B/cf.txt && echo "jsmath: the ColdFire's maths give the host's every answer"
