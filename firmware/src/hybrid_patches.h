/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (Clipper) */
/* The HYBRID factory patches (eng_hybrid.c) and the presets that play them (one each: the part's amplitude
 * ADSR, voice mode, FX sends and the EDIT values; PTCH = the patch).
 * Scales: cutoff 0..127 = 30 Hz .. 16 kHz (38 ~ 200 Hz, 57 ~ 500 Hz, 71 ~ 1 kHz, 85 ~ 2 kHz, 99 ~ 4 kHz);
 * times 0..127 = 1 ms .. 10 s (32 ~ 10 ms, 54 ~ 50 ms, 64 ~ 100 ms, 78 ~ 300 ms, 95 ~ 1 s, 104 ~ 2 s);
 * LFO rates 0..127 = 0.05 .. 40 Hz (26 ~ 0.2 Hz, 57 ~ 1 Hz, 77 ~ 3 Hz, 87 ~ 5 Hz), synced (HL_SYNC):
 * 21 1/16, 38 1/8, 55 1/4, 90 a bar, 107 two bars; matrix amounts -64..63 (pitch rows: quadratic, 6 ~ a
 * fifth of a semitone at full source, 63 ~ 23 semitones). */
#define HOSC(t, c, f, l, s, fl) {HO_##t, c, f, l, s, fl}
#define HFLT(t, cut, res, env, key, vel) {HF_##t, cut, res, env, key, vel}
#define HLFO(w, r, fl, fd) {HW_##w, r, fl, fd}
#define HMOD(s, v, d, a) {HS_##s, HS_##v, HD_##d, a}
#define HNOOSC {HO_OFF, 0, 0, 0, 0, 0}

