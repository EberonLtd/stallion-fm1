/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Changes: Copyright (C) 2026 Eberon Limited (John Gorman) */
/* ANALOG: two band-limited oscillators (saw / square / tri / sine / PWM),
 * noise, drive and a trapezoidal low-pass. */
static const char *const N_ANALOG_WAVE[] = {"SAW", "SQR", "TRI", "SIN", "PWM"};

static void analog_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->env && !v->env_out)
        v->ph[0] = 0;                                 /* a fresh note: from phase 0 (an 808 starts the same every time) */
    v->ph[1] = v->ph[0] + 0x40000000u;
    v->s[0] = v->s[1] = 0;                            /* filter */
    v->s[3] = v->s[4] = v->s[5] = v->s[6] = 0;        /* (MOOG: the ladder's four stages) */
    if (!v->s[2])
        v->s[2] = 0x1234567 + (int32_t)v->age;        /* noise state */
}

static void analog_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *ev = vm_ev(t, m);              /* the EDIT values: the voice's own or the part's */
    uint32_t wave = (uint32_t)ev[0], i;
    int32_t det = ev[1], mix = ev[2], noise = ev[3];
    int32_t cut = (ev[4] << 8) + m->cutoff + (ev[7] * (v->pitch16 - 60 * 16) >> 4);
    fmod_t fm;                                                   /* Clipper: the filter MODEL (P_FMODEL, dsp.c) */
    int32_t model = fm_model(t);
    int32_t drive = 32768 + ev[6] * 768;                       /* 1x .. 4x */
    int32_t dw = ev[6] * 258;                                  /* DRV: dry -> driven (a clean range low) */
    uint32_t inc1 = m->inc;
    /* DTN in cents: whole 1/16 semitones from the table, the rest as a fine factor */
    int32_t d16 = det * 16 / 100, rem = det * 16 - d16 * 100;            /* rem: 1/1600 semitone */
    uint32_t inc2 = pitch_inc(clamp(m->pitch16 + d16, 0, 2047));
    inc2 += (uint32_t)((int32_t)(inc2 >> 12) * (rem * 2367 / 16000));
    uint32_t pw = 0x80000000u + (uint32_t)((m->shape - (64 << 8)) << 15);
    int32_t m2 = mix * 258, m1 = 32767 - m2;                    /* osc mix Q15 */
    int32_t nz = noise * 200, drv = ev[6];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1];                  /* state in locals: out[] may alias v->s[] */
    int32_t sv[2] = {v->s[0], v->s[1]}, nst = v->s[2];
    int32_t ls[8] = {v->s[3], v->s[4], v->s[5], v->s[6], v->fs[0], v->fs[1], v->fs[2], v->fs[3]};   /* (LADDR's stages,
                                                                 * in locals too; SLOPE 48's four more) */
    f2pair_t f2;                                                 /* (FILTER 2: engines.c f2_pair) */
    __typeof__(&fmod_tick) tick = HOT_FAR(fmod_tick);            /* (Stallion: the filter models in RAM, hal/fm1_hot.h) */
    HOT_FAR(fmod_coef)(&fm, model, cut, ev[5]);
    fmod_slope(&fm, fm_slope(t));
    if (HOT_FAR(f2_pair)(&fm, &f2, t, v, m, cut, out))
        model = FM_PAIR;
    if (det == 0)
        inc2 = inc1;
    for (i = 0; i < n; i++) {
        int32_t a, b = 0, s;
        switch (wave) {
        case 1:
            a = osc_pulse_o(ph0, inc1, 0x80000000u);
            if (m2)
                b = osc_pulse_o(ph1, inc2, 0x80000000u);
            break;
        case 2:                                       /* (Clipper 0.2: band-limited, dsp.c osc_tri_bl; 1.1: _o) */
            a = osc_tri_bl_o(ph0, inc1);
            if (m2)
                b = osc_tri_bl_o(ph1, inc2);
            break;
        case 3:
            a = osc_sine(ph0);
            if (m2)
                b = osc_sine(ph1);
            break;
        case 4:
            a = osc_pulse_o(ph0, inc1, pw);
            if (m2)
                b = osc_pulse_o(ph1, inc2, pw);
            break;
        default:
            a = osc_saw_o(ph0, inc1);
            if (m2)
                b = osc_saw_o(ph1, inc2);
            break;
        }
        ph0 += inc1;
        ph1 += inc2;
        s = m2 ? mulq15(a, m1) + mulq15(b, m2) : a;  /* (MIX 0, the 808s and subs: one oscillator) */
        if (nz)
            s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, nz);
        if (drv)                                      /* pre-shifts: drive is up to 4x, no overflow */
            s += mulq15(softclip(((s >> 2) * (drive >> 2)) >> 11) - s, dw);
        {   /* filter: linear up to half scale, then a soft knee (only resonance peaks saturate) */
            int32_t y = model == FM_SVF ? tsvf_lp(&fm.sv, s >> 1, &sv[0], &sv[1]) : tick(&fm, s, sv, ls);
            int32_t a = y < 0 ? -y : y;
            if (a > 16000) {
                a = 16000 + (softclip((a - 16000) * 2) >> 1);
                y = y < 0 ? -a : a;
            }
            s = y << 1;
        }
        out[i] += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->s[0] = sv[0];
    v->s[1] = sv[1];
    v->s[3] = ls[0], v->s[4] = ls[1], v->s[5] = ls[2], v->s[6] = ls[3];
    v->fs[0] = ls[4], v->fs[1] = ls[5], v->fs[2] = ls[6], v->fs[3] = ls[7];
    if (model == FM_PAIR)
        f2_pair_end(&f2, v);
    v->s[2] = nst;
}

