#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""deep.py [--depth N ...] [--prg EWEB.PRG]: EWEB.PRG on a graphics card's
screen, deeper than the Falcon's (DESIGN.md, Screens deeper than the
Falcon's), in ARAnyM: fVDI's ARAnyM driver at 32, 24 or 16 bits a
pixel under FreeMiNT 1.17.0 and XaAES, as on a CT60's Radeon or a
FireBee. Hatari has no such screen.

For each depth EWeb starts by itself (XAAES.CNF's run line) on a page of
colour swatches (HOME=), and the run passes when:
  - EWeb's log (NatFeats) names the screen's form (ui/tcpix.c) and says
    it is ready in true colour, with the page shown;
  - each swatch on the screen is the colour EWeb draws it: at 24 and 32
    bits the page's own (drawn at 32 bits, DESIGN.md, True 32 bits), at
    15 and 16 its 5-6-5 widened as ui/tcpix.c widens it (within the
    emulator's own widening of its 5-bit parts).
Then Edit > Find's dialog is opened over the page (^F) and closed
(Return), and the swatches are checked again (the page put back under
it). Pictures of the window are kept in build/aranym (DEPTH.png,
DEPTH-dialog.png, DEPTH-find.png). fVDI's ARAnyM driver has no 15-bit
mode (it gives 16): 15 bits are tests/screen/tctest.c's.

Needs aranym (apt install aranym), Xvfb, mtools (mformat, mcopy) and
ImageMagick's import; FreeMiNT from common/tests/xaaes/fetch.py's
archive, fVDI's snapshot and EmuTOS 1.4 (common/tools/machines.py), each
fetched once and checked against its SHA-256. ARAnyM 1.1's EmuTOS is the
512 KB image: its own 1024 KB one isn't taken, and with the ROM's boot
drive A: the hard disk's AUTO folder isn't run, so FreeMiNT is started
as ARAnyM's bootstrap, and fVDI by MINT.CNF's exec line before XaAES.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import time
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "build", "aranym")
sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
sys.path.insert(0, os.path.join(ROOT, "common", "tests", "xaaes"))
import machines  # noqa: E402
import fetch as mintfetch  # noqa: E402

FVDI = ("https://tho-otto.de/snapshots/fvdi/fvdi-0.96a-35a-020.zip",
        "fa22a1ab915e3af132cf82c98b0c4e6038373f302c9eaf72e86ae1e4a67def1e")

# the swatches: each colour as the page gives it
SWATCHES = [0xff0000, 0x00ff00, 0x0000ff, 0xe2a76f, 0x808080, 0xffffff, 0x123456, 0xb87333]
SW_W, SW_H = 60, 40


def page():
    cells = "".join("<div style='float:left;width:%dpx;height:%dpx;background:#%06x'></div>" % (SW_W, SW_H, c)
                    for c in SWATCHES)
    return ("<!DOCTYPE html><title>deep</title><body style='margin:0;background:#000'>%s</body>\n" % cells)


def fvdi_zip():
    path = os.path.join(OUT, "fvdi.zip")
    if not os.path.exists(path):
        os.makedirs(OUT, exist_ok=True)
        print("fetching %s" % FVDI[0], flush=True)
        with urllib.request.urlopen(FVDI[0], timeout=120) as r, open(path + ".part", "wb") as f:
            shutil.copyfileobj(r, f)
        os.replace(path + ".part", path)
    if hashlib.sha256(open(path, "rb").read()).hexdigest() != FVDI[1]:
        sys.exit("deep.py: %s is not the expected fVDI snapshot" % path)
    return zipfile.ZipFile(path)


def tree(depth, prg, root):
    """C:'s files for one run, in root"""
    shutil.rmtree(root, ignore_errors=True)
    z = zipfile.ZipFile(mintfetch.get_zip(None))
    for name in z.namelist():		# (as fetch.py takes XaAES: the 68030's module as XAAES.KM)
        base = os.path.basename(name)
        if not name.startswith(mintfetch.XA) or name.endswith("/") or \
           (base.endswith(".km") and base != "xaaes030.km") or base in ("xaaes_de.rsc", "xa_he_de.txt"):
            continue
        out = os.path.join(root, name if base != "xaaes030.km" else mintfetch.XA + "xaaes.km")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as f:
            f.write(z.read(name))
    cnf = os.path.join(ROOT, "common", "tests", "xaaes", "cnf")
    shutil.copy(os.path.join(cnf, "mint.ini"), os.path.join(root, "mint", "1-17-0", "mint.ini"))
    with open(os.path.join(cnf, "mint.cnf")) as f:
        mint = f.read()
    with open(os.path.join(root, "mint", "1-17-0", "mint.cnf"), "w") as f:
        f.write("exec u:\\c\\fvdi.prg\n" + mint)		# (the VDI before the AES)
    shutil.copy(os.path.join(cnf, "xaaes.cnf"), os.path.join(root, mintfetch.XA, "xaaes.cnf"))
    with open(os.path.join(root, mintfetch.XA, "xaaes.cnf"), "a") as f:
        f.write("run c:\\eweb.prg\n")
    os.makedirs(os.path.join(root, "clipbrd"), exist_ok=True)
    fz = fvdi_zip()
    os.makedirs(os.path.join(root, "gemsys"), exist_ok=True)
    with open(os.path.join(root, "fvdi.prg"), "wb") as f:
        f.write(fz.read("fvdi/auto/fvdi.prg"))
    with open(os.path.join(root, "gemsys", "aranym.sys"), "wb") as f:
        f.write(fz.read("fvdi/gemsys/aranym.sys"))
    with open(os.path.join(root, "fvdi.sys"), "w", newline="") as f:
        f.write("PATH = C:\\GEMSYS\r\nbooted\r\n01r aranym.sys mode 800x600x%d@60\r\n" % depth)
    shutil.copy(prg, os.path.join(root, "eweb.prg"))
    shutil.copy(os.path.join(ROOT, "binary", "EWEB.FNT"), os.path.join(root, "eweb.fnt"))
    with open(os.path.join(root, "deep.htm"), "w") as f:
        f.write(page())
    with open(os.path.join(root, "eweb.inf"), "w", newline="") as f:
        f.write("HOME=C:\\DEEP.HTM\r\n")
    return z


def disk(root, img):
    """a 30 MB disk of one FAT16 partition (MBR) with root's files, each
    word's bytes turned as ARAnyM keeps an IDE disk"""
    size = 30 * 2048
    mbr = bytearray(512)
    mbr[446:462] = bytes([0, 0, 0, 0, 6, 0, 0, 0]) + (2048).to_bytes(4, "little") + (size - 2048).to_bytes(4, "little")
    mbr[510:512] = b"\x55\xaa"
    with open(img, "wb") as f:
        f.write(mbr)
        f.truncate(size * 512)
    part = img + "@@1M"
    subprocess.run(["mformat", "-i", part, "-v", "EWEB", "::"], check=True)
    subprocess.run(["mcopy", "-s", "-i", part] + [os.path.join(root, n) for n in os.listdir(root)] + ["::"], check=True)
    b = bytearray(open(img, "rb").read())
    b[0::2], b[1::2] = b[1::2], b[0::2]
    open(img, "wb").write(b)


def config(path, kernel, img):
    with open(path, "w") as f:
        f.write("[GLOBAL]\nFastRAM = 64\nBootstrap = %s\nBootstrapArgs = MEM_PROT=NO\nEmuTOS = %s\n"
                "BootDrive = C\n[STARTUP]\nGrabMouse = No\nDebugger = No\n[VIDEO]\nFullScreen = No\n"
                "BootColorDepth = 8\n[IDE0]\nPresent = Yes\nIsCDROM = No\nByteSwap = No\nReadOnly = No\n"
                "Path = %s\nCylinders = 61\nHeads = 16\nSectorsPerTrack = 63\nModelName = Disk\n"
                % (kernel, machines.emutos(), img))


def widened(c, depth):
    """the colour EWeb draws c on a screen of depth bits: at 24 and 32
    bits its own (the page drawn at 32 bits, a byte a colour), else its
    5-6-5 widened"""
    r, g, b = c >> 16, c >> 8 & 255, c & 255
    if depth >= 24:
        return (r, g, b)
    r5, g6, b5 = r >> 3, g >> 2, b >> 3
    return ((r5 << 3 | r5 >> 2), (g6 << 2 | g6 >> 4), (b5 << 3 | b5 >> 2))


def run(depth, prg, display):
    work = os.path.join(OUT, str(depth))
    root = os.path.join(work, "c")
    z = tree(depth, prg, root)
    kernel = os.path.join(work, "mintara.prg")
    with open(kernel, "wb") as f:
        f.write(z.read("auto/mintara.prg"))
    img = os.path.join(work, "disk.img")
    disk(root, img)
    cfg = os.path.join(work, "config")
    config(cfg, kernel, img)
    env = dict(os.environ, DISPLAY=display, SDL_AUDIODRIVER="dummy")
    log = os.path.join(work, "aranym.log")
    fails = []
    with open(log, "w") as lf:
        p = subprocess.Popen(["aranym", "-c", cfg, "-N"], env=env, stdout=lf, stderr=subprocess.STDOUT)
    try:
        t0 = time.time()
        text = ""
        while time.time() - t0 < 150:
            time.sleep(2)
            text = open(log, errors="replace").read()
            if "EWEB shown" in text and "EWEB trimmed" in text:
                break
            if p.poll() is not None:
                break
        time.sleep(3)
        text = open(log, errors="replace").read()
        win = subprocess.run(["xdotool", "search", "--name", "ARAnyM"], env=env, capture_output=True,
                             text=True).stdout.split()
        shot = os.path.join(OUT, "%d.png" % depth)
        shot2 = os.path.join(OUT, "%d-find.png" % depth)
        if win:
            subprocess.run(["import", "-window", win[0], shot], env=env, check=False)
            # a dialog over the page (Edit > Find, ^F), closed (Return):
            # the page under it put back
            subprocess.run(["xdotool", "windowfocus", win[0]], env=env, check=False)
            subprocess.run(["xdotool", "key", "ctrl+f"], env=env, check=False)
            time.sleep(4)
            subprocess.run(["import", "-window", win[0], os.path.join(OUT, "%d-dialog.png" % depth)], env=env,
                           check=False)
            subprocess.run(["xdotool", "key", "Return"], env=env, check=False)
            time.sleep(4)
            subprocess.run(["import", "-window", win[0], shot2], env=env, check=False)
    finally:
        p.kill()
        p.wait()
    lines = [ln for ln in text.splitlines() if ln.startswith("EWEB ")]
    screen = next((ln for ln in lines if ln.startswith("EWEB screen:")), None)
    ready = next((ln for ln in lines if ln.startswith("EWEB ready")), None)
    print("%d bits: %s" % (depth, screen or "no screen line"))
    print("%d bits: %s" % (depth, ready or "not ready"))
    if screen is None or ready is None or "true colour" not in ready:
        fails.append("%d bits: EWeb not ready in true colour (see %s)" % (depth, log))
    if not any(ln.startswith("EWEB shown") for ln in lines):
        fails.append("%d bits: the page not shown" % depth)
    if fails or not os.path.exists(shot):
        return fails or ["%d bits: no picture of the screen" % depth]
    fails += swatches(depth, shot, "")
    if os.path.exists(shot2):
        fails += swatches(depth, shot2, " after the Find dialog")
    return fails


def swatches(depth, shot, when):
    """the swatches on the screen in shot, each the colour EWeb draws"""
    fails = []
    # the swatches' row: the first run of the first swatch's colour at
    # least 20 pixels long (not the closer's red), its length a swatch's
    # width as EWeb draws it (CSS pixels to the window's)
    from PIL import Image
    im = Image.open(shot).convert("RGB")
    want0 = widened(SWATCHES[0], depth)
    px = im.load()
    tol = 0 if depth >= 24 else 8

    def near(p, w):
        return all(abs(a - b) <= tol for a, b in zip(p, w))
    at = None
    for y in range(im.height):
        x = 0
        while x < im.width and at is None:
            if near(px[x, y], want0):
                n = 0
                while x + n < im.width and near(px[x + n, y], want0):
                    n += 1
                if n >= 20:
                    at, sw = (x, y), n
                x += n
            else:
                x += 1
        if at:
            break
    if at is None:
        return ["%d bits%s: the first swatch %s not found on the screen" % (depth, when, want0)]
    for k, c in enumerate(SWATCHES):
        x, y = at[0] + k * sw + sw // 2, at[1] + 4
        got, want = px[x, y], widened(c, depth)
        ok = all(abs(a - b) <= tol for a, b in zip(got, want))
        print("%d bits%s: #%06x at %d,%d: %s, want %s%s" % (depth, when, c, x, y, got, want, "" if ok else "  FAIL"))
        if not ok:
            fails.append("%d bits%s: #%06x drawn %s, want %s" % (depth, when, c, got, want))
    return fails


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--depth", type=int, action="append", choices=(16, 24, 32))
    ap.add_argument("--prg", default=os.path.join(ROOT, "binary", "EWEB.PRG"))
    ap.add_argument("--display", default=":93")
    a = ap.parse_args()
    for tool in ("aranym", "mformat", "mcopy", "import", "xdotool"):
        if shutil.which(tool) is None:
            sys.exit("deep.py: %s is needed (aranym: apt install aranym)" % tool)
    os.makedirs(OUT, exist_ok=True)
    xvfb = machines.xvfb(a.display, "1024x768x24")
    fails = []
    try:
        for d in a.depth or (32, 24, 16):
            fails += run(d, a.prg, a.display)
    finally:
        machines.xvfb_stop(xvfb)
    for f in fails:
        print("FAIL " + f)
    print("deep.py: %s" % ("%d failed" % len(fails) if fails else "passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
