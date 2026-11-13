/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Phase distortion. The waveforms are a C port of the oscillator of
 * CrispyZebra (Leo Kuroshita, GPL-3.0, github.com/hugelton/CrispyZebra), the
 * author's own phase-distortion core: a -cos table read through a bent phase whose
 * bend is DCW. WAVE2 alternates with WAVE every other cycle. A second
 * line (DTN) can be mixed or ring-modulated; SUB adds a sine an octave down.
 * Envelopes are Felucca's own (ADSR, ENV -> DCW). */
static const char *const N_PD_WAVE[] = {"SAW", "SQR", "PLS", "DSIN", "SPLS", "RSAW", "RTRI", "RTRP"};
static const char *const N_PD_WAVE2[] = {"-", "SAW", "SQR", "PLS", "DSIN", "SPLS", "RSAW", "RTRI", "RTRP"};
static const char *const N_PD_LINE[] = {"MIX", "RING"};

static inline int32_t pd_cos(uint32_t ph16)             /* -cos, Q15, from the 16-bit PD phase */
{
    return -sine_i(((ph16 & 0xFFFFu) << 16) + 0x40000000u);
}

/* one wave's bend for a block: break points and the slopes of the bent phase
 * (Q16 reciprocals), so a sample costs a multiply instead of a divide (the
 * bent phase comes out the same or 1 lower in 65536) */
typedef struct {
    uint32_t w, x, peak, rez;
    uint32_t k0, k1;
} pd_t;

static inline uint32_t pd_slope(uint32_t span, uint32_t len) { return (span << 16) / (len ? len : 1u); }

static void pd_setup(pd_t *b, uint32_t w, uint32_t dcw)
{
    uint32_t x;
    b->w = w;
    switch (w) {
    case 0:                                                  /* SAW */
        x = 32768u + ((dcw * 32767u) >> 16);
        b->k0 = pd_slope(32768u, x);
        b->k1 = pd_slope(32767u, 65535u - x);
        break;
    case 1:                                                  /* SQUARE */
        x = 32768u - ((dcw * 31120u) >> 16);
        b->k0 = pd_slope(32768u, x);
        b->k1 = pd_slope(32767u, x);
        break;
    case 2:                                                  /* PULSE */
        x = 65535u - ((dcw * 63487u) >> 16);
        b->k0 = pd_slope(65535u, x);
        break;
    case 3:                                                  /* DOUBLE SINE */
        x = 65535u - ((dcw * 49151u) >> 16);
        b->k0 = pd_slope(65535u, x);
        b->k1 = pd_slope(65535u, 65535u - x);
        break;
    case 4:                                                  /* SAW-PULSE */
        x = 65535u - ((dcw * 32767u) >> 16);
        b->peak = (x * (32768u + ((dcw * 29491u) >> 16))) >> 16;
        if (!b->peak)
            b->peak = 1;
        b->k0 = pd_slope(32768u, b->peak);
        b->k1 = pd_slope(32767u, x - b->peak);
        break;
    default:                                                 /* RESONANCE */
        x = 0;
        b->rez = (dcw * 44000u) >> 16;
        break;
    }
    b->x = x;
}

