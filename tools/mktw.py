#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Tailwind's utilities for EWeb (src/browse/tw.c; DESIGN.md, Tailwind's
utilities made by EWeb).

    tools/mktw.py --from-tailwind DIR      src/css/tw.txt from Tailwind 3
    tools/mktw.py src/css/tw.txt OUT.h     the table EWeb is built with
    tools/mktw.py --cases DIR              tests/browse/twcases.txt

--from-tailwind runs Tailwind 3 (npm install tailwindcss@3.4.17 in DIR;
node) once for every utility it has and its base sheet (preflight), and
keeps in src/css/tw.txt only what EWeb's CSS engine reads (the properties
it knows, the custom properties they use), as families where it can: a
colour family (text-red-500, its /50) from the palette, a family of a
theme's scale (mx-4, w-1/2) from the scale, the rest one rule each. The
data is Tailwind's (MIT, Copyright (c) Tailwind Labs, Inc.). The second
form writes build/gen/tw_data.h from it, sorted for a binary search.
--cases writes what Tailwind makes of each class in tests/browse/twclasses.txt
(as kept: what EWeb reads), for tests/browse/twtest.c to hold EWeb's to."""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
COLOURS = ("slate gray zinc neutral stone red orange amber yellow lime green emerald teal cyan sky blue "
           "indigo violet purple fuchsia pink rose").split()
# sections whose values aren't classes' tokens, or that EWeb has no use for
NOSEC = ("colors", "screens", "aria", "content", "fontFamily", "animation", "keyframes", "supports", "data")


def known_props():
    out = set()
    d = os.path.join(ROOT, "src", "css")
    for f in os.listdir(d):
        if f.endswith((".c", ".h")):
            out.update(re.findall(r'"([a-z][a-z-]+)"', open(os.path.join(d, f), encoding="utf-8").read()))
    return out


def decls(body):
    out = []
    for d in body.split(";"):
        d = d.strip()
        if ":" in d:
            k, v = d.split(":", 1)
            out.append((k.strip(), " ".join(v.split())))
    return out


