/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (Clipper) */
/* VPS: vector phaseshaping (Clipper 0.5; Felucca discussion #145, after J. Kleimola, V. Lazzarini, J. Timoney and
 * V. Valimaki, "Vector Phaseshaping Synthesis", DAFx-11: the idea, not any code). A sine read through a bent phase:
 * the phase phi (0..1) is mapped by a line through the inflection point (D, V): phi < D goes 0 -> V, phi >= D goes
 * V -> 1, and the output is -cos(2 pi P(phi)). V below 1: a pulse-like, phase-distorted wave (CZ style); V above 1:
 * V cycles squeezed into the first D of the period, a formant whose centre is V x f0 and whose width D sets, as a
 * hard sync or a vowel sweep does. A non-integer V would leave a cut-off cycle at D (a bright spike): the output is
 * the crossfade of floor(V) and floor(V) + 1, so V sweeps smoothly and every cycle is whole. A few multiplies and two
 * sine reads a sample: no table, no filter bank.
 * EDIT 1  D  V  VENV  OSC2        EDIT 2  CUT  RES  DTN  SUB
 *   D     the inflection's phase (2 % .. 98 %); SHAPE (LFO / ENV DEST SHP, MW / AT SHP, the matrix) moves it
 *   V     its height, 0 .. 7.9 cycles; VENV the amp envelope on it (+-4 cycles: a formant sweep with each note)
 *   OSC2  a second VPS oscillator, D mirrored (1 - D), detuned by DTN (0 .. 100 cents): wider, a second formant
 *   SUB   a sine an octave down
 *   CUT RES  a 12 dB low-pass (keyboard ~40 %), after them
 * High notes: V is held under ~8 kHz / f0 (a formant above it would only alias).
 * Voice state: ph[0..2] osc 1, osc 2, the sub; s[0..1] the SVF; s[2] the DC blocker (V < 1 and a short D: a DC). */
typedef struct {
    int32_t d, s1, s2, v;        /* the inflection's phase (Q16), the two slopes (Q8 of turns per turn), its V (Q8) */
} vps_seg_t;

static void vps_seg(vps_seg_t *g, int32_t d16, int32_t v8)   /* 32-bit divides: twice a block per oscillator */
{
    g->d = d16;
    g->v = v8;
    g->s1 = (v8 << 16) / d16;
    g->s2 = (256 - v8) * 65536 / (65536 - d16);
}
/* the bent phase of ph, as a sine phase (P mod 1 in 32 bits) */
static inline uint32_t vps_ph(const vps_seg_t *g, uint32_t ph)
{
    int32_t f = (int32_t)(ph >> 16), p;
    if (f < g->d)
        p = (f * g->s1) >> 8;                        /* (< V x 65536 <= 2^27) */
    else
        p = (g->v << 8) + (((f - g->d) * g->s2) >> 8);   /* (|(f - d) s2| < |1 - V| x 65536) */
    return (uint32_t)p << 16;                        /* (the whole turns fall off: a sine phase) */
}
/* one VPS sample: floor(V) and floor(V) + 1 crossed by V's fraction (fr, Q8) */
static inline int32_t vps_out(const vps_seg_t *a, const vps_seg_t *b, int32_t fr, uint32_t ph)
{
    int32_t y = osc_sine(vps_ph(a, ph) - 0x40000000u);   /* -cos */
    if (fr)
        y += ((osc_sine(vps_ph(b, ph) - 0x40000000u) - y) * fr) >> 8;
    return y;
}

static void vps_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->env && !v->env_out) {
        v->ph[0] = v->ph[2] = 0;
        v->ph[1] = 0x40000000u;                      /* (osc 2 a quarter on: no comb when they line up) */
        v->s[0] = v->s[1] = v->s[2] = 0;
    }
}

