#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Eberon Limited (John Gorman)
"""Draw Stallion's parameter icon atlas, assets/icons.png, from primitives (lines, boxes, circles, plotted
functions): every icon is the code below, so the atlas is GPL-3.0 as this file is (Stallion R51: it replaces
the earlier atlas, which was not free). tools/gen_icons.py turns the atlas into build/gen/felucca_icons.h.

  draw_icons.py [OUT.png]          (default: assets/icons.png)
  draw_icons.py --check            exit 1 if assets/icons.png is not what this script draws (tests/run_tests.sh)
  draw_icons.py --preview OUT.png  the atlas at 8x with a grid and the names' order, for review

The cells follow assets/icons.json (cell 12, 16 columns, the names in order; ICON_<NAME> in the firmware).
Ink: 0 background, 1 / 2 / 3 a third / two thirds / full (written as 0 / 85 / 170 / 255 grey). The label's cap
height on the screen is the cell's rows 1..10 (ui_draw.c draw_column: the icon at y 1): the drawings keep to
rows 1..10 and full ink for what must read, dim ink (1) for context (an envelope's other stages, an axis)."""
import json
import math
import sys
from pathlib import Path

def rnd(v):
    """round half up (Python's round() halves to even: lopsided drawings)"""
    return int(math.floor(v + 0.5))


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
F, D, M = 3, 1, 2                                  # full, dim, mid ink


class Cell:
    def __init__(self, n=12):
        self.n = n
        self.px = [[0] * n for _ in range(n)]

    def dot(self, x, y, v=F):
        x, y = rnd(x), rnd(y)
        if 0 <= x < self.n and 0 <= y < self.n:
            self.px[y][x] = max(self.px[y][x], v)

    def line(self, x0, y0, x1, y1, v=F):
        """Bresenham, both ends drawn"""
        x0, y0, x1, y1 = (rnd(c) for c in (x0, y0, x1, y1))
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        e = dx + dy
        while True:
            self.dot(x0, y0, v)
            if x0 == x1 and y0 == y1:
                return
            e2 = 2 * e
            if e2 >= dy:
                e += dy
                x0 += sx
            if e2 <= dx:
                e += dx
                y0 += sy

    def poly(self, pts, v=F):
        for (a, b), (c, d) in zip(pts, pts[1:]):
            self.line(a, b, c, d, v)

    def rect(self, x0, y0, x1, y1, v=F):
        self.poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0)], v)

    def fill(self, x0, y0, x1, y1, v=F):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.dot(x, y, v)

    def circle(self, cx, cy, r, v=F, a0=0, a1=360, filled=False):
        """outline (or disc) of radius r about (cx, cy); a0..a1 degrees, 0 = right, counter-clockwise"""
        if filled:
            for y in range(self.n):
                for x in range(self.n):
                    if (x - cx) ** 2 + (y - cy) ** 2 <= r * r + 0.25:
                        self.dot(x, y, v)
            return
        for y in range(self.n):                    # the pixels within half a pixel of the circle, in a0..a1
            for x in range(self.n):
                if abs(math.hypot(x - cx, y - cy) - r) <= 0.5:
                    a = math.degrees(math.atan2(cy - y, x - cx)) % 360
                    if any(a0 <= a + k <= a1 for k in (-360, 0, 360)):
                        self.dot(x, y, v)

    def plot(self, f, x0=0, x1=11, yc=5.5, amp=4.5, v=F):
        """y = f(t), t 0..1 over columns x0..x1, f in -1..1 (up), joined"""
        pts = [(x, yc - amp * f((x - x0) / (x1 - x0))) for x in range(x0, x1 + 1)]
        self.poly(pts, v)

    def arrow(self, x0, y0, x1, y1, v=F, head=2):
        self.line(x0, y0, x1, y1, v)
        a = math.atan2(y1 - y0, x1 - x0)
        for s in (-1, 1):
            b = a + math.pi + s * math.pi / 4
            self.line(x1, y1, x1 + head * math.cos(b), y1 + head * math.sin(b), v)


