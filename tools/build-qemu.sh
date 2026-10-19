#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Builds QEMU's 68k Linux emulator with plugins (Debian's and Ubuntu's
# qemu-user has none) for tools/count68k.sh: QEMU 9.2.0 from github.com/qemu,
# in build/qemu (or $1). Needs: git, gcc, ninja-build, libglib2.0-dev,
# python3-venv. About ten minutes.
set -e
dir=${1:-build/qemu}
mkdir -p "$dir"
cd "$dir"
[ -d src ] || git clone -q --depth 1 --branch v9.2.0 https://github.com/qemu/qemu src
mkdir -p out
cd out
../src/configure --target-list=m68k-linux-user --enable-plugins --disable-system \
    --disable-docs --disable-tools --disable-werror --disable-debug-info > configure.log
# hotblocks lists 20 blocks; every one, for a profile by function
sed -i 's/^static guint64 limit = 20;/static guint64 limit = 1000000;/' ../src/contrib/plugins/hotblocks.c
ninja qemu-m68k tests/tcg/plugins/libinsn.so contrib/plugins/libcache.so contrib/plugins/libhotblocks.so > ninja.log
echo "$(pwd)/qemu-m68k"
