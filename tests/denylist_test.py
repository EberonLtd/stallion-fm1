#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Stallion R6: no trademark word in anything a user sees, checked against tests/denylist.txt (case-insensitive, whole
# words: a word ends at a character that is not a Latin letter (accents too), digit or _ (so a Japanese or Chinese character next to
# it, with or without a space, ends it: "Korg opsix に着想", "PPG 風", "DX7の"), and a word ending in a letter may be followed
# by digits, so JUNO1 is JUNO; full-width letters count as their ASCII ones (NFKC); a list word written in kana or kanji
# (ヤマハ) is found anywhere, since those scripts have no spaces between words). Checked:
#   the names the firmware holds at run time (the file tests/denylist_dump.c writes, argv[1]): engines, their page
#     titles and presets, every parameter label and value name, the drum kits;
#   every string literal of the firmware (firmware/**/*.c, *.h, build/gen/*.h): menus, screens, the console;
#   the editor and installer pages (web/*.html, the installer's index_pkg.html too, web/*.js): their text in every
#     language (the English, French and Japanese blocks, the TEXT tables), shown attributes and the string literals of
#     their scripts (labels, help text, the mock device): a page says what an engine does, never what it is modelled on;
#   the docs (README.md, QUICKSTART.md, DEMARRAGE-RAPIDE-FR.md, BUILDING.md, LICENSING.md, docs/*.md,
#     web/EDITOR_PROTOCOL.md) for the engines' old names (R4, R5) in capitals: they name an engine by its own name;
#   the same docs, every line, for every list word (R9: no brand or model a user reads), but the
#     history in HISTORY (Clipper's and SLOOP's release notes and manuals, as written then) and what
#     tests/denylist_docs_allow.txt lets through by exact text: README's one "modelled on" line and its trademark
#     notice (whole lines), the credits the licences require (phrases: CS-1, ...), the arpeggiator's ARP / arp
#     (words); an allow entry that matches nothing fails too, so the list cannot grow stale or wide.
#   the source (Stallion 1.0, public as a fresh snapshot): every tracked text file we wrote, every line (code
#     identifiers word by word, comments, tests, tools, web code) and every file name, for the list words; every one,
#     docs too, for private working notes (source_check below; tests/denylist_source_allow.txt the exceptions).
# Left out of the string checks: comments, #include lines, _Static_assert messages, a line marked "denylist-ok", and
# the exact strings of tests/denylist_allow.txt (each with its reason).
#   python3 tests/denylist_test.py [names.txt]
import fnmatch, glob, html.parser, os, re, sys, unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MARK = "denylist-ok"
OLD_ENGINES = ["JUNO", "JUPITER", "CS80", "OBER", "MATRI"]           # R4, R5: PONY, PALOMINO, STAMPEDE, RODEO, MARE
DOCS = ["README.md", "GUIDE.md", "QUICKSTART.md", "DEMARRAGE-RAPIDE-FR.md", "BUILDING.md", "LICENSING.md", "docs/*.md",
        "web/EDITOR_PROTOCOL.md"]
HISTORY = ["docs/release-*.md", "docs/sloop/*.md"]                    # release notes and manuals as written then


def load_words(path):
    out = []
    for ln in open(path, encoding="utf-8"):
        ln = ln.split("#", 1)[0].strip()
        if ln:
            out.append(ln.upper())
    return out


def load_allow(path):
    out = set()
    for ln in open(path, encoding="utf-8"):
        if not ln.startswith("#") and ln.strip():
            out.add(ln.rstrip("\n").split("\t", 1)[0])
    return out


LAT = "A-Za-z\u00C0-\u00D6\u00D8-\u00F6\u00F8-\u024F"   # the letters a word goes on with: Latin, accents too (arpégiateur)


def pattern(words, flags=re.I):
    """whole words, the boundary Latin letters only (a CJK character next to a word ends it); a word in another script (kana,
    kanji) anywhere"""
    alt, cjk = [], []
    for w in sorted(words, key=len, reverse=True):
        if not w.isascii():
            cjk.append(re.escape(w))
            continue
        tail = rf"(?![{LAT}_])" if w[-1].isalpha() else rf"(?![{LAT}0-9_])"
        alt.append(re.escape(w) + tail)
    return re.compile(rf"(?<![{LAT}0-9_])(?:" + "|".join(alt) + ")" + "".join("|" + c for c in cjk), flags)