def env(c, hi):
    """an ADSR outline, the stage hi (0 attack .. 3 release; 4 all) in full ink, the others dim"""
    pts = [(0, 10), (3, 1), (6, 5), (9, 5), (11, 10)]
    for i in range(4):
        if hi not in (i, 4):
            c.line(*pts[i], *pts[i + 1], D)
    for i in range(4):
        if hi in (i, 4):
            c.line(*pts[i], *pts[i + 1], F)


def note(c, x, y, stem=6, flag=True):
    """a note head (2 px tall, 3 wide) with its foot at (x, y), stem up from its right"""
    c.fill(x, y - 1, x + 2, y)
    c.dot(x + 1, y - 2)
    c.line(x + 2, y - 1, x + 2, y - stem)
    if flag:
        c.line(x + 2, y - stem, x + 4, y - stem + 2)


def pulse_wave(c, duty, v=F, y0=2, y1=9):
    """one cycle: high for duty of it, then low"""
    xe = rnd(duty * 11)
    c.poly([(0, y1), (0, y0), (xe, y0), (xe, y1), (11, y1), (11, y0)], v)


def sin1(t):
    return math.sin(2 * math.pi * t)


def jag(seed, n):
    """a fixed pseudo-random sequence in -1..1 (a small LCG: the same on every machine)"""
    s, out = seed, []
    for _ in range(n):
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        out.append(((s >> 16) % 2001) / 1000 - 1)
    return out