/* one sample at phase ph (16 bit): bipolar Q15 */
static int32_t pd_wave(const pd_t *b, uint32_t ph)
{
    uint32_t pd, x = b->x;
    switch (b->w) {
    case 0:                                                  /* SAW */
        pd = ph < x ? (ph * b->k0) >> 16 : 32768u + (((ph - x) * b->k1) >> 16);
        break;
    case 1:                                                  /* SQUARE */
        if (ph < x)
            pd = (ph * b->k0) >> 16;
        else if (ph < 32768u)
            pd = 32768u;
        else if (ph < 32768u + x)
            pd = 32768u + (((ph - 32768u) * b->k1) >> 16);
        else
            pd = 65535u;
        break;
    case 2:                                                  /* PULSE */
        pd = ph < x ? (ph * b->k0) >> 16 : 65535u;
        break;
    case 3:                                                  /* DOUBLE SINE */
        pd = ph < x ? (ph * b->k0) >> 16 : ((ph - x) * b->k1) >> 16;
        break;
    case 4:                                                  /* SAW-PULSE */
        if (ph >= x)
            pd = 65535u;
        else if (ph < b->peak)
            pd = (ph * b->k0) >> 16;
        else
            pd = 32768u + (((ph - b->peak) * b->k1) >> 16);
        break;
    default: {                                               /* RESONANCE: windowed sine, rez = pitch of the core */
        uint32_t rp = (ph + ((ph * b->rez) >> 16) * 7u) & 0xFFFFu, win;
        int32_t core = (pd_cos(rp) * 5) >> 3;
        if (b->w == 5)
            win = 65535u - ph;                               /* saw window */
        else if (b->w == 6)
            win = ph < 32768u ? ph << 1 : (65535u - ph) << 1; /* triangle window */
        else
            win = ph < 16384u ? ph << 2 : ph > 49152u ? (65535u - ph) << 2 : 65535u;   /* trapezoid */
        if (win > 65535u)
            win = 65535u;
        return (core * (int32_t)win) >> 16;                  /* |core| <= 20479: fits 32 bits */
    }
    }
    return pd_cos(pd);
}

static void phase_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[1] = 0;
    v->ph[2] = 0;
    v->s[0] = 0;                 /* WAVE / WAVE2 toggle, line 1 */
    v->s[1] = 0;                 /* line 2 */
}

static void phase_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *ev = vm_ev(t, m);              /* the EDIT values: the voice's own or the part's */
    uint32_t w1 = (uint32_t)ev[0] & 7u, w2 = (uint32_t)ev[1], i;
    int32_t depth = (ev[2] << 8) + m->cutoff + mulq15(m->envq15, ev[3] * 256);
    uint32_t dcw, inc = m->inc, inc2;
    int32_t det = ev[4], line2 = det != 0 || ev[5], ring = ev[5], sub = ev[6] * 200;
    int32_t d16 = det * 16 / 100, rem = det * 16 - d16 * 100;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t tg0 = v->s[0], tg1 = v->s[1];                    /* WAVE / WAVE2 toggles */
    pd_t b1, b2;
    depth = clamp(depth + (m->shape - (64 << 8)), 0, 127 << 8);
    dcw = (uint32_t)depth * 65535u / (127u << 8);
    dcw = (dcw * 56000u) >> 16;                              /* the classic range of the bend */
    inc2 = pitch_inc(clamp(m->pitch16 + d16, 0, 2047));
    inc2 += (uint32_t)((int32_t)(inc2 >> 12) * (rem * 2367 / 16000));
    pd_setup(&b1, w1, dcw);
    pd_setup(&b2, w2 ? w2 - 1u : w1, dcw);                   /* WAVE2 (every other cycle) */
    for (i = 0; i < n; i++) {
        uint32_t old = ph0;
        int32_t s;
        s = pd_wave(tg0 ? &b2 : &b1, ph0 >> 16);
        ph0 += inc;
        if (ph0 < old)
            tg0 ^= 1;
        if (line2) {
            uint32_t old2 = ph1;
            int32_t s2 = pd_wave(tg1 ? &b2 : &b1, ph1 >> 16);
            ph1 += inc2;
            if (ph1 < old2)
                tg1 ^= 1;
            s = ring ? mulq15(s, s2) : (s + s2) >> 1;     /* both within +-32767 */
        }
        if (sub) {
            ph2 += inc >> 1;
            s += mulq15(osc_sine(ph2), sub);
        }
        out[i] += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = tg0;
    v->s[1] = tg1;
}

