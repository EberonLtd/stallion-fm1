/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (Clipper) */
/* Changes: Copyright (C) 2026 Eberon Limited (John Gorman) */
/* VECTOR (Clipper 0.8): vector synthesis, after the Sequential Prophet VS and the Korg Wavestation's joystick (the
 * idea, not any code or wave of theirs). Four single-cycle waves sit at the corners of a square, A top left, B top
 * right, C bottom left, D bottom right; a point in it (X, Y) mixes them, each by its nearness (bilinear: the centre
 * an equal mix, a corner its wave alone); MOVE sends the point round a path by itself, from each note's start.
 * EDIT 1  A  B  C  D           the corners' waves (16 of Clipper's: vector_tables.h, tools/gen_vector.py)
 * EDIT 2  X  Y  MOVE  CUT
 *   X Y   the point, -64 (A's side) .. 63; SHAPE (LFO DEST SHP, MW / AT SHP, the matrix's SHP) moves X; the matrix
 *         moves X or Y themselves (DEST E5, E6: X, Y)
 *   MOVE  OFF; CIRC (a circle round the point); 8 (a figure eight); SWEEP (from A to the point, over the note: the
 *         VS's mix envelope); LOOP (the square's edge, A B D C, round the point); RND (a smooth random walk). Each slow
 *         and fast (+): 0.25 Hz / 2 Hz, a sweep of 4 s / 0.5 s
 *   CUT   a 12 dB low-pass (keyboard half), its envelope ENV DEST FLT; (0.9) FX > FILTER > MODL another (dsp.c fmod_*)
 * The waves: three band-limited copies each (512 points to the 96th harmonic, 128 to the 24th, 32 to the 6th), the
 * voice reading the copy that stays under ~20 kHz at its pitch.
 * Voice state: ph[0] the oscillator, ph[1] MOVE's phase (from the note's start), ph[2] RND's generator; s[0..3] the
 * filter (the SVF its first two), s[4] RND's point now, s[5] its target (x, y: two int16), s[6..7] the point at the last
 * block's end (Q15; s[6] < 0: none) */
static inline int32_t vec_lo(int32_t w) { return (int32_t)(int16_t)(w & 0xFFFF); }
static inline int32_t vec_hi(int32_t w) { return w >> 16; }
static inline int32_t vec_pk(int32_t x, int32_t y) { return (int32_t)((uint32_t)(uint16_t)x | (uint32_t)y << 16); }
#include "vector_tables.h"
enum { VM_OFF, VM_CIRC, VM_CIRCF, VM_EIGHT, VM_EIGHTF, VM_SWEEP, VM_SWEEPF, VM_LOOP, VM_LOOPF, VM_RND, VM_RNDF, VM_N };
static const char *const N_VMOVE[VM_N] = {"OFF", "CIRC", "CIRC+", "8", "8+", "SWEEP", "SWP+", "LOOP", "LOOP+", "RND",
                                          "RND+"};
static const uint16_t VEC_OFF[3] = {0, 512, 640};
#define VEC_RADIUS 13107                 /* CIRC 8 RND: 0.4 of the square */

static void vector_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[1] = 0;
    v->s[4] = v->s[5] = 0;
    v->s[6] = -1;
    if (!v->ph[2])
        v->ph[2] = 0x2545F491u ^ ((uint32_t)v->note * 2654435761u);
    if (!v->env && !v->env_out)
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;
}

