#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Changes: Copyright (C) 2026 Eberon Limited (John Gorman)
"""Make the site (GitHub Pages):

  index.html                  redirect to the installer; with --smk37: index_site.html, the front page (what Stallion
                              is, the two installers, the device's screens, the engines, the editor, the credits)
  whats-new.html              (with --smk37) whats_new.html: everything Stallion 1.0 adds to Clipper
  cheat-sheet.html            (with --smk37) cheat_sheet.html: the one-page cheat sheet (the panel on both instruments,
                              the sounds, playing, the arpeggiator and sequencer, saving, MIDI, updating), printable
  firmware/stallion-VER.fwsc  the package (stallion R2)
  firmware/stallion-smk37-VER.fwsc  (with --smk37) the SMK-37 Pro package (Stallion supports the SMK-37 Pro only)
  webapp/installer-smk37/index.html  (with --smk37) index_smk37.html with smkpkg.js, smkota.js (genkigenki's
                              Windows-safe SMK-37 update library) and the package's metadata inlined; it installs on an
                              SMK-37 Pro (and its Starrykey 37 Play twin) and refuses the other models. Both installers
                              then show the chooser at the top
  webapp/installer/index.html index_pkg.html with fm1pkg.js, fm1ota.js and the metadata inlined, its counts
                              ({{ENGINES}} {{SOUNDS}} {{FILTERS}} {{TUNINGS}}) from the firmware (tools/site_counts.c)
  webapp/editor/index.html    editor.html (+ fukiai.ttf, FUKIAI-LICENSE.txt)
  webapp/editor/stallion-editor.html  the editor in one file, for offline use (1.1, after SLOOP 2.6): its icon font
                              inlined, its links to the installer and the font licence online; opened from the disk
                              in Chrome / Edge (file:// is a secure context: Web MIDI asks for permission)
  webapp/screens/*.png        (Stallion) the device's screens (web/screens, tools/make_screens.py), shown on the installer
  webapp/sw.js, webapp/stallion.webmanifest, webapp/icon-*.png  the site without internet (1.1, after SLOOP 2.6): an
                              app to install; opened once online, the installer, the editor and the firmware are
                              kept by the browser and work offline, still on https (on Android a downloaded file
                              opens as content://, where Chrome refuses Web MIDI)
  src/                        not touched

  web/make_site.py build/felucca-X.Y.fwsc X.Y OUT_DIR [--smk37 SMK.fwsc ...] [--beta]
  (--beta: web/beta_banner.html at the top of the installer: the beta channel, docs/beta)
  (--smk37: the SMK-37 Pro package, once. The FM-1 package must be an FM-1 one and the --smk37 package an SMK-37 Pro
   one: anything else, a second package or another SMK-37 model's, is refused)

  web/make_site.py --pro X.Y OUT_DIR build/clipper-pro.fwsc [build/clipper-pro-elite.fwsc build/clipper-pro-mke.fwsc]
  Clipper Pro (the SMK-37 build): webapp/installer/ from index_pro.html with smkpkg.js, smkota.js (genkigenki's
  Windows-safe update library, as SLOOP Pro's) and one package per model (it installs the one for the connected
  keyboard), firmware/clipper-pro[-elite|-mke]-X.Y.fwsc, the same editor.

The package identity (Stallion's own number: FM-1_1042XY, SMK-37 Pro_1042XY; tools/fm1pkg_make.py STALLION_NUM) is
read from the package; the device reports it after the install. A package with another identity (Clipper's FM-1_9xx,
the FM-1_900 Stallion had before its own number) is refused.
"""
import base64
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tools"))
from fm1pkg_make import STALLION_NUM  # noqa: E402  (Stallion's identity number, set there once)
OWN_NUM = STALLION_NUM + r"\d\d"      # a package's number: FM-1_1042XY
ONLINE = "https://eberonltd.github.io/stallion-fm1/"           # (the offline editor's links: Stallion's site)


