#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
#
# EWEB.ACC beside EWEB.PRG under FreeMiNT + XaAES (the guidelines' 2.3,
# common's twin.c; DESIGN.md, The accessory: EWEB.ACC): XaAES loads
# EWEB.ACC from the root (the TT: 16 MB of TT RAM, its heap there), then
# EWEB.PRG is started (run.py --later) with no address: it finds its
# accessory (appl_search), hands it VA_START and goes, and the
# accessory's window shows the home page. AERA-Atari's common/tests/xaaes
# (make base and build/LATER.PRG there first) and EmuTOS 1.4.
#   tests/hatari/xaaes_acc.sh XAAES_DIR EMUTOS.IMG [WORK]
set -e
X=${1:?AERA-Atari\'s common/tests/xaaes}
TOS=${2:?EmuTOS 1.4 (etos512us.img)}
W=${3:-build/xaaes_acc}
make binary/EWEB.PRG binary/EWEB.FNT
mkdir -p "$W.files"
cp binary/EWEB.FNT "$W.files/EWEB.FNT"
cp binary/EWEB.PRG "$W.files/EWEB.ACC"
cp tests/bench/sample.html "$W.files/SAMPLE.HTM"
printf 'HOME=file:///C:/SAMPLE.HTM\r\nNATFEATS=1\r\nFOLDER=C:\\\r\n' > "$W.files/EWEB.INF"
python3 "$X/run.py" "$W" "$TOS" binary/EWEB.PRG --machine tt --later 20 \
	--copy "$W.files/EWEB.FNT" "$W.files/EWEB.ACC" "$W.files/EWEB.INF" "$W.files/SAMPLE.HTM" \
	--steps "until accessory: heap 300;until shown file:///C:/SAMPLE.HTM 300;wait 4;shot;wait 2"
acc=$(grep -ac "accessory: heap .* in TT RAM" "$W/hatari.log" || true)
opened=$(grep -ac "^EWEB accessory opened by VA_START" "$W/hatari.log" || true)
win=$(grep -ac "^EWEB window 1 of" "$W/hatari.log" || true)
shown=$(grep -ac "^EWEB shown file:///C:/SAMPLE.HTM" "$W/hatari.log" || true)
echo "xaaes_acc: accessory's heap in TT RAM $acc, opened by the program's VA_START $opened, windows $win, pages shown $shown; shots in $W/shots"
# one window, the accessory's: the program handed it over and opened none
[ "$acc" = 1 ] && [ "$opened" = 1 ] && [ "$win" = 1 ] && [ "$shown" = 1 ]
