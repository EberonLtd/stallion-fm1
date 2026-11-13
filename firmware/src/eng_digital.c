/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DIGITAL: four-operator FM. */
/* Four sine operators, eight classic 4-operator algorithms (ALG = P_E0, EDIT 1 KNOB 1),
 * op 4 with feedback, one modulation INDEX shaped by a modulator envelope.
 * Phase modulation wraps naturally in the 32-bit phase. */
static const char *const N_FMALG[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
static const char *const N_RATIO[] = {".5", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "14", "16"};
static const uint16_t RATIO_Q8[15] = {128, 256, 512, 768, 1024, 1280, 1536, 1792, 2048, 2304, 2560, 2816,
                                      3072, 3584, 4096};

static void digital_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->s[7] = 0;                 /* op 4 phase */
    v->s[4] = 1 << 24;           /* modulator envelope, Q24 */
    v->s[5] = v->s[6] = 0;       /* feedback history */
    v->s[3] = -1;                /* the index of the block before (-1: none, a new note) */
}

/* operator increment = carrier * ratio (Q8) without 32-bit overflow; kept below Nyquist */
static inline uint32_t fm_ratio_inc(uint32_t inc, uint32_t r)
{
    uint32_t lim = 0x73000000u / r;                      /* (inc >> 8) * r must stay < 0x73000000 */
    return (inc >> 8) > lim ? 0x73000000u : (inc >> 8) * r;
}

static inline uint32_t digital_mod(int32_t x, int32_t idx) { return (uint32_t)(x * idx) * 2065u; }

static void digital_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *ev = vm_ev(t, m);              /* the EDIT values: the voice's own or the part's */
    uint32_t alg = (uint32_t)ev[0] & 7u, i;
    uint32_t i1 = m->inc;
    uint32_t i2 = fm_ratio_inc(m->inc, RATIO_Q8[ev[1] % 15]), i3 = fm_ratio_inc(m->inc, RATIO_Q8[ev[2] % 15]);
    uint32_t i4 = fm_ratio_inc(m->inc, RATIO_Q8[ev[3] % 15]);
    int32_t me, idx, fb = ev[6], fb1, fb2, didx, idq;      /* fb1, fb2: op 4 feedback history */
    uint32_t ph0, ph1, ph2, ph4;
    /* modulator envelope: decays (MODDEC) towards 25 %, per block */
    v->s[4] += mulq16((1 << 22) - v->s[4], ENV_EXP[ev[5] & 127]);
    me = v->s[4] >> 9;                                         /* Q15 */
    idx = (ev[4] * me) >> 15;                                 /* 0..127 */
    idx = clamp(idx + ((m->cutoff + m->shape - (64 << 8)) >> 8) + (m->vel - 96) / 4, 0, 127);   /* (the velocity as played) */
    {   /* (Clipper 0.9) anti-aliasing: the index held where the sidebands (Carson: (index + 1) x the modulator)
         * would pass ~16 kHz, from the fastest operator that modulates in this algorithm; a high note gets a
         * little less bright instead of folding back as noise */
        static const uint8_t MODS[8] = {14, 14, 14, 14, 10, 8, 8, 0};   /* bit 1: op 2, 2: op 3, 3: op 4 */
        uint32_t mk = MODS[alg], im = 0, fm;
        if (mk & 2u)
            im = i2;
        if ((mk & 4u) && i3 > im)
            im = i3;
        if ((mk & 8u) && i4 > im)
            im = i4;
        fm = ((im >> 16) * 44100u) >> 16;                      /* Hz */
        if (fm > 0u) {
            int32_t cap = (int32_t)(161760u / fm) - 10;        /* 16 kHz / fm - 1, in index steps (0.0989 rad) */
            idx = idx < cap ? idx : (cap > 0 ? cap : 0);
        }
    }
    {   /* (0.9) the index glides through the block from the last one: a new modulator envelope (a retriggered
         * mono bass) or a fast decay no longer steps the waveform at the block's edge */
        int32_t i0 = v->s[3] < 0 ? idx : v->s[3];
        v->s[3] = idx;
        idx = i0;
        didx = ((v->s[3] - i0) * 2048) / (int32_t)(n ? n : 1u);   /* Q11 per sample */
    }
    ph0 = v->ph[0];                                             /* state in locals: out[] may alias v->s[] */
    ph1 = v->ph[1];
    ph2 = v->ph[2];
    ph4 = (uint32_t)v->s[7];
    fb1 = v->s[5];
    fb2 = v->s[6];
    idq = idx << 11;
    for (i = 0; i < n; i++) {
        int32_t o1, o2, o3, o4, s;
        idx = idq >> 11;
        idq += didx;
        o4 = sine_i((ph4 + digital_mod((fb1 + fb2) >> 1, fb)));
        fb2 = fb1;
        fb1 = o4;
        switch (alg) {
        case 0:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i((ph1 + digital_mod(o3, idx)));
            s = sine_i((ph0 + digital_mod(o2, idx)));
            break;
        case 1:
            o3 = sine_i(ph2);
            o2 = sine_i((ph1 + digital_mod((o3 + o4) >> 1, idx)));
            s = sine_i((ph0 + digital_mod(o2, idx)));
            break;
        case 2:
            o3 = sine_i(ph2);
            o2 = sine_i((ph1 + digital_mod(o3, idx)));
            s = sine_i((ph0 + digital_mod((o2 + o4) >> 1, idx)));
            break;
        case 3:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i(ph1);
            s = sine_i((ph0 + digital_mod((o2 + o3) >> 1, idx)));
            break;
        case 4:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i(ph1);
            o1 = sine_i((ph0 + digital_mod(o2, idx)));
            s = (o1 + o3) >> 1;
            break;
        case 5:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i((ph1 + digital_mod(o4, idx)));
            o1 = sine_i((ph0 + digital_mod(o4, idx)));
            s = mulq15(o1 + o2 + o3, 10923);           /* / 3 without a divide */
            break;
        case 6:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i(ph1);
            o1 = sine_i(ph0);
            s = mulq15(o1 + o2 + o3, 10923);           /* / 3 without a divide */
            break;
        default:
            o3 = sine_i(ph2);
            o2 = sine_i(ph1);
            o1 = sine_i(ph0);
            s = (o1 + o2 + o3 + o4) >> 2;
            break;
        }
        ph0 += i1;
        ph1 += i2;
        ph2 += i3;
        ph4 += i4;
        out[i] += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS);   /* full-scale sines: 6 dB below the others */
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[7] = (int32_t)ph4;
    v->s[5] = fb1;
    v->s[6] = fb2;
}