static const preset_t PHASE_PRESETS[] = {
    /* name, {WAVE, WAVE2, DCW, ENV, DTN, LINE, SUB, -}, {A D S R}, fenv, mono; every send 0 (the factory sounds are dry,
     * centred). The classic sounds of the phase-distortion family: its factory sheets
     * (brass and string ensembles, organs, piano, clav, basses) and its resonant waves */
    {"INIT", {0, 0, 40, 0, 0, 0, 0, 0}, {0, 64, 127, 30}, 0, 0, FX(0, 0, 0, 0)},
    {"BRASS ENS", {0, 0, 20, 70, 8, 0, 0, 0}, {50, 72, 100, 45}, 0, 0, FX(0, 0, 0, 0)},
    {"STRING ENS", {0, 0, 30, 30, 12, 0, 0, 0}, {70, 90, 115, 75}, 0, 0, FX(0, 0, 0, 0)},
    {"SYNTH BASS", {0, 0, 25, 70, 0, 0, 50, 0}, {0, 66, 60, 24}, 0, 1, FX(0, 0, 0, 0)},
    {"ELEC PIANO", {3, 0, 30, 70, 4, 0, 0, 0}, {0, 86, 20, 60}, 0, 0, FX(0, 0, 0, 0)},
    {"ELEC ORGAN", {1, 0, 35, 0, 0, 0, 40, 0}, {0, 64, 127, 24}, 0, 0, FX(0, 0, 0, 0)},
    {"SAW LEAD", {0, 0, 70, 40, 6, 0, 0, 0}, {2, 70, 110, 30}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 20, P_MWDST + 1, MD_VIB)},
    {"RESO BASS", {5, 0, 30, 90, 0, 0, 64, 0}, {0, 64, 50, 22}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 24)},
#if CLIPPER_SYNTH
    /* (Stallion: no imitations, no mallets) the replacements move through PHASE_PATCHES: ENV 2 sweeping the DCW apart
     * from the level, as the original's own DCW envelope did */
    {"SWEEP LEAD", {0, 0, 40, 30, 6, 0, 0, 0}, {2, 70, 112, 30}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 20, P_MWDST + 1, MD_VIB)},
    {"SOFT BRASS", {0, 2, 15, 60, 6, 0, 0, 0}, {55, 80, 105, 50}, 0, 0, FX(0, 0, 0, 0)},
    {"SYNTH STRNG", {0, 3, 45, 0, 10, 0, 0, 0}, {45, 90, 120, 70}, 0, 0, FX(0, 0, 0, 0)},
    {"MOTION PAD", {3, 1, 30, 20, 10, 0, 0, 0}, {70, 90, 118, 84}, 0, 0, FX(0, 0, 0, 0)},
    {"SOFT LEAD", {3, 0, 14, 20, 4, 0, 0, 0}, {16, 70, 118, 30}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 14, P_MWDST + 1, MD_VIB)},
    {"FAT BASS", {0, 0, 35, 60, 7, 0, 60, 0}, {0, 68, 70, 24}, 0, 1, FX(0, 0, 0, 0)},
    {"FUNKY CLAV", {2, 0, 35, 90, 0, 0, 0, 0}, {0, 64, 20, 16}, 0, 0, FX(0, 0, 0, 0)},
    {"PD KEYS", {3, 1, 36, 60, 4, 0, 0, 0}, {0, 88, 30, 56}, 0, 0, FX(0, 0, 0, 0)},
    {"PULSE PLUCK", {2, 0, 24, 80, 0, 0, 20, 0}, {0, 90, 20, 56}, 0, 0, FX(0, 0, 0, 0)},
    {"PHASE STRING", {1, 1, 36, 10, 14, 0, 0, 0}, {60, 90, 118, 84}, 0, 0, FX(0, 0, 0, 0)},
    {"MOTION BASS", {0, 0, 20, 50, 0, 0, 50, 0}, {0, 66, 80, 22}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 16)},
    {"HARPSICHORD", {4, 0, 60, 40, 4, 0, 0, 0}, {0, 82, 0, 44}, 0, 0, FX(0, 0, 0, 0)},
    {"PIPE ORGAN", {3, 0, 20, 0, 3, 0, 60, 0}, {30, 64, 127, 60}, 0, 0, FX(0, 0, 0, 0)},
    {"SOFT ORGAN", {3, 0, 10, 0, 0, 0, 40, 0}, {6, 64, 127, 36}, 0, 0, FX(0, 0, 0, 0)},
    {"RESO LEAD", {6, 0, 55, 50, 0, 0, 0, 0}, {2, 72, 100, 30}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 24, P_MWDST + 1, MD_VIB)},
    {"RESO PAD", {7, 0, 70, 20, 6, 0, 0, 0}, {40, 100, 110, 70}, 0, 0, FX(0, 0, 0, 0),
     XP(P_LRATE + 1, 30, P_LD_SHP + 1, 30)},
    {"SYNTH CHOIR", {3, 5, 40, 30, 10, 0, 0, 0}, {60, 90, 115, 85}, 0, 0, FX(0, 0, 0, 0)},
    {"PULSE PAD", {1, 5, 45, 30, 14, 0, 0, 0}, {60, 90, 115, 85}, 0, 0, FX(0, 0, 0, 0)},
    {"SOFT KEYS", {3, 0, 10, 40, 0, 0, 0, 0}, {0, 80, 30, 50}, 0, 0, FX(0, 0, 0, 0)},
    {"SAW PLUCK", {0, 0, 30, 90, 0, 0, 0, 0}, {0, 70, 0, 50}, 0, 0, FX(0, 0, 0, 0)},
    {"RESO PLUCK", {5, 0, 40, 70, 0, 0, 40, 0}, {0, 76, 0, 55}, 0, 0, FX(0, 0, 0, 0)},
    {"SQUARE LEAD", {1, 0, 55, 30, 0, 0, 0, 0}, {2, 70, 115, 30}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 18, P_MWDST + 1, MD_VIB)},
    {"PICK BASS", {2, 0, 30, 80, 0, 0, 30, 0}, {0, 60, 30, 18}, 0, 1, FX(0, 0, 0, 0)},
    {"BRASS STAB", {0, 2, 30, 80, 0, 0, 0, 0}, {4, 62, 40, 24}, 0, 0, FX(0, 0, 0, 0)},
