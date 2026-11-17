#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Eberon Limited (John Gorman)
"""Stallion's identity (stallion R1, R2, R3, R9): nothing a user sees says Clipper, except where it credits it.

  - the firmware: every C string literal in firmware/ (what the FM-1 shows, the USB names, the console) names no
    Clipper, except the credit lines in CREDIT; _Static_assert / #error messages and #include names are the
    build's, not the user's
  - the web pages (web/editor.html, web/index_pkg.html, the SMK-37 installer web/index_smk37.html, the front page
    web/index_site.html): their <title> says Stallion; their text and every string
    of their scripts names no Clipper unless it is a credit (it also says Stallion, isod89 or the lineage) or one
    of the internal ids in INTERNAL (file formats, browser storage keys, the mock of an old Clipper device: kept
    so files and settings made under Clipper still load)
  - the site (web/make_site.py): the app's name, the package's name stallion-<version>.fwsc
  - the firmware's version (firmware/src/ui.c) and the boot screen's / ABOUT's line (ui_menu.c ver_line)
  - README: Stallion's logo first, the lineage credited with links (R3), the trademark notice (R9)

  python3 tests/brand_test.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BAD = re.compile(r"clipper", re.I)
CREDIT = {"FROM CLIPPER + SLOOP (ISOD89)", "from Clipper, SLOOP + Felucca"}
INTERNAL = {"clipper-pattern", "clipper-pattern-bank", "clipper-hybrid", "clipper-fmx", "clipper-seq-lane",
            "clipper-installer-lang", "clipper", "FELUCCA CLIPPER 0.2 (MOCK)"}
LINEAGE = re.compile(r"stallion|isod89|clipper-fm1|from clipper|clipper (by|0\.|1\.)|clipper's and|clipper made|"
                     r"de clipper|venues de clipper|clipper から|clipper が|clipper \d|clipper's (pony|engine)|moteurs de clipper|clipper の", re.I)
fails = []


def check(ok, what):
    print(f"brand: {what:<96} {'ok' if ok else 'FAIL'}")
    if not ok:
        fails.append(what)


def c_strings(src):
    """(line, text) of each string literal of C source, without comments, #include / #error lines and the
    messages of _Static_assert"""
    out, i, n, line, depth_sa = [], 0, len(src), 1, -1
    depth = 0
    while i < n:
        ch = src[i]
        if ch == "\n":
            line += 1
            i += 1
            j = i
            while j < n and src[j] in " \t":
                j += 1
            if src.startswith(("#include", "#error"), j):
                while i < n and src[i] != "\n":
                    i += 1
            continue
        if src.startswith("/*", i):
            k = src.find("*/", i + 2)
            line += src.count("\n", i, k)
            i = k + 2
        elif src.startswith("//", i):
            while i < n and src[i] != "\n":
                i += 1
        elif src.startswith("_Static_assert", i):
            depth_sa = depth
            i += len("_Static_assert")
        elif ch == "(":
            depth += 1
            i += 1
        elif ch == ")":
            depth -= 1
            if depth == depth_sa:
                depth_sa = -1
            i += 1
        elif ch == "'":
            i += 2 if src[i + 1] != "\\" else 3
            while i < n and src[i] != "'":
                i += 1
            i += 1
        elif ch == '"':
            j, s = i + 1, []
            while src[j] != '"':
                if src[j] == "\\":
                    s.append(src[j:j + 2])
                    j += 2
                else:
                    s.append(src[j])
                    j += 1
            if depth_sa < 0:
                out.append((line, "".join(s)))
            i = j + 1
        else:
            i += 1
    return out


REGEX_BEFORE = set("=(:[!&|?{};,+-*%<>~^")


def js_strings(src):
    """the string literals (and template literals) of a script, without comments and regular expressions"""
    out, i, n, prev = [], 0, len(src), ";"
    while i < n:
        ch = src[i]
        if src.startswith("/*", i):
            i = src.find("*/", i + 2) + 2
        elif src.startswith("//", i):
            while i < n and src[i] != "\n":
                i += 1
        elif ch == "/" and (prev in REGEX_BEFORE or re.search(r"\b(return|typeof|case)\s*$", src[max(0, i - 8):i])):
            j, cls = i + 1, False                       # a regular expression: up to its closing slash
            while j < n and (src[j] != "/" or cls):
                if src[j] == "\\":
                    j += 1
                elif src[j] == "[":
                    cls = True
                elif src[j] == "]":
                    cls = False
                j += 1
            i, prev = j + 1, "x"
        elif ch in "\"'`":
            j = i + 1
            while j < n and src[j] != ch:
                j += 2 if src[j] == "\\" else 1
            out.append(src[i + 1:j])
            i, prev = j + 1, "x"
        else:
            if not ch.isspace():
                prev = ch
            i += 1
    return out


def page(path):
    html = (ROOT / path).read_text(encoding="utf-8")
    t = re.search(r"<title>(.*?)</title>", html)
    check(t and "Stallion" in t.group(1) and not BAD.search(t.group(1)), f"{path}: the title says Stallion")
    scripts = re.findall(r"<script[^>]*>(.*?)</script>", html, re.S)
    body = re.sub(r"<script[^>]*>.*?</script>|<style[^>]*>.*?</style>|<!--.*?-->", "", html, flags=re.S)
    blocks = re.split(r"</?(?:p|div|dd|dt|li|footer|h\d|section|details|summary|button|label|main|aside|header|nav|ul|"
                      r"ol|dl|tr|td|th|table|option|select|br)\b[^>]*>", body)   # a sentence stays whole (its links)
    text = [re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", s)).strip() for s in blocks]
    text = [s for s in text if s]
    attrs = re.findall(r'(?:alt|title|aria-label|download|href)="([^"]*)"', body)
    bad = [s for s in text + attrs if BAD.search(s) and not LINEAGE.search(s) and s not in INTERNAL]
    check(not bad, f"{path}: the page's text names no Clipper but in credits " + (repr(bad[:3]) if bad else ""))
    lits = [s for sc in scripts for s in js_strings(sc)]
    bad = [s for s in lits if BAD.search(s) and not LINEAGE.search(s) and s not in INTERNAL]
    check(not bad, f"{path}: its scripts' strings name no Clipper but in credits " + (repr(bad[:3])[:200] if bad else ""))
    return html


def main():
    # the checker itself: it sees a string the FM-1 would show, a page's script string, and skips comments
    c = c_strings('/* Clipper */ x = "CLIPPER 1.3"; _Static_assert(1, "Clipper"); y = "OK";')
    j = js_strings('// "Clipper"\nconst a = /clipper/i, b = "Clipper not found", c = `Clipper ${x}`;')
    check([s for _, s in c] == ["CLIPPER 1.3", "OK"] and j == ["Clipper not found", "Clipper ${x}"],
          "the checker finds C and script strings, skips comments, asserts and regular expressions")
    # the firmware
    bad = []
    for f in sorted([*(ROOT / "firmware").rglob("*.c"), *(ROOT / "firmware").rglob("*.h")]):
        for line, s in c_strings(f.read_text(encoding="utf-8", errors="replace")):
            if BAD.search(s) and s not in CREDIT:
                bad.append(f"{f.relative_to(ROOT)}:{line}: {s!r}")
    check(not bad, "firmware: no string names Clipper but the credits " + (repr(bad[:3]) if bad else ""))
    usb = (ROOT / "firmware/src/usb.c").read_text()
    check("'S', 0, 't', 0, 'a', 0, 'l', 0, 'l', 0, 'i', 0, 'o', 0, 'n', 0" in usb, "firmware: the USB MIDI port is named Stallion")
    ui = (ROOT / "firmware/src/ui.c").read_text()
    m = re.search(r'#define FELUCCA_VERSION "([^"]*)"', ui)
    check(m and re.fullmatch(r"STALLION \d+\.\d+.*", m.group(1)), f"firmware: the version is STALLION X.Y ({m and m.group(1)})")
    menu = (ROOT / "firmware/src/ui_menu.c").read_text()
    check('cv_text(4, 4, &FONT_L, "STALLION"' in menu and "ver_line(vl" in menu and "fmt_int(b + str_len(b), NVOICE)" in menu,
          "firmware: ABOUT shows STALLION, the version, the voices and the cores (R1)")
    notice = "Copyright (C) 2026 Eberon Limited (John Gorman)"      # (LICENSING.md, additional terms 1: section 7(b))
    about = re.findall(r'cv_text\(4, (\d+), &FONT_S, "((?:COPYRIGHT|EBERON)[^"]*)", C_WHITE\)', menu)
    check(len(about) == 2 and " ".join(t for _, t in about) == notice.upper() and int(about[1][0]) - int(about[0][0]) >= 13,
          "firmware: ABOUT shows Stallion's notice in FONT_S, white, on two lines (LICENSING.md 7(b))")
    for f in ("web/editor.html", "web/index_pkg.html"):
        check(notice in (ROOT / f).read_text(encoding="utf-8"), f"{f}: its credits carry Stallion's notice (LICENSING.md 7(b))")
    sp = (ROOT / "firmware/src/splash.c").read_text()
    check("ver_line(v" in sp and "splash_ver();" in (ROOT / "firmware/src/main.c").read_text(),
          "firmware: the boot screen shows the version, the voices and the cores under the logo (R1)")
    # the web
    ed, inst = page("web/editor.html"), page("web/index_pkg.html")
    smk, front = page("web/index_smk37.html"), page("web/index_site.html")
    for path, h in (("web/index_pkg.html", inst), ("web/index_smk37.html", smk), ("web/index_site.html", front)):
        check(all(u in h for u in ("github.com/isod89/clipper-fm1", "github.com/isod89/sloop-fm1", "github.com/hugelton/Felucca")),
              f"{path}: credits Clipper, SLOOP and Felucca with links (R3)")
    site = (ROOT / "web/make_site.py").read_text()
    check('"name": "Stallion", "short_name": "Stallion"' in site and 'name = f"stallion-' in site and "stallion.webmanifest" in site,
          "web/make_site.py: the app is Stallion, the package stallion-<version>.fwsc (R2)")
    js = [s for f in ("web/fm1ota.js", "web/fm1pkg.js", "web/smkota.js", "web/smkpkg.js", "web/sw.js")
          for s in js_strings((ROOT / f).read_text())]
    check(not [s for s in js if BAD.search(s) and s not in INTERNAL],
          "web/*.js: no string names Clipper")
    build = (ROOT / "tools/build.py").read_text()
    check('user = f"stallion-' in build, "tools/build.py: writes build/stallion-<version>.fwsc (R2)")
    # the front page
    rd = (ROOT / "README.md").read_text(encoding="utf-8")
    head = rd.split("\n## ")[0]
    check("assets/logo/stallion-" in head.splitlines()[0] + head and "clipper-logo" not in rd, "README: Stallion's logo on top")
    check(all(u in rd for u in ("github.com/isod89/clipper-fm1", "github.com/isod89/sloop-fm1", "github.com/hugelton/Felucca")),
          "README: credits Clipper, SLOOP and Felucca with links (R3)")
    check(re.search(r"trademarks? .*(belong to|of) (their|its) (respective )?owners?", rd, re.I | re.S) and
          re.search(r"not affiliated with or endorsed by", rd), "README: the trademark notice (R9)")
    qs = (ROOT / "QUICKSTART.md").read_text(encoding="utf-8")
    check("github.com/isod89/clipper-fm1" in qs and "github.com/hugelton/Felucca" in qs and "github.com/isod89/sloop-fm1" in qs,
          "QUICKSTART: credits the lineage with links (R3)")
    for f in ("assets/logo/stallion-mark.svg", "assets/logo/stallion-wordmark.svg", "assets/logo/stallion-logo.svg"):
        check((ROOT / f).exists() and f in (ROOT / "assets/MANIFEST").read_text(), f"{f}: there, and in assets/MANIFEST")
    check(not list((ROOT / "assets/logo").glob("clipper-*")), "assets/logo: no Clipper logo left")
    print("brand: " + ("FAILED" if fails else "all passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