COUNTS = ("ENGINES", "SOUNDS", "FILTERS", "TUNINGS")
PIXEL = "stallion-pixel.ttf"     # (Stallion) the FM-1's font (Terminus, OFL) for the pages' headings, from editor.html


def site_css(html, page):
    """(Stallion) web/site.css, the style the front page and both installers share, at the page's SITE_CSS marker"""
    if html.count("/*SITE_CSS*/") != 1:
        raise SystemExit(f"{page} must contain /*SITE_CSS*/ once; update make_site.py")
    return html.replace("/*SITE_CSS*/", (HERE / "site.css").read_text(encoding="utf-8"))


def pixel_font(app):
    """(Stallion) webapp/stallion-pixel.ttf: the Terminus TrueType the editor inlines (tools/gen_webfont.py), as a file
    the pages share and the browser keeps"""
    m = re.search(r"/\*SLOOP-FONT\*/url\(data:font/ttf;base64,([A-Za-z0-9+/=]+)\)", (HERE / "editor.html").read_text(encoding="utf-8"))
    if not m:
        raise SystemExit("editor.html: no /*SLOOP-FONT*/ data URI (tools/gen_webfont.py); update make_site.py")
    app.mkdir(parents=True, exist_ok=True)
    (app / PIXEL).write_bytes(base64.b64decode(m.group(1)))


def photos(html, out, page):
    """(Stallion) the product photos' slots: <!--PHOTO:name.jpg alt="..."-->fallback<!--/PHOTO--> becomes the photo
    when web/photos/name.jpg exists (copied to webapp/photos/), else stays the fallback (a drawn device frame around a
    real screen). A photo is added by dropping it in web/photos/ (hero-fm1.jpg, hero-smk37.jpg; 4:3); the editor's
    pictures (editor-sound.png, editor-patchbay.png, editor-matrix.png) are screenshots of its demo (?mock=1)"""
    def one(m):
        name, alt, fallback = m.group(1), m.group(2), m.group(3)
        src = HERE / "photos" / name
        if not src.exists():
            return fallback
        (out / "webapp" / "photos").mkdir(parents=True, exist_ok=True)
        shutil.copy(src, out / "webapp" / "photos" / name)
        w, h = 1200, 900                             # a photo: 4:3; a PNG (the editor's pictures): its own size
        head = src.read_bytes()[:24]
        if head[:8] == b"\x89PNG\r\n\x1a\n":
            w, h = int.from_bytes(head[16:20], "big"), int.from_bytes(head[20:24], "big")
        lazy = "" if name.startswith("hero-") else ' loading="lazy"'
        return f'<img class="photo" src="webapp/photos/{name}" alt="{alt}" width="{w}" height="{h}"{lazy}>'
    html, n = re.subn(r'<!--PHOTO:([\w.-]+) alt="([^"]*)"-->(.*?)<!--/PHOTO-->', one, html, flags=re.S)
    if "<!--PHOTO:" in html:
        raise SystemExit(f"{page}: a PHOTO slot not written <!--PHOTO:name.jpg alt=\"...\"-->...<!--/PHOTO-->")
    return html


def firmware_counts():
    """(Stallion) the numbers the installer page states, from the firmware (tools/site_counts.c on the host: the
    engines, their factory sounds, the filter models, the tunings), never typed by hand; needs a build (build/gen)"""
    root = HERE.parent
    if not (root / "build/gen").is_dir():
        raise SystemExit("make_site.py: build/gen is missing: build the firmware first (tools/build.py)")
    with tempfile.TemporaryDirectory() as d:
        exe = os.path.join(d, "site_counts")
        cc = os.environ.get("CC", "cc").split()
        subprocess.run(cc + ["-O1", "-w", "-DCLIPPER_SYNTH=1", "-Ibuild/gen", "-Ifirmware/src", "-Ifirmware/hal",
                             "tools/site_counts.c", "-lm", "-o", exe], cwd=root, check=True)
        c = json.loads(subprocess.run([exe], check=True, capture_output=True, text=True).stdout)
    return {k: int(c[k.lower()]) for k in COUNTS}