def draw(name):
    c = Cell()
    if name == "generic":                          # a knob: its body and pointer
        c.circle(5.5, 5.5, 4.5)
        c.line(5.5, 5.5, 8, 2.5)
    elif name in ("attack", "decay", "sustain", "release", "env"):
        env(c, ("attack", "decay", "sustain", "release", "env").index(name))
    elif name == "cutoff":                         # a low-pass response
        c.plot(lambda t: 0.6 if t < 0.5 else 0.6 - 3.2 * (t - 0.5) ** 1.3 * 1.6, v=F)
    elif name == "reso":                           # a low-pass with its resonant peak
        c.line(0, 6, 4, 6)
        c.poly([(4, 6), (5, 5), (6, 2), (7, 1), (8, 3), (9, 7), (10, 9), (11, 10)])
    elif name == "rate":                           # two chevrons: speed
        for x in (1, 6):
            c.poly([(x, 1), (x + 4, 5), (x + 4, 6), (x, 10)])
    elif name == "lfo_wave":                       # a slow sine over its dim axis
        c.line(0, 5.5, 11, 5.5, D)
        c.plot(sin1, amp=4)
    elif name == "wave":                           # a waveform: a sine's half then a ramp
        c.plot(lambda t: math.sin(2 * math.pi * t) if t < 0.5 else (t - 0.5) * 4 - 1, amp=4)
    elif name == "pulse":                          # a narrow pulse
        c.poly([(0, 9), (3, 9), (3, 2), (6, 2), (6, 9), (11, 9)])
    elif name == "shape":                          # a triangle beside a round hump: the shape morphs
        c.poly([(0, 9), (3, 2), (6, 9)])
        c.circle(8.5, 9, 3, a0=0, a1=180)
    elif name == "pitch":                          # an eighth note
        note(c, 2, 10, stem=8)
    elif name == "tune":                           # a tuning fork
        c.poly([(3, 1), (3, 6), (4, 7), (7, 7), (8, 6), (8, 1)])
        c.line(5.5, 8, 5.5, 10)
        c.line(5, 8, 5, 10)
    elif name == "detune":                         # two sines a little apart in pitch
        c.plot(sin1, amp=3.5, v=D)
        c.plot(lambda t: math.sin(2 * math.pi * t * 1.35), amp=3.5)
    elif name == "transpose":                      # up and down arrows
        c.arrow(3, 10, 3, 1)
        c.arrow(8, 1, 8, 10)
    elif name == "octave":                         # a low and a high note head, joined
        c.fill(0, 8, 2, 10)
        c.fill(8, 1, 10, 3)
        c.line(3, 8, 7, 4, D)
    elif name == "level":                          # a meter: bars rising
        for i, h in enumerate((3, 5, 7, 9)):
            c.fill(i * 3, 10 - h + 1, i * 3 + 1, 10)
    elif name == "pan":                            # a position between left and right
        c.line(0, 6, 11, 6, D)
        c.line(0, 4, 0, 8)
        c.line(11, 4, 11, 8)
        c.dot(5, 7, D)
        c.dot(6, 7, D)
        c.fill(3, 3, 4, 9)
    elif name == "mute":                           # a speaker crossed out
        c.fill(0, 4, 1, 7)
        c.poly([(2, 4), (5, 1), (5, 10), (2, 7)])
        c.line(7, 3, 11, 8)
        c.line(11, 3, 7, 8)
    elif name == "dist":                           # a sine clipped flat
        c.plot(lambda t: max(-0.55, min(0.55, 1.6 * sin1(t))) / 0.55, amp=4)
    elif name == "drive":                          # a gauge, its needle far right
        c.circle(5.5, 8, 5, a0=0, a1=180)
        c.line(5.5, 8, 9.5, 4.5)
        c.fill(5, 8, 6, 9)
    elif name == "fold":                           # a sine folded back at its peaks
        c.plot(lambda t: (lambda s: s if abs(s) <= 0.6 else math.copysign(1.2 - abs(s), s))(1.0 * sin1(t)) / 0.6,
               amp=4)
    elif name == "bits":                           # a staircase: few bits
        c.poly([(0, 10), (0, 8), (3, 8), (3, 5), (6, 5), (6, 3), (9, 3), (9, 1), (11, 1)])
    elif name == "chorus":                         # three voices overlapping
        for x in (2.5, 5.5, 8.5):
            c.circle(x, 5.5, 2.5)
    elif name == "delay":                          # repeats dying away
        for i, h in enumerate((9, 6, 4, 2)):
            c.line(i * 3 + 1, 10, i * 3 + 1, 10 - h + 1, F if i < 2 else M)
        c.line(0, 10, 11, 10, D)
    elif name == "reverb":                         # a source and its reflections
        c.fill(0, 5, 1, 6)
        for r in (3, 6, 9):
            c.circle(0.5, 5.5, r, a0=-50, a1=50, v=F if r < 9 else M)
    elif name == "feedback":                       # a loop back into itself
        c.circle(5.5, 5.5, 4, a0=60, a1=350)
        c.poly([(7.5, 6), (9.5, 4), (11, 6)])
    elif name == "arp":                            # notes up then down
        for i, y in enumerate((9, 6, 3, 6, 9)):
            c.fill(i * 2 + 1, y, i * 2 + 2, y + 1)
    elif name == "gate":                           # a gate: on, held, off
        c.line(0, 10, 11, 10, D)
        c.poly([(0, 10), (2, 10), (2, 2), (8, 2), (8, 10)])
    elif name == "swing":                          # a pendulum
        c.fill(5, 1, 6, 1)
        c.circle(5.5, 1.5, 8, a0=-135, a1=-45, v=D)
        c.line(5.5, 1.5, 1, 8)
        c.fill(0, 7, 2, 9)
    elif name == "tempo":                          # a metronome
        c.poly([(3, 1), (8, 1), (10, 10), (1, 10), (3, 1)])
        c.line(5.5, 8, 8, 3)
        c.line(2, 8, 9, 8, D)
    elif name == "scale":                          # piano keys
        c.rect(0, 1, 11, 10)
        for x in (4, 7):
            c.line(x, 6, x, 10)
        for x in (3, 6, 9):
            c.fill(x - 1, 1, x, 5)
    elif name == "quantize":                       # notes snapped to a grid
        for x in (1, 5, 9):
            c.line(x, 0, x, 11, D)
        c.fill(1, 8, 3, 9)
        c.fill(5, 5, 7, 6)
        c.fill(9, 2, 11, 3)
    elif name == "glide":                          # one pitch gliding into the next
        c.fill(0, 9, 1, 10)
        c.plot(lambda t: -0.8 + 1.6 * (3 * t * t - 2 * t * t * t), x0=1, x1=10, amp=4.5)
        c.fill(10, 1, 11, 2)
    elif name == "slide":                          # a straight slide up
        c.arrow(1, 10, 10, 1, head=3)
    elif name == "voice":                          # a person: a voice
        c.circle(5.5, 3, 2)
        c.poly([(1, 10), (2, 7), (4, 6), (7, 6), (9, 7), (10, 10)])
    elif name == "mod":                            # a wave acting on a value
        c.plot(sin1, yc=3, amp=2.5)
        c.arrow(5.5, 6, 5.5, 10, head=2)
    elif name == "noise":                          # random
        c.poly([(x, 5.5 - 4.5 * v) for x, v in enumerate(jag(5, 12))])
    elif name == "sub":                            # a wave and its sub an octave down
        c.plot(lambda t: sin1(2 * t), yc=2.5, amp=1.5, v=D)
        c.plot(sin1, yc=7, amp=3)
    elif name == "ratio":                          # a small and a large wheel
        c.circle(2.5, 7.5, 2)
        c.circle(7.5, 5.5, 3.5)
    elif name == "algorithm":                      # operators: two into one
        c.rect(0, 1, 3, 4)
        c.rect(8, 1, 11, 4)
        c.rect(4, 7, 7, 10)
        c.line(2, 5, 4, 7, D)
        c.line(9, 5, 7, 7, D)
    elif name == "sample":                         # a recording: its decaying envelope
        for x, h in zip(range(0, 12, 2), (2, 5, 4, 3, 2, 1)):
            c.line(x, 5.5 - h, x, 5.5 + h)
    elif name == "steps":                          # a row of steps, some set
        for i in range(4):
            if i in (0, 2):
                c.fill(i * 3, 4, i * 3 + 1, 7)
            else:
                c.rect(i * 3, 4, i * 3 + 1, 7, D)
    elif name == "accent":                         # the accent mark
        c.poly([(1, 2), (10, 5), (10, 6), (1, 9)])
        c.poly([(1, 3), (8, 5.5), (1, 8)], M)
    elif name == "time":                           # a clock
        c.circle(5.5, 5.5, 4.5)
        c.line(5.5, 5.5, 5.5, 2)
        c.line(5.5, 5.5, 8, 5.5)
    elif name == "phase":                          # the phase sign: a circle struck through
        c.circle(5.5, 5.5, 3.5)
        c.line(2, 10, 9, 1)
    elif name == "fade":                           # a wedge growing from nothing
        for x in range(12):
            h = x * 4 // 11
            c.line(x, 5.5 - h, x, 5.5 + h, 1 + x * 2 // 11)
    elif name == "mix":                            # two inputs into one
        c.line(0, 2, 6, 5.5)
        c.line(0, 9, 6, 5.5)
        c.line(6, 5.5, 11, 5.5)
        c.fill(5, 5, 6, 6)
    elif name == "keytrack":                       # keys and a rising line over them
        c.rect(0, 7, 11, 10, D)
        for x in (3, 6, 9):
            c.line(x, 7, x, 10, D)
        c.arrow(1, 6, 10, 1, head=2)
    elif name == "size":                           # four corners pushed out
        for sx, sy in ((0, 1), (11, 1), (0, 10), (11, 10)):
            dx, dy = (1 if sx == 0 else -1), (1 if sy == 1 else -1)
            c.line(sx, sy, sx + 3 * dx, sy)
            c.line(sx, sy, sx, sy + 3 * dy)
        c.rect(4, 4, 7, 7, D)
    elif name == "damp":                           # a damped ringing
        c.plot(lambda t: math.exp(-3.5 * t) * math.cos(2 * math.pi * 2.5 * t), amp=4.5)
    elif name == "tone":                           # a tilt about a pivot
        c.line(0, 9, 11, 2)
        c.line(0, 5.5, 11, 5.5, D)
        c.fill(5, 5, 6, 6)
    elif name == "sweep":                          # a rising chirp
        c.plot(lambda t: math.sin(2 * math.pi * (0.5 * t + 2.2 * t * t)), amp=4)
    elif name == "vibrato":                        # a fast small wobble
        c.plot(lambda t: math.sin(2 * math.pi * 3 * t), yc=5.5, amp=2)
    elif name == "loop":                           # a loop with its arrow
        c.poly([(3, 2), (9, 2), (10, 3), (10, 7), (9, 8), (2, 8), (1, 7), (1, 3), (2, 2)])
        c.poly([(6, 0), (8, 2), (6, 4)])
        c.poly([(5, 6), (3, 8), (5, 10)])
    elif name == "hold":                           # a fermata: held
        c.circle(5.5, 7.5, 5, a0=0, a1=180)
        c.fill(5, 6, 6, 7)
        c.line(0, 10, 11, 10, D)
    elif name == "order":                          # a list sorted by length
        for i, w in enumerate((3, 6, 9)):
            c.fill(0, 1 + i * 4, w, 2 + i * 4)
        c.arrow(11, 1, 11, 10, head=2)
    elif name == "prob":                           # a die
        c.rect(0, 0, 11, 11)
        for p in (2, 5, 8):
            c.fill(p, p, p + 1, p + 1)
    elif name == "length":                         # a span, end to end
        c.line(0, 2, 0, 9)
        c.line(11, 2, 11, 9)
        c.arrow(6, 5.5, 2, 5.5)
        c.arrow(5, 5.5, 9, 5.5)
    elif name == "division":                       # the division sign
        c.line(1, 5.5, 10, 5.5)
        c.line(1, 6, 10, 6)
        c.fill(5, 1, 6, 2)
        c.fill(5, 9, 6, 10)
    elif name == "chip":                           # an integrated circuit
        c.fill(3, 1, 8, 10, M)
        c.rect(3, 1, 8, 10)
        for y in (2, 4, 6, 8):
            c.line(0, y, 2, y)
            c.line(9, y, 11, y)
    elif name == "midi":                           # a five-pin round socket
        c.circle(5.5, 5.5, 5)
        c.fill(5, 1, 6, 1)
        for x, y in ((3, 5), (4, 7), (5.5, 8), (7, 7), (8, 5)):
            c.dot(x, y)
    elif name == "save":                           # a disk
        c.poly([(0, 1), (9, 1), (11, 3), (11, 10), (0, 10), (0, 1)])
        c.fill(3, 1, 7, 3, M)
        c.rect(2, 6, 9, 10)
    elif name == "load":                           # into the tray
        c.arrow(5.5, 0, 5.5, 6, head=3)
        c.poly([(0, 6), (0, 10), (11, 10), (11, 6)])
    elif name == "clear":                          # a bin
        c.line(0, 2, 11, 2)
        c.line(4, 1, 7, 1)
        c.poly([(1, 3), (2, 10), (9, 10), (10, 3)])
        for x in (4, 7):
            c.line(x, 4, x, 8, M)
    elif name == "w_sin":
        c.plot(sin1, amp=4.5)
    elif name == "w_tri":
        c.poly([(0, 5.5), (3, 1), (8, 10), (11, 5.5)])
    elif name == "w_saw":
        c.poly([(0, 10), (10, 1), (10, 10), (11, 9)])
    elif name == "w_sqr":
        pulse_wave(c, 0.5, y0=1, y1=10)
    elif name == "w_pls":
        pulse_wave(c, 0.25, y0=1, y1=10)
    elif name == "w_pwm":                          # a pulse whose width moves
        c.line(6, 1, 6, 10, D)
        c.line(8, 1, 8, 10, D)
        pulse_wave(c, 0.36, y0=1, y1=10)
    elif name == "w_sh":                           # sample and hold: random steps
        ys, pts = [rnd(5.5 - 4.5 * v) for v in jag(11, 4)], []
        for i, y in enumerate(ys):
            pts += [(i * 3, y), (min(11, i * 3 + 3), y)]
        c.poly(pts)
    elif name == "w_dsin":                         # a double sine: two humps up, one down
        c.plot(lambda t: abs(math.sin(4 * math.pi * t)) if t < 0.5 else math.sin(2 * math.pi * t), amp=4.5)
    elif name == "w_spls":                         # a sine pulse: a fast cycle, then rest
        c.plot(lambda t: math.sin(2 * math.pi * t / 0.55) if t < 0.55 else 0, amp=4.5)
    elif name in ("w_rsaw", "w_rtri", "w_rtrp"):   # a resonant wave: ringing inside a saw / triangle / trapezoid
        envf = {"w_rsaw": lambda t: 1 - t, "w_rtri": lambda t: 1 - abs(2 * t - 1),
                "w_rtrp": lambda t: min(1, 4 * t, 4 * (1 - t))}[name]
        c.plot(lambda t: 2 * envf(t) - 1, v=D)
        c.plot(lambda t: envf(t) * math.cos(2 * math.pi * 3.5 * t), amp=4.5)
    elif name == "tape":                           # a cassette
        c.rect(0, 1, 11, 10)
        c.circle(3.5, 5, 1.5)
        c.circle(7.5, 5, 1.5)
        c.line(3, 8, 8, 8, M)
    elif name == "drum":                           # a drum and its sticks
        c.rect(1, 4, 10, 10)
        c.poly([(1, 5), (3, 9), (5, 5), (7, 9), (9, 5)], M)
        c.line(2, 0, 5, 3)
        c.line(9, 0, 6, 3)
    elif name == "mouth":                          # lips, parted
        c.poly([(0, 5), (3, 2), (5, 3), (6, 3), (8, 2), (11, 5)])
        c.poly([(0, 5), (3, 9), (8, 9), (11, 5)])
        c.line(2, 5, 9, 5, M)
    elif name == "trio":                           # three oscillators
        c.poly([(2, 8), (5.5, 2.5), (9, 8), (2, 8)], D)
        for x, y in ((2, 8), (5.5, 2.5), (9, 8)):
            c.circle(x, y, 1.5, filled=True)
    elif name == "drawbar":                        # organ drawbars pulled out
        for i, h in enumerate((8, 5, 9, 3)):
            c.fill(i * 3, 1, i * 3 + 1, h)
            c.dot(i * 3, h + 1, M)
            c.dot(i * 3 + 1, h + 1, M)
    elif name == "slice":                          # a recording cut into pieces
        for x, h in enumerate((2, 4, 3, 0, 4, 2, 3, 0, 3, 4, 2, 1)):
            if h:
                c.line(x, 5.5 - h, x, 5.5 + h)
        for x in (3, 7):
            c.line(x, 0, x, 11, M)
    elif name == "grain":                          # scattered grains
        for x, y in ((1, 2), (4, 1), (2, 6), (6, 4), (9, 2), (8, 7), (4, 9), (10, 10), (0, 10), (6, 8)):
            c.fill(x, y, x + 1, y)
    else:
        raise SystemExit(f"draw_icons: no drawing for {name!r} (add one, assets/icons.json names it)")
    return c


def atlas():
    """-> (Pillow image, names)"""
    from PIL import Image
    meta = json.loads((ASSETS / "icons.json").read_text())
    cell, cols, names = int(meta["cell"]), int(meta["cols"]), list(meta["names"])
    rows = (len(names) + cols - 1) // cols
    rows = max(rows, 6)                            # (192 x 72: room for 96)
    img = Image.new("L", (cols * cell, rows * cell), 0)
    for i, n in enumerate(names):
        c = draw(n)
        ox, oy = (i % cols) * cell, (i // cols) * cell
        for y in range(cell):
            for x in range(cell):
                img.putpixel((ox + x, oy + y), c.px[y][x] * 85)
    return img, names


def main(argv):
    img, names = atlas()
    if argv[:1] == ["--check"]:
        from PIL import Image
        have = Image.open(ASSETS / "icons.png").convert("L")
        if have.size != img.size or have.tobytes() != img.tobytes():
            print("draw_icons: assets/icons.png is not what tools/draw_icons.py draws: run it")
            return 1
        print(f"draw_icons: assets/icons.png matches ({len(names)} icons)")
        return 0
    if argv[:1] == ["--preview"]:
        from PIL import Image, ImageDraw
        s = 8
        big = img.resize((img.width * s, img.height * s), Image.NEAREST).convert("RGB")
        d = ImageDraw.Draw(big)
        for x in range(0, big.width, 12 * s):
            d.line([(x, 0), (x, big.height)], fill=(90, 0, 0))
        for y in range(0, big.height, 12 * s):
            d.line([(0, y), (big.width, y)], fill=(90, 0, 0))
        big.save(argv[1])
        print(f"draw_icons: preview {argv[1]}")
        return 0
    out = Path(argv[0]) if argv else ASSETS / "icons.png"
    img.save(out, optimize=True)
    print(f"draw_icons: {len(names)} icons -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
