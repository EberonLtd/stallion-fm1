/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* PONY's controls after the instrument it follows (firmware/src/eng_pony.c; EDIT 3 RANGE KYBD VCA CHAR, EDIT 1 WAVE
 * OFF, FX > FILTER > MODL):
 *   build/host/pony_test
 * 1. the defaults play as before, sample for sample: a copy of the render before these controls (old_pony below) on
 *    every factory preset's values, several notes, the knob still (STEPS OFF starts at its target)
 * 2. RANGE: 16' plays the DCO at half the increment, 4' at twice, exactly (the same samples as 8' an octave away);
 *    through the whole voice, note 60 at 16' / 8' / 4' measures an octave apart
 * 3. WAVE OFF: SUB alone is a square an octave down (no energy at the note, its odd harmonics there), SUB and NOIS 0
 *    silence; at RES 127 the filter sings alone, in tune with KYBD 100 (t_sing)
 * 4. KYBD: 0 no tracking, the default the old half (d << 3) for every note, 100 one octave of cutoff an octave
 * 5. MODL: LADDR is Clipper's PONY filter (its ladder, >> 1) sample for sample; OTA (the default) as now; PONY's
 *    list shows OTA first
 * 6. VCA GATE: the amp on within 3 ms of the key, off within 4 ms of its release whatever the envelope, the steps no
 *    bigger than the held sound's (no click); ENV as before
 * 7. STEPS: ON the old code under a moving CUT, sample for sample; OFF a short glide (the target within 10 ms, values
 *    between the steps, no plateau while the knob moves a step a block)
 * 8. stored: a project (the autosave) and a user preset keep RANGE KYBD VCA CHAR of a PONY sound (CHAR's ten bits:
 *    both bytes); a project block written before them (MARE's default bytes) loads PONY's defaults; MARE's VCO3 VCO4
 *    WAV34 kept as before
 * 9. CHAR (core.h JQ_*): CLEAN is the render as before (1., 5. above run CLEAN); the sets and the device's names; the
 *    factory sounds CLASSIC (three with STEPS); each quirk on against off: HISS (in the silence after a chord, ~63 dB
 *    under it, L and R apart; off, or the chorus off: silence), HPF (the voice's own passed over; the part's shelf and
 *    high-passes), LEVER, LFO (hold-off, straight fade, free-running), GATE (1 ms), VCA (the curve), NOISE (darker),
 *    COARS (0.11-octave steps), TICK (~4.2 ms)
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>
#define PROJ_HOST 1
static const uint8_t TRK_DEF[NPART][2] = {{ENGI_HYBRID, 0}, {1, 0}, {4, 5}};   /* (ui.c, Clipper) */
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? TRK_DEF[i][0] : 0u; }
#include "../firmware/src/project.c"
#define UP_HOST 1
#include "../firmware/src/upreset.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("pony: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* the render before RANGE, WAVE OFF, KYBD, MODL and STEPS (eng_pony.c as of 50d1890), FILTER 2 off, with the
 * character pass after the instrument (pony-character: the pulse 48.5 % .. ~11 %, a shape below the
 * square folded, the OTA's passband held as the resonance rises); ladder 1: the filter as Clipper's PONY had it
 * before f59ad57 (ladder_coef, ladder_tick >> 1) */
static void old_pony(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m, int ladder)
{
    const int16_t *ev = vm_ev(t, m);
    uint32_t i, ph = v->ph[0], fl = v->ph[1] & 0x80000000u, nz = v->ph[2], inc = m->inc, incs = inc >> 1;
    uint32_t wave = (uint32_t)clamp(ev[0], 0, 2), hpf = (uint32_t)clamp(ev[4], 0, 3);
    int32_t w = abs(ev[1] * 256 + (m->shape - (64 << 8)) * 2), xo = 1024;
    uint32_t pw = PAL_SQ - (uint32_t)(w < 127 * 256 ? w : 127 * 256) * PAL_NARROW;
    int32_t sub = ev[2] * 200, nois = ev[3] * 160, hp = v->s[4], bb = v->s[5];
    int32_t cut = (ev[5] << 8) + m->cutoff + ((v->pitch16 - 60 * 16) << 3);
    int32_t ls[8] = {v->s[0], v->s[1], v->s[2], v->s[3], v->fs[0], v->fs[1], v->fs[2], v->fs[3]};
    int32_t sl = fm_slope(t);
    fmod_t fm;
    ladder_t lad;
    fmod_coef(&fm, FM_OTA, cut, ev[6]);
    if (cut < 100 << 8)
        pony_ota_g(&fm.lad, cut);
    fmod_slope(&fm, sl);
    ladder_coef(&lad, cut, ev[6]);
    if (!ladder)
        xo = 1024 + ((fm.lad.k * 3) >> 5);              /* x (1 + 3k / 8): the passband held */
    for (i = 0; i < n; i++) {
        int32_t s, y, a;
        if (wave == JW_SAW)
            s = osc_saw(ph, inc);
        else if (wave == JW_PLS)
            s = osc_pulse(ph, inc, pw);
        else
            s = (osc_saw(ph, inc) + osc_pulse(ph, inc, pw)) * 5 >> 3;
        if (sub)
            s += mulq15(osc_pulse(fl | ph >> 1, incs, 0x80000000u), sub);
        if (nois) {
            nz = nz * 1664525u + 1013904223u;
            s += mulq15((int32_t)(nz >> 16) - 32768, nois);
        }
        ph += inc;
        if (ph < inc)
            fl ^= 0x80000000u;
        s = s * 5 >> 3;
        if (hpf != 1u) {
            if (!hpf) {
                bb += ((s - bb) * 560) >> 15;
                s += bb >> 1;
            } else {
                hp += ((s - hp) * (hpf == 2u ? 1120 : 3360)) >> 15;
                s -= hp;
            }
        }
        if (ladder) {
            y = ladder_tick(&lad, s, ls) >> 1;
        } else {
            y = ota_tick(&fm.lad, s, ls, sl == FS_12);
            if (sl == FS_48)
                y = pole4_tick(&fm.x4, y, ls + 4);
            y = (y * xo) >> 11;
        }
        a = y < 0 ? -y : y;
        if (a > 16000) {
            a = 16000 + (softclip((a - 16000) * 2) >> 1);
            y = y < 0 ? -a : a;
        }
        out[i] += mulq15(mulq15(y << 1, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph;
    v->ph[1] = fl;
    v->ph[2] = nz;
    v->s[0] = ls[0], v->s[1] = ls[1], v->s[2] = ls[2], v->s[3] = ls[3];
    v->fs[0] = ls[4], v->fs[1] = ls[5], v->fs[2] = ls[6], v->fs[3] = ls[7];
    v->s[4] = hp;
    v->s[5] = bb;
}

/* one voice straight through the render (no allocation, no envelope): which 0 the new render, 1 the old OTA one, 2
 * the old ladder; cuts[b] (if given) the CUT knob at block b; the voice started as a note from silence */
static void direct(track_t *t, int which, int32_t *out, uint32_t n, int32_t p16, uint32_t inc, const int16_t *cuts)
{
    voice_t v;
    vmod_t m;
    uint32_t b;
    memset(&v, 0, sizeof v);
    memset(&m, 0, sizeof m);
    v.note = (uint8_t)(p16 / 16);
    v.pitch16 = p16;
    m.inc = inc;
    m.pitch16 = p16;
    m.amp0 = m.amp1 = 32767;
    m.shape = 64 << 8;
    m.cutoff = 900;
    memset(out, 0, n * sizeof *out);
    if (cuts)
        t->p[P_E5] = cuts[0];
    pony_note_on(t, &v);
    for (b = 0; b + CTL <= n; b += CTL) {
        if (cuts)
            t->p[P_E5] = cuts[b / CTL];
        if (which)
            old_pony(t, &v, out + b, CTL, &m, which == 2);
        else
            pony_render(t, &v, out + b, CTL, &m);
    }
}
static double mag_of(const int32_t *b, uint32_t n, double f)
{
    double re = 0, im = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / n);
        re += b[i] * w * cos(2 * M_PI * f * i / FS);
        im += b[i] * w * sin(2 * M_PI * f * i / FS);
    }
    return sqrt(re * re + im * im) / n;
}
static double freq_of(const int32_t *b, uint32_t n)   /* rising zero crossings, interpolated */
{
    uint32_t i;
    double first = -1, last = -1;
    int k = 0;
    for (i = 1; i < n; i++)
        if (b[i - 1] < 0 && b[i] >= 0) {
            double x = i - 1 + (double)-b[i - 1] / (double)(b[i] - b[i - 1]);
            if (first < 0)
                first = x;
            else
                k++;
            last = x;
        }
    return k > 0 ? k * (double)FS / (last - first) : 0;
}
static void pony_defaults(track_t *t)
{
    t->p[P_JRNG] = TP[P_JRNG].def, t->p[P_JKYB] = TP[P_JKYB].def;
    t->p[P_JVCA] = TP[P_JVCA].def, t->p[P_JCHR] = 0;   /* (CHAR CLEAN: the render as before, the quirks below) */
    t->p[P_FMODEL] = TP[P_FMODEL].def, t->p[P_FSLOPE] = TP[P_FSLOPE].def, t->p[P_F2MODEL] = 0;
    t->fm_eff = 0, t->fx_eff = 0;
}

#define NB 8192u
static int32_t a[NB], b[NB];

static void t_defaults(void)
{
    track_t *t = &trk[0];
    uint32_t pi, k, diff = 0, runs = 0;
    static const int32_t notes[] = {28, 45, 60, 72, 96};
    char w[160];
    check("defaults: RANGE 8', KYBD 43 %, VCA ENV, CHAR CLASSIC, MODL OTA (PONY's first), WAVE has OFF last",
          TP[P_JRNG].def == 1 && TP[P_JKYB].def == 43 && TP[P_JVCA].def == 0 && TP[P_JCHR].def == (int16_t)JQ_CLASSIC &&
          ENG_PONY.edit[0].max == 3 && !strcmp(N_JWAVE[3], "OFF") && !strcmp(N_JWAVE[2], "S+P"));
    for (pi = 0; pi < ENG_PONY.npresets; pi++)
        for (k = 0; k < NELEM(notes); k++) {
            host_preset(t, ENGI_PONY, pi);
            pony_defaults(t);
            if (t->p[P_E0] == JW_OFF)                    /* (WHISTLE: the old render had no WAVE OFF; its RES 125 */
                t->p[P_E0] = JW_SP, t->p[P_E6] = 100;    /* sings from a seed the old render had not) */
            t->p[P_JCHR] = (uint8_t)ENG_PONY.presets[pi].x[0] == P_JCHR + 1 ? JQ_STEPS : 0;   /* (a preset's own STEPS: the
                                                                   * same; CLEAN: no other quirk) */
            direct(t, 0, a, 4096, notes[k] * 16, pitch_inc((uint32_t)notes[k] * 16), 0);
            t->p[P_JCHR] = 0;
            direct(t, 1, b, 4096, notes[k] * 16, pitch_inc((uint32_t)notes[k] * 16), 0);
            diff += memcmp(a, b, 4096 * sizeof *a) != 0;
            runs++;
        }
    snprintf(w, sizeof w, "defaults: every factory preset's values, 5 notes, as the old render sample for sample (%u of %u differ)",
             diff, runs);
    check(w, !diff);
}

static void t_range(void)
{
    track_t *t = &trk[0];
    uint32_t inc = pitch_inc(60 * 16);
    int same16, same4;
    char w[160];
    host_preset(t, ENGI_PONY, 0);
    pony_defaults(t);
    t->p[P_E0] = JW_SP, t->p[P_E2] = 60, t->p[P_E5] = 90;
    t->p[P_JRNG] = 0;                                    /* 16' at twice the increment = 8' at the increment */
    direct(t, 0, a, NB, 60 * 16, inc * 2u, 0);
    t->p[P_JRNG] = 1;
    direct(t, 0, b, NB, 60 * 16, inc, 0);
    same16 = !memcmp(a, b, sizeof a);
    t->p[P_JRNG] = 2;                                    /* 4' at the increment = 8' at twice it */
    direct(t, 0, a, NB, 60 * 16, inc, 0);
    t->p[P_JRNG] = 1;
    direct(t, 0, b, NB, 60 * 16, inc * 2u, 0);
    same4 = !memcmp(a, b, sizeof a);
    check("RANGE: 16' is the DCO at half its increment, 4' at twice, exactly (8' an octave away, sample for sample)",
          same16 && same4);
    {   /* the pitch measured: a sine-like sound (SUB 0, a low cutoff) at note 57 (220 Hz) */
        double f[3];
        uint32_t r;
        t->p[P_E0] = JW_SAW, t->p[P_E2] = 0, t->p[P_E5] = 40, t->p[P_E6] = 0;
        t->p[P_JKYB] = 0;
        for (r = 0; r < 3u; r++) {
            t->p[P_JRNG] = (int16_t)r;
            direct(t, 0, a, NB, 57 * 16, pitch_inc(57 * 16), 0);
            f[r] = freq_of(a + 2048, NB - 2048);
        }
        snprintf(w, sizeof w, "RANGE: note 57 at 16' %.2f Hz, 8' %.2f Hz, 4' %.2f Hz (an octave apart)", f[0], f[1], f[2]);
        check(w, fabs(f[1] / f[0] - 2.0) < 0.002 && fabs(f[2] / f[1] - 2.0) < 0.002 && fabs(f[1] - 220.0) < 1.0);
    }
}

static void t_wave_off(void)
{
    track_t *t = &trk[0];
    double f0 = 440.0, msub, mnote, m3;
    uint32_t i;
    int silent = 1;
    char w[160];
    host_preset(t, ENGI_PONY, 0);
    pony_defaults(t);
    t->p[P_E0] = 3, t->p[P_E2] = 127, t->p[P_E3] = 0, t->p[P_E4] = 1, t->p[P_E5] = 127, t->p[P_E6] = 0;
    t->p[P_JKYB] = 0;
    direct(t, 0, a, NB, 69 * 16, pitch_inc(69 * 16), 0);
    msub = mag_of(a + 4096, 4096, f0 / 2), mnote = mag_of(a + 4096, 4096, f0), m3 = mag_of(a + 4096, 4096, f0 * 1.5);
    snprintf(w, sizeof w, "WAVE OFF, SUB alone: a square an octave down (note %+.1f dB, 3rd harmonic %+.1f dB vs its fundamental)",
             20 * log10(mnote / msub), 20 * log10(m3 / msub));
    check(w, msub > 100 && mnote / msub < 0.01 && fabs(20 * log10(m3 / msub) + 9.5) < 2.0);
    t->p[P_E2] = 0;
    direct(t, 0, a, NB, 69 * 16, pitch_inc(69 * 16), 0);
    for (i = 0; i < NB; i++)
        silent &= a[i] == 0;
    check("WAVE OFF with SUB 0 and NOIS 0: silence (every sample 0)", silent);
}

/* (pony-character) the filter as an oscillator, as the instrument's: WAVE OFF, SUB and NOIS 0, KYBD 100, RES 127: it
 * sings by itself (eng_pony.c JF_SEED), within 5 ms of the key, a steady tone in tune with the keys (C2 .. C6 within
 * 6 cents of the C4-anchored octaves, pony_ota_g), no step at its start bigger than its own; louder than a saw at RES 0
 * by no more than the instrument's ~+9 dB plus 5 (peaks; its RMS ~+11); RES 100 and RES 0 with no source: silence (every sample 0) */
static void t_sing(void)
{
    track_t *t = &trk[0];
    static const int notes[] = {36, 48, 60, 72, 84};
    double f[5], worst = 0, saw = 0;
    uint32_t i, k, late = 0;
    int32_t pk = 0, held, onset_ok = 1, click = 0, silent = 1;
    char w[200];
    host_preset(t, ENGI_PONY, 0);
    pony_defaults(t);
    t->p[P_E0] = 0, t->p[P_E2] = 0, t->p[P_E3] = 0, t->p[P_E4] = 1, t->p[P_E5] = 60, t->p[P_E6] = 0;
    t->p[P_JKYB] = 100;
    direct(t, 0, a, NB, 60 * 16, pitch_inc(60 * 16), 0);
    for (i = 2048; i < NB; i++)
        saw = abs(a[i]) > saw ? abs(a[i]) : saw;
    t->p[P_E0] = 3;
    for (k = 0; k < 2; k++) {                           /* RES 0, 100: no source, nothing */
        t->p[P_E6] = k ? 100 : 0;
        direct(t, 0, a, NB, 60 * 16, pitch_inc(60 * 16), 0);
        for (i = 0; i < NB; i++)
            silent &= a[i] == 0;
    }
    check("WAVE OFF, SUB and NOIS 0, RES 0 and 100 (the filter short of singing): silence (every sample 0)", silent);
    t->p[P_E6] = 127;
    for (k = 0; k < 5; k++) {
        int32_t dmax = 0, dheld = 0;
        uint32_t on = NB;
        direct(t, 0, a, NB, notes[k] * 16, pitch_inc((uint32_t)notes[k] * 16), 0);
        held = 0;
        for (i = NB / 2; i < NB; i++) {
            held = abs(a[i]) > held ? abs(a[i]) : held;
            dheld = abs(a[i] - a[i - 1]) > dheld ? abs(a[i] - a[i - 1]) : dheld;
        }
        for (i = 1; i < NB / 2; i++) {
            if (on == NB && abs(a[i]) > held / 4)
                on = i;
            dmax = abs(a[i] - a[i - 1]) > dmax ? abs(a[i] - a[i - 1]) : dmax;
        }
        pk = held > pk ? held : pk;
        f[k] = freq_of(a + NB / 2, NB / 2);
        onset_ok &= on * 1000.0 / FS <= 5.0 && held > 1000;
        late = on > late ? on : late;
        click |= dmax > dheld * 3 / 2 + 64;
    }
    for (k = 0; k < 5; k++) {
        double c = 1200.0 * log2(f[k] / (f[2] * pow(2.0, (notes[k] - 60) / 12.0)));
        worst = fabs(c) > fabs(worst) ? c : worst;
    }
    snprintf(w, sizeof w, "the filter sings alone (RES 127, KYBD 100): C2 .. C6 %.1f %.1f %.1f %.1f %.1f Hz, worst %+.1f cents "
             "off the octaves", f[0], f[1], f[2], f[3], f[4], worst);
    check(w, fabs(worst) < 6.0 && f[2] > 100.0);
    snprintf(w, sizeof w, "the filter sings alone: sounding within %.1f ms of the key (5), no step at its start bigger than "
             "its own (no click)", late * 1000.0 / FS);   /* (sounding: a quarter of its held peak) */
    check(w, onset_ok && !click);
    snprintf(w, sizeof w, "the filter sings alone: its peak %+.1f dB over a saw's at RES 0 (the instrument's ~+9)",
           20 * log10(pk / saw));
    check(w, pk < saw * 5.0 && pk > saw);
}

static void t_kybd(void)
{
    int32_t d, ok0 = 1, okd = 1;
    double oct;
    char w[160];
    for (d = -1200; d <= 1200; d++) {
        ok0 &= pony_kt(d, 0) == 0;
        okd &= pony_kt(d, TP[P_JKYB].def) == d * 8;     /* (the old (v->pitch16 - 60 * 16) << 3) */
    }
    check("KYBD 0: no tracking, any note", ok0);
    check("KYBD default: the old half tracking (<< 3) for every note, exactly", okd);
    oct = pony_kt(192, 100) / 256.0 / (127.0 / log2(16000.0 / 30.0));   /* cutoff steps (CUTOFF_HZ) an octave */
    snprintf(w, sizeof w, "KYBD 100: %.3f octaves of cutoff an octave of the keys (full tracking)", oct);
    check(w, fabs(oct - 1.0) < 0.01);
    {   /* through the render: KYBD 0, the cutoff no longer follows the note (note 72's brightness at 48 the same) */
        track_t *t = &trk[0];
        host_preset(t, ENGI_PONY, 0);
        pony_defaults(t);
        t->p[P_E0] = JW_SAW, t->p[P_E5] = 60, t->p[P_E6] = 0, t->p[P_JKYB] = 0, t->p[P_JRNG] = 0;
        direct(t, 0, a, 4096, 72 * 16, pitch_inc(72 * 16), 0);   /* 16' at note 72: the DCO of note 60 */
        t->p[P_JRNG] = 1;
        direct(t, 0, b, 4096, 60 * 16, pitch_inc(60 * 16), 0);
        check("KYBD 0: the same DCO, notes 72 and 60, the same filter (sample for sample)", !memcmp(a, b, 4096 * sizeof *a));
    }
}

static void t_modl(void)
{
    track_t *t = &trk[0];
    uint32_t pi, diff = 0, runs = 0;
    char w[160];
    const param_desc_t *d;
    host_preset(t, ENGI_PONY, 0);
    pony_defaults(t);
    t->eng_req = ENGI_PONY;
    d = track_desc(t, P_FMODEL);
    check("MODL on PONY: OTA its first (the stored default), LADDR second, SVF last", d->def == FM_SVF &&
          !strcmp(d->names[FM_SVF - d->min], "OTA") && !strcmp(d->names[FM_LADDER - d->min], "LADDR") &&
          !strcmp(d->names[FM_OTA - d->min], "SVF") && pony_model(t) == FM_OTA);
    for (pi = 0; pi < ENG_PONY.npresets; pi++) {
        host_preset(t, ENGI_PONY, pi);
        pony_defaults(t);
        t->p[P_FMODEL] = FM_LADDER;
        t->p[P_E6] = (int16_t)(pi * 4 % 128);            /* (resonances over the range) */
        if (t->p[P_E0] == JW_OFF)
            t->p[P_E0] = JW_SP;                          /* (WHISTLE: the old render had no WAVE OFF) */
        direct(t, 0, a, 4096, (36 + pi) * 16, pitch_inc((36 + pi) * 16), 0);
        direct(t, 2, b, 4096, (36 + pi) * 16, pitch_inc((36 + pi) * 16), 0);
        diff += memcmp(a, b, 4096 * sizeof *a) != 0;
        runs++;
    }
    snprintf(w, sizeof w, "MODL LADDR: Clipper's PONY ladder (ladder_tick >> 1), every preset, sample for sample (%u of %u differ)",
             diff, runs);
    check(w, !diff);
}

/* the whole voice (track_render): note 60 held 300 ms, released, 300 ms more; the level per block (peak) */
static uint32_t voice_run(track_t *t, int32_t *lvl, int32_t *maxd_on, int32_t *maxd_hold, int32_t *maxd_off)
{
    static int32_t o[CTL];
    uint32_t blk, i, nb = 2u * (uint32_t)(0.3 * FS / CTL), rel = nb / 2u;
    int32_t prev = 0;
    *maxd_on = *maxd_hold = *maxd_off = 0;
    for (i = 0; i < NPART; i++)
        trk_all_off(&trk[i]);
    for (blk = 0; blk < 400u; blk++) {
        memset(o, 0, sizeof o);
        track_render(t, o, CTL);
    }
    trk_note_on(t, 60, 100);
    for (blk = 0; blk < nb; blk++) {
        int32_t pk = 0;
        if (blk == rel)
            trk_note_off(t, 60);
        memset(o, 0, sizeof o);
        track_render(t, o, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t d = abs(o[i] - prev), *md = blk < 8u ? maxd_on : blk >= rel && blk < rel + 8u ? maxd_off :
                                                   blk >= rel - 100u && blk < rel ? maxd_hold : 0;
            prev = o[i];
            pk = abs(o[i]) > pk ? abs(o[i]) : pk;
            if (md && d > *md)
                *md = d;
        }
        lvl[blk] = pk;
    }
    return rel;
}
static int32_t maxw(const int32_t *l, uint32_t from, uint32_t n)   /* the loudest block of n (a period or more) */
{
    int32_t m = 0;
    uint32_t i;
    for (i = from; i < from + n; i++)
        m = l[i] > m ? l[i] : m;
    return m;
}
static void t_gate(void)
{
    track_t *t = &trk[0];
    static int32_t lg[1024], le[1024];
    int32_t don, dhold, doff, eon, ehold, eoff, steady;
    uint32_t rel, b3 = (uint32_t)(0.003 * FS / CTL) + 1u, b4 = (uint32_t)(0.004 * FS / CTL) + 1u;
    char w[200];
    host_preset(t, ENGI_PONY, 0);
    pony_defaults(t);
    t->p[P_E0] = JW_SAW, t->p[P_E2] = 0, t->p[P_E5] = 50, t->p[P_E6] = 0, t->p[P_E7] = 0;
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0, t->p[P_ITYPE] = 0, t->p[P_VINT] = 0;
    t->p[P_ATK] = 90, t->p[P_DEC] = 64, t->p[P_SUS] = 127, t->p[P_REL] = 100;   /* a slow envelope: ~800 ms, ~1.5 s */
    t->p[P_JVCA] = 1;
    rel = voice_run(t, lg, &don, &dhold, &doff);
    steady = maxw(lg, rel - 12u, 12u);                   /* (12 blocks: 8.7 ms, two periods of note 60) */
    t->p[P_JVCA] = 0;
    voice_run(t, le, &eon, &ehold, &eoff);
    snprintf(w, sizeof w, "VCA GATE: at the key %d%% of the held level within 3 ms (ENV %d%%), after the release %d%% within 4 ms (ENV %d%%)",
             (int)(100.0 * maxw(lg, b3, 12u) / steady), (int)(100.0 * maxw(le, b3, 12u) / steady),
             (int)(100.0 * maxw(lg, rel + b4, 12u) / steady), (int)(100.0 * maxw(le, rel + b4, 12u) / steady));
    check(w, maxw(lg, b3, 12u) > steady * 4 / 5 && maxw(le, b3, 12u) < steady / 10 && maxw(lg, rel + b4, 12u) == 0 &&
             maxw(le, rel + b4, 12u) > steady / 5 && steady > 1000);
    snprintf(w, sizeof w, "VCA GATE: no click (the largest step %d at the key, %d at the release, %d held)", don, doff, dhold);
    check(w, don <= dhold * 3 / 2 + 64 && doff <= dhold * 3 / 2 + 64);
    check("VCA ENV: the slow envelope as before (no sound at the key, still sounding after the release)",
          maxw(le, 0, 4u) < steady / 20 && maxw(le, rel + 20u, 12u) > steady / 5);
}

static void t_steps(void)
{
    track_t *t = &trk[0];
    static int16_t cuts[NB / CTL];
    uint32_t k, nbk = NB / CTL, plateau = 0, reach = 0;
    char w[160];
    for (k = 0; k < nbk; k++)                            /* the knob moving: up a step a block, then back down */
        cuts[k] = (int16_t)(k < 128u ? 30 + k / 2u : 94 - (k - 128u) / 2u);
    host_preset(t, ENGI_PONY, 7);
    pony_defaults(t);
    t->p[P_JCHR] = 1;
    direct(t, 0, a, NB, 48 * 16, pitch_inc(48 * 16), cuts);
    direct(t, 1, b, NB, 48 * 16, pitch_inc(48 * 16), cuts);
    check("STEPS ON: the old code under a moving CUT, sample for sample (the original's 128 unsmoothed steps)",
          !memcmp(a, b, sizeof a));
    {   /* OFF: the knob's part glides (the voice's s[7], Q8); a step a block: never two blocks the same */
        voice_t v;
        vmod_t m;
        int32_t o[CTL], last = -1;
        memset(&v, 0, sizeof v);
        memset(&m, 0, sizeof m);
        m.inc = pitch_inc(48 * 16), m.pitch16 = v.pitch16 = 48 * 16, m.amp0 = m.amp1 = 32767, m.shape = 64 << 8;
        t->p[P_JCHR] = 0;
        t->p[P_E5] = 30;
        pony_note_on(t, &v);
        check("STEPS OFF: a note starts at the knob's place (no glide from elsewhere)", v.s[7] == 30 << 8);
        for (k = 0; k < 64u; k++) {
            t->p[P_E5] = (int16_t)(30 + k);
            pony_render(t, &v, o, CTL, &m);
            plateau += v.s[7] == last;
            last = v.s[7];
        }
        t->p[P_E5] = 100;                                /* a jump: the target within 10 ms */
        for (k = 0; k < 64u && v.s[7] != 100 << 8; k++)
            pony_render(t, &v, o, CTL, &m);
        reach = k;
        snprintf(w, sizeof w, "STEPS OFF: a moving knob glides (%u plateaus in 64 blocks); a jump of 70 steps lands in %.1f ms",
                 plateau, reach * CTL * 1000.0 / FS);
        check(w, !plateau && reach * CTL * 1000.0 / FS <= 10.0 && reach >= 3u);
    }
}

static void t_stored(void)
{
    static project_t pj;
    static const int16_t PV[4] = {0, 80, 1, 0x2A5}, MV[3] = {7, -12, 85};
    track_t *a = &trk[0], *m = &trk[1];
    uint32_t k;
    int ok = 1, okm = 1, okd = 1;
    host_tracks_init();
    host_preset(a, ENGI_PONY, 7);
    host_preset(m, ENGI_MARE, 0);
    for (k = 0; k < 4u; k++)
        a->p[P_JRNG + k] = PV[k];
    m->p[P_MFFM] = MV[0], m->p[P_MRING] = MV[1], m->p[P_MW34] = MV[2];
    proj_capture(&pj);
    for (k = 0; k < 4u; k++)
        a->p[P_JRNG + k] = TP[P_JRNG + k].def;
    m->p[P_MFFM] = m->p[P_MRING] = -25, m->p[P_MW34] = 0;
    proj_apply(&pj, 1);
    for (k = 0; k < 4u; k++)
        ok &= a->p[P_JRNG + k] == PV[k];
    okm = m->p[P_MFFM] == MV[0] && m->p[P_MRING] == MV[1] && m->p[P_MW34] == MV[2];
    check("a project keeps PONY's RANGE KYBD VCA CHAR (part 1, CHAR 0x2A5: every byte's bits) and MARE's VCO3 VCO4 WAV34 (part 2)",
          ok && okm);
    a->p[P_JCHR] = JQ_FULL;                              /* (every bit, and none) */
    proj_capture(&pj);
    a->p[P_JCHR] = 0;
    proj_apply(&pj, 1);
    ok = a->p[P_JCHR] == (int16_t)JQ_FULL;
    a->p[P_JCHR] = 0;
    proj_capture(&pj);
    a->p[P_JCHR] = JQ_FULL;
    proj_apply(&pj, 1);
    check("a project keeps CHAR FULL (every bit) and CLEAN (none)", ok && a->p[P_JCHR] == 0);
    a->p[P_JCHR] = PV[3];
    proj_capture(&pj);
    {   /* the block as a build before PONY's EDIT 3 wrote it: MARE's defaults for a PONY sound, the spare byte 0 */
        proj_xs_t *x = &proj_xss[proj_x10_at(&pj)];
        x->xm[0] = 0, x->xm[1] = 0, x->xm[2] = (uint8_t)(TP[P_MW34].def - TP[P_MW34].min), x->xm[2u * PROJ_XM] = 0;
        x->xchk = proj_hash(x, __builtin_offsetof(proj_xs_t, xchk));
        for (k = 0; k < 4u; k++)
            a->p[P_JRNG + k] = PV[k];
        proj_apply(&pj, 1);
        for (k = 0; k < 3u; k++)
            okd &= a->p[P_JRNG + k] == TP[P_JRNG + k].def;
        okd &= a->p[P_JCHR] == (a->p[P_JCHR] & ~(int16_t)(JQ_STEPS | JQ_TICK)) && !(a->p[P_JCHR] & 1);
        check("a project block from before them: PONY's defaults (8', 43 %, ENV), STEPS and TICK off", okd);
    }
    {   /* a user preset */
        up_rec_t r;
        int16_t v[P_COUNT], out[P_COUNT], def[P_COUNT];
        uint32_t i;
        int same = 1;
        for (i = 0; i < P_COUNT; i++)
            def[i] = i >= P_E0 ? 0 : TP[i].def, v[i] = def[i];
        for (k = 0; k < 4u; k++)
            v[P_JRNG + k] = PV[k];
        v[P_FMODEL] = FM_LADDER;
        memset(&r, 0, sizeof r);
        r.used = UP_USED, r.name[0] = 'P', r.engine = ENGI_PONY;
        up_rec_set(&r, v);
        up_params(&r, out, def);
        for (i = 0; i < P_COUNT; i++)
            same &= out[i] == v[i];
        check("a user preset keeps RANGE KYBD VCA CHAR (0x2A5: both bytes) and MODL", same);
    }
}

/* ---- CHAR (core.h JQ_*): the sets, each quirk off and on ---- */
/* the part through the whole mix (mix_block: the chorus, the post-sum HPF), stereo; notes held from block on to off */
static void mix_run(track_t *t, int16_t *lr, uint32_t nb, const int *notes, uint32_t nn, uint32_t on, uint32_t off)
{
    static int32_t o[2 * CTL];
    uint32_t blk, i;
    for (blk = 0; blk < nb; blk++) {
        if (blk == on)
            for (i = 0; i < nn; i++)
                input_on(t, (uint8_t)notes[i], 100);
        if (blk == off)
            for (i = 0; i < nn; i++)
                input_off(t, (uint8_t)notes[i]);
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            lr[blk * 2u * CTL + i] = (int16_t)clamp(o[i], -32768, 32767);
    }
}
static double rms_lr(const int16_t *lr, uint32_t from, uint32_t n, int ch)   /* ch 0 L, 1 R, 2 both */
{
    double e = 0;
    uint32_t i, k = 0;
    for (i = from; i < from + n; i++)
        for (int c = 0; c < 2; c++)
            if (ch == 2 || ch == c)
                e += (double)lr[2u * i + (uint32_t)c] * lr[2u * i + (uint32_t)c], k++;
    return sqrt(e / (k ? k : 1));
}
static void char_part(track_t *t, uint32_t pi, int32_t chr)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NPART; i++)
        trk_all_off(&trk[i]);
    host_preset(t, ENGI_PONY, pi);
    song.sel = 0;
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0, t->p[P_ITYPE] = 0, t->p[P_VINT] = 0;
    t->p[P_JCHR] = (int16_t)chr;
    t->p[P_LEVEL] = 56;                                  /* (under the master limiter: the chord as played) */
}
#define NM (2u * 44100u)
static int16_t lr1[2 * NM], lr2[2 * NM];

static void t_char(void)
{
    track_t *t = &trk[0];
    static const int chord[] = {48, 52, 55, 59, 62};
    char w[220];
    uint32_t nb = NM / CTL, i;
    check("CHAR: CLASSIC the default; the sets CLEAN 0, CLASSIC HISS HPF LEVER GATE NOISE, FULL every bit; the device's names",
          TP[P_JCHR].def == (int16_t)JQ_CLASSIC && TP[P_JCHR].min == 0 && TP[P_JCHR].max == (int16_t)JQ_ALL &&
          JQ_CLASSIC == (JQ_HISS | JQ_HPF | JQ_LEVER | JQ_GATE | JQ_NOISE) && JQ_FULL == 0x3FFu &&
          jq_char(0) == 0 && jq_char(JQ_CLASSIC) == 1 && jq_char(JQ_FULL) == 2 && jq_char(JQ_CLASSIC | JQ_STEPS) == 3 &&
          jq_char(JQ_TICK) == 3 && !strcmp(N_JCHAR[jq_char(JQ_CLASSIC)], "CLASC") && N_JQBIT[10] == 0 &&
          !strcmp(N_JQBIT[1], "HISS") && TP[P_JCHR].fmt == F_CHAR);
    {   /* each factory preset: CHAR as stored (CLASSIC, the three STEPS ones CLASSIC + STEPS) */
        uint32_t pi, nst = 0, ncl = 0;
        for (pi = 0; pi < ENG_PONY.npresets; pi++) {
            t->p[P_JCHR] = TP[P_JCHR].def;               /* (the device: every value its default first, ui.c) */
            host_preset(t, ENGI_PONY, pi);
            nst += t->p[P_JCHR] == (int16_t)(JQ_CLASSIC | JQ_STEPS);
            ncl += t->p[P_JCHR] == (int16_t)JQ_CLASSIC;
        }
        snprintf(w, sizeof w, "the factory sounds: %u CLASSIC, %u CLASSIC + STEPS (RESO SWEEP, FUNK BASS, DARK PLUCK)", ncl, nst);
        check(w, nst == 3u && ncl + nst == ENG_PONY.npresets);
    }
    {   /* HISS: the chorus's hiss in silence while the chorus is on, ~-63 dB under a held chord, L and R apart */
        double chordl, hiss, hissoff, hissdry, corr = 0, el = 0, er = 0;
        uint32_t g0 = (uint32_t)(1.5 * FS), gn = (uint32_t)(0.4 * FS);
        char_part(t, 0, JQ_CLASSIC);
        t->p[P_E7] = 1, t->p[P_E5] = 127, t->p[P_REL] = 10;
        mix_run(t, lr1, nb, chord, 5, 4, (uint32_t)(1.0 * FS / CTL));
        chordl = rms_lr(lr1, (uint32_t)(0.4 * FS), (uint32_t)(0.5 * FS), 2);
        hiss = rms_lr(lr1, g0, gn, 2);
        for (i = g0; i < g0 + gn; i++)
            corr += (double)lr1[2 * i] * lr1[2 * i + 1], el += (double)lr1[2 * i] * lr1[2 * i], er += (double)lr1[2 * i + 1] * lr1[2 * i + 1];
        corr /= sqrt(el * er) + 1e-9;
        char_part(t, 0, JQ_CLASSIC & ~JQ_HISS);
        t->p[P_E7] = 1, t->p[P_E5] = 127, t->p[P_REL] = 10;
        mix_run(t, lr2, nb, chord, 5, 4, (uint32_t)(1.0 * FS / CTL));
        hissoff = rms_lr(lr2, g0, gn, 2);
        char_part(t, 0, JQ_CLASSIC);
        t->p[P_E7] = 0, t->p[P_E5] = 127, t->p[P_REL] = 10;
        mix_run(t, lr2, nb, chord, 5, 4, (uint32_t)(1.0 * FS / CTL));
        hissdry = rms_lr(lr2, g0, gn, 2);
        snprintf(w, sizeof w, "HISS: in the silence after a chord %.1f dB under it (the original's ~-61), L/R correlation %.2f; HISS off: %s, CHOR off: %s",
                 20 * log10(hiss / chordl), corr, hissoff < 0.5 ? "silent" : "NOT silent", hissdry < 0.5 ? "silent" : "NOT silent");
        check(w, hiss > 0 && 20 * log10(hiss / chordl) > -66 && 20 * log10(hiss / chordl) < -57 && fabs(corr) < 0.15 &&
                 hissoff < 0.5 && hissdry < 0.5);
    }
    {   /* HPF: the voice's own bypassed (as HPF 1), the part's after the sum: 0 a shelf, 2 and 3 a high-pass */
        int32_t x[CTL], y[CTL];
        double g[3][2];
        static const double fq[2] = {35.0, 1000.0};
        int pos, f;
        host_preset(t, ENGI_PONY, 7);
        pony_defaults(t);
        t->p[P_E4] = 2;
        t->p[P_JCHR] = JQ_HPF;
        direct(t, 0, a, 4096, 48 * 16, pitch_inc(48 * 16), 0);
        t->p[P_JCHR] = 0, t->p[P_E4] = 1;
        direct(t, 0, b, 4096, 48 * 16, pitch_inc(48 * 16), 0);
        check("HPF: the voice's own high-pass and shelf passed over (the voice as at HPF 1)", !memcmp(a, b, 4096 * sizeof *a));
        for (pos = 0; pos < 3; pos++)
            for (f = 0; f < 2; f++) {
                double ein = 0, eout = 0;
                uint32_t k;
                memset(jq_hp, 0, sizeof jq_hp);
                t->p[P_E4] = (int16_t)(pos == 0 ? 0 : pos == 1 ? 2 : 3);
                for (k = 0; k < 2000u; k++) {
                    for (i = 0; i < CTL; i++)
                        x[i] = y[i] = (int32_t)(8000.0 * sin(2 * M_PI * fq[f] * (k * CTL + i) / FS));
                    pony_hpf(t, y, 0, CTL);
                    if (k >= 1000u)
                        for (i = 0; i < CTL; i++)
                            ein += (double)x[i] * x[i], eout += (double)y[i] * y[i];
                }
                g[pos][f] = 10 * log10(eout / ein);
            }
        snprintf(w, sizeof w, "HPF after the sum: 0 %+.1f dB at 35 Hz, %+.1f at 1 kHz; 2 %+.1f, %+.1f; 3 %+.1f, %+.1f",
                 g[0][0], g[0][1], g[1][0], g[1][1], g[2][0], g[2][1]);
        check(w, g[0][0] > 6.0 && fabs(g[0][1]) < 0.5 && g[1][0] < -15.0 && fabs(g[1][1]) < 0.5 && g[2][0] < -25.0 &&
                 g[2][1] < -0.5 && g[2][1] > -3.0);
    }
    {   /* LEVER, LFO: pony_lfo */
        int32_t vib = 1234, l, l2;
        host_preset(t, ENGI_PONY, 1);
        t->p[P_JCHR] = 0;
        t->lfo_val = 20000, t->lfo_fade = 32767;
        l = pony_lfo(t, mulq15(t->lfo_val, t->lfo_fade), &vib, CTL);
        check("CLEAN: pony_lfo passes the LFO and the wheel's sine as they are", vib == 1234 && l == mulq15(20000, 32767));
        t->p[P_JCHR] = JQ_LEVER;
        t->lfo_fade = 0;
        pony_lfo(t, 0, &vib, CTL);
        check("LEVER: the wheel's vibrato is the LFO itself (its rate and wave, not its delay)", vib == 20000);
        t->p[P_JCHR] = JQ_LFO;
        t->lfo_fade = 12000;
        l = pony_lfo(t, mulq15(20000, 12000), &vib, CTL);
        t->lfo_fade = 24576;
        l2 = pony_lfo(t, mulq15(20000, 24576), &vib, CTL);
        snprintf(w, sizeof w, "LFO: the delay a hold-off (fade 37 %%: %d) then straight (75 %%: %d, half the LFO)", l, l2);
        check(w, l == 0 && abs(l2 - 10000) < 4);
    }
    {   /* LFO: free-running, its phase kept over a new phrase */
        uint32_t ph;
        char_part(t, 1, JQ_LFO);
        t->lfo_ph = 0x12345678u;
        input_on(t, 60, 100);
        ph = t->lfo_ph;
        input_off(t, 60);
        char_part(t, 1, 0);
        t->lfo_ph = 0x12345678u;
        input_on(t, 60, 100);
        snprintf(w, sizeof w, "LFO: free-running (a phrase's first key leaves its phase: %08x; off: restarted %08x)", ph, t->lfo_ph);
        check(w, ph == 0x12345678u && t->lfo_ph == (uint32_t)t->p[P_LPHASE] << 25);
        input_off(t, 60);
    }
    {   /* GATE: VCA GATE's ramp ~1 ms (2 blocks) with GATE, ~3 ms (4 blocks) without */
        voice_t v;
        uint32_t k, n1 = 0, n3 = 0;
        host_preset(t, ENGI_PONY, 0);
        t->p[P_JVCA] = 1;
        for (k = 0; k < 2; k++) {
            uint32_t nb2 = 0;
            memset(&v, 0, sizeof v);
            v.active = 1, v.stage = 1, v.vel = 127;
            t->p[P_JCHR] = k ? JQ_GATE : 0;
            while (pony_vamp(t, &v, 0) < mulq15(32767, 127 * 258) && nb2 < 10u)
                nb2++;
            if (k)
                n1 = nb2 + 1u;
            else
                n3 = nb2 + 1u;
        }
        snprintf(w, sizeof w, "GATE: VCA GATE full in %u blocks (%.1f ms; off: %u, %.1f ms)", n1, n1 * CTL * 1000.0 / FS, n3,
                 n3 * CTL * 1000.0 / FS);
        check(w, n1 == 2u && n3 == 4u);
    }
    {   /* VCA: the curve under 30 %; above as the envelope */
        voice_t v;
        int32_t e[4] = {32767, 16384, 9830, 3277}, r[4];
        uint32_t k;
        memset(&v, 0, sizeof v);
        v.vel = 127;
        host_preset(t, ENGI_PONY, 0);
        t->p[P_JVCA] = 0, t->p[P_JCHR] = JQ_VCA;
        for (k = 0; k < 4; k++)
            r[k] = pony_vamp(t, &v, e[k]);
        snprintf(w, sizeof w, "VCA: the curve: 100 %% %+.1f dB, 50 %% %+.1f, 30 %% %+.1f, 10 %% %+.1f re the envelope",
                 20 * log10((double)r[0] / mulq15(e[0], 127 * 258)), 20 * log10((double)r[1] / mulq15(e[1], 127 * 258)),
                 20 * log10((double)r[2] / mulq15(e[2], 127 * 258)), 20 * log10((double)r[3] / mulq15(e[3], 127 * 258)));
        check(w, r[0] == mulq15(e[0], 127 * 258) && r[1] == mulq15(e[1], 127 * 258) &&
                 fabs(20 * log10((double)r[2] / mulq15(e[2], 127 * 258))) < 0.2 &&
                 20 * log10((double)r[3] / mulq15(e[3], 127 * 258)) < -15.0);
    }
    {   /* NOISE: darker (the top octave down >= 8 dB against 1 kHz), the voice's noise alone */
        double hi[2], lo[2];
        uint32_t k;
        for (k = 0; k < 2; k++) {
            host_preset(t, ENGI_PONY, 0);
            pony_defaults(t);
            t->p[P_E0] = JW_OFF, t->p[P_E2] = 0, t->p[P_E3] = 127, t->p[P_E5] = 127, t->p[P_E6] = 0;
            t->p[P_JCHR] = k ? JQ_NOISE : 0;
            direct(t, 0, a, NB, 60 * 16, pitch_inc(60 * 16), 0);
            hi[k] = lo[k] = 0;
            for (i = 0; i < 40; i++)
                hi[k] += mag_of(a + 4096, 4096, 12000.0 + 100.0 * i), lo[k] += mag_of(a + 4096, 4096, 800.0 + 10.0 * i);
        }
        snprintf(w, sizeof w, "NOISE: 12 kHz against 1 kHz %.1f dB darker (the ~5 kHz low-pass)",
                 20 * log10((hi[0] / lo[0]) / (hi[1] / lo[1])));
        check(w, 20 * log10((hi[0] / lo[0]) / (hi[1] / lo[1])) > 6.0);
    }
    {   /* COARS: the CUT knob in 0.11-octave steps (Q8 395), no glide */
        static int16_t cuts[NB / CTL];
        voice_t v;
        vmod_t m;
        uint32_t k, nvals = 0;
        int32_t last = -1;
        int ok = 1;
        host_preset(t, ENGI_PONY, 7);
        pony_defaults(t);
        t->p[P_JCHR] = JQ_COARSE;
        memset(&v, 0, sizeof v), memset(&m, 0, sizeof m);
        m.inc = pitch_inc(48 * 16), m.pitch16 = v.pitch16 = 48 * 16, m.amp0 = m.amp1 = 32767, m.shape = 64 << 8;
        for (k = 0; k < 128u; k++) {
            t->p[P_E5] = (int16_t)k;
            pony_render(t, &v, a, CTL, &m);
            ok &= v.s[7] % 395 == 0 && abs(v.s[7] - (int32_t)k * 256) <= 198;
            nvals += v.s[7] != last;
            last = v.s[7];
        }
        (void)cuts;
        snprintf(w, sizeof w, "COARS: the CUT knob's 128 places in %u steps of 0.11 octave (each within half a step)", nvals);
        check(w, ok && nvals >= 80u && nvals <= 84u);
    }
    {   /* TICK: the envelope held from tick to tick (~4.2 ms), half way to it a block */
        voice_t v;
        uint32_t k, changes = 0;
        int32_t held = -1, vib = 0;
        host_preset(t, ENGI_PONY, 0);
        t->p[P_JCHR] = JQ_TICK;
        memset(&v, 0, sizeof v);
        for (k = 0; k < 64u; k++) {
            int32_t o;
            pony_lfo(t, 0, &vib, CTL);
            o = pony_amp(t, &v, (int32_t)k * 500);
            changes += v.jq[2] != held;
            held = v.jq[2];
            (void)o;
        }
        snprintf(w, sizeof w, "TICK: a rising envelope taken %u times in 64 blocks (46 ms: every ~4.2 ms); off: as it is", changes);
        t->p[P_JCHR] = 0;
        check(w, changes >= 10u && changes <= 12u && pony_amp(t, &v, 777) == 777);
    }
}

int main(void)
{
    host_tracks_init();
    t_defaults();
    t_range();
    t_wave_off();
    t_sing();
    t_kybd();
    t_modl();
    t_gate();
    t_steps();
    t_stored();
    t_char();
    printf(bad ? "pony: %d FAILED\n" : "pony: all passed\n", bad);
    return bad;
}
