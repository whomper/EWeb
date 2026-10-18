#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Turn docs/GUIDE.md into an ST-Guide hypertext source (EWEB.STG); from EFTP.

    mkhelp.py docs/GUIDE.md VERSION build/EWEB.STG

Each "## " section becomes a page, the text before the first one the
main page with the contents. ST-Guide shows text as it is written, so
tables are laid out in columns here and every line is kept within the
page width; bold becomes @{B}, links between sections become ST-Guide
links. The text is written in the Atari character set. Compile with HCP
(the hypview project): hcp -o binary/EWEB.HYP build/EWEB.STG
"""
import re
import sys
import time

WIDTH = 75

# what the guide uses beyond ASCII, in the Atari character set
ATARI = {"»": "\xaf", "«": "\xae", "×": "x", "—": "-", "–": "-",
         "─": "-", "‘": "'", "’": "'", "“": '"', "”": '"',
         "é": "\x82", "è": "\x8a", "ü": "\x81", "ö": "\x94", "ä": "\x84"}


def atari(s):
    return "".join(ATARI.get(c, c if ord(c) < 128 else "?") for c in s)


def slug(title):
    """GitHub's anchor for a heading: lower case, spaces to dashes"""
    return re.sub(r"[^a-z0-9 -]", "", title.lower()).replace(" ", "-")


def plain(s):
    """the visible text of a line of markdown, without its markup"""
    s = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", s)
    return s.replace("**", "").replace("`", "")


def markup(s, anchors):
    """markdown inline markup to ST-Guide's; links to sections become links"""
    def link(m):
        text, target = m.group(1), m.group(2)
        if target.startswith("#") and target[1:] in anchors:
            return '@{"%s" LINK "%s"}' % (text, anchors[target[1:]])
        return text
    s = s.replace("@", "@@")
    s = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"@{B}\1@{b}", s)
    return s.replace("`", "")


def vislen(w):
    """the width of a word on the page: no @{...} commands, @@ as one"""
    return len(re.sub(r"@\{[^}]*\}", "", w).replace("@@", "@"))


def wrap(text, width):
    """words into lines of at most width characters; an ST-Guide command
    with spaces in it (a link) stays one word"""
    text = re.sub(r"@\{[^}]*\}", lambda m: m.group(0).replace(" ", "\0"), text)
    out, line, n = [], "", 0
    for w in text.split():
        lw = vislen(w.replace("\0", " "))
        if line and n + 1 + lw > width:
            out.append(line)
            line, n = w, lw
        else:
            line, n = (w, lw) if not line else (line + " " + w, n + 1 + lw)
    if line or not out:
        out.append(line)
    return [l.replace("\0", " ") for l in out]


def table(rows):
    """a markdown table in columns, the widest column wrapped to fit"""
    def split(r):
        r = re.sub(r"`[^`]*`", lambda m: m.group(0).replace("|", "\0"), r.strip().strip("|"))
        return [plain(c.strip().replace("\0", "|")).replace("@", "@@") for c in r.split("|")]
    cells = [split(r) for r in rows]
    cells = [r for r in cells if not all(re.fullmatch(r":?-+:?", c) for c in r)]
    n = max(len(r) for r in cells)
    cells = [r + [""] * (n - len(r)) for r in cells]
    widths = [max(len(r[i]) for r in cells) for i in range(n)]
    while sum(widths) + 2 * (n - 1) > WIDTH:
        widest = widths.index(max(widths))
        widths[widest] -= 1
    out = []
    for k, r in enumerate(cells):
        parts = [wrap(c, widths[i]) for i, c in enumerate(r)]
        for j in range(max(len(p) for p in parts)):
            line = "  ".join((p[j] if j < len(p) else "").ljust(widths[i]) for i, p in enumerate(parts))
            out.append(("@{B}%s@{b}" % line.rstrip()) if k == 0 else line.rstrip())
        if k == 0:
            out.append("  ".join("-" * w for w in widths))
    return out


def page(lines, anchors):
    """the body of one page: paragraphs and list items flowed to the page
    width, code as it is, tables in columns"""
    out, i = [], 0
    while i < len(lines):
        s = lines[i]
        if s.startswith("```"):
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                out.append(lines[i].replace("@", "@@"))
                i += 1
            i += 1
        elif s.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append(lines[i])
                i += 1
            out += table(rows)
        elif s.startswith("### "):
            out.append("@{B}%s@{b}" % s[4:].strip())
            i += 1
        elif not s.strip():
            out.append("")
            i += 1
        else:
            # a paragraph, or a list item with its indented continuation
            ind = len(s) - len(s.lstrip())
            item = s.lstrip().startswith("- ")
            text = [s.strip()]
            i += 1
            while i < len(lines) and lines[i].strip() and not lines[i].startswith(("```", "|", "#")):
                nxt = lines[i]
                nind = len(nxt) - len(nxt.lstrip())
                if nxt.lstrip().startswith("- ") or (item and nind <= ind):
                    break
                text.append(nxt.strip())
                i += 1
            hang = ind + (2 if item else 0)
            for k, l in enumerate(wrap(markup(" ".join(text), anchors), WIDTH - hang)):
                out.append(" " * (ind if k == 0 else hang) + l)
    while out and not out[-1].strip():
        out.pop()
    return out


def main():
    src, version, dst = sys.argv[1], open(sys.argv[2]).read().strip(), sys.argv[3]
    text = open(src, encoding="utf-8").read().split("\n")
    title = text[0].lstrip("# ").strip()
    sections, cur = [], ("Main", [])
    for s in text[1:]:
        if s.startswith("## "):
            sections.append(cur)
            cur = (s[3:].strip(), [])
        else:
            cur[1].append(s)
    sections.append(cur)
    anchors = {slug(t): t for t, _ in sections[1:]}
    out = ['@database "EWeb - a web browser for the Atari Falcon"',
           '@subject "Internet"',
           '@author "AERA | Erez Yaary"',
           '@$VER: EWEB.HYP %s (%s)' % (version, time.strftime("%d.%m.%Y")),
           "@width %d" % WIDTH,
           "@inputenc atarist",
           "@charset atarist",
           "@autorefoff",
           "",
           '@node "Main" "EWeb %s"' % version,
           "@{U}%s@{u}" % title, ""]
    out += page(sections[0][1], anchors)
    out += ["", "@{B}Contents@{b}", ""]
    out += ['  @{"%s" LINK "%s"}' % (t, t) for t, _ in sections[1:]]
    out += ["@endnode", ""]
    for t, body in sections[1:]:
        out += ['@node "%s" "EWeb: %s"' % (t, t), "@{U}%s@{u}" % t, ""]
        out += page(body, anchors)
        out += ["", '@{"Contents" LINK "Main"}', "@endnode", ""]
    with open(dst, "w", encoding="latin-1", newline="\r\n") as f:
        f.write(atari("\n".join(out)) + "\n")


if __name__ == "__main__":
    main()