/* the point (Q15, 0..32767 each) at this block's end: X Y, SHAPE, MOVE */
static void vector_point(const track_t *t, voice_t *v, const vmod_t *m, uint32_t n, int32_t *px, int32_t *py)
{
    const int16_t *ev = vm_ev(t, m);
    uint32_t mv = (uint32_t)clamp(ev[6], 0, VM_N - 1), fast = mv && !(mv & 1u);
    uint32_t inc = ((fast ? 6233054u : 779132u) >> CTL_LOG2) * n, ph = v->ph[1];
    int32_t x = (clamp(ev[4], -64, 63) + 64) * 258 + (m->shape - (64 << 8)), y = (clamp(ev[5], -64, 63) + 64) * 258;
    int32_t dx = 0, dy = 0;
    switch (mv) {
    case VM_CIRC: case VM_CIRCF:
        dx = mulq15(osc_sine(ph + 0x40000000u), VEC_RADIUS), dy = mulq15(osc_sine(ph), VEC_RADIUS);
        break;
    case VM_EIGHT: case VM_EIGHTF:
        dx = mulq15(osc_sine(ph), VEC_RADIUS), dy = mulq15(osc_sine(ph << 1), VEC_RADIUS * 3 / 4);
        break;
    case VM_SWEEP: case VM_SWEEPF: {                 /* from A (0, 0) to the point, eased (1 - (1 - q)^2) */
        int32_t q = (int32_t)(ph >> 17), e = 32767 - ((int32_t)(32767 - q) * (32767 - q) >> 15);
        x = mulq15(clamp(x, 0, 32767), e), y = mulq15(clamp(y, 0, 32767), e);
        if (ph + inc < ph)
            inc = 0xFFFFFFFFu - ph;                  /* (held at its end) */
        break;
    }
    case VM_LOOP: case VM_LOOPF: {                   /* the square's edge round the point, A B D C, half-side 0.5 */
        uint32_t side = ph >> 30;
        int32_t f = (int32_t)((ph >> 15) & 0x7FFFu) - 16384;   /* -0.5 .. 0.5 along the side */
        dx = side == 0 ? f : side == 1 ? 16384 : side == 2 ? -f : -16384;
        dy = side == 0 ? -16384 : side == 1 ? f : side == 2 ? 16384 : -f;
        break;
    }
    case VM_RND: case VM_RNDF:
        if (ph + inc < ph || v->s[6] < 0) {          /* a new target each period */
            uint32_t r = v->ph[2] = v->ph[2] * 1664525u + 1013904223u;
            v->s[5] = vec_pk(mulq15((int32_t)(r >> 16) - 32768, VEC_RADIUS), mulq15((int32_t)(r & 0xFFFFu) - 32768, VEC_RADIUS));
        }
        dx = vec_lo(v->s[4]), dy = vec_hi(v->s[4]);
        dx += (vec_lo(v->s[5]) - dx) >> (fast ? 2 : 5);
        dy += (vec_hi(v->s[5]) - dy) >> (fast ? 2 : 5);
        v->s[4] = vec_pk(dx, dy);
        break;
    default:
        break;
    }
    v->ph[1] = ph + inc;
    *px = clamp(x + dx, 0, 32767);
    *py = clamp(y + dy, 0, 32767);
}