#endif
};

#if CLIPPER_SYNTH
/* (Stallion: no bells, no imitations) the replacements' patching (params.c PATCH_TABLES). SHAPE and CUT both bend the
 * DCW here: ENV 2 is the DCW's own envelope, apart from the level */
static const pcable_t PHC_SWEEP[] = {   /* SWEEP LEAD */
    CB(1, ENV2, SHAPE, 39),       /* ENV 2: the bend sweeps up on the attack and settles */
    CB(2, VEL, SHAPE, 15),        /* velocity: brighter */
    CB(3, AT, SHAPE, 24),         /* pressure: brighter */
    CB(4, LFO3, SHAPE, 6),        /* LFO 3 (0.4 Hz): a slow breath */
};
static const pcable_t PHC_MPAD[] = {    /* MOTION PAD */
    CB(1, LFO2, SHAPE, 15),       /* LFO 2 (slow) and LFO 3 (slower): the bend moves, never the same twice */
    CB(2, LFO3, SHAPE, 9),
    CB(3, LFO3, L2RATE, 12),      /* .. LFO 3 also moves LFO 2's rate */
    CB(4, MW, SHAPE, 30),         /* the wheel: brighter */
    CB(5, VEL, SHAPE, 9),
};
static const pcable_t PHC_SOFT[] = {    /* SOFT LEAD */
    CB(1, AT, SHAPE, 30),         /* pressure: the double sine opens */
    CB(2, VEL, SHAPE, 12),
    CB(3, ENV2, SHAPE, 18),       /* ENV 2: a little bite on the attack */
    CB(4, LFO3, SHAPE, 4),        /* LFO 3: a slow breath */
};
static const pcable_t PHC_KEYS[] = {    /* PD KEYS */
    CB(1, VEL, SHAPE, 21),        /* velocity: brighter */
    CB(2, ENV2, SHAPE, 24),       /* ENV 2 (short): the bright attack */
    CB(3, LFO3, SHAPE, 6),        /* LFO 3: a slow breath in the held notes */
    CB(4, MW, SHAPE, 27),         /* the wheel: brighter */
};
static const pcable_t PHC_PLUCK[] = {   /* PULSE PLUCK */
    CB(1, ENV2, SHAPE, 33),       /* ENV 2 (short): the snap */
    CB(2, VEL, SHAPE, 18),
    CB(3, LFO2, SHAPE, 8),        /* LFO 2: the tail moves */
    CB(4, MW, SHAPE, 24),
};
static const pcable_t PHC_STR[] = {     /* PHASE STRING */
    CB(1, LFO2, SHAPE, 12),       /* LFO 2 (slow) and LFO 3: the square and saw lines shimmer */
    CB(2, LFO3, SHAPE, 8),
    CB(3, AT, SHAPE, 21),         /* pressure: brighter */
    CB(4, VEL, SHAPE, 9),
    CB(5, LFO1, PITCH, 2),        /* LFO 1: a faint ensemble wobble */
};
static const pcable_t PHC_BASS[] = {    /* MOTION BASS */
    CB(1, ENV2, SHAPE, 30),       /* ENV 2: the bend's own decay, the bass's punch */
    CB(2, VEL, SHAPE, 18),
    CB(3, RAND, SHAPE, 6),        /* each note a little its own */
    CB(4, MW, SHAPE, 27),         /* the wheel: brighter */
};
static const preset_patch_t PHASE_PATCHES[] = {
    PPATCH(&PHASE_PRESETS[8], PHC_SWEEP, P_E2ATK + 1, 10, P_E2DEC + 1, 64),
    PPATCH(&PHASE_PRESETS[11], PHC_MPAD, P_L2RATE + 1, 40, P_L3RATE + 1, 22),
    PPATCH(&PHASE_PRESETS[12], PHC_SOFT, P_E2DEC + 1, 50),
    PPATCH(&PHASE_PRESETS[15], PHC_KEYS, P_E2DEC + 1, 46),
    PPATCH(&PHASE_PRESETS[16], PHC_PLUCK, P_E2DEC + 1, 34, P_L2RATE + 1, 52),
    PPATCH(&PHASE_PRESETS[17], PHC_STR, P_L2RATE + 1, 34, P_LRATE + 1, 70),
    PPATCH(&PHASE_PRESETS[18], PHC_BASS, P_E2DEC + 1, 40),
};
#endif

