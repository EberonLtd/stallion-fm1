/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (Clipper) */
/* TERRAIN: wave terrain synthesis, "lite" (Clipper 0.5; Felucca discussion #146). Instead of a waveform read along
 * one axis, an orbit circles over a surface: the surface's height under it is the sound. One turn of the orbit is
 * one period of the note, so it is pitched; where the orbit lies on the surface and how wide it is choose the
 * harmonics, and moving it (X, Y, DRIFT, the matrix) makes the timbre evolve as no single wave can.
 * The surfaces: 8 tables of 32 x 32 (terrain_tables.h, tools/gen_terrain.py), periodic in x and y (a torus: the
 * orbit crosses an edge without a step), each of a few low spatial harmonics so the output stays band-limited; read
 * with bilinear interpolation (4 reads, 3 multiplies). The orbit: an ellipse of radius RAD around (X, Y), squeezed
 * flat by ORB below 64 and turned into a figure eight above it (its second harmonic: brighter, hollow). DRIFT moves
 * its centre round a slow circle (~0.03 .. 1 Hz, each note its own), SHAPE (LFO / ENV DEST SHP, MW / AT SHP, the
 * matrix) widens it. High notes: the radius shrinks an octave at a time above C6 (a wide orbit would only alias).
 * EDIT 1  TERR  RAD  X  Y        EDIT 2  ORB  DRIFT  CUT  RES
 * Voice state: ph[0] the orbit, ph[1] the drift; s[0..1] the SVF; s[2] the DC blocker. */
#include "terrain_tables.h"

/* the surface's height at (x, y): 2^32 = one period of the table (32 cells), bilinear */
static inline int32_t ter_at(const int16_t *T, uint32_t x, uint32_t y)
{
    uint32_t xi = x >> 27, yi = y >> 27, xj = (xi + 1u) & 31u, yj = ((yi + 1u) & 31u) << 5;
    int32_t fx = (int32_t)((x >> 12) & 32767u), fy = (int32_t)((y >> 12) & 32767u);
    int32_t a = T[(yi << 5) + xi], b = T[(yi << 5) + xj], c = T[yj + xi], d = T[yj + xj];
    int32_t ab = a + mulq15(b - a, fx), cd = c + mulq15(d - c, fx);
    return ab + mulq15(cd - ab, fy);
}

static void terrain_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->env && !v->env_out) {
        v->ph[0] = 0;
        v->ph[1] = (uint32_t)v->note * 0x9E3779B9u;  /* (each note's drift from its own place: a chord moves apart) */
        v->s[0] = v->s[1] = v->s[2] = 0;
    }
}