def load_docs_allow(path):
    """[(file glob, kind, text)]: kind line (the whole line, exact), phrase (a hit inside this exact text), word (the
    hit is exactly this text, case and all)"""
    out = []
    for ln in open(path, encoding="utf-8"):
        if ln.startswith("#") or not ln.strip():
            continue
        f = ln.rstrip("\n").split("\t")
        assert len(f) == 4 and f[1] in ("line", "phrase", "word") and f[2] and f[3].strip(), f"denylist_docs_allow: {ln!r}"
        out.append((f[0], f[1], f[2]))
    return out


def doc_hits(pat, rel, lines, allow, used):
    """[(line number, word, line)] of the list words in a doc's lines that no allow entry for the file excuses"""
    mine = [(i, k, s) for i, (g, k, s) in enumerate(allow) if fnmatch.fnmatchcase(rel, g)]
    out = []
    for n, ln in enumerate(lines, 1):
        ln = norm(ln.rstrip("\n"))
        spans = []
        for i, k, s in mine:
            if k == "line" and ln == s:
                spans.append((i, 0, len(ln)))
            elif k == "phrase":
                spans += [(i, m.start(), m.end()) for m in re.finditer(re.escape(s), ln)]
        for m in pat.finditer(ln):
            ok = [i for i, a, b in spans if a <= m.start() and m.end() <= b]
            ok += [i for i, k, s in mine if k == "word" and m.group(0) == s]
            if ok:
                used.update(ok)
            else:
                out.append((n, m.group(0), ln))
    return out


def norm(s):
    return unicodedata.normalize("NFKC", s)               # full-width ＫＯＲＧ, ＤＸ７ as KORG, DX7


def marked(text):
    """the line numbers that carry the mark"""
    return {i + 1 for i, ln in enumerate(text.split("\n")) if MARK in ln}


def c_strings(text):
    """the string literals of C source, comments, #include lines and _Static_assert messages left out"""
    out, i, n, line = [], 0, len(text), 1
    skip_line = False
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            line += text.count("\n", i, j)
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif c == "\n":
            line += 1
            skip_line = False
            i += 1
        elif c == "#" and text[text.rfind("\n", 0, i) + 1:i].strip() == "":
            m = re.match(r"#\s*(include|error|warning|pragma)", text[i:])
            skip_line = bool(m)
            i += 1
        elif text.startswith("_Static_assert", i):
            depth, j = 0, i
            while j < n:                              # to the closing parenthesis (strings inside skipped)
                if text[j] == "(":
                    depth += 1
                elif text[j] == ")":
                    depth -= 1
                    if depth == 0:
                        break
                elif text[j] == '"':
                    j += 1
                    while j < n and text[j] != '"':
                        j += 2 if text[j] == "\\" else 1
                j += 1
            line += text.count("\n", i, j)
            i = j + 1
        elif c == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"' and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            if not skip_line:
                out.append((line, text[i + 1:j]))
            i = j + 1
        else:
            i += 1
    return out


def js_strings(text, line0=1):
    """the string literals of a script (template literals whole), comments left out: [(line, text)]"""
    out, i, n, line = [], 0, len(text), line0
    prev = ""                                         # the last significant character (a / after it: a regex?)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            line += text.count("\n", i, j)
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif c == "/" and prev and prev in "(,=:[!&|?{};+-*%<>~^" :
            j = i + 1                                 # a regex literal: to its closing / (classes skipped)
            cls = False
            while j < n and text[j] != "\n" and (cls or text[j] != "/"):
                if text[j] == "\\":
                    j += 1
                elif text[j] == "[":
                    cls = True
                elif text[j] == "]":
                    cls = False
                j += 1
            i, prev = j + 1, "x"
        elif c in "\"'`":
            j = i + 1
            while j < n and text[j] != c and not (c != "`" and text[j] == "\n"):
                j += 2 if text[j] == "\\" else 1
            out.append((line, text[i + 1:j]))
            line += text.count("\n", i, j)
            i, prev = j + 1, "x"
        else:
            if c == "\n":
                line += 1
            elif not c.isspace():
                prev = c
            i += 1
    return out