/* (after Clipper 1.2) UNISON (voice.c unison_spread): line 1, line 2 (DTN cents up) and the sub (an octave
 * down) moved by r (Q32) of a turn of the note, each by its share (lines 2 and the sub from 0 at each note) */
static void phase_spread(track_t *t, voice_t *v, uint32_t r)
{
    v->ph[0] += r;
    v->ph[1] += uni_turns(r, v->pitch16, t->p[P_E4] * 16 / 100);
    v->ph[2] += r >> 1;
}

static const engine_t ENG_PHASE = {
    "PHASE", {"PHS", "LINE"},
    {
        {"WAVE", F_ENUM, 0, 7, 0, N_PD_WAVE, 0},
        {"WAVE2", F_ENUM, 0, 8, 0, N_PD_WAVE2, 0},
        {"DCW", F_PCT, 0, 127, 60, 0, 0},
        {"ENV", F_PCT, 0, 127, 64, 0, 0},
        {"DTN", F_INT, 0, 127, 0, 0, "ct"},
        {"LINE", F_ENUM, 0, 1, 0, N_PD_LINE, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
    PHASE_PRESETS, sizeof(PHASE_PRESETS) / sizeof(PHASE_PRESETS[0]), 0, phase_note_on, phase_render,
    0x05DF, {P_E2, P_E3, P_E4, P_REL},
};