static void vector_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *ev = vm_ev(t, m);              /* the EDIT values: the voice's own or the part's */
    uint32_t i, ph = v->ph[0], inc = m->inc, lv = inc < 20270000u ? 0u : inc < 81100000u ? 1u : 2u;
    uint32_t sh = 23u + 2u * lv, mask = (512u >> (2u * lv)) - 1u, fsh = sh - 15u;
    const int16_t *wa = VEC_WAVE[(uint32_t)clamp(ev[0], 0, VEC_NW - 1)] + VEC_OFF[lv];
    const int16_t *wb = VEC_WAVE[(uint32_t)clamp(ev[1], 0, VEC_NW - 1)] + VEC_OFF[lv];
    const int16_t *wc = VEC_WAVE[(uint32_t)clamp(ev[2], 0, VEC_NW - 1)] + VEC_OFF[lv];
    const int16_t *wd = VEC_WAVE[(uint32_t)clamp(ev[3], 0, VEC_NW - 1)] + VEC_OFF[lv];
    int32_t x1, y1, x0, y0, st[8] = {v->s[0], v->s[1], v->s[2], v->s[3], v->fs[0], v->fs[1], v->fs[2], v->fs[3]};
    int32_t model = fm_model(t);
    int32_t a0, b0, c0, d0, a1, b1, c1, d1, da, db, dc, dd;
    fmod_t fm;
    f2pair_t f2;                                     /* (FILTER 2: engines.c f2_pair) */
    int32_t cut;
    __typeof__(&fmod_tick) tick;                     /* (Stallion: in RAM, hal/fm1_hot.h) */
    vector_point(t, v, m, n, &x1, &y1);
    x0 = v->s[6] < 0 ? x1 : v->s[6], y0 = v->s[6] < 0 ? y1 : v->s[7];
    /* the corners' weights at the block's start and end, Q14 (their sum 16384): ramped over the block */
    a0 = ((32767 - x0) * (32767 - y0)) >> 16, b0 = (x0 * (32767 - y0)) >> 16, c0 = ((32767 - x0) * y0) >> 16;
    d0 = 16383 - a0 - b0 - c0;
    a1 = ((32767 - x1) * (32767 - y1)) >> 16, b1 = (x1 * (32767 - y1)) >> 16, c1 = ((32767 - x1) * y1) >> 16;
    d1 = 16383 - a1 - b1 - c1;
    da = (a1 - a0) / (int32_t)n, db = (b1 - b0) / (int32_t)n, dc = (c1 - c0) / (int32_t)n, dd = (d1 - d0) / (int32_t)n;
    cut = (ev[7] << 8) + m->cutoff + ((v->pitch16 - 60 * 16) << 3);
    HOT_FAR(fmod_coef)(&fm, model, cut, model == FM_SVF ? 24 : 64);   /* (Stallion: the filter models run from RAM,
                                                                       * hal/fm1_hot.h) */
    fmod_slope(&fm, fm_slope(t));
    if (HOT_FAR(f2_pair)(&fm, &f2, t, v, m, cut, out))
        model = FM_PAIR;
    tick = HOT_FAR(fmod_tick);
    for (i = 0; i < n; i++, ph += inc) {
        uint32_t k = ph >> sh, k1 = (k + 1u) & mask;
        int32_t fr = (int32_t)((ph >> fsh) & 0x7FFFu), s, y;
        s = (wa[k] + (((wa[k1] - wa[k]) * fr) >> 15)) * a0;
        s += (wb[k] + (((wb[k1] - wb[k]) * fr) >> 15)) * b0;
        s += (wc[k] + (((wc[k1] - wc[k]) * fr) >> 15)) * c0;
        s += (wd[k] + (((wd[k1] - wd[k]) * fr) >> 15)) * d0;
        a0 += da, b0 += db, c0 += dc, d0 += dd;
        y = (s >> 14) * 3 >> 2;
        y = model == FM_SVF ? tsvf_lp(&fm.sv, y, &st[0], &st[1]) : tick(&fm, y << 1, st, st);
        if (y > 16000 || y < -16000)
            y = soft_knee(y, 16000);
        out[i] += mulq15(mulq15(y << 1, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph;
    v->s[0] = st[0], v->s[1] = st[1], v->s[2] = st[2], v->s[3] = st[3];
    v->fs[0] = st[4], v->fs[1] = st[5], v->fs[2] = st[6], v->fs[3] = st[7];
    if (model == FM_PAIR)
        f2_pair_end(&f2, v);
    v->s[6] = x1;
    v->s[7] = y1;
}

/* waves: 0 SINE 1 TRI 2 SAW 3 SQR 4 PLS25 5 PLS10 6 ORGAN 7 VOX A 8 VOX O 9 BELL 10 HOLLW 11 BRASS 12 RESO 13 DIGI
 * 14 STRNG 15 METAL */
static const preset_t VECTOR_PRESETS[] = {
    /* {A, B, C, D, X, Y, MOVE, CUT}, {A D S R}, ENV -> FILTER, mono. (Stallion 1.0: the vector-synthesis classics of
     * the late 1980s: moving pads, bell pads, choirs, joystick brass, the mix envelope's sweep (SWEEP) from one wave
     * to the others) */
    {"INIT", {2, 7, 14, 9, 0, 0, VM_OFF, 100}, {10, 70, 90, 60}, 0, 0, FX(0, 0, 0, 0)},
    {"VECTOR PAD", {2, 7, 14, 9, 0, 0, VM_CIRC, 100}, {70, 100, 115, 95}, 0, 0, FX(0, 0, 0, 0)},
    /* (Stallion 1.0) the vowel at A, the mix envelope taking it slowly into
     * strings, hollow and saw: the evolving vector pad (its movement in VECTOR_PATCHES) */
    {"EVOLVE PAD", {7, 14, 10, 2, 20, 20, VM_SWEEP, 98}, {70, 100, 115, 95}, 0, 0, FX(0, 0, 0, 0)},
    {"VECTOR STR", {14, 2, 7, 10, 10, -10, VM_CIRC, 96}, {80, 100, 120, 100}, 0, 0, FX(0, 0, 0, 0)},
    {"VOX MORPH", {7, 8, 10, 1, 0, -10, VM_LOOP, 104}, {50, 100, 115, 90}, 0, 0, FX(0, 0, 0, 0)},
    {"DIGI CHOIR", {7, 8, 0, 10, 0, 20, VM_SWEEP, 100}, {90, 100, 120, 100}, 0, 0, FX(0, 0, 0, 0)},
    /* (replaces GLASS MORPH, bell and metal waves) sine, vowel, triangle and hollow drifting at random: a soft,
     * breathy pad */
    {"BREATH PAD", {0, 8, 1, 10, 0, 0, VM_RND, 90}, {60, 100, 115, 90}, 0, 0, FX(0, 0, 0, 0)},
    /* hollow, vowel and triangle waves drifting at random: the hollow pad */
    {"HOLLOW PAD", {10, 8, 1, 0, 0, 0, VM_RND, 96}, {60, 100, 115, 90}, 0, 0, FX(0, 0, 0, 0)},
    /* saw, digital, strings and vowel round the square's edge: the moving pad */
    {"MOVING PAD", {2, 13, 14, 8, 0, 0, VM_LOOP, 100}, {60, 100, 115, 95}, 0, 0, FX(0, 0, 0, 0)},
    {"VECTOR SWEEP", {0, 11, 13, 12, 30, 30, VM_SWEEP, 110}, {40, 100, 115, 90}, 0, 0, FX(0, 0, 0, 0)},
    /* saw, square, triangle and strings, low-passed, circling slowly: the analogue-style pad */
    {"WARM PAD", {2, 3, 1, 14, 0, 10, VM_CIRC, 80}, {60, 100, 115, 90}, 0, 0, FX(0, 0, 0, 0)},
    {"HOLLOW KEYS", {10, 1, 6, 4, -20, -20, VM_OFF, 98}, {0, 88, 60, 60}, 20, 0, FX(0, 0, 0, 0)},
    /* the bell's strike swept fast into the sine: a tine piano */
    {"VECTOR EP", {9, 0, 1, 10, 40, -50, VM_SWEEPF, 100}, {0, 88, 40, 60}, 0, 0, FX(0, 0, 0, 0)},
    {"DIGI CLAV", {5, 12, 4, 2, 0, -30, VM_OFF, 110}, {0, 55, 30, 15}, 20, 0, FX(0, 0, 0, 0)},
    {"ORGAN DRIFT", {6, 10, 1, 0, -30, 20, VM_RND, 112}, {6, 60, 127, 40}, 0, 0, FX(0, 0, 0, 0)},
    {"VECTOR BRASS", {11, 2, 3, 12, -40, -20, VM_OFF, 84}, {16, 84, 100, 50}, 30, 0, FX(0, 0, 0, 0)},
    /* brass at A, the mix envelope taking it into saw and square: the joystick brass swell */
    {"BIG BRASS", {11, 2, 3, 12, -40, -40, VM_SWEEP, 90}, {30, 80, 110, 50}, 20, 0, FX(0, 0, 0, 0)},
    {"BRASS STAB", {11, 2, 11, 3, 0, -40, VM_OFF, 90}, {0, 60, 50, 20}, 40, 0, FX(0, 0, 0, 0)},
    {"VECTOR BASS", {2, 3, 4, 0, -50, -50, VM_SWEEPF, 70}, {0, 76, 100, 24}, 30, 1, FX(0, 0, 0, 0)},
    {"DIODE BASS", {2, 3, 4, 0, -50, -50, VM_SWEEPF, 52}, {0, 70, 90, 24}, 40, 1, FX(0, 0, 0, 0),
     XP(P_FMODEL + 1, FM_DIODE, P_VINT + 1, 40)},
    /* saw with a metallic edge at the pick, swept fast to the square: a picked bass */
    {"PICK BASS", {2, 15, 3, 1, -40, -40, VM_SWEEPF, 70}, {0, 70, 60, 20}, 30, 1, FX(0, 0, 0, 0)},
    {"SYNTH BASS", {3, 2, 4, 0, 0, -64, VM_OFF, 60}, {0, 70, 90, 20}, 40, 1, FX(0, 0, 0, 0)},
    {"VECTOR LEAD", {2, 11, 4, 12, -10, -40, VM_CIRCF, 104}, {0, 80, 110, 40}, 10, 1, FX(0, 0, 0, 0),
     XP(P_GLIDE + 1, 16)},
    {"HOWL VECTOR", {2, 11, 3, 12, 20, -20, VM_EIGHTF, 66}, {0, 80, 100, 40}, 30, 1, FX(0, 0, 0, 0),
     XP(P_FMODEL + 1, FM_SK, P_GLIDE + 1, 18, P_VINT + 1, 30)},
    {"DIGI LEAD", {13, 2, 5, 12, -20, -20, VM_OFF, 110}, {0, 80, 110, 40}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 16)},
    {"RESO VECTOR", {12, 2, 5, 11, 0, 0, VM_EIGHTF, 100}, {0, 90, 110, 50}, 10, 0, FX(0, 0, 0, 0)},
    /* (replace DIGI BELLS, WAVE MALLET, CRYSTAL BELL, METAL BELL, VECTOR HARP) saw, hollow, organ and triangle,
     * the touch moving the point: comping keys */
    {"VECTOR KEYS", {2, 10, 6, 1, -20, -10, VM_OFF, 92}, {0, 86, 60, 50}, 24, 0, FX(0, 0, 0, 0)},
    /* saw, narrow pulse, digital and square, the mix envelope's fast sweep under a closing filter: a synth pluck */
    {"VECTOR PLUCK", {2, 4, 13, 3, 0, -20, VM_SWEEPF, 96}, {0, 86, 16, 56}, 30, 0, FX(0, 0, 0, 0)},
    /* brass, strings, saw and hollow circling slowly, the filter swelling: a brass-and-strings pad */
    {"BRASS PAD", {11, 14, 2, 10, -10, -10, VM_CIRC, 86}, {40, 100, 115, 80}, 20, 0, FX(0, 0, 0, 0)},
    /* the two pulses, square and saw circling: a PWM-like pad */
    {"PULSE PAD", {4, 5, 3, 2, 0, 0, VM_CIRC, 90}, {50, 100, 115, 90}, 0, 0, FX(0, 0, 0, 0)},
    /* triangle and sine, the wheel bringing in hollow and vowel: a soft solo voice */
    {"SOFT LEAD", {1, 0, 10, 8, -20, -20, VM_OFF, 96}, {4, 80, 115, 40}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 16)},
    {"WAVE LOOP", {3, 13, 12, 5, 0, 0, VM_LOOPF, 108}, {10, 90, 110, 60}, 0, 0, FX(0, 0, 0, 0)},
};