static const preset_t ANALOG_PRESETS[] = {
    /* {WAVE, DTN, MIX, NOIS, CUT, RES, DRV, KTR}, {A D S R}, fenv, mono. Stallion 1.0: the classic sounds of the
     * two-oscillator subtractive synth, dry (sends 0); with FX > FILTER > MODL = LADDR the 1970s ladder mono synth */
    {"INIT", {0, 10, 0, 0, 90, 30, 0, 64}, {0, 70, 127, 30}, 0, 0, FX(0, 0, 0, 0)},   /* (plain: one saw, MIX 0; two at
                                                         * DTN 0 sit half a cycle apart, an octave up) */
    /* basses. Boom basses: a sine that decays (SUS 0) with a little pitch drop (ENV -> PITCH) and slides (GLIDE, legato) */
    {"BOOM BASS", {3, 0, 0, 0, 127, 0, 48, 0}, {0, 112, 0, 52}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 59, P_GLMODE + 1, 1, P_ED_PIT + 1, 18, P_TRANS + 1, -24)},
    {"SLIDE BOOM", {3, 0, 0, 0, 127, 0, 72, 0}, {0, 118, 0, 60}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 82, P_GLMODE + 1, 1, P_ED_PIT + 1, 12, P_TRANS + 1, -24)},
    {"SUB BASS", {3, 0, 0, 0, 127, 0, 10, 0}, {0, 60, 118, 24}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 40, P_TRANS + 1, -24)},
    /* two detuned saws beating, the filter moving slowly (drum & bass) */
    {"DRIFT BASS", {0, 22, 64, 0, 56, 22, 36, 40}, {0, 70, 118, 28}, 0, 1, FX(0, 0, 0, 0), XP(P_LRATE + 1, 22, P_LD_FLT + 1, 10, P_TRANS + 1, -24)},
    {"WOBBLE", {0, 9, 64, 0, 38, 72, 70, 30}, {0, 64, 127, 22}, 0, 1, FX(0, 0, 0, 0), XP(P_LRATE + 1, 74, P_LD_FLT + 1, 40, P_TRANS + 1, -24)},
    /* the sliding square bass of west-coast funk */
    {"FUNK BASS", {1, 6, 50, 0, 50, 30, 22, 64}, {0, 55, 62, 20}, 30, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 50, P_GLMODE + 1, 1, P_TRANS + 1, -24)},
    /* the ladder: a fat punchy mono bass, a deep sustained pedal bass */
    {"PUNCH BASS", {0, 6, 64, 0, 42, 56, 40, 64}, {0, 64, 70, 24}, 46, 1, FX(0, 0, 0, 0),
     XP(P_FMODEL + 1, FM_LADDER, P_GLIDE + 1, 26, P_TRANS + 1, -24)},
    {"PEDAL BASS", {0, 4, 64, 0, 34, 30, 40, 64}, {4, 96, 118, 60}, 36, 1, FX(0, 0, 0, 0),
     XP(P_FMODEL + 1, FM_LADDER, P_TRANS + 1, -24)},
    /* a resonant 2-pole envelope "quack" (fusion), a short synth-pop sequence bass, the rolling off-beat trance bass */
    {"QUACK BASS", {0, 0, 0, 0, 30, 92, 30, 64}, {0, 72, 90, 20}, 50, 1, FX(0, 0, 0, 0), XP(P_VELF + 1, 20, P_TRANS + 1, -24)},
    {"SEQ BASS", {1, 0, 0, 0, 42, 40, 10, 64}, {0, 62, 40, 30}, 40, 1, FX(0, 0, 0, 0), XP(P_TRANS + 1, -24)},
    {"ROLLING BASS", {0, 8, 64, 0, 46, 30, 20, 64}, {0, 64, 60, 20}, 44, 1, FX(0, 0, 0, 0), XP(P_TRANS + 1, -24)},
    /* leads: the 1970s ladder lead (glide; vibrato on the wheel), a square fusion lead, PWM, an overdriven ladder */
    {"SOLO LEAD", {0, 9, 64, 0, 62, 40, 30, 64}, {2, 70, 110, 40}, 26, 1, FX(0, 0, 0, 0), XP(P_FMODEL + 1, FM_LADDER, P_GLIDE + 1, 36)},
    {"SQUARE LEAD", {1, 6, 64, 0, 70, 30, 20, 64}, {2, 70, 110, 40}, 20, 1, FX(0, 0, 0, 0), XP(P_FMODEL + 1, FM_LADDER, P_GLIDE + 1, 30)},
    {"PWM LEAD", {4, 0, 0, 0, 80, 20, 10, 64}, {2, 70, 110, 30}, 0, 1, FX(0, 0, 0, 0), XP(P_LRATE + 1, 70, P_LD_SHP + 1, 40, P_GLIDE + 1, 20)},
    {"DRIVE LEAD", {0, 7, 64, 0, 58, 50, 100, 64}, {2, 70, 115, 40}, 30, 1, FX(0, 0, 0, 0), XP(P_FMODEL + 1, FM_LADDER, P_GLIDE + 1, 24)},
    /* the high portamento sine-like lead with a delayed vibrato (west-coast hip-hop) */
    {"G-FUNK LEAD", {2, 0, 0, 0, 92, 0, 10, 64}, {6, 70, 112, 46}, 0, 1, FX(0, 0, 0, 0),
     XP(P_GLIDE + 1, 74, P_LD_PIT + 1, 2, P_LRATE + 1, 89, P_LFADE + 1, 50)},
    /* trance: eight detuned saw voices on one note */
    {"SUPERSAW", {0, 14, 64, 0, 96, 12, 10, 64}, {4, 70, 112, 46}, 10, 1, FX(0, 0, 0, 0), XP(P_VOICE + 1, V_UNISON, P_DETUNE + 1, 64, P_GLIDE + 1, 30)},
    /* the patch-sheet leads and brass (saw, the filter closed, its contour opening it); the groovebox keeps the sheets'
     * flute, clarinet, tuba and cello */
