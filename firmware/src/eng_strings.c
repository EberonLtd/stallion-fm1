/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (Clipper) */
/* STRINGS: a string machine in the manner of the 1970s ensembles (Solina, ARP, Elka) (Clipper 0.2).
 * Divide-down registers: per tap one phase at the 16' pitch, the 8' and the 4' its double and quadruple, so the
 * registers stay locked as on the originals (polyBLEP saws, or towards a square with SHP). The ensemble is three taps,
 * each a copy of the registers whose pitch two LFOs move, 120 degrees apart from tap to tap (the slow ~0.6 Hz sweep
 * and the fast ~6 Hz shimmer of the three BBD lines), the depth ENS, the speed RATE. Then a 12 dB low-pass.
 * EDIT 1  16'  8'  4'  ENS        EDIT 2  CUT  RATE  SHP  RES
 * Voice state: ph[0..2] the three taps (16' phase), s[0..1] the SVF. Part state: the ensemble LFOs (strings_block). */
static uint32_t str_lfo[NPART][2];               /* the ensemble's slow and fast LFO, per part (strings_block) */

static void strings_block(track_t *t)
{
    uint32_t part = (uint32_t)(t - trk) % NPART;
    int32_t rate = t->p[P_E5];                       /* x 0.25 .. x 2 */
    uint32_t k = 8192u + (uint32_t)rate * 448u;      /* Q15 */
    str_lfo[part][0] += (uint32_t)(((uint64_t)(58000u * CTL) * k) >> 15);      /* ~0.6 Hz at RATE 50 */
    str_lfo[part][1] += (uint32_t)(((uint64_t)(584000u * CTL) * k) >> 15);     /* ~6 Hz */
}

static void strings_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->env && !v->env_out) {
        v->ph[0] = v->ph[1] = v->ph[2] = 0;          /* the taps start together (as the BBD lines share one input:
                                                      * set apart a third of a turn they would cancel all but every
                                                      * third harmonic); the LFOs pull them apart */
        v->s[0] = v->s[1] = 0;
    }
}

/* a register: a saw, towards a square with SHP (x Q15: 0 saw .. 32767 square) */
static inline int32_t str_reg(uint32_t ph, uint32_t inc, int32_t x)
{
    int32_t a = hy_saw(ph, inc);
    return x ? a + mulq15(((a - hy_saw(ph + 0x80000000u, inc)) >> 1) - a, x) : a;
}

