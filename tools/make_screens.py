#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Eberon Limited (John Gorman)
"""The device screens the site and the guide show, from the host harness (never a photo of a unit, never drawn):

  tools/make_screens.py [--smk-ppm DIR]     (from the repo root; needs a build: build/gen, and the matrix patching work)

  tools/site_shots.c   the real UI on a 240 x 240 framebuffer (tests/ui_pages_test.c's harness): HOME with a chord held,
                       the four new engines' EDIT pages on their own factory sounds, MARE's EDIT 3, the ENV page, a
                       MARE patch on the MATRIX page with six cables
  --smk-ppm DIR        the SMK-37's KEY AT, PAD AT (and AFTERTOUCH) screens, shot-smk-*.ppm, made by site_shots.c built
                       with -DSITE_SHOTS=1 -DSITE_SMK=1 on a host build that has those pages (the tree has them only with
                       BOARD_SMK37, whose input layer the host harness lacks); without it the PNGs already in web/screens
                       are kept

Writes:
  web/screens/NAME.png, NAME@2x.png, NAME@3x.png    each screen at the panel's size, 2 and 3 times (nearest
                                                    neighbour: the pixels stay sharp); web/make_site.py copies them
  assets/screens/stallion-engines.png               sheets in the manner of SLOOP's README: the screens on dark tiles,
  assets/screens/stallion-features.png              each with a title and a line under it in large type (Terminus,
                                                    the device's own font, doubled), for GUIDE.md
Every PNG is reduced to its few colours (palette PNG): a sheet stays well under 250 KB.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import BdfFontFile, Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web" / "screens"
SHEETS = ROOT / "assets" / "screens"
CC = ["cc", "-O2", "-w", "-DCLIPPER_SYNTH=1", "-Ibuild/gen", "-Ifirmware/src", "-Ifirmware/hal", "-Itests"]

FM1 = ["shot-home", "shot-env", "shot-matrix", "shot-palomino-edit", "shot-palomino-edit2", "shot-stampede-edit",
       "shot-stampede-edit2", "shot-rodeo-edit", "shot-rodeo-edit2", "shot-mare-edit", "shot-mare-edit2",
       "shot-mare-edit3", "shot-home-palomino", "shot-home-stampede", "shot-home-rodeo", "shot-home-mare",
       "shot-filter", "shot-filter2", "shot-reverb", "shot-reverb2", "shot-insert", "shot-touch", "shot-touch3",
       "shot-touchcal"]
SMK = ["shot-smk-keyat", "shot-smk-padat"]

ENGINES = [("shot-home-palomino", "PALOMINO", "GOLDEN PAD, a chord held:", "the sound, its engine, the scope"),
           ("shot-home-stampede", "STAMPEDE", "DUSK BRASS: two channels", "on every note"),
           ("shot-home-rodeo", "RODEO", "NEON PAD: two oscillators,", "a filter in six modes"),
           ("shot-home-mare", "MARE", "SPACE PAD: two filters", "apart in stereo"),
           ("shot-palomino-edit", "PALOMINO > EDIT 1", "XMOD BELL: triangle and sine,", "VCO 2 up 19, cross mod 55"),
           ("shot-stampede-edit2", "STAMPEDE > EDIT 2", "BRASS SWELL: high-pass then", "low-pass, and their envelope"),
           ("shot-rodeo-edit2", "RODEO > EDIT 2", "BP QUACK: the filter as", "band-pass, resonance 55"),
           ("shot-mare-edit2", "MARE > EDIT 2", "SPACE PAD: two ladders 40", "apart, one left, one right"),
           ("shot-mare-edit3", "MARE > EDIT 3", "FILTER FM and RING:", "growls and bells")]
MATRIX_TOUCH = [("shot-matrix", "LFO > MATRIX", "MARE BASS, six cables, each", "coloured by its kind of signal"),
                ("shot-filter", "FX > FILTER", "the OTA model at 48 dB,", "FILTER 2 linked to it"),
                ("shot-filter2", "FX > FILTER 2", "band-pass, resonant, split:", "filter 1 left, filter 2 right"),
                ("shot-reverb2", "FX > REVERB 2", "SHIM: an octave-up", "shimmer on any reverb"),
                ("shot-insert", "FX > INSERT", "PITCH: a pitch shifter,", "with feedback"),
                ("shot-touch", "GLO > TOUCH (FM-1)", "velocity and aftertouch", "from the FM-1's own keys"),
                ("shot-touchcal", "TOUCH CAL (FM-1)", "calibrate your keys,", "with a live meter"),
                ("shot-touch3", "GLO > TOUCH 3 (FM-1)", "the lever and the ribbon;", "MREC records them"),
                ("shot-env", "ENV", "the envelope, drawn as you", "turn the four knobs"),
                ("shot-smk-keyat", "SMK-37 PRO > KEY AT", "pressure made from each", "key's hit, hold and swell"),
                ("shot-smk-padat", "SMK-37 PRO > PAD AT", "touch plays, a harder", "squeeze is aftertouch"),
                ("shot-home", "HOME", "the sound, its engine, the", "four knobs and the scope")]


def run(cmd, **kw):
    subprocess.run(cmd, cwd=ROOT, check=True, **kw)


def flat(im):
    """a palette PNG: the screens have a few dozen colours"""
    return im.convert("RGB").quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)


def save(im, name):
    im = im.convert("RGB")
    for k, sfx in ((1, ""), (2, "@2x"), (3, "@3x")):
        flat(im.resize((im.width * k, im.height * k), Image.NEAREST)).save(WEB / f"{name}{sfx}.png", optimize=True)


def font():
    """Terminus 16 (assets/fonts, OFL), the FM-1's own font"""
    with tempfile.TemporaryDirectory() as d:
        with open(ROOT / "assets/fonts/ter-u16n.bdf", "rb") as f:
            BdfFontFile.BdfFontFile(f).save(str(Path(d) / "ter"))
        from PIL import ImageFont
        return ImageFont.load(str(Path(d) / "ter.pil"))


