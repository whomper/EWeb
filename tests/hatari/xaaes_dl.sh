#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
#
# The download's box under FreeMiNT + XaAES (the guidelines' 3.5), with no
# network: build/dltest/EWEB.PRG (-DEW_TEST_DL) opens it at start, twice,
# fed 40 KB a second. Moved by its title bar (the bar drawn again after
# the move), closed with its closer (Cancel), then Esc (Cancel).
# AERA-Atari's common/tests/xaaes (make base there first) and EmuTOS 1.4.
#   tests/hatari/xaaes_dl.sh XAAES_DIR EMUTOS.IMG [WORK]
set -e
X=${1:?AERA-Atari\'s common/tests/xaaes}
TOS=${2:?EmuTOS 1.4 (etos512us.img)}
W=${3:-build/xaaes_dl}
make build/dltest/EWEB.PRG binary/EWEB.FNT
mkdir -p "$W.files"
cp binary/EWEB.FNT "$W.files/EWEB.FNT"
printf 'HOME=about:blank\r\n' > "$W.files/EWEB.INF"
python3 "$X/run.py" "$W" "$TOS" build/dltest/EWEB.PRG --copy "$W.files/EWEB.FNT" "$W.files/EWEB.INF" \
	--steps "until download test round 1 300;wait 4;shot;drag 320 174 320 104;wait 4;shot;click 152 104;until download test round 2 60;wait 4;shot;key 0x01;wait 4;shot;wait 3"
n=$(grep -ac "download box closed 2 " "$W/hatari.log" || true)
w=$(grep -ac "download box area .* window" "$W/hatari.log" || true)
echo "xaaes_dl: $n of 2 boxes cancelled, $w of 2 in a window; shots in $W/shots"
[ "$n" = 2 ] && [ "$w" = 2 ]