#if CLIPPER_SYNTH
    /* (Stallion: no imitations) a soft saw lead, the filter near 2 kHz breathing on a delayed LFO; a ladder lead that
     * opens under the fingers (ANALOG_PATCHES) */
    {"SOFT LEAD", {0, 0, 0, 0, 56, 30, 0, 90}, {20, 70, 127, 30}, 0, 1, FX(0, 0, 0, 0), XP(P_LRATE + 1, 90, P_LD_FLT + 1, 6, P_LFADE + 1, 60)},
    {"TOUCH LEAD", {0, 8, 64, 0, 48, 50, 20, 64}, {2, 70, 115, 36}, 20, 1, FX(0, 0, 0, 0), XP(P_FMODEL + 1, FM_LADDER, P_GLIDE + 1, 24)},
#else
    {"SYNTH FLUTE", {0, 0, 0, 0, 56, 30, 0, 90}, {20, 70, 127, 30}, 0, 1, FX(0, 0, 0, 0), XP(P_LRATE + 1, 90, P_LD_FLT + 1, 6, P_LFADE + 1, 60)},
    {"CLARINET", {1, 0, 0, 0, 60, 10, 0, 80}, {14, 70, 127, 24}, 0, 1, FX(0, 0, 0, 0)},
#endif
    {"SOLO BRASS", {0, 0, 0, 0, 30, 16, 0, 127}, {56, 88, 100, 30}, 40, 1, FX(0, 0, 0, 0), XP(P_FMODEL + 1, FM_LADDER)},
