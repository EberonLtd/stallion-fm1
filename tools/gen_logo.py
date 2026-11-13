#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Eberon Limited (John Gorman)
"""The Stallion identity, drawn from code (stallion R1; replaces Clipper's logo and this file's Clipper version).

Stallion is named after the Sea Stallion from Glendalough (Havhesten), the longship built in Viking Dublin around
1042 from Wicklow oak. The mark: a longship side on, its stem rising into a horse's head (the Stallion), its stern
post curled, a square sail in sail-white and rust stripes on one mast, shields along the gunwale, two wave lines
under the keel. The wordmark: STALLION in a pixel letterform of Stallion's own (5 x 7 cells; cells that touch only
at a corner are joined by a 45-degree bridge, so the diagonals are cut, not stepped). Every shape below is plain
geometry written here (lines, quadratic and cubic curves, circles): no tracing, no outside artwork. The palette:
deep-sea navy, Wicklow oak (amber), sail white, rust, sea; a dark variant (navy ground) and a light one (sail).

  tools/gen_logo.py OUT.h          firmware boot splash (RLE, 16-colour palette, RGB565)
  tools/gen_logo.py --assets DIR   stallion-{mark,wordmark,logo}[-light].{svg,png}, stallion-splash.png
  tools/gen_logo.py --web DIR      the web app's icon-192.png, icon-512.png, icon-maskable-512.png
  tools/gen_logo.py --preview PNG  a sheet of all of them (for review)
  tools/gen_logo.py --check        exit 1 if assets/logo/ or the web icons are not what this script draws (run_tests.sh)
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

# ---- the palette -------------------------------------------------------------------------------
NAVY, OAK, SAIL, RUST, SEA = (11, 30, 54), (217, 142, 47), (243, 238, 226), (184, 67, 42), (59, 143, 168)
DARK = {"bg": NAVY, "sail": SAIL, "oak": OAK, "rust": RUST, "sea": SEA, "eye": NAVY, "word": SAIL}
LIGHT = {"bg": SAIL, "sail": NAVY, "oak": (196, 122, 30), "rust": RUST, "sea": (45, 120, 144), "eye": SAIL,
         "word": NAVY}
HEX = lambda c: "#%02X%02X%02X" % c


# ---- geometry ----------------------------------------------------------------------------------
def quad(p0, c, p1, n=16):
    return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * c[0] + t * t * p1[0],
             (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * c[1] + t * t * p1[1]) for t in (i / n for i in range(n + 1))]


def cubic(p0, c0, c1, p1, n=20):
    return [((1 - t) ** 3 * p0[0] + 3 * (1 - t) ** 2 * t * c0[0] + 3 * (1 - t) * t * t * c1[0] + t ** 3 * p1[0],
             (1 - t) ** 3 * p0[1] + 3 * (1 - t) ** 2 * t * c0[1] + 3 * (1 - t) * t * t * c1[1] + t ** 3 * p1[1])
            for t in (i / n for i in range(n + 1))]


def lerp(a, b, t):
    return a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t


def along(pts, t):
    """the point a fraction t of the way along a polyline, by length"""
    seg = [math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]
    d, total = 0.0, sum(seg) * t
    for i, s in enumerate(seg):
        if d + s >= total or i == len(seg) - 1:
            return lerp(pts[i], pts[i + 1], 0 if s == 0 else min(1.0, (total - d) / s))
        d += s


# shapes: ("poly", pts, col), ("line", pts, width, col) (round caps and joins), ("dot", (x, y), r, col)
def ship():
    S = []
    # the hull: a crescent of oak, both ends rising
    top = quad((44, 138), (120, 170), (196, 138))
    bot = cubic((200, 135), (192, 182), (62, 188), (40, 136))
    S.append(("poly", top + bot, "oak"))
    # the stem, rising into the horse's head; the stern post, curled in
    S.append(("line", quad((196, 140), (212, 120), (205, 99)), 9, "oak"))
    cx, cy = 54, 106
    spiral = [(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)))
              for a, r in ((160 + k * 10, 12 - k * 0.24) for k in range(30))]
    S.append(("line", quad((44, 140), (30, 126), spiral[0]) + spiral[1:], 9, "oak"))
    # the horse's head, looking forward over the bow: an arched crest with a cut mane, the ear, a long straight face
    # sloping to a rounded muzzle, the round jowl, the throat back down to the stem
    ax, ay = 205, 100
    crest = quad((-6, 4), (-13, -30), (2, -48), 12)
    mane = [(x - (4 if 2 <= i <= 10 and i % 2 else 0), y) for i, (x, y) in enumerate(crest)]
    head = mane + [(0, -60), (9, -50), (31, -28)] + quad((31, -28), (35, -21), (28, -18), 6)[1:] + \
        quad((28, -18), (14, -17), (12, -29), 8)[1:] + quad((12, -29), (6, -16), (6, 3), 6)[1:]
    S.append(("poly", [(ax + x, ay + y) for x, y in head], "oak"))
    S.append(("dot", (ax + 12, ay - 41), 2.6, "eye"))
    # the mast and the stays, the yard
    S.append(("line", [(120, 152), (120, 32)], 4, "sail"))
    S.append(("line", [(120, 34), (196, 128)], 2.2, "sail"))
    S.append(("line", [(120, 34), (50, 126)], 2.2, "sail"))
    S.append(("line", [(68, 45), (172, 45)], 5, "oak"))
    # the sail: square, filled by the wind (the foot bellied, the leeches bowed), five stripes
    tope = quad((74, 48), (120, 52), (166, 48))
    foot = quad((80, 128), (120, 142), (160, 128))
    for k in range(5):
        u0, u1 = k / 5, (k + 1) / 5
        a, b = along(tope, u0), along(tope, u1)
        c, d = along(foot, u1), along(foot, u0)
        bow0, bow1 = (k == 0) * -5, (k == 4) * 5
        left = quad(d, ((a[0] + d[0]) / 2 + bow0, (a[1] + d[1]) / 2), a, 8)
        right = quad(b, ((b[0] + c[0]) / 2 + bow1, (b[1] + c[1]) / 2), c, 8)
        topk = [p for p in tope if a[0] < p[0] < b[0]]
        footk = [p for p in foot[::-1] if d[0] < p[0] < c[0]]
        S.append(("poly", left + topk + right + footk, "rust" if k % 2 else "sail"))
    # the shields along the gunwale, sail and rust in turn, an oak boss each
    for k in range(7):
        x, y = along(top, 0.16 + k * 0.113)
        S.append(("dot", (x, y - 1), 6.2, "sail" if k % 2 == 0 else "rust"))
        S.append(("dot", (x, y - 1), 2.0, "oak"))
    # the sea
    for y, x0, x1, w in ((198, 34, 206, 5), (212, 62, 178, 4)):
        S.append(("line", [(x, y + 3.5 * math.sin((x - x0) / 34 * 2 * math.pi)) for x in range(x0, x1 + 1, 2)], w, "sea"))
    return S


# ---- the wordmark: 5 x 7 cells a letter, a cell between letters --------------------------------
GLYPH = {
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "A": ["..#..", ".#.#.", "#...#", "#...#", "#####", "#...#", "#...#"],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "I": ["#", "#", "#", "#", "#", "#", "#"],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "N": ["#...#", "##..#", "##..#", "#.#.#", "#..##", "#..##", "#...#"],
}


def word_cells(text="STALLION"):
    """the cells (column, row) of the word, and its width in cells"""
    cells, x = set(), 0
    for ch in text:
        g = GLYPH[ch]
        for r, row in enumerate(g):
            for c, v in enumerate(row):
                if v == "#":
                    cells.add((x + c, r))
        x += len(g[0]) + 1
    return cells, x - 1


def wordmark(cell=10.0, ox=0.0, oy=0.0, col="word"):
    cells, _ = word_cells()
    S = []
    for c, r in sorted(cells, key=lambda q: (q[1], q[0])):
        x, y = ox + c * cell, oy + r * cell
        if (c - 1, r) not in cells:                         # a run of cells along the row: one rectangle
            n = 1
            while (c + n, r) in cells:
                n += 1
            S.append(("poly", [(x, y), (x + n * cell, y), (x + n * cell, y + cell), (x, y + cell)], col))
        for dc in (-1, 1):                                  # a neighbour below touching at a corner only: bridged
            if (c + dc, r + 1) in cells and (c + dc, r) not in cells and (c, r + 1) not in cells:
                xe, ym = x + (cell if dc > 0 else 0), y + cell  # the shared corner
                S.append(("poly", [(xe, y), (xe + dc * cell, ym), (xe, ym)], col))      # half of each empty cell
                S.append(("poly", [(xe - dc * cell, ym), (xe, ym), (xe, ym + cell)], col))
    return S


# ---- drawing -----------------------------------------------------------------------------------
def bbox(shapes):
    xs, ys = [], []
    for s in shapes:
        if s[0] == "poly":
            pts, m = s[1], 0
        elif s[0] == "line":
            pts, m = s[1], s[2] / 2
        else:
            pts, m = [s[1]], s[2]
        for x, y in pts:
            xs += [x - m, x + m]
            ys += [y - m, y + m]
    return min(xs), min(ys), max(xs), max(ys)


def fit(shapes, W, H, pad):
    """scale and move the shapes to sit centred in W x H with pad around"""
    x0, y0, x1, y1 = bbox(shapes)
    k = min((W - 2 * pad) / (x1 - x0), (H - 2 * pad) / (y1 - y0))
    ox, oy = (W - (x1 - x0) * k) / 2 - x0 * k, (H - (y1 - y0) * k) / 2 - y0 * k
    return move(shapes, k, ox, oy)


def move(shapes, k, ox, oy):
    T = lambda p: (ox + p[0] * k, oy + p[1] * k)
    out = []
    for s in shapes:
        if s[0] == "poly":
            out.append(("poly", [T(p) for p in s[1]], s[2]))
        elif s[0] == "line":
            out.append(("line", [T(p) for p in s[1]], s[2] * k, s[3]))
        else:
            out.append(("dot", T(s[1]), s[2] * k, s[3]))
    return out


def raster(shapes, W, H, theme, bg=True, ss=4, radius=0):
    im = Image.new("RGBA", (W * ss, H * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if bg:
        d.rounded_rectangle([0, 0, W * ss - 1, H * ss - 1], radius=radius * ss, fill=theme["bg"])
    for s in shapes:
        if s[0] == "poly":
            d.polygon([(x * ss, y * ss) for x, y in s[1]], fill=theme[s[2]])
        elif s[0] == "line":
            pts, w, col = [(x * ss, y * ss) for x, y in s[1]], s[2] * ss, theme[s[3]]
            d.line(pts, fill=col, width=max(1, round(w)), joint="curve")
            for x, y in (pts[0], pts[-1]):
                d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=col)
        else:
            (x, y), r = (s[1][0] * ss, s[1][1] * ss), s[2] * ss
            d.ellipse([x - r, y - r, x + r, y + r], fill=theme[s[3]])
    return im.resize((W, H), Image.LANCZOS)


def svg(shapes, W, H, theme, label, radius=0):
    L = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W:g} {H:g}" width="{W:g}" height="{H:g}" '
         f'role="img" aria-label="{label}">',
         f'<!-- Stallion (EberonLtd), drawn by tools/gen_logo.py; GPL-3.0-only -->',
         f'<rect width="{W:g}" height="{H:g}" rx="{radius:g}" fill="{HEX(theme["bg"])}"/>']
    for s in shapes:
        if s[0] == "poly":
            L.append('<polygon points="' + " ".join(f"{x:.1f},{y:.1f}" for x, y in s[1]) + f'" fill="{HEX(theme[s[2]])}"/>')
        elif s[0] == "line":
            L.append('<polyline points="' + " ".join(f"{x:.1f},{y:.1f}" for x, y in s[1]) +
                     f'" fill="none" stroke="{HEX(theme[s[3]])}" stroke-width="{s[2]:.2f}" stroke-linecap="round" '
                     f'stroke-linejoin="round"/>')
        else:
            L.append(f'<circle cx="{s[1][0]:.1f}" cy="{s[1][1]:.1f}" r="{s[2]:.2f}" fill="{HEX(theme[s[3]])}"/>')
    return "\n".join(L) + "\n</svg>\n"


# ---- the pieces --------------------------------------------------------------------------------
def mark(W=240, H=240, pad=14):
    return fit(ship(), W, H, pad)


def word(W, H, pad):
    return fit(wordmark(), W, H, pad)


def lockup(H=240):
    """mark and wordmark side by side"""
    m = fit(ship(), H, H, 14)
    _, wc = word_cells()
    cell = H * 0.34 / 7
    w = wordmark(cell, H + 4, (H - 7 * cell) / 2)
    return m + w, H + 4 + wc * cell + H * 0.16


# ---- the boot splash (240 x 240 LCD; drawn in bands, splash.c) ----------------------------------
SPLASH_W, SPLASH_H = 240, 184
SPLASH_CELL = 5                                       # the wordmark's cell, in LCD pixels (crisp: whole pixels)
SPLASH_MARK_H = 140


def splash_image():
    sh = fit(ship(), SPLASH_W, SPLASH_MARK_H, 2)
    _, wc = word_cells()
    wx = (SPLASH_W - wc * SPLASH_CELL) // 2
    sh += wordmark(SPLASH_CELL, wx, SPLASH_MARK_H + 8)
    assert wc * SPLASH_CELL <= SPLASH_W - 4 and SPLASH_MARK_H + 8 + 7 * SPLASH_CELL <= SPLASH_H, "the splash must fit"
    return raster(sh, SPLASH_W, SPLASH_H, DARK).convert("RGB")


def rgb565(c):
    r, g, b = c
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def splash_palette():
    """navy, and each colour of the mark at 1/3, 2/3 and full over navy (the anti-aliased edges)"""
    cols = [NAVY]
    for c in (SAIL, OAK, RUST, SEA):
        cols += [tuple(round(n + (v - n) * f) for v, n in zip(c, NAVY)) for f in (1 / 3, 2 / 3, 1.0)]
    while len(cols) < 16:
        cols.append(NAVY)
    return cols


def splash_pixels():
    im, cols = splash_image(), splash_palette()
    src, px, cache = im.tobytes(), [], {}
    for i in range(0, len(src), 3):
        p = (src[i], src[i + 1], src[i + 2])
        if p not in cache:
            cache[p] = min(range(13), key=lambda k: sum((p[j] - cols[k][j]) ** 2 for j in range(3)))
        px.append(cache[p])
    return px, cols


def splash_header(path):
    px, cols = splash_pixels()
    rle = bytearray()                                 # (run - 1) << 4 | index, runs of 1..16
    i = 0
    while i < len(px):
        j = i
        while j < len(px) and px[j] == px[i] and j - i < 16:
            j += 1
        rle.append(((j - i - 1) << 4) | px[i])
        i = j
    # (the names stay SLOOP_*: firmware/src/splash.c reads them; index 0 is the ground, splash.c fills it)
    L = ["/* generated by tools/gen_logo.py: the Stallion boot splash */", "#pragma once", "#include <stdint.h>",
         f"#define SLOOP_SPLASH_W {SPLASH_W}", f"#define SLOOP_SPLASH_H {SPLASH_H}",
         f"#define SLOOP_SPLASH_BG 0x{rgb565(NAVY):04X}",
         "static const uint16_t SLOOP_SPLASH_PAL[16] = {" + ", ".join(f"0x{rgb565(c):04X}" for c in cols) + "};",
         f"static const uint8_t SLOOP_SPLASH_RLE[{len(rle)}] = {{"]
    for k in range(0, len(rle), 24):
        L.append("    " + ", ".join(str(b) for b in rle[k:k + 24]) + ",")
    L.append("};")
    Path(path).write_text("\n".join(L) + "\n")
    print(f"logo: splash {SPLASH_W}x{SPLASH_H}, {len(rle)} B RLE -> {path}")


def splash_preview():
    """what the LCD shows (the quantised splash on the full 240 x 240 screen), without the text lines"""
    px, cols = splash_pixels()
    im = Image.new("RGB", (240, 240), NAVY)
    sp = Image.new("RGB", (SPLASH_W, SPLASH_H))
    sp.putdata([cols[i] for i in px])
    im.paste(sp, ((240 - SPLASH_W) // 2, 8))
    return im


# ---- files -------------------------------------------------------------------------------------
def assets(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    for suf, th in (("", DARK), ("-light", LIGHT)):
        m = mark()
        (out / f"stallion-mark{suf}.svg").write_text(svg(m, 240, 240, th, "Stallion", radius=36))
        raster(mark(512, 512, 30), 512, 512, th, radius=77).save(out / f"stallion-mark{suf}.png")
        _, wc = word_cells()
        W, H = wc * 10 + 40, 110
        (out / f"stallion-wordmark{suf}.svg").write_text(svg(word(W, H, 20), W, H, th, "STALLION"))
        raster(word(W * 2, H * 2, 40), W * 2, H * 2, th).save(out / f"stallion-wordmark{suf}.png")
        sh, W = lockup(240)
        (out / f"stallion-logo{suf}.svg").write_text(svg(sh, W, 240, th, "Stallion", radius=24))
        sh2, W2 = lockup(480)
        raster(sh2, round(W2), 480, th, radius=48).save(out / f"stallion-logo{suf}.png")
    splash_preview().resize((480, 480), Image.NEAREST).save(out / "stallion-splash.png")
    print(f"logo: assets in {out}")


def web(out):
    out = Path(out)
    for n, size in (("icon-192.png", 192), ("icon-512.png", 512)):
        raster(mark(size, size, size * 0.08), size, size, DARK).convert("RGB").save(out / n)
    # maskable: the mark inside the safe zone (the centre circle of 80 %), the ground to the edges
    raster(mark(512, 512, 512 * 0.17), 512, 512, DARK).convert("RGB").save(out / "icon-maskable-512.png")
    print(f"logo: web icons in {out}")


def preview(path):
    """the sheet for review: marks, wordmarks, lockups, the splash on the LCD, the app icons"""
    sheet = Image.new("RGB", (1400, 1180), (128, 128, 128))
    d = ImageDraw.Draw(sheet)
    for i, th in enumerate((DARK, LIGHT)):
        im = raster(mark(300, 300, 18), 300, 300, th, radius=45)
        sheet.paste(im, (40 + i * 330, 40), im)
        _, wc = word_cells()
        W, H = wc * 8 + 40, 96
        sheet.paste(raster(word(W, H, 20), W, H, th), (700, 40 + i * 120))
        sh, Wl = lockup(200)
        im = raster(sh, round(Wl), 200, th, radius=20)
        sheet.paste(im, (40, 380 + i * 230), im)
    sp = splash_preview()
    sheet.paste(sp.resize((480, 480), Image.NEAREST), (880, 300))
    x = 40
    for size, pad in ((192, 0.08), (96, 0.08), (48, 0.08), (32, 0.08)):
        ic = raster(mark(size, size, size * pad), size, size, DARK)
        sheet.paste(ic, (x, 860), ic)
        x += size + 30
    mk = raster(mark(192, 192, 192 * 0.17), 192, 192, DARK)
    cut = Image.new("L", (192, 192), 0)
    ImageDraw.Draw(cut).ellipse([0, 0, 191, 191], fill=255)
    sheet.paste(mk, (x, 860), cut)
    d.text((40, 1100), "Stallion identity (tools/gen_logo.py): marks dark / light, wordmarks, lockups, the boot "
                       "splash on the LCD (2x), app icons 192 / 96 / 48 / 32 px and maskable (circle cut)", fill=(255, 255, 255))
    sheet.save(path)
    print(f"logo: preview {path}")


def check():
    """the committed files are this script's drawing (their origin: R1, R55); PNGs to within 2 levels a channel
    (Pillow's resampling may round differently from one version to the next)"""
    import tempfile
    from PIL import ImageChops
    root = Path(__file__).resolve().parent.parent
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / "logo").mkdir()
        (tmp / "web").mkdir()
        assets(tmp / "logo")
        web(tmp / "web")
        pairs = [(f, root / "assets" / "logo" / f.name) for f in sorted((tmp / "logo").iterdir())] + \
                [(f, root / "web" / f.name) for f in sorted((tmp / "web").iterdir())]
        have = sorted(p.name for p in (root / "assets" / "logo").iterdir())
        if have != sorted(f.name for f in (tmp / "logo").iterdir()):
            bad.append(f"assets/logo holds {have}")
        for new, old in pairs:
            if not old.exists():
                bad.append(f"{old.name} missing")
            elif new.suffix == ".svg":
                if new.read_text() != old.read_text():
                    bad.append(old.name)
            else:
                a, b = Image.open(new).convert("RGBA"), Image.open(old).convert("RGBA")
                if a.size != b.size or max(hi for _, hi in ImageChops.difference(a, b).getextrema()) > 2:
                    bad.append(old.name)
    if bad:
        print("logo: not what tools/gen_logo.py draws (run it with --assets assets/logo and --web web): " + ", ".join(bad))
        return 1
    print(f"logo: assets/logo and the web icons match tools/gen_logo.py ({len(pairs)} files)")
    return 0


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--check":
        sys.exit(check())
    if len(sys.argv) == 3 and sys.argv[1] == "--assets":
        assets(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "--web":
        web(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "--preview":
        preview(sys.argv[2])
    elif len(sys.argv) == 2:
        splash_header(sys.argv[1])
    else:
        sys.exit(__doc__)