static void vps_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *ev = vm_ev(t, m);              /* the EDIT values: the voice's own or the part's */
    vps_seg_t a1, b1, a2, b2;
    int32_t d16 = 1310 + ev[0] * 495 + (m->shape - (64 << 8)) * 2;
    int32_t v8 = ev[1] * 16 + ((ev[2] * m->envq15) >> 11), vmax = 2032, fr, d2;
    int32_t mix2 = ev[3] * 258, sub = ev[7] * 258, ic1 = v->s[0], ic2 = v->s[1], dc = v->s[2];
    int32_t over = m->pitch16 - 83 * 16;
    uint32_t i, ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], inc = m->inc, inc2 = inc, inc_s = inc >> 1;
    tsvf_t flt;
    if (over > 0) {                                  /* V under ~8 kHz / f0: halved an octave up */
        uint32_t oc = (uint32_t)over / 192u, vo = oc >= 11u ? 0u : 2032u >> oc;
        vmax = (int32_t)(vo - vo * ((uint32_t)over % 192u) / 384u);
    }
    v8 = clamp(v8, 0, vmax);
    d16 = clamp(d16, 1310, 64226);
    d2 = 65536 - d16;
    fr = v8 & 255;
    vps_seg(&a1, d16, v8 & ~255);
    vps_seg(&b1, d16, (v8 & ~255) + 256);
    if (mix2) {
        int32_t fq = ev[6] * 30;                   /* cents x ~38 (Q16): 0 .. ~100 cents */
        inc2 = inc + (uint32_t)(((uint64_t)inc * (uint32_t)fq) >> 16);
        vps_seg(&a2, d2, v8 & ~255);
        vps_seg(&b2, d2, (v8 & ~255) + 256);
    }
    tsvf_coef(&flt, (ev[4] << 8) + m->cutoff + ((v->pitch16 - 60 * 16) << 3), ev[5]);
    for (i = 0; i < n; i++) {
        int32_t y = vps_out(&a1, &b1, fr, ph0), x;
        if (mix2)
            y += mulq15(vps_out(&a2, &b2, fr, ph1), mix2);
        if (sub)
            y += mulq15(osc_sine(ph2), sub);
        ph0 += inc;
        ph1 += inc2;
        ph2 += inc_s;
        dc += (y - (dc >> 8));                       /* DC blocker (~27 Hz): dc is 256 x its low-pass */
        x = (y - (dc >> 8)) * 5 >> 3;                /* (three sources: headroom) */
        x = tsvf_lp(&flt, x, &ic1, &ic2);
        if (x > 16000 || x < -16000)
            x = soft_knee(x, 16000);
        out[i] += mulq15(mulq15(x << 1, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = dc;
}

static const preset_t VPS_PRESETS[] = {
    /* {D, V, VENV, OSC2, CUT, RES, DTN, SUB}, {A D S R}, ENV -> FILTER, mono. (Stallion 1.0: what vector phaseshaping
     * is known to do well (Kleimola et al., DAFx-11): V 1 (16) and a short D, the phase-distortion saw and resonance
     * sounds; V below 1 a pulse-like wave; V above 1 a formant (vowels, sync-like sweeps) whose envelope is VENV;
     * ENV DEST SHP sweeps D as a phase-distortion envelope would) */
    {"INIT", {50, 32, 0, 0, 110, 10, 20, 0}, {10, 70, 90, 60}, 0, 0, FX(0, 0, 0, 0)},
    {"VPS LEAD", {40, 40, 0, 0, 112, 10, 0, 0}, {0, 80, 110, 40}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 20)},
    {"SYNC SWEEP", {50, 64, 40, 0, 120, 0, 0, 0}, {0, 90, 100, 40}, 0, 1, FX(0, 0, 0, 0),
     XP(P_LRATE + 1, 40, P_LD_SHP + 1, 30)},
    /* V 0.5 and a short D: the bent-phase saw, a solo voice */
    {"PD SAW LEAD", {10, 8, 0, 0, 115, 10, 0, 0}, {0, 80, 110, 40}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 20)},
    /* a formant near the 3rd harmonic, the LFO moving D: a talking, vowel-like lead */
    {"VOWEL LEAD", {40, 48, 0, 0, 110, 10, 0, 0}, {2, 80, 110, 40}, 0, 1, FX(0, 0, 0, 0),
     XP(P_LRATE + 1, 30, P_LD_SHP + 1, 20)},
    /* V 0: half a period held, a cycle in the other: a square-like pulse */
    {"PULSE LEAD", {64, 0, 0, 0, 110, 10, 0, 0}, {0, 80, 110, 40}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 16)},
    /* the formant thrown up by the envelope and held: a hard-sync-like lead */
    {"SYNC LEAD", {60, 40, 30, 0, 120, 10, 0, 0}, {0, 80, 110, 40}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 16)},
    {"VPS BASS", {20, 24, 30, 0, 80, 30, 0, 90}, {0, 70, 90, 30}, 10, 1, FX(0, 0, 0, 0)},
    /* V from 1 to ~3.5 and back as the note decays: the resonance-sweep bass of phase distortion */
    {"RESO BASS", {20, 16, 40, 0, 90, 20, 0, 60}, {0, 60, 40, 20}, 0, 1, FX(0, 0, 0, 0)},
    /* the saw bent harder at the strike (ENV DEST SHP shortens D), then rounder */
    {"PD BASS", {40, 8, 0, 0, 90, 10, 0, 60}, {0, 70, 80, 20}, 0, 1, FX(0, 0, 0, 0), XP(P_ED_SHP + 1, -40)},
    {"SUB BASS", {64, 16, 0, 0, 80, 0, 0, 90}, {0, 70, 110, 20}, 0, 1, FX(0, 0, 0, 0)},
    {"SYNC BASS", {50, 24, 40, 0, 90, 20, 0, 70}, {0, 60, 70, 20}, 0, 1, FX(0, 0, 0, 0)},
    {"WOBBLE VPS", {30, 48, 0, 0, 90, 30, 0, 80}, {0, 80, 110, 30}, 0, 1, FX(0, 0, 0, 0),
     XP(P_LRATE + 1, 70, P_LD_SHP + 1, 50)},
    {"HOLLOW KEYS", {36, 26, 10, 100, 104, 10, 5, 40}, {0, 80, 70, 50}, 0, 0, FX(0, 0, 0, 0)},
    /* V 1, D short: a bright bark at the strike over a near-sine */
    {"PD EPIANO", {30, 16, 10, 0, 105, 0, 0, 0}, {0, 86, 30, 50}, 0, 0, FX(0, 0, 0, 0)},
    {"DIGI CLAV", {12, 8, 20, 0, 115, 20, 0, 0}, {0, 55, 25, 15}, 0, 0, FX(0, 0, 0, 0)},
    /* V 2 and its mirror: the octave stacks of a drawbar organ */
    {"VPS ORGAN", {64, 32, 0, 60, 115, 0, 0, 40}, {0, 30, 127, 20}, 0, 0, FX(0, 0, 0, 0)},
    {"DIGI BRASS", {60, 40, 40, 50, 105, 15, 20, 30}, {10, 80, 100, 40}, 10, 0, FX(0, 0, 0, 0)},
    /* the bent saw opening with the swell (D from short to long), the second oscillator detuned */
    {"PD BRASS", {16, 8, 0, 40, 100, 10, 10, 0}, {16, 80, 100, 40}, 20, 0, FX(0, 0, 0, 0), XP(P_ED_SHP + 1, -30)},
    {"BRASS STAB", {14, 8, 0, 40, 100, 10, 10, 0}, {0, 60, 50, 20}, 30, 0, FX(0, 0, 0, 0)},
    {"FORMANT PAD", {70, 56, 16, 80, 100, 10, 30, 0}, {70, 100, 120, 90}, 0, 0, FX(0, 0, 0, 0),
     XP(P_LD_SHP + 1, 12)},
    {"VPS CHOIR", {64, 80, 10, 100, 96, 10, 40, 0}, {80, 100, 120, 100}, 0, 0, FX(0, 0, 0, 0),
     XP(P_LRATE + 1, 25, P_LD_SHP + 1, 8)},
    /* two bent saws, mirrored and detuned, the LFO moving D: the phase-distortion string pad */
    {"PD STRINGS", {20, 8, 0, 100, 100, 10, 30, 0}, {60, 100, 115, 90}, 0, 0, FX(0, 0, 0, 0),
     XP(P_LRATE + 1, 30, P_LD_SHP + 1, 10)},
    {"WARM PAD", {40, 8, 0, 100, 90, 10, 20, 40}, {60, 100, 115, 90}, 0, 0, FX(0, 0, 0, 0)},
    /* the formant rising slowly with the envelope */
    {"SWEEP PAD", {30, 24, 30, 100, 100, 10, 30, 0}, {90, 110, 110, 100}, 0, 0, FX(0, 0, 0, 0)},
    {"GLASS PAD", {90, 64, 0, 80, 120, 0, 40, 0}, {60, 100, 110, 90}, 0, 0, FX(0, 0, 0, 0)},
    /* a low formant (V 2), a wide D: the closed "ooh" */
    {"OOH PAD", {80, 32, 0, 100, 90, 10, 20, 0}, {70, 100, 120, 90}, 0, 0, FX(0, 0, 0, 0),
     XP(P_LRATE + 1, 25, P_LD_SHP + 1, 6)},
    {"VOX PLUCK", {80, 72, 30, 60, 110, 20, 10, 0}, {0, 76, 24, 56}, 0, 0, FX(0, 0, 0, 0)},
    /* (Stallion 1.0) two mirrored oscillators, V rising slowly with ENV 2: the
     * phase-distortion resonance pad (its movement in VPS_PATCHES) */
    {"RESO PAD", {30, 16, 20, 100, 100, 10, 30, 0}, {70, 110, 115, 95}, 0, 0, FX(0, 0, 0, 0)},
    {"RESO PLUCK", {30, 16, 50, 0, 110, 10, 0, 0}, {0, 65, 0, 40}, 0, 0, FX(0, 0, 0, 0)},
    {"SYNC PLUCK", {50, 40, 40, 0, 115, 10, 0, 0}, {0, 60, 0, 40}, 0, 0, FX(0, 0, 0, 0)},
    /* (replaces PD HARP) a bent saw and its detuned mirror, ENV 2 bending it harder at the strike: a synth pluck */
    {"PD PLUCK", {30, 8, 0, 60, 104, 10, 12, 30}, {0, 86, 16, 56}, 0, 0, FX(0, 0, 0, 0)},
};