def fill_counts(html, counts):
    """the page's {{ENGINES}} .. {{TUNINGS}} filled; none left"""
    for k in COUNTS:
        html = html.replace("{{" + k + "}}", str(counts[k]))
    left = re.findall(r"\{\{[A-Z]+\}\}", html)
    if left:
        raise SystemExit(f"index_pkg.html: {sorted(set(left))} not known to make_site.py")
    return html


def offline_editor(html):
    """editor.html as one file for the disk: the icon font inlined, the download link out, the relative links online"""
    font = base64.b64encode((HERE / "fukiai.ttf").read_bytes()).decode()
    for a, b in (('url("fukiai.ttf")', 'url(data:font/ttf;base64,' + font + ')'),
                 ('href="../installer/"', 'href="' + ONLINE + 'webapp/installer/" target="_blank" rel="noopener"'),
                 ('href="FUKIAI-LICENSE.txt"', 'href="' + ONLINE + 'webapp/editor/FUKIAI-LICENSE.txt" target="_blank" rel="noopener"')):
        if html.count(a) != 1:
            raise SystemExit(f"editor.html must contain {a} once; update make_site.py")
        html = html.replace(a, b)
    for pat, what in ((r"<!--DL-->.*?<!--/DL-->", "<!--DL-->...<!--/DL-->"),       # (the file has no site
                      (r"<!--PWA-->.*?<!--/PWA-->", "<!--PWA-->...<!--/PWA-->"),    # around it: no app to
                      (r"/\*PWA\*/.*?/\*/PWA\*/", "/*PWA*/.../*/PWA*/"),           # install, no copy kept)
                      (r'<button id="install"[^>]*></button>\s*', '<button id="install">')):
        html, n = re.subn(pat, "", html, flags=re.S)
        if n != 1:
            raise SystemExit(f"editor.html must contain {what} once; update make_site.py")
    return html


ICONS = ("icon-192.png", "icon-512.png", "icon-maskable-512.png")


def app_files(out, version, fw_name, extra=None):
    """webapp/: the manifest, the icons and sw.js with the list of what it keeps (relative to webapp/) and a
    stamp of their contents, so any change makes a new sw.js and the browser takes the new site"""
    app = out / "webapp"
    for f in ICONS:
        shutil.copy(HERE / f, app / f)
    manifest = {
        "name": "Stallion", "short_name": "Stallion", "id": "editor/", "start_url": "editor/", "scope": "./",
        "display": "standalone", "background_color": "#0B1E36", "theme_color": "#0B1E36",
        "description": "Stallion for the M-VAVE FM-1: the editor and the installer, also without internet",
        "icons": [{"src": "icon-192.png", "sizes": "192x192", "type": "image/png", "purpose": "any"},
                  {"src": "icon-512.png", "sizes": "512x512", "type": "image/png", "purpose": "any"},
                  {"src": "icon-maskable-512.png", "sizes": "512x512", "type": "image/png", "purpose": "maskable"}],
    }
    (app / "stallion.webmanifest").write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8")
    pixel_font(app)
    keep = {"installer/": app / "installer" / "index.html", "editor/": app / "editor" / "index.html", PIXEL: app / PIXEL,
            "../firmware/" + fw_name: out / "firmware" / fw_name, "stallion.webmanifest": app / "stallion.webmanifest"}
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt"):
        if (app / "editor" / f).exists():
            keep["editor/" + f] = app / "editor" / f
    for f in ICONS:
        keep[f] = app / f
    for p in sorted((app / "screens").glob("*.png")):      # (Stallion) the installer's screenshots
        keep["screens/" + p.name] = p
    keep.update(extra or {})                                 # (Stallion) the SMK-37 installer and its packages
    h = hashlib.sha256()
    for k in sorted(keep):
        h.update(k.encode() + b"\0" + keep[k].read_bytes())
    sw = (HERE / "sw.js").read_text(encoding="utf-8")
    for mark in ("/*STAMP*/", "/*FILES*/[]"):
        if sw.count(mark) != 1:
            raise SystemExit(f"sw.js must contain {mark} once; update make_site.py")
    sw = sw.replace("/*STAMP*/", f"{version}-{h.hexdigest()[:12]}").replace("/*FILES*/[]", json.dumps(sorted(keep)))
    (app / "sw.js").write_text(sw, encoding="utf-8")
    return sorted(keep)


BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


SMK_MODELS = (("SMK-37 Pro", ""),)   # (Stallion: the SMK-37 Pro only; model -> file suffix)


def boards(current):
    """(Stallion) the FM-1 / SMK-37 Pro chooser at the top of both installers: large, the current one filled"""
    links = []
    for name, href, key in (("FM-1", "../installer/", "fm1"), ("SMK-37 Pro", "../installer-smk37/", "smk37")):
        cur = ' aria-current="page"' if key == current else ""
        links.append(f'<a href="{href}"{cur}>{name}</a>')
    return '<nav class="boards"><p data-t="boardPick"></p><div>' + "".join(links) + "</div></nav>"


def smk_packages(pkgs):
    """(Stallion) the SMK-37 packages, checked before anything is written: model -> (identity, bytes)"""
    raws = {}
    for pkg in map(Path, pkgs):
        raw = pkg.read_bytes()
        product = product_of(raw)
        m = re.fullmatch(r"(SMK-37 Pro|SMK-37 Elite|MKE-P37)_" + OWN_NUM, product)
        if not m:
            raise SystemExit(f"{pkg}: identity {product!r} is not a Stallion SMK-37 package (SMK-37 Pro / SMK-37 Elite / "
                             f"MKE-P37_{STALLION_NUM}XY): the SMK-37 installer refuses it" +
                             (" (an FM-1 package goes first, without --smk37)" if product.startswith("FM-1_") else ""))
        if b"FELUCCA-LOADER-1" not in raw:
            raise SystemExit(f"{pkg}: no Felucca update loader in it")
        if m[1] != "SMK-37 Pro":
            raise SystemExit(f"{pkg}: identity {product!r}: Stallion supports the SMK-37 Pro only (no {m[1]} package)")
        if m[1] in raws:
            raise SystemExit(f"{pkg}: a second package for {m[1]}")
        raws[m[1]] = (product, raw)
    if "SMK-37 Pro" not in raws:
        raise SystemExit("--smk37: the SMK-37 Pro package is needed (it also serves the Starrykey 37 Play)")
    return raws