def text2x(draw_on, xy, s, fnt, fill, k=2):
    """text in Terminus, doubled (k times) with sharp pixels"""
    w, h = fnt.getbbox(s)[2] + 2, 18
    t = Image.new("L", (w, h), 0)
    ImageDraw.Draw(t).text((0, 0), s, font=fnt, fill=255)
    t = t.resize((w * k, h * k), Image.NEAREST)
    draw_on.paste(Image.new("RGB", t.size, fill), xy, t)


def sheet(items, out, imgs):
    """3 columns: a screen at 2x on a dark tile, a title in sail white, two lines in oak (SLOOP's sheets, larger)"""
    fnt = font()
    tile, pad, gap, cap = 480, 16, 28, 120
    cols = 3
    rows = (len(items) + cols - 1) // cols
    W = cols * (tile + 2 * pad) + (cols + 1) * gap
    H = rows * (tile + 2 * pad + cap) + (rows + 1) * gap
    bg, card, white, oak = (11, 30, 54), (19, 41, 74), (243, 238, 226), (233, 168, 82)
    S = Image.new("RGB", (W, H), bg)
    d = ImageDraw.Draw(S)
    for i, (name, title, l1, l2) in enumerate(items):
        x = gap + (i % cols) * (tile + 2 * pad + gap)
        y = gap + (i // cols) * (tile + 2 * pad + cap + gap)
        d.rounded_rectangle((x, y, x + tile + 2 * pad, y + tile + 2 * pad), 18, fill=(4, 10, 20))
        S.paste(imgs[name].resize((tile, tile), Image.NEAREST), (x + pad, y + pad))
        text2x(S, (x, y + tile + 2 * pad + 10), title, fnt, white)
        text2x(S, (x, y + tile + 2 * pad + 48), l1, fnt, oak)
        text2x(S, (x, y + tile + 2 * pad + 80), l2, fnt, oak)
    _ = card
    flat(S).save(out, optimize=True)
    print(f"{out.relative_to(ROOT)}  {S.width} x {S.height}  {out.stat().st_size} B")


def main():
    smk_dir = None
    if "--smk-ppm" in sys.argv:
        smk_dir = Path(sys.argv[sys.argv.index("--smk-ppm") + 1])
    if not (ROOT / "build/gen").is_dir():
        raise SystemExit("make_screens.py: build/gen is missing: build the firmware first (sh build.sh)")
    WEB.mkdir(parents=True, exist_ok=True)
    SHEETS.mkdir(parents=True, exist_ok=True)
    imgs = {}
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        r = subprocess.run(CC + ["-o", str(d / "shots"), "tools/site_shots.c", "-lm", "-lpthread"], cwd=ROOT,
                           capture_output=True, text=True)
        if r.returncode:
            raise SystemExit("make_screens.py: tools/site_shots.c does not build here (it needs the matrix patching "
                             "work: the MATRIX page, MS_GATE, MT_TRIG2):\n" + r.stderr[-1500:])
        run([str(d / "shots"), str(d)], stdout=subprocess.DEVNULL)
        for name in FM1:
            p = d / f"{name}.ppm"
            if not p.exists():
                raise SystemExit(f"make_screens.py: the harness wrote no {p.name}")
            imgs[name] = Image.open(p).convert("RGB")
            save(imgs[name], name)
    for name in SMK:
        if smk_dir and (smk_dir / f"{name}.ppm").exists():
            imgs[name] = Image.open(smk_dir / f"{name}.ppm").convert("RGB")
            save(imgs[name], name)
        elif (WEB / f"{name}.png").exists():
            imgs[name] = Image.open(WEB / f"{name}.png").convert("RGB")
        else:
            raise SystemExit(f"make_screens.py: no {name} (give --smk-ppm DIR)")
    sheet(ENGINES, SHEETS / "stallion-engines.png", imgs)
    sheet(MATRIX_TOUCH, SHEETS / "stallion-features.png", imgs)
    for p in sorted(WEB.glob("*.png")):
        print(f"{p.relative_to(ROOT)}  {p.stat().st_size} B")


if __name__ == "__main__":
    shutil.which("cc") or sys.exit("make_screens.py: no C compiler (cc)")
    main()