static const preset_t DIGITAL_PRESETS[] = {
    /* ALG R2 R3 R4 INDEX MODDEC FDBK - ; ALG 5 (4 here) = two stacks: 2 -> 1 and 4 -> 3 (the tine). (Stallion 1.0:
     * the 4-operator FM classics, after the factory sheets of the 1980s 4-op FM modules and keyboards) */
    {"INIT", {0, 1, 1, 1, 60, 60, 0, 0}, {10, 70, 90, 60}, 0, 0, FX(0, 0, 0, 0)},
    {"TINE KEYS", {4, 1, 1, 14, 26, 35, 0, 0}, {0, 85, 35, 55}, 0, 0, FX(0, 0, 0, 0), XP(P_LD_AMP + 1, 26, P_LRATE + 1, 84)},
    {"FM TINE KEYS", {4, 1, 1, 14, 85, 45, 0, 0}, {0, 82, 40, 55}, 0, 0, FX(0, 0, 0, 0)},
    {"REED KEYS", {4, 1, 2, 3, 62, 35, 8, 0}, {0, 75, 30, 45}, 0, 0, FX(0, 0, 0, 0), XP(P_LD_AMP + 1, 16, P_LRATE + 1, 89)},
    {"FM GRAND", {4, 1, 2, 12, 60, 40, 10, 0}, {0, 90, 20, 60}, 0, 0, FX(0, 0, 0, 0)},
    /* two voices a key a few cents apart: the out-of-tune upright */
    {"HONKY TONK", {4, 1, 2, 12, 56, 36, 10, 0}, {0, 88, 18, 55}, 0, 0, FX(0, 0, 0, 0),
     XP(P_VOICE + 1, V_PUNI2, P_DETUNE + 1, 26)},
    /* house piano: bright, fast hammer, the tine stack for the attack */
    {"HOUSE PIANO", {4, 1, 2, 13, 74, 30, 12, 0}, {0, 86, 30, 48}, 0, 0, FX(0, 0, 0, 0)},
    {"CLAV", {3, 1, 3, 5, 84, 25, 7, 0}, {0, 58, 34, 18}, 0, 0, FX(0, 0, 0, 0)},
    /* 3 -> 2 (x3) -> 1 and a fed-back 4 on 1: a bright, buzzy pluck with no sustain */
    {"HARPSICHORD", {2, 3, 1, 1, 64, 40, 40, 0}, {0, 80, 0, 40}, 0, 0, FX(0, 0, 0, 0)},
    {"PERC ORGAN", {4, 2, 1, 3, 30, 20, 0, 0}, {0, 64, 120, 30}, 0, 0, FX(0, 0, 0, 0)},
    /* ALG 8: four sines added, 1 2 3 4, op 4 fed back (a reedy top): the transistor combo organ */
    {"COMBO ORGAN", {7, 2, 3, 4, 0, 60, 60, 0}, {0, 40, 127, 20}, 0, 0, FX(0, 0, 0, 0),
     XP(P_LD_PIT + 1, 3, P_LRATE + 1, 80)},
#if CLIPPER_SYNTH
    /* (Stallion 1.0, no imitations: DIGITAL_PATCHES moves it) two 1:1 stacks, a little fed-back index swelling in on
     * ENV 2, two voices a key a few cents apart: the warm 4-op FM pad */
    {"WARM PAD", {4, 1, 1, 1, 22, 100, 20, 0}, {70, 100, 120, 90}, 0, 0, FX(0, 0, 0, 0),
     XP(P_VOICE + 1, V_PUNI2, P_DETUNE + 1, 22)},
#else
    /* ALG 7: a fed-back 4 on 3 (the reed), 1 and 2 (the octave) plain; two reeds a few cents apart (the musette) */
    {"ACCORDION", {6, 2, 1, 1, 40, 100, 30, 0}, {6, 60, 120, 30}, 0, 0, FX(0, 0, 0, 0),
     XP(P_VOICE + 1, V_PUNI2, P_DETUNE + 1, 20)},
#endif
    /* the dance-record FM bass: two 1:1 stacks, op 4 fed back, the index falling fast to a round body */
    {"SOLID BASS", {4, 1, 1, 1, 56, 28, 40, 0}, {0, 70, 90, 20}, 10, 1, FX(0, 0, 0, 0)},
    {"FM BASS", {0, 1, 1, 1, 34, 40, 8, 0}, {0, 60, 70, 25}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 36, P_TRANS + 1, -24)},
    {"DIGI BASS", {0, 1, 1, 1, 46, 26, 10, 0}, {0, 60, 80, 20}, 0, 1, FX(0, 0, 0, 0), XP(P_TRANS + 1, -12, P_VELF + 1, 30)},