def smk_site(version, out, raws, beta=False):
    """(Stallion) webapp/installer-smk37/: Stallion on the SMK-37, one package per model (smk_packages); returns the
    files sw.js keeps (relative to webapp/)"""
    vtag = re.sub(r"[^A-Za-z0-9.-]", "-", version)
    inst, fw = out / "webapp" / "installer-smk37", out / "firmware"
    inst.mkdir(parents=True, exist_ok=True)
    entries, keep = [], {}
    for model, sfx in SMK_MODELS:
        if model not in raws:
            continue
        product, raw = raws[model]
        name = f"stallion-smk37{sfx}-{vtag}.fwsc"
        (fw / name).write_bytes(raw)
        entries.append({"product": product, "pkg": "../../firmware/" + name, "sha256": hashlib.sha256(raw).hexdigest()})
        keep["../firmware/" + name] = fw / name
    html = (HERE / "index_smk37.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "smkpkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "smkota.js").read_text(encoding="utf-8"))
    for mark in ("/*LIB*/", "/*META*/", "<!--BOARDS-->", "<!--LOGO-->", "<!--BANNER-->"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_smk37.html must contain {mark} once; update make_site.py")
    logo = HERE.parent / "assets" / "logo" / "stallion-logo.svg"
    banner = ""
    if beta:
        notes = HERE / "beta_banner.html"
        banner = notes.read_text(encoding="utf-8") if notes.exists() else "<p><b>BETA</b></p>"
    html = (html.replace("/*LIB*/", lib).replace("/*META*/", json.dumps({"version": version, "pkgs": entries}))
            .replace("<!--BOARDS-->", boards("smk37"))
            .replace("<!--LOGO-->", logo.read_text(encoding="utf-8") if logo.exists() else "<b>Stallion</b>")
            .replace("<!--BANNER-->", banner))
    html = site_css(html, "index_smk37.html")
    (inst / "index.html").write_text(html, encoding="utf-8")
    keep["installer-smk37/"] = inst / "index.html"
    print(f"site: {out}: webapp/installer-smk37 ({len(html)} B), firmware/" + ", ".join(
        f"{e['pkg'].rsplit('/', 1)[1]} ({e['product']})" for e in entries))
    return keep


def main(pkg, version, out, smk=()):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if re.fullmatch(r"(SMK-37 Pro|SMK-37 Elite|MKE-P37)_\d+", product):
        raise SystemExit(f"{pkg}: identity {product!r} is an SMK-37 package: the FM-1 installer refuses it "
                         "(give it with --smk37)")
    if not re.fullmatch(r"FM-1_" + OWN_NUM, product):
        raise SystemExit(f"{pkg}: identity {product!r} is not a Stallion FM-1 package (FM-1_{STALLION_NUM}XY)")
    smk = smk_packages(smk) if smk else {}          # (refused before anything is written)
    if b"FELUCCA-LOADER-1" not in raw:              # marker of firmware/loader
        raise SystemExit(f"{pkg}: no Felucca update loader in it")
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8"))
    name = f"stallion-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"   # (stallion R2)
    meta = json.dumps({"version": version, "product": product, "pkg": "../../firmware/" + name,
                       "sha256": hashlib.sha256(raw).hexdigest()})   # the page checks the download against it
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    counts = firmware_counts()
    html = fill_counts(html, counts)   # (Stallion: the engines, sounds, filters, tunings as the firmware has them)
    logo = HERE.parent / "assets" / "logo" / "stallion-logo.svg"  # the Stallion logo, inline (tools/gen_logo.py)
    html = html.replace("<!--LOGO-->", logo.read_text(encoding="utf-8") if logo.exists() else "<b>Stallion</b>")
    inst, ed, fw = out / "webapp" / "installer", out / "webapp" / "editor", out / "firmware"
    for d in (inst, ed, fw):
        d.mkdir(parents=True, exist_ok=True)
    guide = HERE.parent / "output/pdf/Studio-0.3-guide-rapide-FR.pdf"
    guide_link = ""
    if guide.exists() and "--studio-guide" in sys.argv:   # LIVE: that guide describes the removed RYTHME tab
        shutil.copy(guide, out / guide.name)
        guide_link = '<p lang="fr"><a href="../../' + guide.name + '">Guide rapide illustré (PDF, 4 pages)</a></p>'
    html = html.replace("<!--STUDIO_GUIDE-->", guide_link)
    banner = ""
    if "--beta" in sys.argv:                       # the beta channel (docs/beta): what it is, what it implies
        notes = HERE / "beta_banner.html"
        banner = notes.read_text(encoding="utf-8") if notes.exists() else "<p><b>BETA</b></p>"
    html = html.replace("<!--BANNER-->", banner)
    if html.count("<!--BOARDS-->") != 1:
        raise SystemExit("index_pkg.html must contain <!--BOARDS--> once; update make_site.py")
    html = html.replace("<!--BOARDS-->", boards("fm1") if smk else "")
    html = site_css(html, "index_pkg.html")
    for old in list(fw.glob("felucca-*.fwsc")) + list(fw.glob("sloop-*.fwsc")) + list(fw.glob("clipper-*.fwsc")) + list(fw.glob("stallion-*.fwsc")):   # one package: the current one
        old.unlink()
    (inst / "index.html").write_text(html, encoding="utf-8")
    shutil.copy(pkg, fw / name)
    ed_html = (HERE / "editor.html").read_text(encoding="utf-8")
    dl = f'download="stallion-editor-{re.sub(r"[^A-Za-z0-9.-]", "-", version)}.html"'
    if ed_html.count('download="stallion-editor.html"') != 1 or html.count('download="stallion-editor.html"') != 1:
        raise SystemExit('editor.html and index_pkg.html must link download="stallion-editor.html" once; update make_site.py')
    (ed / "index.html").write_text(ed_html.replace('download="stallion-editor.html"', dl), encoding="utf-8")
    (ed / "stallion-editor.html").write_text(offline_editor(ed_html), encoding="utf-8")
    (inst / "index.html").write_text(html.replace('download="stallion-editor.html"', dl), encoding="utf-8")
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt"):
        if (HERE / f).exists():
            shutil.copy(HERE / f, ed / f)
    shots = out / "webapp" / "screens"           # (Stallion) the device's screens the installer shows
    if shots.exists():
        shutil.rmtree(shots)
    if (HERE / "screens").is_dir():
        shutil.copytree(HERE / "screens", shots)
    for img in re.findall(r'src="\.\./screens/([^"]+)"', html):
        if not (shots / img).exists():
            raise SystemExit(f"index_pkg.html shows screens/{img}, not in web/screens (tools/make_screens.py)")
    smk_dir = out / "webapp" / "installer-smk37"
    if smk_dir.exists():
        shutil.rmtree(smk_dir)
    extra = smk_site(version, out, smk, "--beta" in sys.argv) if smk else {}
    app_files(out, version, name, extra)
    if smk:                                          # (Stallion) the front page: which instrument
        front = (HERE / "index_site.html").read_text(encoding="utf-8")
        for mark in ("<!--LOGO-->", "<!--LOGO2-->"):
            if front.count(mark) != 1:
                raise SystemExit(f"index_site.html must contain {mark} once; update make_site.py")
        svg = logo.read_text(encoding="utf-8") if logo.exists() else "<b>Stallion</b>"
        front = front.replace("<!--LOGO-->", svg).replace("<!--LOGO2-->", svg.replace('aria-label="Stallion"', 'aria-hidden="true"'))
        front = photos(site_css(fill_counts(front, counts), "index_site.html"), out, "index_site.html")
        front = front.replace('download="stallion-editor.html"', dl)
        for img in re.findall(r'(?:src="|, |srcset=")webapp/(screens/[^" ,]+)', front):
            if not (out / "webapp" / img).exists():
                raise SystemExit(f"index_site.html shows webapp/{img}: not in web/screens (tools/make_screens.py)")
        (out / "index.html").write_text(front, encoding="utf-8")
        new = (HERE / "whats_new.html").read_text(encoding="utf-8")   # (Stallion) What's new in 1.0, beside it
        if new.count("<!--LOGO-->") != 1:
            raise SystemExit("whats_new.html must contain <!--LOGO--> once; update make_site.py")
        (out / "whats-new.html").write_text(site_css(new.replace("<!--LOGO-->", svg), "whats_new.html"), encoding="utf-8")
        cheat = (HERE / "cheat_sheet.html").read_text(encoding="utf-8")   # (Stallion) the one-page cheat sheet
        if cheat.count("<!--LOGO-->") != 1:
            raise SystemExit("cheat_sheet.html must contain <!--LOGO--> once; update make_site.py")
        cheat = fill_counts(cheat.replace("<!--LOGO-->", svg), counts)
        (out / "cheat-sheet.html").write_text(site_css(cheat, "cheat_sheet.html"), encoding="utf-8")
    else:
        (out / "index.html").write_text(
            '<!doctype html><meta charset="utf-8"><title>Stallion</title>'
            '<meta http-equiv="refresh" content="0; url=webapp/installer/">'
            '<a href="webapp/installer/">Stallion installer</a>\n', encoding="utf-8")
    print(f"site: {out}: webapp/installer ({len(html)} B), webapp/editor, firmware/{name} ({len(raw)} B, {product})")


PRO_MODELS = (("SMK-37 Pro", ""), ("SMK-37 Elite", "-elite"), ("MKE-P37", "-mke"))   # Clipper Pro: model -> file suffix


def main_pro(version, out, pkgs):
    """Clipper Pro's site: one package per SMK-37 model; the installer picks the one for the connected keyboard"""
    out = Path(out)
    vtag = re.sub(r"[^A-Za-z0-9.-]", "-", version)
    entries, names, raws = [], [], {}
    for pkg in map(Path, pkgs):
        raw = pkg.read_bytes()
        product = product_of(raw)
        m = re.fullmatch(r"(SMK-37 Pro|SMK-37 Elite|MKE-P37)_" + OWN_NUM, product)
        if not m:
            raise SystemExit(f"{pkg}: identity {product!r} is not a Stallion SMK-37 package (SMK-37 Pro / SMK-37 Elite / MKE-P37_{STALLION_NUM}XY)")
        if b"FELUCCA-LOADER-1" not in raw:
            raise SystemExit(f"{pkg}: no Felucca update loader in it")
        if m[1] in raws:
            raise SystemExit(f"{pkg}: a second package for {m[1]}")
        raws[m[1]] = (product, raw)
    if "SMK-37 Pro" not in raws:
        raise SystemExit("--pro: the SMK-37 Pro package is needed (it also serves the Starrykey 37 Play)")
    inst, ed, fw = out / "webapp" / "installer", out / "webapp" / "editor", out / "firmware"
    for d in (inst, ed, fw):
        d.mkdir(parents=True, exist_ok=True)
    for old in list(fw.glob("felucca-*.fwsc")) + list(fw.glob("sloop-*.fwsc")) + list(fw.glob("clipper-*.fwsc")):
        old.unlink()                                # the current packages only
    for model, sfx in PRO_MODELS:
        if model not in raws:
            continue
        product, raw = raws[model]
        name = f"clipper-pro{sfx}-{vtag}.fwsc"
        (fw / name).write_bytes(raw)
        names.append(name)
        entries.append({"product": product, "pkg": "../../firmware/" + name, "sha256": hashlib.sha256(raw).hexdigest()})
    html = (HERE / "index_pro.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "smkpkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "smkota.js").read_text(encoding="utf-8"))
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pro.html must contain {mark} once; update make_site.py")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", json.dumps({"version": version, "pkgs": entries}))
    logo = HERE.parent / "assets" / "logo" / "clipper-logo.svg"
    html = html.replace("<!--LOGO-->", logo.read_text(encoding="utf-8") if logo.exists() else "<b>Clipper</b>")
    banner = ""
    if "--beta" in sys.argv:
        notes = HERE / "beta_banner.html"
        banner = notes.read_text(encoding="utf-8") if notes.exists() else "<p><b>BETA</b></p>"
    html = html.replace("<!--BANNER-->", banner)
    (inst / "index.html").write_text(html, encoding="utf-8")
    shutil.copy(HERE / "editor.html", ed / "index.html")
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt"):
        if (HERE / f).exists():
            shutil.copy(HERE / f, ed / f)
    (out / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Clipper Pro</title>'
        '<meta http-equiv="refresh" content="0; url=webapp/installer/">'
        '<a href="webapp/installer/">Clipper Pro installer</a>\n', encoding="utf-8")
    print(f"site: {out}: webapp/installer ({len(html)} B), webapp/editor, firmware/" + ", ".join(
        f"{n} ({e['product']})" for n, e in zip(names, entries)))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--pro":
        args = [a for a in sys.argv[2:] if a != "--beta"]
        if len(args) < 3:
            sys.exit(__doc__)
        main_pro(args[0], args[1], args[2:])
        sys.exit(0)
    args, smk, rest = [], [], [a for a in sys.argv[1:] if a not in ("--studio-guide", "--beta")]
    while rest:
        a = rest.pop(0)
        if a == "--smk37":
            if not rest:
                sys.exit(__doc__)
            smk.append(rest.pop(0))
        else:
            args.append(a)
    if len(args) != 3:
        sys.exit(__doc__)
    main(*args, smk=smk)
