#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Hebrew pages through Hubbub: the same page in UTF-8, Windows-1255 and
# ISO-8859-8 under each of its labels (iso-8859-8 and "visual" for visual
# order, iso-8859-8-i and "logical" for logical order) must give the text
# in expected.txt.  tests/hebrew/run.sh HEBTEST [EMULATOR]
here=$(dirname "$0")
prog=$1
shift
out=$(mktemp)
failed=0
for page in "$here"/*.HTM; do
	if "$@" "$prog" "$page" "$out" > /dev/null && cmp -s "$out" "$here/expected.txt"; then
		echo "ok   $(basename "$page")"
	else
		echo "FAIL $(basename "$page")"
		diff "$here/expected.txt" "$out" | head -10
		failed=$((failed + 1))
	fi
done
rm -f "$out"
[ $failed -eq 0 ]