class Page(html.parser.HTMLParser):
    """a web page's text, the shown attributes and its scripts' string literals: [(line, text)]"""
    SHOWN = ("title", "placeholder", "alt", "aria-label", "value", "content", "label")

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.out, self.tag = [], ""

    def handle_starttag(self, tag, attrs):
        self.tag = tag
        for k, v in attrs:
            if k in self.SHOWN and v:
                self.out.append((self.getpos()[0], v))

    def handle_endtag(self, tag):
        self.tag = ""

    def handle_data(self, data):
        if self.tag == "script":
            self.out += js_strings(data, self.getpos()[0])
        elif self.tag != "style" and data.strip():
            self.out.append((self.getpos()[0], data))


def web_strings(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    if not path.endswith(".html"):
        return js_strings(text)
    p = Page()
    p.feed(text)
    return p.out


# Stallion 1.0 (the source goes public): no list word anywhere in the files we wrote either: code identifiers, file
# names, comments, tests, tools, web code, our docs (git ls-files: every tracked text file but SOURCE_KEEP). An identifier
# counts word by word (ENG_JUNO, juno_render: JUNO). The short words (letters only, 3 or fewer) and the numbers count
# in capitals as they are (MT_CUT is the matrix's, not a word MT; "dx" is a step); the arpeggiator's ARP and the
# companies of the third-party code we credit by name are not checked. tests/denylist_source_allow.txt lets an exact
# line or phrase through, each with its reason (the upstream lines we keep as they were, the credits); an entry that
# excuses nothing fails.
SOURCE_KEEP = [                                   # (glob, reason)
    ("LICENSE", "the GPL text"), ("LICENSES/*", "the licence texts"), ("CREDITS.md", "credits the licences require"),
    ("assets/MANIFEST", "the assets' sources and licences"), ("assets/fonts/*", "the fonts' licences"),
    ("assets/hiphop-pack/*", "upstream SLOOP's pack, unchanged"), ("README-CLIPPER.md", "upstream Clipper's README"),
    ("docs/sloop/*", "upstream SLOOP's manuals"), ("docs/release-*.md", "release notes as written then (history)"),
    ("firmware/src/midi_uart.c", "upstream Clipper's, unchanged"), ("tests/filter_test.c", "upstream Clipper's, unchanged"),
    ("tools/gen_hiphop_pack.py", "upstream SLOOP's, unchanged"), ("tools/gen_builtin_hiphop.py", "upstream SLOOP's, unchanged"),
    ("firmware/src/lcd.c", "upstream Clipper's, unchanged"), ("firmware/src/drum_synth.c", "upstream Clipper's, unchanged"),
    ("firmware/src/ui_studio.c", "upstream Clipper's, unchanged"), ("tests/midi_uart_test.c", "upstream Clipper's, unchanged"),
    ("assets/samples-cc0/*", "the samples' sources and credits, as their authors wrote them"),
    ("tests/denylist.txt", "the list itself"), ("tests/denylist_test.py", "this test (its own examples)"),
    ("tests/denylist_allow.txt", "the exceptions"), ("tests/denylist_docs_allow.txt", "the exceptions"),
    ("tests/denylist_source_allow.txt", "the exceptions"),
]
SOURCE_SKIP = {"ARP", "PLAITS", "SERUM", "HOOVER", "EMULATOR", "TAURUS",   # the arpeggiator; ordinary words; MACRO's credited source
               "CS", "MT"}                        # a chip select, USB's class-specific; the matrix's targets (MT_CUT)
SOURCE_CAPS = {"NORD"}                            # a word in capitals only (nord: a variable's name)


def source_patterns(words):
    """(long words: any case, _ and camelCase humps end a word; short words: capitals, as written, nothing joined to
    them; numbers: only as a word in prose (the 303-style, a 106's, the 808 kit), never in a table of numbers)"""
    short = lambda w: (len(w) <= 3 and w.isalpha()) or w in SOURCE_CAPS
    use = [w for w in words if w not in SOURCE_SKIP]
    longw = [w for w in use if not w.isascii() or not (short(w) or w.isdigit())]
    alt = "|".join(re.escape(w) for w in sorted((w for w in use if w.isascii() and short(w)), key=len, reverse=True))
    num = "|".join(re.escape(w) for w in use if w.isdigit())
    spat = re.compile(rf"(?<![{LAT}0-9_])(?:{alt})(?![{LAT}0-9_#])"
                      rf"|(?<=[A-Za-z] )(?:{num})(?![0-9.,])|(?<![0-9.,\w-])(?:{num})(?=-?[A-Za-z]|'s| ?(?:kit|cymbal|style)\b)")
    return pattern(longw), spat


def number_only(ln, m):
    """a list number inside a C literal (106u) or a hash (303cc9b7...): a number, not a model"""
    a, b = m.start(), m.end()
    while a and ln[a - 1].isalnum():
        a -= 1
    while b < len(ln) and ln[b].isalnum():
        b += 1
    tok = ln[a:b]
    return bool(re.fullmatch(r"[0-9]+[uUlL]+|[0-9a-fA-F]{8,}", tok))


def source_hits(lpat, spat, rel, lines, allow, used):
    mine = [(i, k, s) for i, (g, k, s) in enumerate(allow) if fnmatch.fnmatchcase(rel, g)]
    out = []
    for n, ln in enumerate(lines, 1):
        ln = norm(ln.rstrip("\n"))
        split, at = [], []                            # the line with a space for each _ and at each hump, and where
        for j, c in enumerate(ln):                    # each of its characters was
            if j and c.isupper() and ln[j - 1].islower():
                split.append(" "), at.append(j)
            split.append(" " if c == "_" else c), at.append(j)
        split, at = "".join(split), at + [len(ln)]
        spans = []
        for i, k, s in mine:
            if k == "line" and ln == s:
                spans.append((i, 0, len(ln)))
            elif k == "phrase":
                spans += [(i, m.start(), m.end()) for m in re.finditer(re.escape(s), ln)]
        found = [(at[m.start()], at[m.end() - 1] + 1, m.group(0)) for m in lpat.finditer(split)]
        found += [(m.start(), m.end(), m.group(0)) for m in spat.finditer(ln) if not number_only(ln, m)]
        for a, b, w in sorted(set(found)):
            ok = [i for i, x, y in spans if x <= a and b <= y]
            if ok:
                used.update(ok)
            else:
                out.append((n, w, ln))
    return out

# Stallion 1.0 (a fresh public snapshot): no private working notes in the files we wrote either: a person quoted or
# a person's choices told, private material outside this repository (its folders, reference recordings, tuning runs).
# A comment gives the technical reason in the code's own voice. tests/denylist_source_allow.txt lets an exact phrase
# through (each with its reason); tools/private_check.py and its test name those folders to keep them out.
_NOTES = (r"\bthe owner\b|\bowner's\b(?! manual)|\bowner (?:said|asked|heard|wants|chose|liked|found|wanted)|\bby ear\b"
          r"|\brefs/(?!heads/|tags/|remotes/)|youtube|youtu\.be|\bJohn\b(?! Gorman\b)")
# more private terms, one regular expression a line, from a file outside the repository (STALLION_PRIVATE_TERMS)
_EXTRA = os.environ.get("STALLION_PRIVATE_TERMS")
if _EXTRA and os.path.isfile(_EXTRA):
    with open(_EXTRA, encoding="utf-8") as f:
        _NOTES += "".join("|" + ln.strip() for ln in f if ln.strip() and not ln.startswith("#"))
NOTES = re.compile(_NOTES, re.I)
NOTES_KEEP = ["tools/private_check.py", "tests/private_check_test.py"]


def note_hits(rel, lines, allow, used):
    mine = [(i, k, s) for i, (g, k, s) in enumerate(allow) if fnmatch.fnmatchcase(rel, g)]
    out = []
    for n, ln in enumerate(lines, 1):
        ln = norm(ln.rstrip("\n"))
        for m in NOTES.finditer(ln):
            ok = [i for i, k, s in mine if k == "line" and ln == s]
            ok += [i for i, k, s in mine if k == "phrase" for x in re.finditer(re.escape(s), ln)
                   if x.start() <= m.start() and m.end() <= x.end()]
            if ok:
                used.update(ok)
            else:
                out.append((n, m.group(0), ln))
    return out


def source_files():
    import subprocess
    try:
        names = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True).stdout.decode().split("\0")
    except (OSError, subprocess.CalledProcessError):
        return []
    hist = {os.path.relpath(f, ROOT) for g in HISTORY for f in glob.glob(os.path.join(ROOT, g))}
    docs = {os.path.relpath(f, ROOT) for g in DOCS for f in glob.glob(os.path.join(ROOT, g))} - hist
    out = []
    for rel in names:
        if not rel or any(fnmatch.fnmatchcase(rel, g) for g, _ in SOURCE_KEEP):
            continue
        p = os.path.join(ROOT, rel)
        if not os.path.isfile(p):
            continue
        raw = open(p, "rb").read()
        if b"\0" in raw[:8192]:
            continue                                          # binary
        out.append((rel, rel in docs, raw.decode("utf-8", errors="replace").split("\n")))
    return out                                                # (a doc's words: the docs check above, R9)