/* (Stallion 1.0) the sounds' movement: matrix cables (core.h preset_patch_t). SHAPE moves D,
 * E2 is V, E4 OSC2, E7 DTN */
static const pcable_t VPC_RESO[] = {     /* RESO PAD: ENV 2 raises V, LFO 2 on D, LFO 3 on V, pressure on the filter */
    CB(1, ENV2, E1 + 1, 10), CB(2, LFO2, SHAPE, 8), CB(3, LFO3, E1 + 1, 6), CB(4, AT, CUT, 12),
};
static const pcable_t VPC_PLUCK[] = {    /* PD PLUCK: ENV 2 bends D at the strike, velocity, the wheel adds OSC2, DTN drift */
    CB(1, ENV2, SHAPE, -20), CB(2, VEL, CUT, 12), CB(3, MW, E1 + 3, 24), CB(4, LFO3, E1 + 6, 4),
};
static const preset_patch_t VPS_PATCHES[] = {
    PPATCH(&VPS_PRESETS[28], VPC_RESO, P_E2ATK + 1, 100, P_E2DEC + 1, 110, P_E2SUS + 1, 60, P_L2RATE + 1, 28),   /* RESO PAD */
    PPATCH(&VPS_PRESETS[31], VPC_PLUCK, P_E2DEC + 1, 50, P_L3RATE + 1, 20),                   /* PD PLUCK */
};