#if CLIPPER_SYNTH
    /* the 1:1:1:1 chain, op 4 fed back hard: a saw-like FM bass, the index on velocity and a short ENV 2 */
    {"SAW BASS", {0, 1, 1, 1, 44, 40, 60, 0}, {0, 70, 100, 18}, 14, 1, FX(0, 0, 0, 0)},
    /* (3 + 4) -> 2 -> 1, 3 an octave up: ENV 2 sweeps the index down on each note, a filter-sweep bass */
    {"SWEEP BASS", {1, 1, 2, 1, 26, 60, 30, 0}, {0, 80, 90, 20}, 0, 1, FX(0, 0, 0, 0)},
#else
    {"BASS GTR", {4, 1, 1, 2, 40, 30, 6, 0}, {0, 76, 40, 22}, 0, 1, FX(0, 0, 0, 0), XP(P_TRANS + 1, -24)},
    /* the pop of the thumb: the tine stack (x16) on 3 decaying fast over a 1:1 body */
    {"SLAP BASS", {4, 1, 1, 14, 70, 18, 20, 0}, {0, 72, 50, 20}, 0, 1, FX(0, 0, 0, 0), XP(P_TRANS + 1, -12)},
#endif
    {"FM BRASS", {0, 1, 1, 1, 48, 70, 6, 0}, {20, 80, 110, 40}, 0, 0, FX(0, 0, 0, 0), XP(P_ED_SHP + 1, 20)},
    {"BRASS STAB", {0, 1, 1, 1, 60, 30, 10, 0}, {4, 60, 60, 20}, 20, 0, FX(0, 0, 0, 0)},