#if CLIPPER_SYNTH
    /* (Stallion) a ladder bass whose filter drifts and answers the strike, a warm detuned pad that breathes */
    {"MOTION BASS", {0, 6, 64, 0, 36, 44, 40, 64}, {0, 70, 110, 24}, 30, 1, FX(0, 0, 0, 0),
     XP(P_FMODEL + 1, FM_LADDER, P_GLIDE + 1, 20, P_TRANS + 1, -24)},
    {"WARM PAD", {0, 14, 64, 0, 52, 16, 0, 64}, {64, 90, 120, 90}, 0, 0, FX(0, 0, 0, 0)},
#else
    {"TUBA", {0, 0, 0, 0, 26, 10, 10, 100}, {30, 80, 118, 30}, 30, 1, FX(0, 0, 0, 0), XP(P_FMODEL + 1, FM_LADDER, P_TRANS + 1, -12)},
    {"CELLO", {0, 4, 64, 0, 56, 10, 0, 100}, {50, 80, 120, 50}, 10, 1, FX(0, 0, 0, 0),
     XP(P_TRANS + 1, -12, P_LD_PIT + 1, 1, P_LRATE + 1, 86, P_LFADE + 1, 80)},
#endif
    /* poly: 80s synth brass, strings, a PWM pad, a slow filter-sweep pad */
    {"POLY BRASS", {0, 12, 64, 0, 40, 16, 10, 64}, {16, 80, 100, 36}, 40, 0, FX(0, 0, 0, 0)},
    {"SYN STRINGS", {0, 16, 64, 0, 70, 6, 0, 64}, {60, 90, 120, 86}, 0, 0, FX(0, 0, 0, 0)},
    {"PWM PAD", {4, 12, 64, 0, 64, 8, 0, 64}, {60, 90, 118, 88}, 0, 0, FX(0, 0, 0, 0), XP(P_LRATE + 1, 40, P_LD_SHP + 1, 40)},
    {"SWEEP PAD", {0, 14, 64, 0, 40, 40, 0, 64}, {70, 90, 120, 90}, 0, 0, FX(0, 0, 0, 0), XP(P_LRATE + 1, 20, P_LD_FLT + 1, 30)},
    /* plucks and keys: a resonant synth pluck, a PWM pluck, harpsichord (the patch sheets), poly keys, a unison trance
     * pluck (the groovebox: pizzicato and xylophone) */
    {"SYNTH PLUCK", {0, 8, 64, 0, 40, 40, 0, 80}, {0, 70, 0, 40}, 50, 0, FX(0, 0, 0, 0)},
#if CLIPPER_SYNTH
    {"PWM PLUCK", {4, 8, 64, 0, 56, 30, 0, 80}, {0, 92, 24, 56}, 40, 0, FX(0, 0, 0, 0)},   /* (Stallion) */
#else
    {"PIZZICATO", {0, 6, 64, 0, 60, 10, 0, 90}, {0, 68, 0, 56}, 30, 0, FX(0, 0, 0, 0)},
#endif
    {"HARPSICHORD", {1, 6, 64, 0, 96, 0, 0, 64}, {0, 80, 0, 60}, 20, 0, FX(0, 0, 0, 0)},
#if CLIPPER_SYNTH
    {"POLY KEYS", {1, 6, 64, 0, 58, 20, 10, 64}, {0, 84, 50, 50}, 34, 0, FX(0, 0, 0, 0)},   /* (Stallion) */
#else
    {"XYLOPHONE", {2, 0, 0, 0, 100, 0, 0, 64}, {0, 64, 0, 50}, 0, 0, FX(0, 0, 0, 0)},
#endif
    {"TRANCE PLUCK", {0, 14, 64, 0, 64, 20, 0, 64}, {0, 74, 40, 50}, 50, 0, FX(0, 0, 0, 0), XP(P_VOICE + 1, V_PUNI2, P_DETUNE + 1, 50)},
};