static void terrain_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *ev = vm_ev(t, m);              /* the EDIT values: the voice's own or the part's */
    const int16_t *T = TERRAIN[(uint32_t)ev[0] % TER_COUNT];
    int32_t r = ev[1] * 129 + (m->shape - (64 << 8)), orb = ev[4], ry, a8 = 0;
    int32_t ic1 = v->s[0], ic2 = v->s[1], dc = v->s[2], over = m->pitch16 - 84 * 16;
    uint32_t i, ph = v->ph[0], inc = m->inc, cx = (uint32_t)ev[2] << 25, cy = (uint32_t)ev[3] << 25;
    tsvf_t flt;
    if (ev[5]) {                                   /* DRIFT: the centre round a circle an eighth of the surface wide */
        uint32_t dinc = (uint32_t)ev[5] * (uint32_t)ev[5] * 6u + 2400u;   /* per sample: ~0.03 .. ~1 Hz */
        v->ph[1] += dinc * n;
        cx += (uint32_t)(osc_sine(v->ph[1]) * 16384);
        cy += (uint32_t)(osc_sine(v->ph[1] + 0x40000000u) * 16384);
    }
    r = clamp(r, 0, 16383);                          /* the radius, 1/65536 of the surface: up to a quarter of it */
    if (over > 0) {                                  /* high notes: an octave narrower an octave up */
        uint32_t oc = (uint32_t)over / 192u, ro = oc >= 15u ? 0u : (uint32_t)r >> oc;
        r = (int32_t)(ro - ro * ((uint32_t)over % 192u) / 384u);
    }
    if (orb <= 64) {
        ry = r * orb >> 6;                           /* an ellipse, flat at 0 */
    } else {
        ry = r;
        a8 = (orb - 64) * 4;                         /* towards a figure eight (Q8) */
    }
    tsvf_coef(&flt, (ev[6] << 8) + m->cutoff + ((v->pitch16 - 60 * 16) << 3), ev[7]);
    for (i = 0; i < n; i++, ph += inc) {
        int32_t sy = osc_sine(ph), y, x;
        if (a8)
            sy += ((osc_sine(ph << 1) - sy) * a8) >> 8;
        y = ter_at(T, cx + ((uint32_t)(osc_sine(ph + 0x40000000u) * r) << 1), cy + ((uint32_t)(sy * ry) << 1));
        dc += y - (dc >> 8);                         /* DC blocker (~27 Hz): an orbit off the middle has an offset */
        x = tsvf_lp(&flt, (y - (dc >> 8)) * 3 >> 2, &ic1, &ic2);
        if (x > 16000 || x < -16000)
            x = soft_knee(x, 16000);
        out[i] += mulq15(mulq15(x << 1, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = dc;
}

static const preset_t TERRAIN_PRESETS[] = {
    /* (Stallion 1.0) the sounds wave terrain is known for: slowly evolving pads (the orbit drifting over the
     * surface), round basses and leads from a small orbit, organs from the figure-eight orbit (its octave), brass
     * and plucks from the orbit's radius following the envelope (ENV -> SHP widens it: brighter as it opens, as an
     * FM index), hollow and reedy keys from the lattice and bump surfaces. Dry, centred.
     * {TERR, RAD, X, Y, ORB, DRIFT, CUT, RES}, {A D S R}, ENV -> FILTER, mono */
    {"INIT", {0, 50, 0, 0, 64, 20, 110, 0}, {10, 70, 90, 60}, 0, 0, FX(0, 0, 0, 0)},
    {"ORBIT PAD", {0, 70, 32, 16, 64, 24, 104, 0}, {60, 100, 120, 90}, 0, 0, FX(0, 0, 0, 0)},
    {"RIPPLE PAD", {1, 70, 20, 40, 64, 30, 100, 10}, {80, 100, 120, 100}, 0, 0, FX(0, 0, 0, 0)},
    {"CRYSTAL PAD", {2, 60, 10, 30, 64, 24, 96, 0}, {70, 100, 120, 96}, 0, 0, FX(0, 0, 0, 0)},
    {"FOLD PAD", {5, 80, 20, 40, 90, 40, 96, 10}, {80, 100, 124, 100}, 0, 0, FX(0, 0, 0, 0)},
    {"SADDLE PAD", {4, 60, 30, 20, 64, 30, 96, 0}, {70, 100, 120, 96}, 0, 0, FX(0, 0, 0, 0)},
    {"TWIST PAD", {6, 70, 40, 10, 80, 30, 100, 0}, {70, 100, 120, 96}, 0, 0, FX(0, 0, 0, 0)},
    {"DARK PAD", {0, 90, 32, 0, 40, 20, 70, 0}, {80, 100, 120, 100}, 0, 0, FX(0, 0, 0, 0)},
    {"DRIFT PAD", {7, 60, 16, 16, 64, 50, 90, 0}, {80, 100, 124, 100}, 0, 0, FX(0, 0, 0, 0)},
    {"GLASS PAD", {2, 90, 32, 0, 64, 20, 110, 0}, {70, 100, 120, 96}, 0, 0, FX(0, 0, 0, 0)},
    {"BUMPY BASS", {3, 70, 0, 0, 40, 0, 84, 20}, {0, 70, 100, 30}, 10, 1, FX(0, 0, 0, 0)},
    {"SINE BASS", {0, 30, 32, 0, 64, 0, 90, 0}, {0, 70, 110, 24}, 0, 1, FX(0, 0, 0, 0)},
    {"FOLD BASS", {5, 60, 20, 40, 64, 0, 80, 20}, {0, 74, 90, 24}, 20, 1, FX(0, 0, 0, 0)},
    {"RIPPLE BASS", {1, 60, 0, 0, 40, 0, 84, 10}, {0, 74, 90, 24}, 16, 1, FX(0, 0, 0, 0)},
    {"SADDLE LEAD", {4, 70, 30, 20, 90, 0, 110, 10}, {0, 80, 110, 40}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 20)},
    {"TWIST LEAD", {6, 80, 40, 10, 64, 10, 110, 10}, {2, 90, 110, 40}, 0, 1, FX(0, 0, 0, 0)},
    {"SOFT LEAD", {0, 50, 32, 0, 64, 0, 100, 0}, {4, 80, 120, 36}, 0, 1, FX(0, 0, 0, 0),
     XP(P_LD_PIT + 1, 1, P_LRATE + 1, 88, P_LFADE + 1, 90, P_GLIDE + 1, 16)},
    {"FOLD LEAD", {5, 70, 20, 40, 100, 0, 110, 10}, {0, 80, 110, 36}, 0, 1, FX(0, 0, 0, 0)},
    {"REED LEAD", {3, 80, 20, 30, 64, 0, 104, 0}, {6, 80, 120, 36}, 0, 1, FX(0, 0, 0, 0),
     XP(P_LD_PIT + 1, 1, P_LRATE + 1, 88, P_LFADE + 1, 90)},
    {"CRYSTAL", {2, 60, 10, 30, 64, 10, 120, 0}, {0, 80, 40, 70}, 0, 0, FX(0, 0, 0, 0)},
    {"ORBIT KEYS", {2, 75, 20, 44, 50, 15, 104, 0}, {0, 80, 70, 50}, 0, 0, FX(0, 0, 0, 0)},
    {"RIPPLE KEYS", {1, 50, 30, 20, 64, 0, 110, 0}, {0, 84, 30, 60}, 0, 0, FX(0, 0, 0, 0), XP(P_ED_SHP + 1, 24)},
    {"ORBIT ORGAN", {0, 60, 32, 0, 96, 0, 110, 0}, {0, 64, 127, 20}, 0, 0, FX(0, 0, 0, 0)},
    {"HOLLOW ORGAN", {2, 40, 32, 0, 64, 0, 110, 0}, {0, 64, 127, 20}, 0, 0, FX(0, 0, 0, 0)},
    {"REED ORGAN", {3, 60, 20, 30, 64, 0, 104, 0}, {4, 64, 127, 24}, 0, 0, FX(0, 0, 0, 0)},
    {"TERRA PLUCK", {1, 60, 40, 0, 80, 0, 115, 10}, {0, 74, 20, 56}, 0, 0, FX(0, 0, 0, 0), XP(P_ED_SHP + 1, 30)},
    {"FOLD PLUCK", {5, 70, 20, 40, 80, 0, 110, 0}, {0, 72, 0, 56}, 0, 0, FX(0, 0, 0, 0), XP(P_ED_SHP + 1, 30)},
    {"WAVE PLUCK", {0, 70, 32, 0, 64, 0, 104, 0}, {0, 70, 0, 50}, 0, 0, FX(0, 0, 0, 0), XP(P_ED_SHP + 1, 40)},
    {"ORBIT BRASS", {1, 30, 0, 0, 64, 0, 100, 0}, {10, 84, 100, 40}, 0, 0, FX(0, 0, 0, 0), XP(P_ED_SHP + 1, 40)},
    {"SOFT BRASS", {0, 40, 32, 0, 64, 0, 96, 0}, {20, 90, 110, 50}, 0, 0, FX(0, 0, 0, 0), XP(P_ED_SHP + 1, 30)},
    {"FOLD BRASS", {5, 40, 20, 40, 64, 0, 104, 10}, {6, 80, 100, 40}, 0, 0, FX(0, 0, 0, 0), XP(P_ED_SHP + 1, 40)},
    {"SADDLE KEYS", {4, 50, 30, 20, 64, 0, 108, 0}, {0, 82, 40, 56}, 0, 0, FX(0, 0, 0, 0)},
};

static const engine_t ENG_TERRAIN = {
    "TERRAIN", {"ORBIT", "MOTION"},
    {
        {"TERR", F_ENUM, 0, TER_COUNT - 1, 0, N_TERRAIN, 0},
        {"RAD", F_PCT, 0, 127, 50, 0, 0},
        {"X", F_PCT, 0, 127, 0, 0, 0},
        {"Y", F_PCT, 0, 127, 0, 0, 0},
        {"ORB", F_PCT, 0, 127, 64, 0, 0},
        {"DRIFT", F_PCT, 0, 127, 20, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 110, 0, 0},
        {"RES", F_PCT, 0, 127, 0, 0, 0},
    },
    TERRAIN_PRESETS, sizeof(TERRAIN_PRESETS) / sizeof(TERRAIN_PRESETS[0]), 1, terrain_note_on, terrain_render,
    0x2E8B, {P_E1, P_E2, P_E3, P_E5}, 0, 0, 0, 0,
};