static void strings_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *ev = vm_ev(t, m);              /* the EDIT values: the voice's own or the part's */
    int32_t acc[CTL];                                /* (0.7: on the stack: two cores) */
    uint32_t part = (uint32_t)(t - trk) % NPART, i, k;
    int32_t g16 = ev[0] * 34, g8 = ev[1] * 34, g4 = ev[2] * 34;  /* Q15 / 7.5 (three taps, three
                                                                         * registers: headroom for chords) */
    int32_t ens = ev[3], shp = ev[6] * 258;
    int32_t ic1 = v->s[0], ic2 = v->s[1];
    uint32_t inc16 = m->inc >> 1;
    tsvf_t flt;
    for (i = 0; i < n; i++)
        acc[i] = 0;
    for (k = 0; k < 3u; k++) {
        /* the tap's pitch: the slow and the fast LFO, 120 degrees on; up to ~+-14 cents and ~+-4 cents at ENS 127 */
        int32_t s1 = osc_sine(str_lfo[part][0] + k * 0x55555555u), s2 = osc_sine(str_lfo[part][1] + k * 0x55555555u);
        int32_t dq = ((s1 * 14 + s2 * 4) >> 4) * ens;                   /* cents x 127 x 2048 */
        int32_t fq = dq * 38 / 260096;                                  /* the pitch factor - 1, Q16 (~38 a cent) */
        uint32_t inc = inc16 + (uint32_t)(((int64_t)(int32_t)inc16 * fq) >> 16), ph = v->ph[k];
        for (i = 0; i < n; i++, ph += inc) {
            int32_t s = 0;
            if (g16)
                s += mulq15(str_reg(ph, inc, shp), g16);
            if (g8)
                s += mulq15(str_reg(ph << 1, inc << 1, shp), g8);
            if (g4)
                s += mulq15(str_reg(ph << 2, inc << 2, shp), g4);
            acc[i] += s;
        }
        v->ph[k] = ph;
    }
    tsvf_coef(&flt, (ev[4] << 8) + m->cutoff + ((v->pitch16 - 60 * 16) << 3), ev[7]);   /* (keyboard ~40 %) */
    for (i = 0; i < n; i++) {
        int32_t y = tsvf_lp(&flt, acc[i] >> 1, &ic1, &ic2);
        if (y > 16000 || y < -16000)
            y = soft_knee(y, 16000);
        out[i] += mulq15(mulq15(y << 1, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->s[0] = ic1;
    v->s[1] = ic2;
}

static const preset_t STRINGS_PRESETS[] = {
    /* {16', 8', 4', ENS, CUT, RATE, SHP, RES}, {A D S R}, ENV -> FILTER, mono; no sends (Stallion 1.0 factory rules:
     * dry, centred). The ensemble is the engine's own (its three taps sum into the one voice: mono, centred). The
     * registers are named as the machine's, not as orchestral instruments (Stallion) */
    {"INIT", {40, 127, 70, 100, 92, 50, 0, 10}, {10, 70, 127, 40}, 0, 0, FX(0, 0, 0, 0)},
    {"ENSEMBLE", {90, 110, 90, 127, 96, 55, 10, 10}, {60, 100, 125, 90}, 0, 0, FX(0, 0, 0, 0)},
    {"UPPER STR", {0, 127, 100, 90, 100, 60, 20, 15}, {40, 100, 120, 70}, 0, 0, FX(0, 0, 0, 0)},
    {"MID STR", {0, 127, 0, 100, 90, 50, 10, 10}, {40, 100, 120, 70}, 0, 0, FX(0, 0, 0, 0)},
    {"CHAMBER STR", {0, 127, 60, 70, 94, 50, 10, 10}, {35, 100, 120, 65}, 0, 0, FX(0, 0, 0, 0)},
    {"DEEP STR", {127, 80, 0, 80, 76, 45, 30, 20}, {50, 100, 120, 80}, 0, 0, FX(0, 0, 0, 0)},
    {"ENS BASS", {127, 50, 0, 70, 64, 40, 20, 10}, {20, 100, 127, 40}, 0, 1, FX(0, 0, 0, 0)},
    {"VELVET", {40, 127, 70, 100, 92, 50, 0, 10}, {50, 100, 120, 80}, 0, 0, FX(0, 0, 0, 0)},
    {"STR MACHINE", {60, 127, 40, 110, 84, 40, 0, 25}, {30, 100, 120, 60}, 0, 0, FX(0, 0, 0, 0)},
    {"SPACE ENS", {70, 127, 90, 127, 88, 30, 0, 10}, {90, 100, 127, 110}, 0, 0, FX(0, 0, 0, 0)},
    {"DARK ENS", {127, 100, 20, 100, 64, 35, 0, 15}, {80, 100, 125, 100}, 0, 0, FX(0, 0, 0, 0)},
    {"COLD ENS", {60, 127, 30, 90, 70, 40, 0, 10}, {30, 100, 120, 60}, 0, 0, FX(0, 0, 0, 0)},
    {"GLASS STR", {0, 90, 127, 120, 110, 70, 50, 30}, {70, 100, 120, 95}, 0, 0, FX(0, 0, 0, 0)},
    {"AIRY STR", {60, 127, 90, 110, 96, 55, 10, 15}, {50, 100, 120, 80}, 0, 0, FX(0, 0, 0, 0)},
    {"SLOW STR", {40, 127, 60, 100, 84, 40, 0, 10}, {100, 100, 125, 105}, 0, 0, FX(0, 0, 0, 0)},
    {"SWELL STR", {50, 127, 70, 100, 60, 45, 0, 15}, {110, 100, 127, 90}, 30, 0, FX(0, 0, 0, 0)},
    {"LUSH STR", {80, 127, 100, 127, 92, 45, 0, 10}, {70, 100, 127, 100}, 0, 0, FX(0, 0, 0, 0)},
    {"SOFT STR", {40, 127, 40, 100, 70, 45, 0, 5}, {50, 100, 120, 80}, 0, 0, FX(0, 0, 0, 0)},
    {"BALLAD STR", {50, 127, 60, 110, 80, 40, 0, 10}, {45, 100, 120, 75}, 0, 0, FX(0, 0, 0, 0)},
    {"BRIGHT STR", {0, 127, 127, 100, 112, 60, 0, 20}, {40, 100, 120, 70}, 0, 0, FX(0, 0, 0, 0)},
    {"OCTAVE STR", {100, 0, 127, 110, 96, 50, 0, 10}, {40, 100, 120, 70}, 0, 0, FX(0, 0, 0, 0)},
    {"LOW STR", {127, 60, 0, 90, 70, 40, 10, 10}, {60, 100, 120, 80}, 0, 0, FX(0, 0, 0, 0), XP(P_TRANS + 1, -12)},
    {"FAST ENS", {60, 127, 60, 90, 92, 110, 0, 10}, {40, 100, 120, 70}, 0, 0, FX(0, 0, 0, 0)},
    {"80S STR", {40, 127, 60, 60, 96, 80, 40, 10}, {40, 100, 120, 70}, 0, 0, FX(0, 0, 0, 0)},
    {"PULSE STR", {50, 127, 60, 90, 90, 50, 110, 10}, {40, 100, 120, 70}, 0, 0, FX(0, 0, 0, 0)},
    {"HOLLOW STR", {40, 127, 60, 100, 86, 50, 64, 10}, {40, 100, 120, 70}, 0, 0, FX(0, 0, 0, 0)},
    {"CHOIR STR", {20, 127, 90, 127, 80, 30, 60, 30}, {70, 100, 120, 90}, 0, 0, FX(0, 0, 0, 0)},
    {"RESO STR", {30, 127, 60, 100, 72, 50, 0, 70}, {40, 100, 120, 70}, 20, 0, FX(0, 0, 0, 0)},
    {"TAPE STR", {60, 127, 50, 127, 80, 20, 0, 10}, {60, 100, 120, 80}, 0, 0, FX(0, 0, 0, 0), XP(P_LRATE + 1, 30, P_LD_PIT + 1, 2)},
    {"DISCO STR", {0, 127, 110, 90, 104, 60, 0, 15}, {8, 70, 110, 30}, 0, 0, FX(0, 0, 0, 0)},
    {"STR STAB", {50, 127, 80, 90, 100, 60, 0, 10}, {0, 80, 60, 50}, 20, 0, FX(0, 0, 0, 0)},
    /* (Stallion: no imitations) the ensemble's brass section, swelling (STRINGS_PATCHES) */
    {"ENS BRASS", {50, 127, 40, 60, 60, 50, 50, 20}, {16, 80, 100, 40}, 34, 0, FX(0, 0, 0, 0)},
};

/* (Stallion: no imitations) the replacement's patching (params.c PATCH_TABLES): E7 SHP, E4 ENS */
static const pcable_t STC_BRASS[] = {   /* ENS BRASS */
    CB(1, ENV2, CUT, 16),         /* ENV 2 (a slow swell): the filter opens apart from the level */
    CB(2, VEL, E1AMT, 24), CB(3, DC, E1AMT, -12),    /* the filter envelope: x 0.8 soft .. x 1.2 hard */
    CB(4, AT, CUT, 14),           /* pressure: brighter */
    CB(5, LFO3, E1 + 6, 8),       /* LFO 3: saw towards square and back, slowly */
};
static const preset_patch_t STRINGS_PATCHES[] = {
    PPATCH(&STRINGS_PRESETS[31], STC_BRASS, P_E2ATK + 1, 20, P_E2DEC + 1, 60, P_E2SUS + 1, 20),
};

/* (after Clipper 1.2) UNISON (voice.c unison_spread): the three taps (at the 16' pitch, an octave down) moved by
 * r (Q32) of a turn of the note (they start together at each note: every voice in phase). (Stallion) by r of a turn
 * of the 16' itself: r >> 1 spread the stack's 16' over half its cycle only, partly in phase (louder at the note's
 * start, then beating: tests/unison_test.c PHASES STRINGS) */
static void strings_spread(track_t *t, voice_t *v, uint32_t r)
{
    (void)t;
    v->ph[0] += r;
    v->ph[1] += r;
    v->ph[2] += r;
}

static const engine_t ENG_STRINGS = {
    "STRINGS", {"REG", "TONE"},
    {
        {"16'", F_PCT, 0, 127, 40, 0, 0},
        {"8'", F_PCT, 0, 127, 127, 0, 0},
        {"4'", F_PCT, 0, 127, 70, 0, 0},
        {"ENS", F_PCT, 0, 127, 100, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 92, 0, 0},
        {"RATE", F_PCT, 0, 127, 50, 0, 0},
        {"SHP", F_PCT, 0, 127, 0, 0, 0},
        {"RES", F_PCT, 0, 127, 10, 0, 0},
    },
    STRINGS_PRESETS, sizeof(STRINGS_PRESETS) / sizeof(STRINGS_PRESETS[0]), 1, strings_note_on, strings_render,
    0xB71C, {P_E3, P_E4, P_E1, P_ATK}, 0, 0, 0, strings_block,
};