def source_check(words, bad):
    """the files we wrote, every line (names, comments, tests, tools, web code, our docs) and every file name"""
    lpat, spat = source_patterns(words)
    for s, hit in [("ENG_JUNO", 1), ("juno_render", 1), ("eng_ober.c", 1), ("MT_CUT", 0), ("float dx;", 0), ("the SEM", 1),
                   ("a TB-303 line", 1), ("OberVave", 1), ("obervave", 0), ("matrix", 0), ("SEMI", 0), ("junoRender", 1),
                   ("FM_MOOG", 1), ("cs80_test", 1), ("IR3109", 1), ("ARP MODE", 0), ("OB_KTRK", 0), ("t_dx7", 1)]:
        assert bool(source_hits(lpat, spat, "x.c", [s], [], set())) == bool(hit), s
    for s_, hit in [("(the owner: louder)", 1), ("tuned by ear", 1), ("see refs/x.wav", 1), ("g->owner = 1", 0),
                    ("Eberon Limited (John Gorman)", 1 - 1), ("the drum audition", 0), ("PEAK climbs", 0)]:
        assert bool(note_hits("x.c", [s_], [], set())) == bool(hit), s_
    allow = load_docs_allow(os.path.join(ROOT, "tests/denylist_source_allow.txt"))
    used, files = set(), source_files()
    for rel, doc, lines in files:
        for n, w, ln in source_hits(lpat, spat, rel, [rel], [], set()):
            bad.append(f"{rel}: the file name holds {w!r}")
        for n, w, ln in ([] if doc else source_hits(lpat, spat, rel, lines, allow, used)):
            bad.append(f"{rel}:{n}: {w!r} in \"{ln.strip()[:90]}\" (the source names no brand or model)")
        if rel not in NOTES_KEEP:
            for n, w, ln in note_hits(rel, lines, allow, used):
                bad.append(f"{rel}:{n}: {w!r} in \"{ln.strip()[:90]}\" (a private working note: say the technical reason)")
    for i, (g, k, s) in enumerate(allow):
        if i not in used:
            bad.append(f"tests/denylist_source_allow.txt: {g} {k} \"{s[:60]}\" excuses nothing (stale: remove or fix it)")
    if len(files) < 300:
        bad.append(f"source: only {len(files)} files scanned (git ls-files?)")
    return len(files)


