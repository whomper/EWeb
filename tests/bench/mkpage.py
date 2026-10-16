#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Writes NEWS.HTM and NEWS.CSS: a made-up news front page and its
stylesheet, for timing NSBENCH on a page like the web's.

    tests/bench/mkpage.py OUTDIR

The page is built like a real one and about the size of the page set's
median (84 KB, 743 elements, 951 attributes; tests/bench/measure.py):
head with meta tags, preloads, critical CSS, JSON-LD and inline scripts;
a header with an inline SVG logo, a two-level menu and a search form;
story cards with responsive images, a most-read list, a results table, a
newsletter form, a Hebrew section, an advert slot and a big footer. The
stylesheet is a modern site's: custom properties, a reset, BEM components,
utility classes, media queries, pseudo-classes and attribute selectors,
about 75 KB. All text is made up from word lists with a fixed seed, so
every run writes the same bytes and the files are ours to hand out.
"""
import os
import random
import sys

R = random.Random(16101990)

WORDS = """atari falcon accelerator memory board screen colour sound network modem
desk window menu icon font driver kernel cache clock program editor music
studio tracker sample synth keyboard joystick cartridge floppy disk drive
archive release update version patch build test reader story report market
city council river bridge garden harbour station museum library festival
season weather storm summer winter evening morning island valley mountain
forest village market street school teacher student science research data
energy solar battery engine train flight airport ticket travel holiday
recipe kitchen bread coffee garden flower friend family history culture art
gallery concert theatre film camera photo picture design paper letter
""".split()
NAMES = """Dana Levi, Sam Cohen, Alex Weiss, Noa Bar, Tom Green, Maya Klein,
Erez Stein, Lior Katz, Ruth Adler, Ben Shaw""".replace("\n", " ").split(", ")
HEBREW = """עיר נהר גשר גן נמל תחנה מוזיאון ספרייה פסטיבל עונה מזג אוויר סערה קיץ
חורף ערב בוקר אי עמק הר יער כפר שוק רחוב בית ספר מורה תלמיד מדע מחקר
אנרגיה שמש סוללה רכבת טיסה נמל תעופה כרטיס חופשה מתכון מטבח לחם קפה פרח
חבר משפחה היסטוריה תרבות אמנות""".split()
SECTIONS = ["News", "World", "Business", "Technology", "Science", "Retro",
            "Music", "Culture", "Travel", "Sport", "Weather", "Opinion"]


def words(n, vocab=WORDS):
    return " ".join(R.choice(vocab) for _ in range(n))


def sentence(lo=8, hi=18, vocab=WORDS):
    s = words(R.randint(lo, hi), vocab)
    return s[0].upper() + s[1:] + "." if vocab is WORDS else s + "."


def title(lo=5, hi=10):
    s = words(R.randint(lo, hi))
    return s[0].upper() + s[1:]


def slug(s):
    return "-".join(s.lower().split()[:6])


LOGO = ('<svg class="logo__mark" viewBox="0 0 120 32" width="120" height="32" aria-hidden="true">'
        + "".join('<path d="M%d %dh%dv%dh-%dz" fill="#%06x"/>'
                  % (i * 9, 4 + i % 3, 7, 24 - i % 5, 7, R.randrange(0x1000000)) for i in range(12))
        + '<text x="2" y="30" font-size="8">AERA NEWS</text></svg>')


def img(path, alt, w, h, cls):
    sizes = "(min-width: 900px) 33vw, 100vw"
    srcset = ", ".join("/img/%s-%d.jpg %dw" % (path, x, x) for x in (320, 640, 960, 1280))
    return ('<picture><source type="image/webp" srcset="%s" sizes="%s">'
            '<img class="%s" src="/img/%s-640.jpg" srcset="%s" sizes="%s" alt="%s" '
            'width="%d" height="%d" loading="lazy" decoding="async"></picture>'
            % (srcset.replace(".jpg", ".webp"), sizes, cls, path, srcset, sizes, alt, w, h))


def card(i, section, featured=False):
    t = title()
    href = "/%s/%d/%s" % (section.lower(), 2026000 + i, slug(t))
    mod = " card--featured" if featured else ""
    tags = "".join('<li class="tags__item"><a class="tag tag--%s" href="/tag/%s">%s</a></li>'
                   % (w, w, w) for w in R.sample(WORDS, 1))
    return ('<article class="card%s" data-id="%d" data-section="%s">\n'
            '<a class="card__link" href="%s">%s</a>\n'
            '<div class="card__body"><span class="card__kicker">%s</span>\n'
            '<h3 class="card__title"><a href="%s">%s</a></h3>\n'
            '<p class="card__summary">%s %s %s</p>\n'
            '<ul class="card__meta"><li class="card__time"><time datetime="2026-10-%02dT%02d:%02d">%d hours ago</time></li>'
            '<li class="card__author">By <a rel="author" href="/people/%s">%s</a></li></ul>\n'
            '<ul class="tags">%s</ul></div>\n</article>\n'
            % (mod, i, section.lower(), href, img(slug(t), t, 640, 360, "card__image"),
               section, href, t, sentence(14, 24), sentence(14, 24), sentence(10, 20),
               R.randint(1, 28), R.randint(0, 23),
               R.randint(0, 59), R.randint(1, 23), slug(NAMES[i % len(NAMES)]),
               NAMES[i % len(NAMES)], tags))


def comment(depth, i):
    replies = ""
    if depth < 2 and R.random() < 0.5:
        replies = '<ol class="comment__replies">%s</ol>' % "".join(
            comment(depth + 1, i * 10 + k) for k in range(R.randint(1, 2)))
    return ('<li class="comment" id="c%d"><article class="comment__body"><header class="comment__head">'
            '<span class="comment__name">%s</span> <time datetime="2026-10-16">%d min ago</time></header>'
            '<p>%s %s</p>%s</article></li>\n'
            % (i, R.choice(NAMES), R.randint(1, 59), sentence(12, 30), sentence(8, 20), replies))


def longread():
    t = title(6, 11)
    paras = "".join("<p>%s %s %s %s</p>\n" % tuple(sentence(12, 26) for _ in range(4)) for _ in range(15))
    return ('<section class="section section--longread"><article class="longread" itemscope>'
            '<header class="longread__head"><span class="card__kicker">Long read</span>'
            '<h2 class="longread__title">%s</h2><p class="longread__standfirst">%s</p>'
            '<p class="longread__byline">By <a rel="author" href="/people/%s">%s</a></p></header>\n'
            '<figure class="longread__figure">%s<figcaption>%s</figcaption></figure>\n'
            '<div class="longread__text">\n%s<blockquote class="longread__quote"><p>%s</p></blockquote>\n%s</div>\n'
            '<section class="comments" id="comments"><h3>Comments</h3><ol class="comments__list">\n%s</ol></section>\n'
            '</article></section>\n'
            % (t, sentence(20, 30), slug(NAMES[3]), NAMES[3], img(slug(t), t, 960, 540, "longread__image"),
               sentence(), paras, sentence(14, 22),
               "".join("<p>%s %s %s</p>\n" % tuple(sentence(12, 26) for _ in range(3)) for _ in range(3)),
               "".join(comment(0, k) for k in range(5))))


def page():
    out = []
    o = out.append
    o('<!DOCTYPE html>\n<html lang="en" class="no-js">\n<head>\n<meta charset="utf-8">\n'
      '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
      '<title>AERA News: %s</title>\n' % title())
    o('<meta name="description" content="%s">\n' % sentence(20, 30))
    for p in ("og:title", "og:description", "og:image", "og:url", "og:type", "og:site_name",
              "twitter:card", "twitter:site", "twitter:title", "twitter:image"):
        o('<meta property="%s" content="%s">\n' % (p, title()))
    for f in ("serif-regular", "serif-bold", "sans-regular", "sans-bold"):
        o('<link rel="preload" href="/fonts/%s.woff2" as="font" type="font/woff2" crossorigin>\n' % f)
    o('<link rel="stylesheet" href="NEWS.CSS">\n<link rel="icon" href="/favicon.ico">\n')
    o('<style>\n:root{--brand:#b00020;--ink:#1a1a1a;--paper:#fff}\n'
      'body{margin:0;font-family:Georgia,serif;color:var(--ink);background:var(--paper)}\n'
      '.site-header{display:flex;align-items:center;padding:.5rem 1rem;border-bottom:1px solid #ddd}\n'
      '.skip-link{position:absolute;left:-999px}\n.skip-link:focus{left:1rem;top:1rem}\n'
      '.card__image{display:block;width:100%;height:auto}\n</style>\n')
    o('<script type="application/ld+json">{"@context":"https://schema.org","@type":"NewsMediaOrganization",'
      '"name":"AERA News","url":"https://news.example/","logo":"https://news.example/logo.png",'
      '"sameAs":[%s]}</script>\n' % ",".join('"https://social.example/%s"' % w for w in WORDS[:12]))
    o('<script>\n(function(w,d){w.dataLayer=w.dataLayer||[];function t(e,p){w.dataLayer.push'
      '({event:e,page:p,ts:Date.now()})}d.documentElement.className=d.documentElement.className'
      '.replace("no-js","js");t("view",location.pathname);\n')
    for i in range(40):
        o('  w.cfg_%s = { id: %d, label: "%s", on: %s };\n'
          % (R.choice(WORDS), i, title(2, 4), R.choice(("true", "false"))))
    o('})(window,document);\n</script>\n</head>\n')

    o('<body class="page page--home">\n<a class="skip-link" href="#main">Skip to content</a>\n')
    o('<header class="site-header" role="banner">\n<a class="logo" href="/">%s</a>\n' % LOGO)
    o('<nav class="nav" aria-label="Sections">\n<ul class="nav__list">\n')
    for s in SECTIONS:
        o('<li class="nav__item"><a class="nav__link" href="/%s">%s</a>\n<ul class="nav__sub">' % (s.lower(), s))
        for _ in range(2):
            w = R.choice(WORDS)
            o('<li><a href="/%s/%s">%s</a></li>' % (s.lower(), w, w.capitalize()))
        o('</ul></li>\n')
    o('</ul>\n</nav>\n<form class="search" role="search" action="/search">'
      '<label class="visually-hidden" for="q">Search</label>'
      '<input class="search__input" id="q" name="q" type="search" placeholder="Search">'
      '<button class="btn btn--primary" type="submit">Search</button></form>\n</header>\n')

    o('<div class="page-wrap"><div class="page-inner">\n')
    o('<main id="main" class="layout"><div class="container"><div class="row"><div class="col col--main">\n'
      '<section class="lead" aria-labelledby="lead-title">\n'
      '<h2 id="lead-title" class="section-title">Top stories</h2>\n<div class="grid grid--lead">\n')
    n = 0
    for i in range(4):
        o(card(n, R.choice(SECTIONS), featured=(i == 0)))
        n += 1
    o('</div>\n</section>\n')
    for s in SECTIONS[1:6]:
        o('<section class="section section--%s" aria-labelledby="t-%s">\n'
          '<h2 id="t-%s" class="section-title"><a href="/%s">%s</a></h2>\n<div class="grid">\n'
          % (s.lower(), s.lower(), s.lower(), s.lower(), s))
        for _ in range(3):
            o(card(n, s))
            n += 1
        o('</div>\n</section>\n')

    o(longread())
    o('</div>\n<div class="col col--side">\n<aside class="sidebar">\n<section class="most-read"><h2 class="section-title">Most read</h2>\n<ol class="ranked">\n')
    for i in range(8):
        t = title()
        o('<li class="ranked__item"><a href="/news/%d/%s"><span class="ranked__n">%d</span> %s</a></li>\n'
          % (2027000 + i, slug(t), i + 1, t))
    o('</ol></section>\n<div class="ad ad--mpu" data-ad-slot="mpu-1" data-ad-size="300x250" '
      'aria-hidden="true"><div class="ad__inner"><div class="ad__label">Advertisement</div></div></div>\n')
    o('<section class="results"><h2 class="section-title">Results</h2>\n'
      '<table class="table table--results"><thead><tr><th scope="col">Team</th><th scope="col">P</th>'
      '<th scope="col">W</th><th scope="col">D</th><th scope="col">Pts</th></tr></thead>\n<tbody>\n')
    for i in range(6):
        o('<tr><th scope="row">%s</th><td>%d</td><td>%d</td><td>%d</td><td class="table__pts">%d</td></tr>\n'
          % (R.choice(WORDS).capitalize() + " " + R.choice(("United", "City", "Rovers")),
             10, R.randint(0, 10), R.randint(0, 5), R.randint(0, 30)))
    o('</tbody></table></section>\n')
    o('<form class="newsletter" action="/newsletter" method="post">\n<h2 class="section-title">Newsletter</h2>\n'
      '<p class="newsletter__text">%s</p>\n'
      '<label for="email">Email</label><input id="email" name="email" type="email" required autocomplete="email">\n'
      '<fieldset class="newsletter__topics"><legend>Topics</legend>\n' % sentence())
    for s in SECTIONS[:6]:
        o('<label class="check"><input type="checkbox" name="topic" value="%s"> %s</label>\n' % (s.lower(), s))
    o('</fieldset>\n<button class="btn btn--primary" type="submit">Sign up</button>\n</form>\n</aside>\n</div></div>\n')

    o('<section class="section section--hebrew" dir="rtl" lang="he" aria-labelledby="t-he">\n'
      '<h2 id="t-he" class="section-title">בעברית</h2>\n<div class="grid">\n')
    for i in range(3):
        t = words(R.randint(4, 7), HEBREW)
        o('<article class="card" data-id="%d"><div class="card__body">'
          '<h3 class="card__title"><a href="/he/%d">%s</a></h3>\n'
          '<p class="card__summary">%s %s</p>\n'
          '<ul class="card__meta"><li class="card__time"><time datetime="2026-10-16">לפני %d שעות</time></li></ul>'
          '</div></article>\n' % (900 + i, 900 + i, t, sentence(10, 16, HEBREW), sentence(10, 16, HEBREW),
                                  R.randint(1, 9)))
    o('</div>\n</section>\n')
    o('<section class="video"><h2 class="section-title">Watch</h2>\n<div class="video__frame">'
      '<iframe title="%s" src="/embed/video/%d" width="640" height="360" loading="lazy" '
      'allowfullscreen></iframe></div>\n<p class="video__caption">%s</p></section>\n'
      % (title(), R.randint(1000, 9999), sentence()))
    o('</div></main>\n')

    o('<footer class="site-footer" role="contentinfo">\n<div class="site-footer__cols">\n')
    for col in range(4):
        o('<div class="site-footer__col"><h2 class="site-footer__title">%s</h2><ul class="site-footer__list">\n'
          % R.choice(SECTIONS))
        for _ in range(7):
            w = R.choice(WORDS)
            o('<li><a class="site-footer__link" href="/%s/%s">%s %s</a></li>\n'
              % (R.choice(SECTIONS).lower(), w, w.capitalize(), R.choice(WORDS)))
        o('</ul></div>\n')
    o('</div>\n<ul class="social">')
    for w in ("feed", "mail", "video", "photo", "chat"):
        o('<li><a class="social__link social__link--%s" href="/follow/%s" aria-label="%s">'
          '<svg width="16" height="16" viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="7"/>'
          '</svg></a></li>' % (w, w, w.capitalize()))
    o('</ul>\n<p class="site-footer__small">%s</p>\n<p class="site-footer__small">'
      'Made-up text for EWeb\'s NSBENCH. Copyright (C) 2026 AERA | Erez Yaary.</p>\n</footer>\n</div></div>\n'
      % sentence(20, 30))
    o('<script src="/js/app.js" defer></script>\n</body>\n</html>\n')
    return "".join(out)


COMPONENTS = ["site-header", "logo", "nav", "nav__list", "nav__item", "nav__link", "nav__sub",
              "search", "search__input", "btn", "layout", "lead", "grid", "grid--lead", "section",
              "section-title", "card", "card--featured", "card__link", "card__image", "card__body",
              "card__kicker", "card__title", "card__summary", "card__meta", "card__time",
              "card__author", "card__comments", "tags", "tags__item", "tag", "sidebar", "most-read",
              "ranked", "ranked__item", "ranked__n", "ad", "ad__inner", "ad__label", "results",
              "table", "table__pts", "newsletter", "newsletter__text", "newsletter__topics", "check",
              "video", "video__frame", "video__caption", "site-footer", "site-footer__cols",
              "site-footer__col", "site-footer__title", "site-footer__list", "site-footer__link",
              "social", "social__link", "site-footer__small", "skip-link", "visually-hidden"]
# what a big site's stylesheet also holds: components this page doesn't use
UNUSED = ["modal", "modal__dialog", "modal__close", "carousel", "carousel__slide", "tabs",
          "tabs__tab", "tabs__panel", "accordion", "accordion__item", "tooltip", "toast", "badge",
          "breadcrumb", "pagination", "pagination__item", "alert", "alert--warning", "avatar",
          "comment", "comment__body", "comment__reply", "quiz", "quiz__answer", "live-blog",
          "live-blog__entry", "paywall", "paywall__offer", "cookie-banner", "cookie-banner__actions",
          "gallery", "gallery__item", "map", "chart", "poll", "poll__bar", "weather-widget",
          "ticker", "ticker__item", "podcast-player", "podcast-player__controls"]
PROPS = [("display", ["block", "flex", "inline-block", "grid", "none", "inline-flex"]),
         ("margin", ["0", "0 auto", ".5rem 0", "1rem", "var(--space-2) 0", "0 0 var(--space-3)"]),
         ("padding", ["0", ".25rem .5rem", "var(--space-2)", "1rem 1.25rem", "var(--space-1) var(--space-3)"]),
         ("color", ["var(--ink)", "var(--brand)", "#555", "inherit", "rgba(0,0,0,.7)", "#fff"]),
         ("background", ["none", "var(--paper)", "#f4f4f4", "var(--brand)", "linear-gradient(#fff,#eee)"]),
         ("font-size", ["var(--step-0)", "var(--step-1)", "var(--step--1)", "1.125rem", ".875rem", "2rem"]),
         ("font-weight", ["400", "700", "600", "bold", "normal"]),
         ("line-height", ["1.2", "1.4", "1.5", "var(--leading)"]),
         ("border", ["0", "1px solid var(--rule)", "2px solid var(--brand)", "1px dashed #ccc"]),
         ("border-radius", ["0", "2px", "4px", "999px", "var(--radius)"]),
         ("text-decoration", ["none", "underline"]),
         ("align-items", ["center", "flex-start", "stretch", "baseline"]),
         ("justify-content", ["space-between", "center", "flex-start", "flex-end"]),
         ("flex", ["1 1 auto", "0 0 auto", "1", "1 1 300px"]),
         ("gap", ["var(--space-2)", "1rem", ".5rem"]),
         ("position", ["relative", "absolute", "sticky", "static"]),
         ("width", ["100%", "auto", "50%", "calc(100% - 2rem)", "300px"]),
         ("max-width", ["none", "var(--measure)", "72rem", "100%"]),
         ("box-shadow", ["none", "0 1px 2px rgba(0,0,0,.15)", "0 4px 12px rgba(0,0,0,.2)"]),
         ("transition", ["color .2s", "background-color .2s ease", "transform .3s ease-out"]),
         ("overflow", ["hidden", "auto", "visible"]),
         ("text-transform", ["uppercase", "none"]),
         ("letter-spacing", [".02em", "0", ".1em"]),
         ("z-index", ["1", "10", "100"])]


def decls(k):
    return ";".join("%s:%s" % (p, R.choice(v)) for p, v in R.sample(PROPS, k))


def rule(sel, k=None):
    return "%s{%s}\n" % (sel, decls(k or R.randint(2, 6)))


def stylesheet():
    out = ["/* NEWS.CSS: the made-up news site's stylesheet (tests/bench/mkpage.py).\n"
           "   Copyright (C) 2026 AERA | Erez Yaary, GPL-2.0-or-later */\n"]
    o = out.append
    o(":root{" + ";".join("--%s:%s" % v for v in [
        ("brand", "#b00020"), ("brand-dark", "#7a0016"), ("ink", "#1a1a1a"), ("paper", "#fff"),
        ("rule", "#ddd"), ("muted", "#666"), ("radius", "4px"), ("leading", "1.5"),
        ("measure", "40rem"), ("step--1", ".875rem"), ("step-0", "1rem"), ("step-1", "1.25rem"),
        ("step-2", "1.5rem"), ("step-3", "2rem")] + [("space-%d" % i, "%.2frem" % (0.25 * 2 ** (i / 2)))
                                                     for i in range(1, 9)]
        + [("c-%s" % w, "#%06x" % R.randrange(0x1000000)) for w in WORDS[:40]]) + "}\n")
    for e in ("*,*::before,*::after", "html", "body", "h1,h2,h3,h4,h5,h6", "p", "a", "a:hover",
              "a:focus-visible", "img,picture,svg,video", "ul[class],ol[class]", "button", "input",
              "input,button,textarea,select", "table", "th,td", "fieldset", "legend", "label",
              "figure", "time", "abbr[title]", "code,kbd,samp,pre", "hr", "[hidden]"):
        o(rule(e))
    for c in COMPONENTS + UNUSED:
        b = "." + c
        o(rule(b, R.randint(3, 8)))
        for extra in R.sample([" a", ":hover", ":focus-within", "::before", "::after", " > li",
                               ":first-child", ":last-child", ":nth-child(2n)", " + " + b,
                               ":not(:last-child)", "[aria-current=\"page\"]", " img", " h3",
                               " .tag", ":empty", "[data-section=\"retro\"]"], R.randint(2, 6)):
            o(rule(b + extra))
    for w in WORDS:
        o(".tag--%s{background:var(--c-%s, #eee);color:#fff}\n" % (w, R.choice(WORDS[:40])))
    for p, short in (("margin", "m"), ("padding", "p")):
        for side, sname in (("", ""), ("-top", "t"), ("-bottom", "b"), ("-left", "l"), ("-right", "r")):
            for i in range(9):
                o(".%s%s-%d{%s%s:var(--space-%d)!important}\n" % (short, sname, i, p, side, max(1, i)))
    for prop, vals in PROPS:
        for v in vals:
            name = "".join(ch if ch.isalnum() else "-" for ch in v).strip("-")
            o(".u-%s-%s{%s:%s}\n" % (prop, name or "x", prop, v))
    o('[dir="rtl"] .card__meta,[dir="rtl"] .tags{flex-direction:row-reverse}\n'
      '[dir="rtl"] .card__body{text-align:right}\n'
      'input[type="search"],input[type="email"],input[type="text"]{%s}\n'
      'input[type="checkbox"]{%s}\nbutton[disabled],.btn[aria-disabled="true"]{opacity:.5}\n'
      'a[href^="http"]:not([href*="news.example"])::after{content:" \\2197"}\n'
      'a[rel="author"]{font-style:italic}\n' % (decls(5), decls(3)))
    for bp in (480, 600, 768, 900, 1200):
        o("@media (min-width: %dpx){\n" % bp)
        for c in R.sample(COMPONENTS + UNUSED, 30):
            o(rule("." + c, R.randint(2, 4)))
        o(".grid{display:grid;grid-template-columns:repeat(%d,1fr)}\n}\n" % max(1, bp // 300))
    o("@media print{\n.nav,.search,.ad,.newsletter,.video,.social{display:none}\n"
      "a::after{content:\" (\" attr(href) \")\"}\n}\n"
      "@media (prefers-reduced-motion: reduce){*{transition:none!important}}\n"
      "@media (prefers-color-scheme: dark){:root{--ink:#eee;--paper:#111;--rule:#333}}\n")
    return "".join(out)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    d = sys.argv[1]
    os.makedirs(d, exist_ok=True)
    html = page()
    css = stylesheet()
    with open(os.path.join(d, "NEWS.HTM"), "w", encoding="utf-8", newline="\n") as f:
        f.write(html)
    with open(os.path.join(d, "NEWS.CSS"), "w", encoding="utf-8", newline="\n") as f:
        f.write(css)
    print("NEWS.HTM %d bytes, NEWS.CSS %d bytes" % (len(html.encode()), len(css.encode())))


if __name__ == "__main__":
    main()