#if CLIPPER_SYNTH
    /* the 1:1 chain, the index following the amplitude (ENV -> index): a mellow synth brass, a small scoop */
    {"SOFT BRASS", {0, 1, 1, 1, 28, 90, 6, 0}, {30, 80, 110, 45}, 24, 0, FX(0, 0, 0, 0)},
    /* (3 + 4) -> 2 -> 1 fed back: the bright poly synth brass, ENV 2 blooming the index after the attack */
    {"SYNTH BRASS", {1, 1, 1, 1, 34, 60, 20, 0}, {14, 80, 100, 36}, 30, 0, FX(0, 0, 0, 0),
     XP(P_LD_PIT + 1, 1, P_LRATE + 1, 80, P_LFADE + 1, 60)},
#else
    /* a soft 1:1 stack, little index, a slow swell */
    {"FRENCH HORN", {0, 1, 1, 1, 24, 90, 0, 0}, {30, 90, 110, 50}, 10, 0, FX(0, 0, 0, 0)},
    /* (3 + 4) -> 2 -> 1, the index rising with the breath */
    {"TRUMPET", {1, 1, 1, 1, 40, 60, 12, 0}, {12, 80, 110, 30}, 16, 1, FX(0, 0, 0, 0),
     XP(P_LD_PIT + 1, 2, P_LRATE + 1, 78, P_LFADE + 1, 60)},
#endif
    {"FM LEAD", {0, 1, 2, 1, 40, 60, 10, 0}, {2, 70, 110, 40}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 30, P_MWDST + 1, MD_VIB)},
#if CLIPPER_SYNTH
    /* two soft stacks, 3 an octave up: a round synth lead, a delayed vibrato, a touch of index on the attack */
    {"SOFT LEAD", {4, 1, 2, 1, 18, 80, 10, 0}, {6, 80, 115, 30}, 0, 1, FX(0, 0, 0, 0),
     XP(P_LD_PIT + 1, 2, P_LRATE + 1, 78, P_LFADE + 1, 60, P_GLIDE + 1, 20)},
#else
    /* a near-sine with a short chiff of index at the start, a delayed vibrato */
    {"PERC FLUTE", {4, 1, 1, 3, 30, 20, 0, 0}, {4, 70, 110, 30}, 0, 1, FX(0, 0, 0, 0),
     XP(P_LD_PIT + 1, 2, P_LRATE + 1, 76, P_LFADE + 1, 60)},
#endif
    /* ALG 6: a fed-back 4 (a saw) on 1 2 3, two voices a key detuned: the FM string section */
    {"FM STRINGS", {5, 1, 1, 1, 30, 90, 50, 0}, {50, 100, 115, 80}, 0, 0, FX(0, 0, 0, 0),
     XP(P_VOICE + 1, V_PUNI2, P_DETUNE + 1, 24)},
    /* 1, 2 (the octave) and a lightly modulated 3 (x3): a breathy choir, a slow vibrato */
    {"FM CHOIR", {6, 2, 3, 1, 20, 90, 0, 0}, {60, 100, 120, 90}, 0, 0, FX(0, 0, 0, 0),
     XP(P_LD_PIT + 1, 2, P_LRATE + 1, 70)},
    {"GLASS PAD", {4, 2, 1, 5, 34, 100, 0, 0}, {62, 92, 112, 92}, 0, 0, FX(0, 0, 0, 0),
     XP(P_LD_PIT + 1, 1, P_LRATE + 1, 40)},