#if CLIPPER_SYNTH
/* (Stallion: no bells, no imitations) the replacements' patching (params.c PATCH_TABLES): movement through the matrix */
static const pcable_t ANC_TOUCH[] = {   /* TOUCH LEAD */
    CB(1, ENV2, CUT, 22),         /* ENV 2 (slow rise, part sustain): the filter opens apart from the level */
    CB(2, VEL, E1AMT, 24), CB(3, DC, E1AMT, -12),    /* the filter envelope: x 0.8 soft .. x 1.2 hard */
    CB(4, AT, CUT, 16),           /* pressure: brighter (and its vibrato: ATDST) */
    CB(5, LFO3, CUT, 4),          /* LFO 3 (0.4 Hz): a slow breath */
};
static const pcable_t ANC_MBASS[] = {   /* MOTION BASS */
    CB(1, LFO2, CUT, 12),         /* LFO 2 (slow): the filter drifts */
    CB(2, LFO3, L2RATE, 16),      /* LFO 3: the drift's speed wanders */
    CB(3, VEL, CUT, 12),          /* velocity: brighter */
    CB(4, ENV2, CUT, 18),         /* ENV 2: a short bite on the attack */
    CB(5, MW, CUT, 20),           /* the wheel: opens it */
};
static const pcable_t ANC_WPAD[] = {    /* WARM PAD */
    CB(1, LFO2, CUT, 10),         /* LFO 2 (~0.6 Hz) and LFO 3 (0.4 Hz): two slow sweeps that never line up */
    CB(2, LFO3, CUT, 8),
    CB(3, LFO3, L2RATE, 14),      /* .. LFO 3 also moves LFO 2's rate */
    CB(4, RAND, PITCH, 2),        /* each note ~2 cents its own */
    CB(5, MW, CUT, 24),           /* the wheel: opens it */
};
static const pcable_t ANC_PWMPL[] = {   /* PWM PLUCK */
    CB(1, ENV2, SHAPE, 24),       /* ENV 2 (short): the width snaps on the attack */
    CB(2, VEL, CUT, 12),          /* velocity: brighter */
    CB(3, VEL, E1AMT, 20),        /* .. and a deeper filter envelope */
    CB(4, LFO2, SHAPE, 10),       /* LFO 2: the width keeps moving in the tail */
    CB(5, MW, CUT, 20),           /* the wheel: opens it */
};
static const pcable_t ANC_PKEYS[] = {   /* POLY KEYS */
    CB(1, VEL, E1AMT, 28), CB(2, DC, E1AMT, -14),    /* the filter envelope: x 0.8 soft .. x 1.2 hard */
    CB(3, VEL, CUT, 8),           /* velocity: brighter */
    CB(4, LFO3, CUT, 4),          /* LFO 3 (0.4 Hz): a slow breath */
    CB(5, AT, CUT, 14),           /* pressure: brighter */
};
static const preset_patch_t ANALOG_PATCHES[] = {
    PPATCH(&ANALOG_PRESETS[19], ANC_TOUCH, P_E2ATK + 1, 24, P_E2DEC + 1, 70, P_E2SUS + 1, 30, P_ATDST + 1, MD_VIB),
    PPATCH(&ANALOG_PRESETS[21], ANC_MBASS, P_L2RATE + 1, 34, P_E2DEC + 1, 40),
    PPATCH(&ANALOG_PRESETS[22], ANC_WPAD, P_L2RATE + 1, 46),
    PPATCH(&ANALOG_PRESETS[28], ANC_PWMPL, P_E2DEC + 1, 40, P_L2RATE + 1, 50),
    PPATCH(&ANALOG_PRESETS[30], ANC_PKEYS),
};
#endif

/* (after Clipper 1.2) UNISON (voice.c unison_spread): both oscillators moved by r (Q32) of a turn of the note,
 * each by its share: as if started that much sooner (osc 2 took its phase from ph[0] before the spread: in phase) */
static void analog_spread(track_t *t, voice_t *v, uint32_t r)
{
    v->ph[0] += r;
    v->ph[1] += uni_turns(r, v->pitch16, t->p[P_E1] * 16 / 100);
}

static const engine_t ENG_ANALOG = {
    "ANALOG", {"OSC", "FLT"},
    {
        {"WAVE", F_ENUM, 0, 4, 0, N_ANALOG_WAVE, 0},
        {"DTN", F_INT, 0, 127, 10, 0, "ct"},
        {"MIX", F_PCT, 0, 127, 64, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"KTR", F_PCT, 0, 127, 64, 0, 0},
    },
    ANALOG_PRESETS, sizeof(ANALOG_PRESETS) / sizeof(ANALOG_PRESETS[0]), 1, analog_note_on, analog_render,
    0xF986, {P_E4, P_E5, P_ATK, P_REL},
};