def main():
    words = load_words(os.path.join(ROOT, "tests/denylist.txt"))
    allow = load_allow(os.path.join(ROOT, "tests/denylist_allow.txt"))
    pat, bad, nstr = pattern(words), [], 0
    for s, hit in [("JUNO1", 1), ("jp-8 pad", 1), ("OB-X BRASS", 1), ("FM BELLS MT", 1), ("808 SUB", 1),   # the rule
                   ("SEMI", 0), ("ARPEGGIO", 0), ("MATRIX", 0), ("OBERVAVE", 0), ("1808", 0), ("SIDE", 0),
                   ("Korg opsix に着想", 1), ("PPG 風スイープ", 1), ("DX7の音色", 1), ("（DX7）", 1), ("ＤＸ７", 1),
                   ("ヤマハ風", 1), ("コルグの", 1), ("オペレーター", 0), ("ウェーブテーブル", 0), ("アルペジエーター", 0), ("arpégiateur", 0), ("Wavestationの", 1)]:
        assert bool(pat.search(norm(s))) == bool(hit), s

    def check(where, s):
        nonlocal nstr
        nstr += 1
        if s not in allow:
            for m in pat.finditer(norm(s)):
                bad.append(f"{where}: {m.group(0)!r} in \"{s.strip()[:90]}\"")

    if len(sys.argv) > 1:                             # the run-time names (tests/denylist_dump.c)
        n0 = nstr
        for ln in open(sys.argv[1], encoding="utf-8", errors="replace"):
            kind, where, text = (ln.rstrip("\n").split("\t") + ["", ""])[:3]
            check(f"{kind} {where}", text)
        if nstr - n0 < 1000:
            bad.append(f"{sys.argv[1]}: only {nstr - n0} names (the dump is incomplete)")
    cfiles = sorted(glob.glob(os.path.join(ROOT, "firmware/**/*.[ch]"), recursive=True))
    cfiles += sorted(glob.glob(os.path.join(ROOT, "build/gen/*.h")))
    for f in cfiles:
        text = open(f, encoding="utf-8", errors="replace").read()
        skip = marked(text)
        for ln, s in c_strings(text):
            if ln not in skip:
                check(f"{os.path.relpath(f, ROOT)}:{ln}", s)
    web = sorted(glob.glob(os.path.join(ROOT, "web/*.html")) + glob.glob(os.path.join(ROOT, "web/*.js")))
    for f in web:
        skip = marked(open(f, encoding="utf-8", errors="replace").read())
        for ln, s in web_strings(f):
            if ln not in skip:
                check(f"{os.path.relpath(f, ROOT)}:{ln}", s)
    old = pattern(OLD_ENGINES, 0)                     # the docs: the old engine names, in capitals
    for g in DOCS:
        for f in sorted(glob.glob(os.path.join(ROOT, g))):
            for i, ln in enumerate(open(f, encoding="utf-8", errors="replace")):
                nstr += 1
                for m in old.finditer(ln):
                    bad.append(f"{os.path.relpath(f, ROOT)}:{i + 1}: engine {m.group(0)!r} by its old name")
    dallow = load_docs_allow(os.path.join(ROOT, "tests/denylist_docs_allow.txt"))
    for s, hit in [("the Moog ladder", 1), ("a DX7 bank", 1), ("JUNO-106", 1), ("ARP MODE", 0), ("the arp's", 0),
                   ("Arp Odyssey", 1), ("a CS-1 ribbon", 0), ("CS-80", 1)]:              # the docs' rule
        assert bool(doc_hits(pat, "x.md", [s], [("*", "word", "ARP"), ("*", "word", "arp"), ("*", "phrase", "CS-1")],
                             set())) == bool(hit), s
    hist = {os.path.relpath(f, ROOT) for g in HISTORY for f in glob.glob(os.path.join(ROOT, g))}
    used, ndocs = set(), 0
    for g in DOCS:
        for f in sorted(glob.glob(os.path.join(ROOT, g))):
            rel = os.path.relpath(f, ROOT)
            if rel in hist:
                continue
            ndocs += 1
            for n, w, ln in doc_hits(pat, rel, open(f, encoding="utf-8", errors="replace").readlines(), dallow, used):
                bad.append(f"{rel}:{n}: {w!r} in \"{ln.strip()[:90]}\" (a doc says what it is, not whose it is)")
    for i, (g, k, s) in enumerate(dallow):
        if i not in used:
            bad.append(f"tests/denylist_docs_allow.txt: {g} {k} \"{s[:60]}\" excuses nothing (stale: remove or fix it)")
    if ndocs < 8:
        bad.append(f"docs: only {ndocs} files scanned")
    nsrc = source_check(words, bad)                   # the source, every line (Stallion 1.0, public)
    if bad:
        print("\n".join(bad))
        print(f"denylist: FAIL, {len(bad)} deny-listed word(s) in what a user sees (tests/denylist.txt; the exceptions "
              f"in tests/denylist_allow.txt)")
        return 1
    print(f"denylist: ok, {len(words)} words, none in {nstr} names, strings and lines of the firmware, web pages and docs, nor in the {nsrc} source files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