#if CLIPPER_SYNTH
    /* ALG 6: a fed-back 4 (a saw) on 1 2 3, 2 an octave up, two voices detuned: a slow sweeping pad */
    {"SWEEP PAD", {5, 2, 1, 1, 20, 110, 60, 0}, {80, 100, 120, 100}, 0, 0, FX(0, 0, 0, 0),
     XP(P_VOICE + 1, V_PUNI2, P_DETUNE + 1, 20)},
    /* two 1:1 stacks, 4 at 2:1 on 3: a digital electric piano, velocity to the bark, the wheel a tremolo */
    {"DIGI KEYS", {4, 1, 1, 2, 36, 40, 8, 0}, {0, 85, 30, 50}, 0, 0, FX(0, 0, 0, 0), XP(P_MWDST + 1, MD_TREM)},
    /* the 1:2:1 chain, the index falling fast: a bright synth pluck with a short body */
    {"FM PLUCK", {0, 1, 2, 1, 48, 25, 16, 0}, {0, 62, 40, 40}, 0, 0, FX(0, 0, 0, 0)},
    /* two stacks with the modulator at 2:1 (odd harmonics): a hollow square-wave lead, the index breathing */
    {"SQUARE LEAD", {4, 2, 1, 2, 40, 120, 0, 0}, {2, 70, 115, 30}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 16)},
    /* (3 + 4) -> 2 -> 1 fed back, the index on the envelope: a solo synth brass lead with a scoop */
    {"BRASS LEAD", {1, 1, 1, 1, 36, 80, 24, 0}, {8, 80, 110, 30}, 20, 1, FX(0, 0, 0, 0),
     XP(P_GLIDE + 1, 24, P_LD_PIT + 1, 2, P_LRATE + 1, 78, P_LFADE + 1, 60)},
    /* ALG 7: a fed-back 4 on 3, 2 an octave up, slow: a string-machine pad, ENV 2 opening it */
    {"STRING PAD", {6, 2, 1, 1, 26, 110, 70, 0}, {90, 100, 118, 100}, 0, 0, FX(0, 0, 0, 0),
     XP(P_VOICE + 1, V_PUNI2, P_DETUNE + 1, 28)},
#else
    {"TUBE BELL", {4, 6, 1, 13, 50, 90, 0, 0}, {0, 100, 0, 90}, 0, 0, FX(0, 0, 0, 0)},
    {"VIBES", {4, 4, 1, 4, 40, 40, 0, 0}, {0, 92, 0, 74}, 0, 0, FX(0, 0, 0, 0), XP(P_LRATE + 1, 70, P_LD_AMP + 1, 20)},
    {"MARIMBA", {4, 4, 1, 1, 60, 30, 0, 0}, {0, 80, 0, 60}, 0, 0, FX(0, 0, 0, 0)},
    {"STEEL DRUM", {4, 2, 2, 2, 45, 45, 0, 0}, {0, 88, 0, 66}, 0, 0, FX(0, 0, 0, 0)},
    {"HARP", {4, 1, 1, 2, 45, 30, 10, 0}, {0, 92, 0, 80}, 0, 0, FX(0, 0, 0, 0)},
    /* 3 (x3) -> 2 -> 1 and a fed-back 4 on 1, falling fast: the plucked guitar */
    {"PLUCK GTR", {2, 3, 1, 1, 50, 30, 30, 0}, {0, 80, 0, 40}, 0, 0, FX(0, 0, 0, 0)},
#endif
};

#if CLIPPER_SYNTH
/* (Stallion 1.0, no bells, no imitations) the replacements' patching (core.h preset_patch_t; params.c PATCH_TABLES).
 * On DIGITAL CUT and SHAPE both add to the index (the brightness), E7 is FB, ENV 1 is the amplitude (ENV -> FILTER and
 * E1AMT move the index with it); the default AT> is CUT, MW> the vibrato */