/* (Stallion 1.0) the sounds' movement: matrix cables (core.h preset_patch_t). SHAPE moves X,
 * E5 X and E6 Y themselves, CUT the filter */
static const pcable_t VCC_EVOLVE[] = {   /* EVOLVE PAD: LFO 2 on Y, LFO 3 on X, ENV 2 and pressure on the filter */
    CB(1, LFO2, E1 + 5, 10), CB(2, LFO3, SHAPE, 12), CB(3, AT, CUT, 12), CB(4, ENV2, CUT, 10),
};
static const pcable_t VCC_BREATH[] = {   /* BREATH PAD: a slow filter sway, S&H nudging X, the wheel on Y, pressure */
    CB(1, LFO2, CUT, 8), CB(2, SH, SHAPE, 6), CB(3, MW, E1 + 5, 24), CB(4, AT, CUT, 10),
};
static const pcable_t VCC_KEYS[] = {     /* VECTOR KEYS: velocity on the filter and X, ENV 2 moving Y, a slow X drift */
    CB(1, VEL, CUT, 14), CB(2, VEL, SHAPE, -12), CB(3, ENV2, E1 + 5, 16), CB(4, LFO3, SHAPE, 6),
};
static const pcable_t VCC_PLUCK[] = {    /* VECTOR PLUCK: velocity, ENV 2 on X, each note's own Y, BRITE */
    CB(1, VEL, CUT, 16), CB(2, ENV2, SHAPE, 20), CB(3, RAND, E1 + 5, 8), CB(4, MAC1, CUT, 24),
};
static const pcable_t VCC_BRASS[] = {    /* BRASS PAD: ENV 2's swell, pressure, LFO 2 on X, velocity on the envelope */
    CB(1, ENV2, CUT, 14), CB(2, AT, CUT, 12), CB(3, LFO2, E1 + 4, 10), CB(4, VEL, E1AMT, 16),
};
static const pcable_t VCC_PULSE[] = {    /* PULSE PAD: LFO 2 on X (its rate drifting with LFO 3), LFO 3 on Y, wheel, AT */
    CB(1, LFO2, SHAPE, 12), CB(2, LFO3, E1 + 5, 10), CB(3, LFO3, L2RATE, 10), CB(4, MW, SHAPE, 20), CB(5, AT, CUT, 10),
};
static const pcable_t VCC_LEAD[] = {     /* SOFT LEAD: vibrato, pressure opens it, the wheel to Y, a slow X drift */
    CB(1, LFO2, PITCH, 4), CB(2, AT, CUT, 14), CB(3, MW, E1 + 5, 30), CB(4, LFO3, SHAPE, 6),
};
static const preset_patch_t VECTOR_PATCHES[] = {
    PPATCH(&VECTOR_PRESETS[2], VCC_EVOLVE, P_L2RATE + 1, 22, P_L3RATE + 1, 10, P_E2ATK + 1, 100, P_E2SUS + 1, 60),   /* EVOLVE PAD */
    PPATCH(&VECTOR_PRESETS[6], VCC_BREATH, P_L2RATE + 1, 34),                                 /* BREATH PAD */
    PPATCH(&VECTOR_PRESETS[26], VCC_KEYS, P_L3RATE + 1, 16),                                  /* VECTOR KEYS */
    PPATCH(&VECTOR_PRESETS[27], VCC_PLUCK, P_E2DEC + 1, 50),                                  /* VECTOR PLUCK */
    PPATCH(&VECTOR_PRESETS[28], VCC_BRASS, P_E2ATK + 1, 60, P_E2DEC + 1, 100, P_E2SUS + 1, 50, P_L2RATE + 1, 20),   /* BRASS PAD */
    PPATCH(&VECTOR_PRESETS[29], VCC_PULSE, P_L2RATE + 1, 40, P_L3RATE + 1, 16),               /* PULSE PAD */
    PPATCH(&VECTOR_PRESETS[30], VCC_LEAD, P_L2RATE + 1, 84, P_L3RATE + 1, 14),                /* SOFT LEAD */
};

static const engine_t ENG_VECTOR = {
    "VECTOR", {"WAVES", "JOYSTICK"},
    {
        {"A", F_ENUM, 0, VEC_NW - 1, 2, N_VEC_WAVE, 0},
        {"B", F_ENUM, 0, VEC_NW - 1, 7, N_VEC_WAVE, 0},
        {"C", F_ENUM, 0, VEC_NW - 1, 14, N_VEC_WAVE, 0},
        {"D", F_ENUM, 0, VEC_NW - 1, 9, N_VEC_WAVE, 0},
        {"X", F_BIPCT, -64, 63, 0, 0, 0},
        {"Y", F_BIPCT, -64, 63, 0, 0, 0},
        {"MOVE", F_ENUM, 0, VM_N - 1, VM_CIRC, N_VMOVE, 0},
        {"CUT", F_CUTOFF, 0, 127, 100, 0, 0},
    },
    VECTOR_PRESETS, sizeof(VECTOR_PRESETS) / sizeof(VECTOR_PRESETS[0]), 1, vector_note_on, vector_render,
    0x6B3F, {P_E4, P_E5, P_E7, P_E6}, 0, 0, 0, 0,
};