/* (after Clipper 1.2) UNISON (voice.c unison_spread): oscillators 1, 2 (DTN up) and the sub moved by r (Q32)
 * of a turn of the note, each by its share (fixed phases at each note: every voice in phase) */
static void vps_spread(track_t *t, voice_t *v, uint32_t r)
{
    v->ph[0] += r;
    v->ph[1] += r + (uint32_t)(((uint64_t)r * (uint32_t)(t->p[P_E6] * 30)) >> 16);
    v->ph[2] += r >> 1;
}

static const engine_t ENG_VPS = {
    "VPS", {"VECTOR", "TONE"},
    {
        {"D", F_PCT, 0, 127, 50, 0, 0},
        {"V", F_INT, 0, 127, 32, 0, 0},
        {"VENV", F_BIPCT, -64, 63, 0, 0, 0},
        {"OSC2", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 110, 0, 0},
        {"RES", F_PCT, 0, 127, 10, 0, 0},
        {"DTN", F_PCT, 0, 127, 20, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
    },
    VPS_PRESETS, sizeof(VPS_PRESETS) / sizeof(VPS_PRESETS[0]), 1, vps_note_on, vps_render,
    0xFB2C, {P_E0, P_E1, P_E2, P_E4}, 0, 0, 0, 0,
};