static const pcable_t DGC_WARM[] = {      /* WARM PAD */
    CB(1, LFO2, CUT, 8),                  /* LFO 2: the index breathing slowly */
    CB(2, LFO3, L2RATE, 12),              /* LFO 3: that breathing's rate drifting */
    CB(3, ENV2, SHAPE, 10),               /* ENV 2: the index swelling in after the attack */
    CB(4, MW, E1 + 6, 20),                /* the wheel: more feedback, a reedier edge */
};
static const pcable_t DGC_SAWB[] = {      /* SAW BASS */
    CB(1, VEL, CUT, 14),                  /* velocity: brighter */
    CB(2, ENV2, SHAPE, 16),               /* ENV 2: a short burst of index on each note */
    CB(3, MW, E1 + 6, 24),                /* the wheel: more feedback, a growl */
    CB(4, KEY, CUT, -6),                  /* high notes a little rounder */
};
static const pcable_t DGC_SWPB[] = {      /* SWEEP BASS */
    CB(1, ENV2, CUT, 30),                 /* ENV 2: the index sweeping down on each note */
    CB(2, KEY, CUT, -8),                  /* high notes rounder */
    CB(3, AT, E1 + 6, 20),                /* pressure: more feedback */
    CB(4, RAND, CUT, 6),                  /* each note a little different */
};
static const pcable_t DGC_SBRS[] = {      /* SOFT BRASS */
    CB(1, VEL, E1AMT, 20),                /* velocity: the envelope's index deeper */
    CB(2, LFO2, CUT, 5),                  /* LFO 2: a slow shimmer */
    CB(3, MW, CUT, 16),                   /* the wheel: brighter */
    CB(4, ENV2, PITCH, -6),               /* ENV 2: a small scoop up into the note */
};
static const pcable_t DGC_SYBR[] = {      /* SYNTH BRASS */
    CB(1, ENV2, SHAPE, 20),               /* ENV 2: the index blooming after the attack */
    CB(2, VEL, CUT, 12),                  /* velocity: brighter */
    CB(3, MAC2, E1 + 6, 30),              /* TIMBR: more feedback ... */
    CB(4, MAC2, CUT, 12),                 /* .. and brighter with it */
};
static const pcable_t DGC_SLEAD[] = {     /* SOFT LEAD */
    CB(1, VEL, CUT, 10),                  /* velocity: brighter */
    CB(2, ENV2, CUT, 12),                 /* ENV 2: a touch of index on the attack */
    CB(3, LFO2, CUT, 4),                  /* LFO 2: a shimmer */
    CB(4, LFO3, L2RATE, 10),              /* LFO 3: the shimmer's rate drifting */
};
static const pcable_t DGC_SWPP[] = {      /* SWEEP PAD */
    CB(1, LFO2, CUT, 14),                 /* LFO 2: a slow sweep of the index */
    CB(2, LFO3, SHAPE, 6),                /* LFO 3: a second, unrelated sweep */
    CB(3, MW, E1 + 6, -30),               /* the wheel: less feedback, mellower */
    CB(4, KEY, CUT, -6),                  /* high notes softer */
};
static const pcable_t DGC_DKEY[] = {      /* DIGI KEYS */
    CB(1, VEL, CUT, 16),                  /* velocity: the bark */
    CB(2, LFO2, CUT, 4),                  /* LFO 2: a gentle shimmer */
    CB(3, KEY, CUT, -10),                 /* high notes mellower */
    CB(4, AT, E1 + 6, 16),                /* pressure: more feedback */
};
static const pcable_t DGC_PLCK[] = {      /* FM PLUCK */
    CB(1, VEL, CUT, 14),                  /* velocity: brighter */
    CB(2, ENV2, SHAPE, 20),               /* ENV 2: the pluck's edge */
    CB(3, LFO3, CUT, 4),                  /* LFO 3: a slow shimmer */
    CB(4, KEY, ERATE, 8),                 /* high notes shorter */
};
static const pcable_t DGC_SQLD[] = {      /* SQUARE LEAD */
    CB(1, LFO2, CUT, 8),                  /* LFO 2: the index moving like a PWM */
    CB(2, LFO3, L2RATE, 12),              /* LFO 3: that movement's rate drifting */
    CB(3, VEL, CUT, 8),                   /* velocity: brighter */
    CB(4, MW, CUT, 14),                   /* the wheel: brighter with the vibrato */
};
static const pcable_t DGC_BRLD[] = {      /* BRASS LEAD */
    CB(1, ENV2, PITCH, -8),               /* ENV 2: a scoop up into each note */
    CB(2, VEL, E1AMT, 24),                /* velocity: the envelope's index deeper */
    CB(3, LFO2, SHAPE, 4),                /* LFO 2: a shimmer */
    CB(4, AT, E1 + 6, 20),                /* pressure: more feedback, a rasp */
};
static const pcable_t DGC_STRP[] = {      /* STRING PAD */
    CB(1, ENV2, CUT, 12),                 /* ENV 2: the index opening slowly */
    CB(2, LFO2, CUT, 6),                  /* LFO 2: a slow sweep */
    CB(3, LFO3, PITCH, 3),                /* LFO 3: a few cents of drift */
    CB(4, AT, AMP, 6),                    /* pressure: a swell */
};
static const preset_patch_t DIGITAL_PATCHES[] = {
    PPATCH(&DIGITAL_PRESETS[11], DGC_WARM, P_L2RATE + 1, 30, P_L3RATE + 1, 18, P_E2ATK + 1, 90, P_E2SUS + 1, 70),   /* WARM PAD */
    PPATCH(&DIGITAL_PRESETS[15], DGC_SAWB, P_E2DEC + 1, 30),                       /* SAW BASS */
    PPATCH(&DIGITAL_PRESETS[16], DGC_SWPB, P_E2DEC + 1, 45),                       /* SWEEP BASS */
    PPATCH(&DIGITAL_PRESETS[19], DGC_SBRS, P_L2RATE + 1, 40, P_E2DEC + 1, 25),     /* SOFT BRASS */
    PPATCH(&DIGITAL_PRESETS[20], DGC_SYBR, P_E2ATK + 1, 20, P_E2DEC + 1, 60),      /* SYNTH BRASS */
    PPATCH(&DIGITAL_PRESETS[22], DGC_SLEAD, P_L2RATE + 1, 66, P_L3RATE + 1, 20, P_E2DEC + 1, 30),   /* SOFT LEAD */
    PPATCH(&DIGITAL_PRESETS[26], DGC_SWPP, P_L2RATE + 1, 12, P_L2WAVE + 1, LW_TRI, P_L3RATE + 1, 30),   /* SWEEP PAD */
    PPATCH(&DIGITAL_PRESETS[27], DGC_DKEY, P_L2RATE + 1, 60),                      /* DIGI KEYS */
    PPATCH(&DIGITAL_PRESETS[28], DGC_PLCK, P_E2DEC + 1, 20, P_L3RATE + 1, 30),     /* FM PLUCK */
    PPATCH(&DIGITAL_PRESETS[29], DGC_SQLD, P_L2RATE + 1, 50, P_L2WAVE + 1, LW_TRI, P_L3RATE + 1, 25),   /* SQUARE LEAD */
    PPATCH(&DIGITAL_PRESETS[30], DGC_BRLD, P_E2DEC + 1, 22, P_L2RATE + 1, 72),     /* BRASS LEAD */
    PPATCH(&DIGITAL_PRESETS[31], DGC_STRP, P_E2ATK + 1, 100, P_E2SUS + 1, 60, P_L2RATE + 1, 26, P_L3RATE + 1, 20),   /* STRING PAD */
};
#endif