static const hy_patch_t HY_FACTORY[] = {
    /* 0 an init patch: one saw, an open low-pass; a start for your own */
    {.osc = {HOSC(SAW, 0, 0, 127, 0, 0), HNOOSC, HNOOSC},
     .drift = 0, .route = HR_SER,
     .flt = {HFLT(LP12, 100, 0, 0, 0, 0), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {0, 64, 64, 64}, .env3 = {0, 64, 64, 64},
     .lfo = {HLFO(SINE, 60, 0, 0), HLFO(SINE, 60, 0, 0)},
     .mod = {HMOD(NONE, NONE, NONE, 0)},
     .bend = 2, .level = 100, .name = "INIT"},
    /* 1 a warm 80s poly pad: two saws a few cents apart, slow filter, LFO 2 on the width of a pulse */
    {.osc = {HOSC(SAW, 0, -6, 100, 0, 0), HOSC(PULSE, 0, 6, 90, 30, 0), HOSC(SAW, -12, 0, 50, 0, 0)},
     .drift = 50, .route = HR_SER,
     .flt = {HFLT(LP12, 60, 25, 25, 20, 0), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {90, 100, 80, 95}, .env3 = {0, 0, 127, 0},
     .lfo = {HLFO(TRI, 40, 0, 0), HLFO(SINE, 20, 0, 0)},
     .mod = {HMOD(LFO2, NONE, SH2, 40), HMOD(LFO3, NONE, CUT, 6), HMOD(M1, NONE, CUT, 40), HMOD(M2, NONE, LV3, 50),
             HMOD(LFO1, MW, PITCH, 6)},
     .bend = 2, .level = 100, .name = "POLY PAD"},
    /* 2 string machine: two pulses, each width moved by its own LFO, a slow attack */
    {.osc = {HOSC(PULSE, 0, -4, 100, 40, 0), HOSC(PULSE, 12, 4, 80, 50, 0), HNOOSC},
     .drift = 40, .route = HR_SER,
     .flt = {HFLT(LP12, 78, 10, 0, 40, 0), HFLT(HP, 30, 0, 0, 0, 0)},
     .env2 = {0, 0, 127, 0}, .env3 = {0, 0, 127, 0},
     .lfo = {HLFO(TRI, 50, 0, 0), HLFO(TRI, 62, 0, 0)},
     .mod = {HMOD(LFO2, NONE, SH1, 45), HMOD(LFO3, NONE, SH2, 45), HMOD(M1, NONE, CUT, 40), HMOD(LFO1, MW, PITCH, 6)},
     .bend = 2, .level = 100, .name = "STRINGS"},
    /* 3 acid: one saw, 24 dB, high resonance, a short envelope; M1 the envelope, M2 the drive */
    {.osc = {HOSC(SAW, 0, 0, 127, 0, HOF_RESET), HNOOSC, HNOOSC},
     .drift = 0, .drive = 30, .route = HR_SER,
     .flt = {HFLT(LP24, 34, 108, 44, 20, 40), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {0, 62, 0, 50}, .env3 = {0, 0, 0, 0},
     .lfo = {HLFO(SINE, 40, 0, 0), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(M1, NONE, CUT, 30), HMOD(M2, NONE, DRIVE, 60), HMOD(ENV2, M1, CUT, 30)},
     .bend = 2, .level = 100, .name = "ACID"},
    /* 4 sync lead: a saw and a slave saw swept by ENV3; the mod wheel adds vibrato */
    {.osc = {HOSC(SAW, 0, 0, 70, 0, 0), HOSC(SYNC, 0, 0, 110, 20, 0), HNOOSC},
     .drift = 20, .route = HR_SER,
     .flt = {HFLT(LP12, 92, 20, 10, 40, 0), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {0, 80, 90, 60}, .env3 = {0, 86, 20, 70},
     .lfo = {HLFO(SINE, 88, HL_KEY, 80), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(ENV3, NONE, SH2, 50), HMOD(LFO2, MW, PITCH, 7), HMOD(M1, NONE, SH2, 50), HMOD(AT, NONE, SH2, 30)},
     .bend = 2, .level = 96, .name = "SYNC LEAD"},
    /* 5 synth clav: a narrow pulse and a thinner one an octave up, a fast resonant low-pass envelope
     * (the clav-like funk comp of 70s and 80s polysynths); M1 opens, M2 narrows the pulse */
    {.osc = {HOSC(PULSE, 0, 0, 124, 50, HOF_RESET), HOSC(PULSE, 12, 3, 50, 80, HOF_RESET), HNOOSC},
     .drift = 10, .route = HR_SER,
     .flt = {HFLT(LP12, 68, 40, 40, 50, 40), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {0, 72, 36, 40}, .env3 = {0, 0, 127, 0},
     .lfo = {HLFO(SINE, 40, 0, 0), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(M1, NONE, CUT, 40), HMOD(VEL, NONE, CUT, 12), HMOD(M2, NONE, SH1, 40)},
     .bend = 2, .level = 124, .name = "PULSE CLAV"},
    /* 6 FM keys: a 1:1 modulator for the body, a bright tine from a 14:1 one on osc 3 */
    {.osc = {HOSC(SINE, 0, 0, 0, 0, HOF_RESET), HOSC(FM, 0, 0, 127, 20, HOF_RESET), HOSC(SINE, 45, 0, 20, 0, HOF_RESET)},
     .drift = 10, .route = HR_SER,
     .flt = {HFLT(LP12, 110, 0, 0, 30, 0), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {0, 0, 127, 0}, .env3 = {0, 92, 30, 80},
     .lfo = {HLFO(SINE, 72, 0, 0), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(ENV3, VEL, SH2, 60), HMOD(ENV3, VEL, LV3, -30), HMOD(LFO2, NONE, AMP, -6), HMOD(M1, NONE, SH2, 40),
             HMOD(M2, NONE, AMP, -50)},
     .bend = 2, .level = 104, .name = "FM KEYS"},
    /* 7 funk bass: a saw and a square an octave down into a resonant 24 dB low-pass, a snappy envelope on it (the
     * 70s synth bass); M1 the cutoff, M2 the resonance */
    {.osc = {HOSC(SAW, 0, 0, 110, 0, HOF_RESET), HOSC(PULSE, -12, 0, 90, 0, HOF_RESET), HNOOSC},
     .drift = 10, .drive = 20, .route = HR_SER,
     .flt = {HFLT(LP24, 32, 70, 50, 30, 40), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {0, 64, 0, 40}, .env3 = {0, 0, 127, 0},
     .lfo = {HLFO(SINE, 40, 0, 0), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(M1, NONE, CUT, 40), HMOD(M2, NONE, RES1, 40), HMOD(VEL, NONE, CUT, 10)},
     .bend = 2, .level = 100, .name = "FUNK BASS"},
    /* 8 reese: two saws slowly beating, a sub, drift; M1 widens the beat */
    {.osc = {HOSC(SAW, 0, -10, 110, 0, 0), HOSC(SAW, 0, 10, 110, 0, 0), HOSC(SINE, -12, 0, 70, 0, HOF_RESET)},
     .drift = 60, .drive = 30, .route = HR_SER,
     .flt = {HFLT(LP24, 56, 30, 0, 30, 0), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {0, 0, 127, 0}, .env3 = {0, 0, 127, 0},
     .lfo = {HLFO(TRI, 22, 0, 0), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(LFO2, NONE, CUT, 6), HMOD(M1, NONE, P2, 5), HMOD(M2, NONE, CUT, 40)},
     .bend = 2, .level = 100, .name = "DRIFT BASS"},
    /* 9 sweep pad: two saws and a pulse, a resonant 24 dB low-pass swept slowly up and down by LFO 2 (the classic
     * analog filter-sweep pad); M1 the cutoff */
    {.osc = {HOSC(SAW, 0, -7, 100, 0, 0), HOSC(SAW, 0, 7, 100, 0, 0), HOSC(PULSE, -12, 0, 60, 40, 0)},
     .drift = 40, .route = HR_SER,
     .flt = {HFLT(LP24, 44, 60, 30, 30, 0), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {96, 104, 100, 100}, .env3 = {0, 0, 127, 0},
     .lfo = {HLFO(TRI, 18, 0, 0), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(LFO2, NONE, CUT, 24), HMOD(M1, NONE, CUT, 40), HMOD(LFO1, MW, PITCH, 6)},
     .bend = 2, .level = 100, .name = "SWEEP PAD"},
    /* 10 brass: two saws, the filter swelling with ENV2, a little pitch rise at the start */
    {.osc = {HOSC(SAW, 0, -5, 110, 0, 0), HOSC(SAW, 0, 5, 110, 0, 0), HNOOSC},
     .drift = 30, .route = HR_SER,
     .flt = {HFLT(LP12, 50, 15, 44, 40, 30), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {60, 80, 90, 70}, .env3 = {0, 50, 0, 50},
     .lfo = {HLFO(SINE, 86, HL_KEY, 90), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(ENV3, NONE, PITCH, -6), HMOD(LFO2, MW, PITCH, 6), HMOD(AT, NONE, CUT, 20), HMOD(M1, NONE, CUT, 40)},
     .bend = 2, .level = 100, .name = "POLY BRASS"},
    /* 11 vox: two band-passes in parallel as formants over a morphing wave; M1 moves the vowel */
    {.osc = {HOSC(MORPH, 0, 0, 120, 80, 0), HOSC(SAW, 0, 7, 60, 0, 0), HOSC(NOISE, 0, 0, 10, 60, 0)},
     .drift = 40, .route = HR_PAR, .fbal = 64,
     .flt = {HFLT(BP, 68, 70, 0, 0, 0), HFLT(BP, 86, 70, 0, 0, 0)},
     .env2 = {0, 0, 127, 0}, .env3 = {0, 0, 127, 0},
     .lfo = {HLFO(SMOOTH, 40, 0, 0), HLFO(SINE, 84, HL_KEY, 90)},
     .mod = {HMOD(M1, NONE, CUT1, 30), HMOD(M1, NONE, CUT2, -24), HMOD(LFO2, NONE, CUT, 4), HMOD(LFO3, NONE, PITCH, 4),
             HMOD(MW, NONE, CUT1, 20)},
     .bend = 2, .level = 112, .name = "VOX"},
    /* 12 pluck: a saw and a triangle, the filter closing fast */
    {.osc = {HOSC(SAW, 0, 0, 110, 0, HOF_RESET), HOSC(TRI, 12, 0, 70, 0, HOF_RESET), HNOOSC},
     .drift = 10, .route = HR_SER,
     .flt = {HFLT(LP12, 56, 30, 50, 50, 40), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {0, 74, 10, 60}, .env3 = {0, 0, 127, 0},
     .lfo = {HLFO(SINE, 40, 0, 0), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(M1, NONE, CUT, 40), HMOD(VEL, NONE, CUT, 16)},
     .bend = 2, .level = 120, .name = "PLUCK"},
    /* 13 combo organ: three squares on the octave, the octave above and the twelfth, an open low-pass (the 60s
     * transistor organ, drawbars as levels); M1 adds the twelfth, the mod wheel a vibrato */
    {.osc = {HOSC(PULSE, 0, 0, 100, 0, HOF_RESET), HOSC(PULSE, 12, 0, 70, 0, HOF_RESET), HOSC(PULSE, 19, 0, 30, 0, HOF_RESET)},
     .drift = 0, .route = HR_SER,
     .flt = {HFLT(LP12, 96, 0, 0, 40, 0), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {0, 0, 127, 0}, .env3 = {0, 0, 127, 0},
     .lfo = {HLFO(SINE, 86, 0, 0), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(M1, NONE, LV3, 40), HMOD(LFO2, MW, PITCH, 6)},
     .bend = 2, .level = 96, .name = "COMBO ORG"},
    /* 14 synth flute: a triangle and a breath of dark noise, a gentle low-pass, a delayed vibrato (the textbook
     * subtractive flute); M1 the breath */
    {.osc = {HOSC(TRI, 0, 0, 124, 0, 0), HOSC(NOISE, 0, 0, 16, 70, 0), HNOOSC},
     .drift = 10, .route = HR_SER,
     .flt = {HFLT(LP12, 76, 10, 14, 60, 20), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {30, 70, 100, 60}, .env3 = {0, 0, 127, 0},
     .lfo = {HLFO(SINE, 82, HL_KEY, 85), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(LFO2, NONE, PITCH, 6), HMOD(M1, NONE, LV2, 40), HMOD(LFO2, MW, PITCH, 6)},
#if CLIPPER_SYNTH                /* (Stallion 1.0, the no-imitations rule: named as the synth lead it is) */
     .bend = 2, .level = 110, .name = "SOFT LEAD"},
#else
     .bend = 2, .level = 110, .name = "SYN FLUTE"},
#endif
    /* 15 pulse bass: a pulse narrowing with the envelope and a sine an octave down, a 24 dB low-pass (the hollow
     * square bass); M1 the cutoff, M2 the width */
    {.osc = {HOSC(PULSE, 0, 0, 110, 30, HOF_RESET), HOSC(SINE, -12, 0, 80, 0, HOF_RESET), HNOOSC},
     .drift = 10, .route = HR_SER,
     .flt = {HFLT(LP24, 44, 30, 36, 30, 20), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {0, 72, 30, 40}, .env3 = {0, 70, 0, 40},
     .lfo = {HLFO(SINE, 40, 0, 0), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(ENV3, NONE, SH1, 30), HMOD(M1, NONE, CUT, 40), HMOD(M2, NONE, SH1, 40)},
     .bend = 2, .level = 100, .name = "PULSE BASS"},
    /* 16 square lead: a square wave, a little filter envelope, a delayed vibrato (the classic monophonic square
     * lead); M1 the cutoff, the mod wheel more vibrato */
    {.osc = {HOSC(PULSE, 0, 0, 120, 0, HOF_RESET), HOSC(PULSE, 0, 5, 50, 20, 0), HNOOSC},
     .drift = 10, .route = HR_SER,
     .flt = {HFLT(LP12, 86, 20, 14, 40, 20), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {0, 80, 90, 50}, .env3 = {0, 0, 127, 0},
     .lfo = {HLFO(SINE, 84, HL_KEY, 85), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(LFO2, NONE, PITCH, 5), HMOD(LFO2, MW, PITCH, 6), HMOD(M1, NONE, CUT, 40)},
     .bend = 2, .level = 96, .name = "SQR LEAD"},
    /* 17 glass pad: an FM sine high above, a slow attack, high-passed */
    {.osc = {HOSC(SINE, 31, 0, 0, 0, 0), HOSC(FM, 0, 0, 110, 30, 0), HOSC(SINE, 12, 6, 50, 0, 0)},
     .drift = 30, .route = HR_SER,
     .flt = {HFLT(HP, 50, 10, 0, 40, 0), HFLT(LP12, 110, 0, 0, 0, 0)},
     .env2 = {0, 0, 127, 0}, .env3 = {100, 110, 60, 100},
     .lfo = {HLFO(SINE, 30, 0, 0), HLFO(TRI, 70, 0, 0)},
     .mod = {HMOD(ENV3, NONE, SH2, 40), HMOD(LFO2, NONE, SH2, 10), HMOD(LFO3, MW, PITCH, 6), HMOD(M1, NONE, SH2, 40)},
     .bend = 2, .level = 104, .name = "GLASS PAD"},
    /* 18 brass stab: two saws a few cents apart and a pulse an octave down, a fast filter envelope with a short
     * decay and a small pitch blip (the 80s synth brass stab); M1 the cutoff */
    {.osc = {HOSC(SAW, 0, -7, 110, 0, 0), HOSC(SAW, 0, 7, 110, 0, 0), HOSC(PULSE, -12, 0, 50, 20, 0)},
     .drift = 30, .route = HR_SER,
     .flt = {HFLT(LP12, 44, 14, 50, 40, 30), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {6, 72, 40, 50}, .env3 = {0, 46, 0, 40},
     .lfo = {HLFO(SINE, 40, 0, 0), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(ENV3, NONE, PITCH, -5), HMOD(M1, NONE, CUT, 40), HMOD(VEL, NONE, CUT, 12)},
     .bend = 2, .level = 100, .name = "BRASS STAB"},
    /* 19 808 sub: a sine with a little feedback, a pitch that falls into place */
    {.osc = {HOSC(SINE, 0, 0, 127, 10, HOF_RESET), HNOOSC, HNOOSC},
     .drift = 0, .drive = 30, .route = HR_SER,
     .flt = {HFLT(OFF, 0, 0, 0, 0, 0), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {0, 0, 127, 0}, .env3 = {0, 60, 0, 40},
     .lfo = {HLFO(SINE, 40, 0, 0), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(ENV3, NONE, PITCH, 14), HMOD(M1, NONE, DRIVE, 60), HMOD(VEL, NONE, SH1, 30)},
     .bend = 12, .level = 110, .name = "BOOM SUB"},
    /* 20 supersaw stack for trance: one big SUPER, chorus and reverb sends in the preset */
    {.osc = {HOSC(SUPER, 0, 0, 127, 80, 0), HOSC(SUPER, 12, 0, 50, 70, 0), HNOOSC},
     .drift = 20, .route = HR_SER,
     .flt = {HFLT(LP12, 104, 10, 0, 20, 0), HFLT(HP, 20, 0, 0, 0, 0)},
     .env2 = {0, 0, 127, 0}, .env3 = {0, 0, 127, 0},
     .lfo = {HLFO(SINE, 40, 0, 0), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(M1, NONE, SH1, 50), HMOD(M1, NONE, SH2, 50), HMOD(M2, NONE, CUT, -50), HMOD(LFO1, MW, PITCH, 6)},
     .bend = 2, .level = 92, .name = "TRANCE"},
    /* 21 a short supersaw pluck for arpeggios */
    {.osc = {HOSC(SUPER, 0, 0, 127, 60, 0), HNOOSC, HNOOSC},
     .drift = 10, .route = HR_SER,
     .flt = {HFLT(LP24, 54, 20, 44, 40, 30), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {0, 78, 16, 60}, .env3 = {0, 0, 127, 0},
     .lfo = {HLFO(SINE, 40, 0, 0), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(M1, NONE, CUT, 40), HMOD(M2, NONE, SH1, 50)},
     .bend = 2, .level = 100, .name = "SAW PLUCK"},
    /* 22 poly keys: a saw and a pulse a few cents apart, a 12 dB low-pass closing to a sustain (the comping sound
     * of the 80s analog polys); M1 the cutoff */
    {.osc = {HOSC(SAW, 0, -4, 110, 0, 0), HOSC(PULSE, 0, 4, 90, 30, 0), HNOOSC},
     .drift = 30, .route = HR_SER,
     .flt = {HFLT(LP12, 56, 20, 40, 50, 40), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {0, 82, 30, 60}, .env3 = {0, 0, 127, 0},
     .lfo = {HLFO(SINE, 40, 0, 0), HLFO(SINE, 40, 0, 0)},
     .mod = {HMOD(M1, NONE, CUT, 40), HMOD(LFO1, MW, PITCH, 6)},
     .bend = 2, .level = 104, .name = "POLY KEYS"},
    /* 23 STALLION, the product's own sound (power-on): two supersaws an octave apart and a sine sub, a 24 dB sweep; M1 opens, M2 spreads */
    {.osc = {HOSC(SUPER, 0, 0, 110, 70, 0), HOSC(SUPER, 12, 4, 70, 60, 0), HOSC(SINE, -12, 0, 80, 0, HOF_RESET)},
     .drift = 30, .route = HR_SER,
     .flt = {HFLT(LP24, 58, 40, 40, 32, 30), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {2, 90, 50, 80}, .env3 = {0, 70, 0, 60},
     .lfo = {HLFO(TRI, 30, 0, 0), HLFO(SINE, 87, HL_KEY, 70)},
     .mod = {HMOD(M1, NONE, CUT, 50), HMOD(M2, NONE, SH1, 50), HMOD(M2, NONE, SH2, 50), HMOD(LFO2, MW, PITCH, 7),
             HMOD(LFO2, NONE, CUT, 4), HMOD(VEL, NONE, CUT, 12), HMOD(AT, NONE, CUT, 20)},
     .bend = 2, .level = 96, .name = "STALLION"},
    /* 24 a classic mono bass: two saws an octave apart and a square under them into the ladder, a snappy
     * envelope on it; M1 the cutoff, M2 the resonance */
    {.osc = {HOSC(SAW, 0, 0, 110, 0, HOF_RESET), HOSC(SAW, -12, 4, 90, 0, HOF_RESET), HOSC(PULSE, -12, 0, 60, 0, HOF_RESET)},
     .drift = 20, .route = HR_SER,
     .flt = {HFLT(LADDER, 38, 50, 48, 32, 30), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {0, 66, 24, 50}, .env3 = {0, 64, 64, 64},
     .lfo = {HLFO(SINE, 60, 0, 0), HLFO(SINE, 60, 0, 0)},
     .mod = {HMOD(M1, NONE, CUT, 50), HMOD(M2, NONE, RES1, 50), HMOD(VEL, NONE, CUT, 10), HMOD(AT, NONE, CUT, 16)},
     .drive = 30, .bend = 2, .level = 100, .name = "LADDR BASS"},
    /* 25 a singing lead: two saws a few cents apart and a triangle an octave up, the ladder near its edge, a
     * delayed vibrato on the mod wheel */
    {.osc = {HOSC(SAW, 0, -5, 110, 0, 0), HOSC(SAW, 0, 6, 100, 0, 0), HOSC(TRI, 12, 0, 50, 0, 0)},
     .drift = 30, .route = HR_SER,
     .flt = {HFLT(LADDER, 62, 84, 30, 48, 20), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {4, 80, 70, 60}, .env3 = {0, 64, 64, 64},
     .lfo = {HLFO(SINE, 82, HL_KEY, 70), HLFO(SINE, 60, 0, 0)},
     .mod = {HMOD(LFO2, MW, PITCH, 7), HMOD(M1, NONE, CUT, 50), HMOD(M2, NONE, RES1, 40), HMOD(AT, NONE, CUT, 20)},
     .drive = 20, .bend = 2, .level = 100, .name = "LADDR LEAD"},
    /* 26 a slow ladder pad: two pulses and a saw, the ladder opening over seconds and breathing with LFO 3 */
    {.osc = {HOSC(PULSE, 0, -6, 100, 30, 0), HOSC(PULSE, 0, 6, 100, 50, 0), HOSC(SAW, -12, 0, 60, 0, 0)},
     .drift = 50, .route = HR_SER,
     .flt = {HFLT(LADDER, 48, 60, 36, 24, 0), HFLT(OFF, 0, 0, 0, 0, 0)},
     .env2 = {100, 104, 90, 100}, .env3 = {0, 64, 64, 64},
     .lfo = {HLFO(TRI, 30, 0, 0), HLFO(SINE, 22, 0, 0)},
     .mod = {HMOD(LFO2, NONE, SH1, 30), HMOD(LFO3, NONE, CUT, 8), HMOD(M1, NONE, CUT, 40), HMOD(M2, NONE, RES1, 40)},
     .bend = 2, .level = 100, .name = "LADDER PAD"},
};
#define HY_INIT 0u               /* the INIT patch (a start for your own) */
#define HY_NFACTORY ((uint32_t)NELEM(HY_FACTORY))
static const char *const N_HY_PATCH[] = {"INIT", "POLY PAD", "STRINGS", "ACID", "SYNC LEAD", "PULSE CLAV",
                                         "FM KEYS", "FUNK BASS", "DRIFT BASS", "SWEEP PAD", "POLY BRASS", "VOX",
#if CLIPPER_SYNTH
                                         "PLUCK", "COMBO ORG", "SOFT LEAD", "PULSE BASS", "SQR LEAD", "GLASS PAD",
#else
                                         "PLUCK", "COMBO ORG", "SYN FLUTE", "PULSE BASS", "SQR LEAD", "GLASS PAD",
#endif
                                         "BRASS STAB", "BOOM SUB", "TRANCE", "SAW PLUCK", "POLY KEYS", "STALLION",
                                         "LADDR BASS", "LADDR LEAD", "LADDER PAD"};
_Static_assert(NELEM(N_HY_PATCH) == NELEM(HY_FACTORY), "a name per factory patch");

/* the presets: {CUT, RES, FENV, PTCH, M1..M4}, {A D S R}, ENV -> FILTER (the part's), mono */
#define HYP(name, i, m1, m2, a, d, s, r, mono, ...) \
    {name, {0, 0, 0, i, m1, m2, 0, 0}, {a, d, s, r}, 0, mono, __VA_ARGS__}
static const preset_t HY_PRESETS[] = {
    HYP("INIT", 0, 0, 0, 0, 64, 110, 40, 0, FX(0, 0, 0, 0)),
    HYP("POLY PAD", 1, 64, 0, 70, 100, 110, 92, 0, FX(0, 0, 0, 0)),
    HYP("STRINGS", 2, 64, 0, 80, 100, 120, 90, 0, FX(0, 0, 0, 0)),
    HYP("ACID", 3, 60, 30, 0, 70, 90, 30, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 30, P_GLMODE + 1, 1, P_TRANS + 1, -12)),
    HYP("SYNC LEAD", 4, 30, 0, 2, 80, 110, 50, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 34)),
    HYP("PULSE CLAV", 5, 50, 0, 0, 74, 40, 30, 0, FX(0, 0, 0, 0)),
    HYP("FM KEYS", 6, 50, 0, 0, 100, 60, 70, 0, FX(0, 0, 0, 0)),
    HYP("FUNK BASS", 7, 50, 30, 0, 70, 100, 30, 1, FX(0, 0, 0, 0), XP(P_TRANS + 1, -12, P_GLIDE + 1, 20)),
    HYP("DRIFT BASS", 8, 40, 50, 0, 60, 127, 40, 1, FX(0, 0, 0, 0), XP(P_TRANS + 1, -24)),
    HYP("SWEEP PAD", 9, 64, 0, 60, 100, 115, 95, 0, FX(0, 0, 0, 0)),
    HYP("POLY BRASS", 10, 60, 0, 30, 80, 110, 50, 0, FX(0, 0, 0, 0)),
    HYP("VOX", 11, 64, 0, 60, 90, 120, 80, 0, FX(0, 0, 0, 0)),
    HYP("PLUCK", 12, 50, 0, 0, 82, 30, 64, 0, FX(0, 0, 0, 0)),
    HYP("COMBO ORGAN", 13, 50, 0, 0, 64, 127, 20, 0, FX(0, 0, 0, 0)),
#if CLIPPER_SYNTH
    HYP("SOFT LEAD", 14, 40, 0, 30, 64, 120, 40, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 16)),   /* (Stallion 1.0: renamed) */
#else
    HYP("SYN FLUTE", 14, 40, 0, 30, 64, 120, 40, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 16)),
#endif
    HYP("PULSE BASS", 15, 40, 30, 0, 70, 100, 30, 1, FX(0, 0, 0, 0), XP(P_TRANS + 1, -12)),
    HYP("SQUARE LEAD", 16, 50, 0, 0, 70, 115, 40, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 24)),
    HYP("GLASS PAD", 17, 50, 0, 90, 110, 110, 100, 0, FX(0, 0, 0, 0)),
    HYP("BRASS STAB", 18, 50, 0, 2, 72, 60, 40, 0, FX(0, 0, 0, 0)),
    HYP("BOOM SUB", 19, 30, 0, 0, 104, 0, 60, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 56, P_GLMODE + 1, 1, P_TRANS + 1, -12)),
    HYP("TRANCE", 20, 60, 30, 4, 80, 115, 50, 0, FX(0, 0, 0, 0)),
    HYP("SAW PLUCK", 21, 50, 50, 0, 82, 30, 60, 0, FX(0, 0, 0, 0)),
    HYP("POLY KEYS", 22, 50, 0, 0, 84, 70, 60, 0, FX(0, 0, 0, 0)),
    HYP("STALLION", 23, 70, 50, 2, 90, 110, 70, 0, FX(0, 0, 0, 0)),
    HYP("LADDER BASS", 24, 40, 30, 0, 72, 90, 34, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 24, P_GLMODE + 1, 1, P_TRANS + 1, -12)),
    HYP("LADDER LEAD", 25, 50, 40, 2, 80, 110, 50, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 36)),
    HYP("LADDER PAD", 26, 50, 30, 80, 100, 115, 95, 0, FX(0, 0, 0, 0)),
#if CLIPPER_SYNTH
    /* the factory patches played another way (their macros, the envelope, the voice) */
    HYP("DARK PAD", 1, 20, 0, 90, 100, 115, 100, 0, FX(0, 0, 0, 0)),
    HYP("ACID HARD", 3, 100, 90, 0, 70, 90, 30, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 30, P_GLMODE + 1, 1, P_TRANS + 1, -12)),
    HYP("SOFT SYNC", 4, 70, 0, 10, 80, 110, 60, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 40)),
    HYP("SOFT BRASS", 10, 30, 0, 50, 90, 110, 70, 0, FX(0, 0, 0, 0)),
    HYP("TRANCE LEAD", 20, 70, 20, 2, 80, 110, 40, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 20)),
#endif
};
_Static_assert(NELEM(HY_PRESETS) >= NELEM(HY_FACTORY), "a preset per factory patch (Clipper: and more)");