def run_tailwind(src):
    cfg_all = os.path.join(src, "eweb_all.config.js")
    cfg_base = os.path.join(src, "eweb_base.config.js")
    with open(cfg_all, "w") as f:
        f.write('module.exports = { content: [{raw: "x"}], safelist: [{pattern: /.*/}], '
                'corePlugins: { preflight: false } }\n')
    with open(cfg_base, "w") as f:
        f.write('module.exports = { content: [{raw: "x"}] }\n')
    with open(os.path.join(src, "eweb_u.css"), "w") as f:
        f.write("@tailwind utilities;\n")
    with open(os.path.join(src, "eweb_b.css"), "w") as f:
        f.write("@tailwind base;\n")
    for c, i, o in ((cfg_all, "eweb_u.css", "eweb_all.css"), (cfg_base, "eweb_b.css", "eweb_base.css")):
        subprocess.run(["npx", "tailwindcss", "-c", c, "-i", i, "-o", o], cwd=src, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    theme = subprocess.run(["node", "-e", 'const r=require("tailwindcss/resolveConfig");'
                            'process.stdout.write(JSON.stringify(r({content:["x"]}).theme))'],
                           cwd=src, check=True, capture_output=True, text=True).stdout
    return (open(os.path.join(src, "eweb_all.css")).read(), open(os.path.join(src, "eweb_base.css")).read(),
            json.loads(theme))


def from_tailwind(src):
    allcss, basecss, theme = run_tailwind(src)
    props = known_props()
    rules = []
    for m in re.finditer(r'(?m)^\.((?:\\.|[^\s{,>:])+)([^{]*)\{([^}]*)\}', allcss):
        n, s = m.group(1).replace("\\", ""), " ".join(m.group(2).split())
        if "-moz-" in s or "-webkit-" in s or re.search(r'-(transparent|current|inherit)/\d+$', n):
            continue
        rules.append((n, s, decls(m.group(3))))
    base = []
    basecss = re.sub(r'(?s)/\*.*?\*/', '', basecss)
    for m in re.finditer(r'(?ms)^([^{}@][^{]*?)\s*\{([^}]*)\}', basecss):
        base.append((" ".join(m.group(1).split()), decls(m.group(2))))
    # what EWeb reads: its properties, and the custom properties they name
    def keep_plain(ds):
        return [(k, v) for k, v in ds if not k.startswith("-") and k in props]
    used = set()
    for _, _, ds in rules:
        for k, v in keep_plain(ds):
            used.update(re.findall(r'var\((--[\w-]+)', v))
    for _, ds in base:
        for k, v in keep_plain(ds):
            used.update(re.findall(r'var\((--[\w-]+)', v))
    # (a custom property set from another: its names too, until none is new)
    while True:
        more = set()
        for _, _, ds in rules:
            for k, v in ds:
                if k in used:
                    more.update(re.findall(r'var\((--[\w-]+)', v))
        if more <= used:
            break
        used |= more

    def keep(ds):
        return [(k, v) for k, v in ds if (k.startswith("--") and k in used) or (not k.startswith("-") and k in props)]

    def text(ds):
        return "; ".join("%s: %s" % kv for kv in ds)
    out = ["# Tailwind 3.4.17's utilities as EWeb reads them: tools/mktw.py --from-tailwind.",
           "# The data is Tailwind's: MIT licence, Copyright (c) Tailwind Labs, Inc.",
           "# B selector<TAB>body: the base sheet (preflight); P colour rgb hex: the palette;",
           "# C order prefix suffix plain opacity: a colour family ($C r g b, $H #hex, $A alpha);",
           "# S section token value: a scale; F order prefix suffix section body: its family ($V);",
           "# R order name suffix body: one rule. order: Tailwind's, for the cascade."]
    for sel, ds in base:
        ds = keep(ds)
        if ds:
            out.append("B\t%s\t%s" % (sel, text(ds)))
    pal = {}
    for n, s, ds in rules:
        m = re.match(r'^bg-(\w+)-(\d+)$', n)
        if m and m.group(1) in COLOURS and not s:
            rgb = re.search(r'rgb\((\d+ \d+ \d+)', text(ds)).group(1)
            pal[m.group(1) + "-" + m.group(2)] = rgb
    pal["black"] = "0 0 0"
    pal["white"] = "255 255 255"
    for k in sorted(pal):
        r, g, b = (int(x) for x in pal[k].split())
        out.append("P\t%s\t%s\t#%02x%02x%02x" % (k, pal[k], r, g, b))
    cre = re.compile(r'^(.*)-((?:%s)-\d+|black|white)(/\d+)?$' % "|".join(COLOURS))
    cfam = {}
    rest = []
    for i, (n, s, ds) in enumerate(rules):
        m = cre.match(n)
        if not m:
            rest.append((i, n, s, ds))
            continue
        pre, col, op = m.groups()
        ds = keep(ds)
        if not ds:
            continue
        r, g, b = (int(x) for x in pal[col].split())
        body = text(ds).replace(pal[col], "$C").replace("#%02x%02x%02x" % (r, g, b), "$H")
        key = (pre, s)
        e = cfam.setdefault(key, [i, None, None])
        if op is None and e[1] is None:
            e[1] = body
        elif op == "/50" and e[2] is None:
            e[2] = body.replace(" / 0.5)", " / $A)")
    for (pre, s), (i, plain, opa) in sorted(cfam.items(), key=lambda x: x[1][0]):
        if plain is not None and opa is not None:
            out.append("C\t%d\t%s\t%s\t%s\t%s" % (i, pre, s or "-", plain, opa))
    # the scales' families
    secs = {}
    for k, v in theme.items():
        if k in NOSEC or not isinstance(v, dict):
            continue
        m = {kk: vv for kk, vv in v.items() if isinstance(vv, str) and vv and kk != "DEFAULT"}
        if len(m) >= 3:
            secs[k] = m
    cands = {}
    for i, n, s, ds in rest:
        body = text(ds)
        for sec, m in secs.items():
            for k, v in m.items():
                if n.endswith("-" + k) and v in body:
                    cands.setdefault((n[:-len(k) - 1], s, sec, body.replace(v, "$V")), {})[k] = i
    best = {}
    for key, toks in cands.items():
        for k, i in toks.items():
            if i not in best or len(toks) > len(cands[best[i]]):
                best[i] = key
    fams = {}
    for i, key in best.items():
        if len(cands[key]) >= 4:
            fams.setdefault(key, []).append(i)
    covered = set(i for v in fams.values() for i in v)
    used_secs = set()
    for (pre, s, sec, body), idx in sorted(fams.items(), key=lambda x: min(x[1])):
        body = text(keep(decls(body.replace("$V", "\x01")))).replace("\x01", "$V")
        if "$V" not in body:
            continue
        used_secs.add(sec)
        out.append("F\t%d\t%s\t%s\t%s\t%s" % (min(idx), pre, s or "-", sec, body))
    for sec in sorted(used_secs):
        for k, v in secs[sec].items():
            out.append("S\t%s\t%s\t%s" % (sec, k, v))
    for i, n, s, ds in rest:
        if i in covered:
            continue
        ds = keep(ds)
        if ds:
            out.append("R\t%d\t%s\t%s\t%s" % (i, n, s or "-", text(ds)))
    with open(os.path.join(ROOT, "src", "css", "tw.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")


def to_header(txt, outh):
    """the tables, NUL-separated in sections and packed with raw DEFLATE
    (tw.c unpacks them on the first page that needs them: 55 KB of
    EWEB.PRG kept to about 10)"""
    import zlib
    base, pal, cfam, secs, fams, rules = [], [], [], {}, [], []
    for line in open(txt, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line or line[0] == "#":
            continue
        f = line.split("\t")
        if f[0] == "B":
            base.append("%s{%s}" % (f[1], f[2]))
        elif f[0] == "P":
            r, g, b = f[2].split()
            pal.append((f[1], int(r), int(g), int(b)))
        elif f[0] == "C":
            cfam.append((int(f[1]), f[2], "" if f[3] == "-" else f[3], f[4], f[5]))
        elif f[0] == "S":
            secs.setdefault(f[1], []).append((f[2], f[3]))
        elif f[0] == "F":
            fams.append((int(f[1]), f[2], "" if f[3] == "-" else f[3], f[4], f[5]))
        elif f[0] == "R":
            rules.append((f[2], int(f[1]), "" if f[3] == "-" else f[3], f[4]))
    sec_names = sorted(secs)
    # Tailwind's order, as ranks (in 16 bits)
    rank = {v: i for i, v in enumerate(sorted(set([x[0] for x in cfam] + [x[0] for x in fams] + [x[1] for x in rules])))}
    cfam = [(rank[a],) + tuple(r) for a, *r in cfam]
    fams = [(rank[a],) + tuple(r) for a, *r in fams]
    rules = [(n, rank[i], su, b) for n, i, su, b in rules]
    pal.sort()
    starts, toks = {}, []
    for sec in sec_names:
        starts[sec] = (len(toks), len(secs[sec]))
        toks += sorted(secs[sec])
    cfam.sort(key=lambda x: (x[1], x[0]))
    fams.sort(key=lambda x: (x[1], x[0]))
    rules.sort()
    # the blob: the counts, the base sheet, then each table's fields as
    # strings, each 0-ended
    out = [str(len(pal)), str(len(toks)), str(len(cfam)), str(len(fams)), str(len(rules)), "".join(base)]
    for n, r, g, b in pal:
        out += [n, str(r), str(g), str(b)]
    for t, v in toks:
        out += [t, v]
    for i, p, su, a, b in cfam:
        out += [p, su, a, b, str(i)]
    for i, p, su, sec, b in fams:
        out += [p, su, b, str(i), str(starts[sec][0]), str(starts[sec][1])]
    for n, i, su, b in rules:
        out += [n, su, b, str(i)]
    blob = ("\0".join(out) + "\0").encode("utf-8")
    co = zlib.compressobj(9, zlib.DEFLATED, -15)
    packed = co.compress(blob) + co.flush()
    o = ["/* made by tools/mktw.py from src/css/tw.txt: Tailwind 3's utilities (MIT,",
         "   Copyright (c) Tailwind Labs, Inc.) as EWeb reads them, packed (raw DEFLATE) */", "",
         "#define TW_UNPACKED %d" % len(blob), "static const unsigned char tw_packed[%d] = {" % len(packed)]
    for k in range(0, len(packed), 16):
        o.append("\t" + ", ".join("%d" % x for x in packed[k:k + 16]) + ",")
    o.append("};")
    with open(outh, "w", encoding="utf-8") as f:
        f.write("\n".join(o) + "\n")


def cases(src):
    props = known_props()
    tw = open(os.path.join(ROOT, "src", "css", "tw.txt"), encoding="utf-8").read()
    used = set(re.findall(r'(--tw-[\w-]+)', tw))
    names = [l.strip() for l in open(os.path.join(ROOT, "tests", "browse", "twclasses.txt"), encoding="utf-8")
             if l.strip() and not l.startswith("#")]
    with open(os.path.join(src, "eweb_cases.html"), "w") as f:
        f.write(" ".join(names))
    with open(os.path.join(src, "eweb_cases.config.js"), "w") as f:
        f.write('module.exports = { content: ["eweb_cases.html"], corePlugins: { preflight: false } }\n')
    with open(os.path.join(src, "eweb_u.css"), "w") as f:
        f.write("@tailwind utilities;\n")
    subprocess.run(["npx", "tailwindcss", "-c", "eweb_cases.config.js", "-i", "eweb_u.css", "-o", "eweb_cases.css"],
                   cwd=src, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    css = re.sub(r'(?s)/\*.*?\*/', '', open(os.path.join(src, "eweb_cases.css")).read())
    def esc(n):
        e = re.sub(r'([^A-Za-z0-9_-])', r'\\\1', n)
        if e[0].isdigit():
            e = ("\\%x " if e[1:2].lower() in tuple("0123456789abcdef ") else "\\%x") % ord(e[0]) + e[1:]
        return "." + e
    escs = [(esc(n), n) for n in names]
    got = {n: [] for n in names}

    def walk(text, media):
        i = 0
        while True:
            m = re.compile(r'\s*(@media [^{]+|[^{}]+)\{').match(text, i)
            if not m:
                return
            head = m.group(1).strip()
            j = m.end()
            if head.startswith("@media"):
                depth, k = 1, j
                while depth:
                    depth += {"{": 1, "}": -1}.get(text[k], 0)
                    k += 1
                walk(text[j:k - 1], media + [head[6:].strip()])
                i = k
                continue
            k = text.index("}", j)
            body = "; ".join("%s: %s" % kv for kv in decls(text[j:k])
                             if (kv[0].startswith("--") and kv[0] in used) or
                             (not kv[0].startswith("-") and kv[0] in props))
            i = k + 1
            if not body:
                continue
            sel = " ".join(head.split())
            for esc, n in escs:
                if re.search(re.escape(esc) + r'(?![A-Za-z0-9_\\-])', sel):
                    got[n].append("%s|%s{%s}" % (" and ".join(media), sel, body))
    walk(css, [])
    with open(os.path.join(ROOT, "tests", "browse", "twcases.txt"), "w", encoding="utf-8") as f:
        f.write("# what Tailwind 3.4.17 makes of each class, as EWeb keeps it: tools/mktw.py --cases\n")
        for n in names:
            f.write("C\t%s\n" % n)
            for r in got[n]:
                f.write("R\t%s\n" % r)


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--cases":
        cases(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "--from-tailwind":
        from_tailwind(sys.argv[2])
    elif len(sys.argv) == 3:
        to_header(sys.argv[1], sys.argv[2])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