/* (after Clipper 1.2) UNISON (voice.c unison_spread): the four operators moved together by r (Q32) of a turn of
 * the note, each by its ratio's share (phases 0 at each note: every voice in phase) */
static void digital_spread(track_t *t, voice_t *v, uint32_t r)
{
    const int16_t *p = t->p;
    v->ph[0] += r;
    v->ph[1] += (uint32_t)(((uint64_t)r * RATIO_Q8[p[P_E1] % 15]) >> 8);
    v->ph[2] += (uint32_t)(((uint64_t)r * RATIO_Q8[p[P_E2] % 15]) >> 8);
    v->s[7] = (int32_t)((uint32_t)v->s[7] + (uint32_t)(((uint64_t)r * RATIO_Q8[p[P_E3] % 15]) >> 8));
}

static const engine_t ENG_DIGITAL = {
    "DIGITAL", {"OPS", "MOD"},
    {
        {"ALG", F_ENUM, 0, 7, 0, N_FMALG, 0},
        {"R2", F_ENUM, 0, 14, 1, N_RATIO, 0},
        {"R3", F_ENUM, 0, 14, 1, N_RATIO, 0},
        {"R4", F_ENUM, 0, 14, 1, N_RATIO, 0},
        {"IDX", F_PCT, 0, 127, 60, 0, 0},
        {"MDEC", F_TIME, 0, 127, 60, 0, 0},
        {"FB", F_PCT, 0, 127, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
    DIGITAL_PRESETS, sizeof(DIGITAL_PRESETS) / sizeof(DIGITAL_PRESETS[0]), -1, digital_note_on, digital_render,
    0xFD20, {P_E4, P_E5, P_E6, P_REL},
};
