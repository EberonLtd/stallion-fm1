/* SPDX-License-Identifier: GPL-3.0-only */
/* STAMPEDE: a voice after the dual-channel 1970s polyphonic's (its user and service manuals: two complete channels,
 * I and II, each a VCO with its waves mixed, a resonant high-pass then a resonant low-pass, 12 dB each, swept by the filter's
 * own envelope (IL, AL, A, D, R), a VCA envelope; channel II detuned against channel I; a ring modulator; the
 * sub-oscillator; the polyphonic aftertouch). No code or sample of its maker's.
 * EDIT 1 (CH I/II)  WAVE  FEET  DTUNE  BAL        EDIT 2 (VCF)  HPF  LPF  RES  IL
 *   WAVE  both channels' VCO, a morph through NOIS SAW PLS SINE RING (0, 32, 64, 96, 127): between two points both
 *         are heard, mixed (the original mixes its waves with levels, not a single choice); RING: channel II becomes
 *         channel I x channel II (their sum and difference: bells, with FEET off the octave)
 *   FEET  channel II's interval, -24 .. +24 semitones (the original's feet: +-12, +-24; the steps between: fifths for
 *         the ring modulator's bells)
 *   DTUNE channel II against channel I, 0 .. 40 cents (I down, II up, half each; finer near 0): the two channels
 *         beating, the heart of the original's brass and strings
 *   BAL   channel I (-64) .. both (0) .. channel II (+63)
 *   HPF LPF RES  the two filters in series (Simper's trapezoidal SVF, as dsp.c tsvf: 12 dB each), keyboard tracking
 *         about half; RES the low-pass's resonance, half of it the high-pass's. The filter envelope and the rest of
 *         the cutoff's modulation (LFO, VEL>F, AT / MW CUT, the MATRIX, the macros) move the LPF, half as far the HPF
 *   IL    the filter envelope's initial level, 0 .. 100 % of its attack level: it starts there, rises to the attack
 *         level (AL: ENV DEST FLT, its depth, -64 .. 63) in its ATK, falls to 0 (the cutoffs as set: the original's
 *         filter envelope has no sustain) in its DEC and goes back to IL in its REL: ENV 2's ATK DEC REL (the original's
 *         VCF envelope has its own times: its brass rises over a third of a second and more while the VCA is open in
 *         a tenth), or with ENV 2 ATK 0 ENV 1's (one envelope)
 * The reduction (8 values, the original has two full channels of sliders):
 *   - the channels share their wave mix and their filters: two filters with the same settings on two channels sum to
 *     the same as one pair on their mix (they are linear), so one HPF and one LPF per voice, half the cost
 *   - the filter envelope's times are ENV 2's (ENV 2 ATK 0: ENV 1's, the VCA's), its depth ENV DEST FLT; IL its own
 *     (the shape the original's IL / AL sliders give): ENV 1 to the filter, as on the other engines, is replaced by this
 *     envelope here. ENV 2 is still a MATRIX source as well (to CUT: more of the same rise). One filter envelope per
 *     voice (the channels share their filters): channel II's own would need its own filter pair
 *   - the ring modulator is per voice, channel I x channel II (the original's is on the whole output, with its own
 *     modulator)
 *   - the sub-oscillator: LFO 1 (to PIT, FLT, SHP: the pulse width, AMP); the touch: VEL>F, the aftertouch's
 *     destination (VIB, CUT: the original's brilliance), the MATRIX; SPREAD spreads the voices (each voice is one
 *     signal: its channels cannot be panned apart)
 *   - the pulse: square at SHAPE's middle, narrower as SHAPE moves either way (LFO / ENV to SHP: the PWM)
 * Voice state: ph[0] channel I, ph[1] channel II, ph[2] the noise; s[0..1] the high-pass, s[2..3] the low-pass,
 * s[4] the filter envelope (Q24), s[5] its stage (1 attack, 2 decay, 3 release), s[6] channel II's drift, s[7] the
 * note's own detune and balance (stampede_note_on). Nothing shared: two voices may
 * render on two cores at once (voice.c vs_ok).
 * Cost: two VCOs (up to two polyBLEP saws each between SAW and PLS), two SVFs, 32-bit products only: ~170 .. 235 host
 * instructions a sample and voice (tests/stampede_test.c; PONY and PALOMINO ~280 and ~265 the same way), so no voice cap
 * (engine_t.poly 0), as PONY and PALOMINO. */
enum { CSW_NOIS = 0, CSW_SAW = 32, CSW_PLS = 64, CSW_SINE = 96, CSW_RING = 127 };
static const char *const N_CSWAVE[] = {"NOIS", "N+SAW", "SAW", "S+P", "PLS", "P+SIN", "SINE", "S+RNG", "RING", 0};

static void stampede_note_on(track_t *t, voice_t *v)
{
    if (v->env < (1 << 17)) {                        /* from silence, or a tail below -42 dB (as RODEO's: REL 110's
                                                      * tail kept the drifted channels, a KEYS sound -18 .. +4 dB at random,
                                                      * tests/tail_level_test.c): the filters clear, the envelope at IL
                                                      * (the VCOs run on) */
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;
        v->s[4] = clamp(t->p[P_E7], -64, 127) * (1 << 17);
        if (!v->ph[2])
            v->ph[2] = 0x2545F491u + v->note * 2654435761u;
        uint32_t r = (v->ph[2] ^ v->ph[0]) * 2654435761u;   /* (per note) the channels' own spread this note:
                                                      * bits 0..7 DTUNE x 0.5 .. 1.5, 8..15 channel II's level x 0.71 ..
                                                      * 1.41 (+-3 dB): the original's held notes, each its own detune
                                                      * (4 .. 12 cents at one setting) and balance (-5 .. +1 dB);
                                                      * STAMPEDE's were all one (8.5 +- 0.2 cents, +- 0.3 dB) */
        v->s[7] = (int32_t)((r ^ (r >> 15)) & 0xFFFFu);
        v->ph[1] = v->ph[0] + 0x2C000000u + (v->ph[2] >> 3) + (v->ph[2] >> 5);   /* channel II 0.17 .. 0.33 of a
                                                      * turn on from I, a new place each note (the original's VCOs run
                                                      * free): never near half a turn (the pulses cancelled: a KEYS sound's
                                                      * note -14 dB, at random) and never one fixed comb (a quarter:
                                                      * harmonics 2, 6, 10 gone on every note at DTUNE 0, measured).
                                                      * (1.0) It was 1/8 .. 3/8: at a small DTUNE the place holds for
                                                      * seconds, and its ends gave the fundamental +2.3 / -5.3 dB of a
                                                      * quarter's (now +1.7 / -2.7): SQUARE LEAD's notes from silence
                                                      * 6.6 dB apart at C3 (2.6 now), GLIDE LEAD's 9.0 (6.7),
                                                      * tests/tail_level_test.c */
    }
    v->s[5] = 1;                                     /* (sounding: the attack from where it is) */
}

#ifndef STA_DTC
#define STA_DTC 52 /* (Stallion) DTUNE's curve: channel II off I by (dt^2 x STA_DTC) >> 10 (1/16 semitone x 1/128) */
#endif
/* UNISON (voice.c unison_spread): channels I and II moved by r (Q32) of a turn of the note, II by its share (FEET and
 * DTUNE above I): as if started that much sooner, their relative phase as the voice had it; the noise from its own
 * place. (Before: ph[1] random of its own, voice.c uni_phases: every voice of the stack, and every note the voice
 * played after it, at a random relative phase, near half a turn the pulses and saws cancelled: UNI4 +17 dB over such
 * a POLY note of a KEYS sound) */
static void stampede_spread(track_t *t, voice_t *v, uint32_t r)
{
    int32_t dt = clamp(t->p[P_E2], 0, 127);
    v->ph[0] += r;
    v->ph[1] += uni_turns(r, v->pitch16, clamp(t->p[P_E1], -24, 24) * 16 + (((dt * dt * STA_DTC) >> 10) >> 7));
    v->ph[2] ^= r;
}

/* the filter envelope, once a block: IL -> 1 (ATK, linear), -> 0 (DEC), -> IL (REL); its times ENV 2's, or ENV 1's
 * with ENV 2 ATK 0; Q24 */
static int32_t stampede_eg(const int16_t *p, const int16_t *ev, voice_t *v)
{
    int32_t il = clamp(ev[7], -64, 127) * (1 << 17), lv = clamp(v->s[4], -(64 << 17), 1 << 24), st = v->s[5];   /* (a voice another
                                                      * engine held: in range) */
    const int16_t *tm = p[P_E2ATK] > 0 ? p + P_E2ATK : p + P_ATK;   /* the original's VCF envelope, its own A D R:
                                                      * ENV 2's (ENV 2 ATK 0: ENV 1's, one envelope, as before) */
#define STA_T(k) ((uint32_t)tm[k] & 127u)
    if (v->stage >= 3u && st && st < 3)
        st = 3;
    switch (st) {
    case 1: {
        int32_t d = (int32_t)(((int64_t)ENV_LIN[STA_T(0)] * ((1 << 24) - il)) >> 24);
        lv += d > 0 ? d : 1;
        if (lv >= 1 << 24)
            lv = 1 << 24, st = 2;
        break;
    }
    case 2:
        lv -= mulq16(lv, ENV_EXP[STA_T(1)]);
        break;
    case 3:
        lv += mulq16(il - lv, ENV_EXP[STA_T(3)]);
        break;
    default:
        break;
    }
    v->s[4] = lv;
    v->s[5] = st;
    return lv;
#undef STA_T
}

/* one channel's VCO at the morph's segment g (0 NOIS-SAW, 1 SAW-PLS, 2 PLS-SINE, 3 SINE: the ring is the mixer's),
 * f its position in it (Q10) */
static inline int32_t stampede_vco(uint32_t g, int32_t f, uint32_t ph, uint32_t inc, uint32_t pw, int32_t nz)
{
    int32_t a, b;
    switch (g) {
    case 0:
        if (!f)
            return nz;
        a = nz, b = osc_saw(ph, inc);
        break;
    case 1:
        a = osc_saw(ph, inc);
        if (!f)
            return a;
        b = (a - osc_saw(ph + pw, inc)) >> 1;        /* (the pulse: the saw less a shifted one) */
        break;
    case 2:                                          /* P+SIN: the pulse at its level, the sine after the filters
                                                      * (stampede_render: the original's sine slider bypasses its VCFs) */
        a = osc_pulse(ph, inc, pw);
        return a - ((a * f) >> 10);
    default:
        return osc_sine(ph);
    }
    return a + (((b - a) * f) >> 10);
}

#ifndef STA_PW0
#define STA_PW0 0x7C28F5C3u /* (Stallion) the pulse's rest width at SHAPE's middle (Q32 of a turn): 48.5 %, the original's
                           * square measured 47.7 .. 48.7 % (evens -19.5 .. -25.6 dB; an exact square's under -50 dB) */
#endif
#ifndef STA_PWS
#define STA_PWS 59000u /* (Stallion) SHAPE's distance from the middle (Q8, 0 .. 127 x 256) -> the width's narrowing */
#endif
#ifndef STA_DRS
#define STA_DRS 23 /* (Stallion) channel II's drift: the walk's step, the noise >> STA_DRS (23: -255 .. 255, Q8) */
#endif
#ifndef STA_DRL
#define STA_DRL 10 /* (Stallion) channel II's drift: the walk's leak back to 0, dr >> STA_DRL a block */
#endif
#ifndef STA_HPK
#define STA_HPK 7600 /* (Stallion) the high-pass's resonance: its damping 8192 less (RES / 2) x STA_HPK / 127 (Q12) */
#endif
#ifndef STA_KTRK
#define STA_KTRK 8 /* (Stallion) the filters' key tracking: (pitch16 - C4) x STA_KTRK cutoff units (256 a CUT step) */
#endif
#ifndef STA_RMS
#define STA_RMS 15 /* (Stallion) the ring modulator: channel I x channel II >> STA_RMS (15: unity) */
#endif
#ifndef STA_SINL
#define STA_SINL 12 /* (Stallion) P+SIN: the unfiltered sine's level, (both channels' sines x f) >> STA_SINL */
#endif
#ifndef STA_NZ
#define STA_NZ 9                                       /* the floor: z >> 9, ~-50 dB under a full saw */
#endif
static void stampede_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *ev = vm_ev(t, m);              /* the EDIT values: the voice's own or the part's */
    const int16_t *p = t->p;
    int32_t wv = clamp(ev[0], 0, 127), dt = clamp(ev[2], 0, 127), bal = clamp(ev[3], -64, 63), f;
    int32_t g1 = clamp((64 - bal) * 256, 0, 32767), g2 = clamp((64 + bal) * 256, 0, 32767);
    int32_t off = (dt * dt * STA_DTC) >> 10, dev = m->shape - (64 << 8);
    int32_t w = clamp((dev < 0 ? -dev : dev) * 2, 0, 127 * 256);
    if (dt) {                                        /* (per note) this note's own detune and balance */
        int32_t r = v->s[7] & 0xFFFF, u = r >> 8;
        int32_t a = 4923 - ((u * 1700) >> 8) + ((((u * u) >> 8) * 257) >> 8);   /* (Q12) both channels by
                                                      * sqrt(1.083 x 2 / (1 + k^2)), k channel II's x 0.71 .. 1.41: the
                                                      * balance turned, the note's power kept (at BAL 0; 1.083 the
                                                      * spread's mean, as before on average). Channel II alone up or
                                                      * down was each note's level too, +-1.5 dB on top of the
                                                      * channels' beating (with the narrower place, stampede_note_on:
                                                      * SWELL PAD's notes from silence 8.7 dB apart at C3, 6.4 now;
                                                      * tests/tail_level_test.c) */
        off = (off * (128 + (r & 255))) >> 8;
        g1 = (g1 * a) >> 12;                         /* (no floor: g1 >= 256, a >= 3484; a clamp to 0 here held
                                                      * one more value into the loop: target cost 195 -> 190) */
        g1 = g1 > 32767 ? 32767 : g1;
        g2 =clamp((((g2 * (181 + (u * 181 >> 8))) >> 8) * a) >> 12, 0, 32767);
    }
    int32_t eg = stampede_eg(p, ev, v) >> 9, efl = p[P_ED_FLT];
    int32_t mc = m->cutoff - ((m->envq15 * efl) >> 7) + ((eg * efl) >> 7), key = (v->pitch16 - 60 * 16) * STA_KTRK;
    int32_t h1 = v->s[0], h2 = v->s[1], l1 = v->s[2], l2 = v->s[3], hk, res = clamp(ev[6], 0, 127);
    uint32_t i, g, ph1 = v->ph[0], ph2 = v->ph[1], nz = v->ph[2], inc1, inc2;
    uint32_t pw = STA_PW0 - (uint32_t)w * STA_PWS;   /* square .. ~5 % */
    tsvf_t hp, lp;
    if (wv > CSW_SINE)                               /* the morph: its segment and the place in it (Q10) */
        g = 3, f = (wv - CSW_SINE) * 1024 / (CSW_RING - CSW_SINE);
    else
        g = (uint32_t)wv >> 5, f = (wv & 31) << 5;
    if (wv == CSW_SINE)                              /* SINE: the P+SIN segment's end, all of it after the filters */
        g = 2, f = 1024;
    int32_t dr = clamp(v->s[6], -(1 << 17), 1 << 17) + (((int32_t)nz >> STA_DRS) | 1);   /* (a voice another engine
                                                      * held: its s[6] in range, else channel II sat at the bottom of
                                                      * the pitch range, unbent, for seconds; the walk itself stays
                                                      * within +-20000) channel II's drift: a slow random walk (Q8, 1/4096
                                                      * semitone; ~0.3 cents, ~0.7 s): the original's held notes
                                                      * wander 0.2 .. 3 cents. The step odd, -255 .. 255: its mean 0
                                                      * (it was (nz >> 28) - 8, mean -1/2 a step: the leak held channel
                                                      * II ~12 cents flat, every single note beating) */
    v->s[6] = dr -= dr >> STA_DRL;
    inc1 = off != m->vo1 ? hy_inc(m->pitch16, m->fine, m->vo1 - off) : m->inc;   /* (vo1, vo2: each its own pitch) */
    inc2 = hy_inc(m->pitch16 + clamp(ev[1], -24, 24) * 16, m->fine, off + m->vo2 + (dr >> 8));
    tsvf_coef(&hp, (ev[4] << 8) + (mc >> 1) + key, res >> 1);
    hk = 8192 - (res >> 1) * STA_HPK / 127;
    tsvf_coef(&lp, (ev[5] << 8) + mc + key, res);
    for (i = 0; i < n; i++) {
        int32_t y1, y2, x, v1, v2, v3, y, a, z;
        nz = nz * 1664525u + 1013904223u;            /* the noise: the NOIS wave, and the voice's own analogue floor */
        z = (int32_t)(nz >> 16) - 32768;
        y1 = stampede_vco(g, f, ph1, inc1, pw, z);
        y2 = stampede_vco(g, f, ph2, inc2, pw, z);
        if (g == 3u)                                 /* SINE -> RING: channel II becomes I x II */
            y2 += (((((y1 * y2) >> STA_RMS) - y2) * f) >> 10);
        ph1 += inc1;
        ph2 += inc2;
        x = ((mulq15(y1, g1) + mulq15(y2, g2)) >> 1) + (z >> STA_NZ);   /* (half scale into the filters, as
                                                      * fmod_tick) and the floor under it: the original's held notes
                                                      * sit 37 .. 60 dB over theirs, STAMPEDE's were 84 .. 90 */
        v3 = x - h2;                                 /* the high-pass: the SVF's input less its band and low parts */
        v1 = (hp.a1 * h1 + hp.a2 * v3) >> 13;
        v2 = h2 + ((hp.a2 * h1 + hp.a3 * v3) >> 13);
        h1 = clamp(2 * v1 - h1, -150000, 150000);
        h2 = clamp(2 * v2 - h2, -150000, 150000);
        x = clamp(x - ((hk * v1) >> 12) - v2, -150000, 150000);
        y = tsvf_lp(&lp, x, &l1, &l2);               /* then the low-pass */
        if (g == 2u)                                 /* P+SIN: the sine under the filtered pulse, unfiltered: the
                                                      * original's held notes, h1 15 .. 35 dB over a high-passed h2 */
            y += ((osc_sine(ph1) + osc_sine(ph2)) * f) >> STA_SINL;   /* (both channels, BAL not applied: 0 B) */
        a = y < 0 ? -y : y;
        if (a > 16000) {                             /* softclip((a - 16000) x 2), its input > 0: no sign to undo */
            int32_t u = (a - 16000) * 2, k = u >> 8;
            u = k >= 256 ? TANH_Q15[256] : TANH_Q15[k] + (((TANH_Q15[k + 1] - TANH_Q15[k]) * (u & 255)) >> 8);
            a = 16000 + (u >> 1);
            y = y < 0 ? -a : a;
        }
        out[i] += mulq15(mulq15(y << 1, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph1;
    v->ph[1] = ph2;
    v->ph[2] = nz;
    v->s[0] = h1, v->s[1] = h2, v->s[2] = l1, v->s[3] = l2;
}

/* (Stallion 1.0) the factory sounds, after the original's preset panel (strings, brass, organ, electric piano, clavichord,
 * harpsichord, funky, bass), its famous swelling brass and pads, its solo leads (no imitations,
 * no ring-modulated bells); dry (no sends, no arp), centred. The aftertouch to CUT (the brilliance) or to VIB */
#define STA_FX0 FX(0, 0, 0, 0)
static const preset_t STAMPEDE_PRESETS[] = {
    /* {WAVE, FEET, DTUNE, BAL, HPF, LPF, RES, IL}, {A D S R}, ENV DEST FLT (the filter envelope's AL), mono; extras */
    {"INIT", {CSW_SAW, 0, 0, 0, 0, 127, 0, 0}, {0, 64, 127, 20}, 0, 0, FX(0, 20, 0, 0),
     XP(P_VINT + 1, 8, P_SPREAD + 1, 32)},
    /* brass (dry: clean and dry by default, a design choice; BRIGHT BRASS the noise in its wave, N+SAW 26: the real one's
     * hiss above 2 kHz, about as loud as its harmonics there) */
    {"BRASS SWELL", {CSW_SAW, 0, 56, 0, 0, 60, 28, 0}, {84, 110, 110, 92}, 60, 0, FX(0, 0, 0, 0),
     XP(P_ATDST + 1, MD_CUT, P_VINT + 1, 16, P_SPREAD + 1, 32, P_E2ATK + 1, 90)},
    {"BRASS", {CSW_SAW, 0, 58, 0, 0, 74, 22, 0}, {36, 88, 104, 60}, 42, 0, FX(0, 0, 0, 0),
     XP(P_ATDST + 1, MD_CUT, P_VINT + 1, 12, P_SPREAD + 1, 32, P_E2ATK + 1, 80)},
    {"BRIGHT BRASS", {26, 0, 50, 0, 24, 62, 34, 20}, {48, 84, 100, 56}, 50, 0, FX(0, 0, 0, 0),
     XP(P_ATDST + 1, MD_CUT, P_VINT + 1, 8, P_SPREAD + 1, 32, P_E2ATK + 1, 83)},
    {"MELLOW BRASS", {48, 0, 78, 0, 0, 64, 20, 8}, {62, 84, 108, 70}, 48, 0, FX(0, 0, 0, 0),
     XP(P_ATDST + 1, MD_CUT, P_VINT + 1, 16, P_SPREAD + 1, 32, P_E2ATK + 1, 83)},
    /* (matrix showcase: STAMPEDE_PATCHES) velocity brass: the strike sets the IL's start, the filter envelope's depth and
     * speed; each key's own pressure opens its filter. More urgency and rise: IL under the cutoff, the
     * filter's own attack ~1.4 s (ENV 2 ATK 100), resonance up: a held chord brightens x1.7 soft, x2.2 hard by 1.5 s
     * (the real one's stabs x1.6 .. 1.9 by 2 s) */
    {"VEL BRASS", {CSW_SAW, 0, 52, 0, 0, 56, 46, -30}, {52, 84, 100, 56}, 63, 0, FX(0, 0, 0, 0),
     XP(P_SPREAD + 1, 32, P_E2ATK + 1, 100, P_E2DEC + 1, 108)},
    /* (the darker of two versions: low filter, less of the octave; and a clear handoff from note to note: a firm
     * level attack, IL 40 so each note starts part open, short releases, dry) */
    {"OCTAVE BRASS", {CSW_SAW, 12, 48, -32, 0, 36, 20, 40}, {20, 74, 100, 40}, 40, 0, FX(0, 0, 0, 0),
     XP(P_ATDST + 1, MD_CUT, P_VINT + 1, 32, P_SPREAD + 1, 32, P_E2ATK + 1, 77)},
    /* strings and pads */
    /* (strings pass: more punch) speaks in ~80 ms, the filter envelope's bite on each chord
     * (velocity deepens it), no high-pass (the body), a little of the sine after the filters */
    {"STRINGS", {68, 0, 100, 0, 0, 76, 4, 0}, {60, 100, 120, 70}, 22, 0, FX(0, 40, 0, 0),
     XP(P_LRATE + 1, 84, P_ATDST + 1, MD_VIB, P_VINT + 1, 24)},
    /* (showcase) strings under each finger: a key's pressure opens its filter, swells it, lowers its HPF and pulls
     * channel II sharp; the aftertouch's vibrato. (strings pass: the original's is a high, flute-like tone, not a
     * bare chiptune square) the soft pulse low-passed, most of it the sine after the filters, LFO 3 breathing; ENV 1 to
     * SHP a narrower pulse (its 2nd harmonic: the original's dry flutes' h2 -15, h3 -30 dB) */
    {"TOUCH STRING", {76, 0, 36, 0, 14, 68, 4, 0}, {66, 100, 120, 86}, 14, 0, FX(0, 45, 0, 0),
     XP(P_LRATE + 1, 84, P_ATDST + 1, MD_VIB, P_ED_SHP + 1, 32)},
    /* (showcase) TIMBR morphs the pad: the balance over to channel II's octave, the HPF and resonance up; LFO 2 sways
     * the balance */
    {"DRIFT PAD", {68, 12, 70, -16, 10, 52, 18, 0}, {96, 108, 120, 108}, 12, 0, FX(0, 45, 0, 0),
     XP(P_LRATE + 1, 80, P_ATDST + 1, MD_VIB, P_L2RATE + 1, 20)},
    {"PULSE PAD", {CSW_PLS, 0, 80, 0, 24, 82, 12, 0}, {84, 100, 120, 120}, -6, 0, FX(0, 45, 0, 0),
     XP(P_LRATE + 1, 56, P_LD_SHP + 1, 34, P_VINT + 1, 20)},
    {"SWEEP PAD", {CSW_SAW, 0, 60, 0, 10, 30, 50, 100}, {100, 124, 110, 110}, 40, 0, FX(0, 45, 0, 0),
     XP(P_VINT + 1, 24)},
    /* (strings pass) two layers in time: channel II's octave speaks first, bright (the filter envelope's bite), and
     * ENV 2 moves the balance back to channel I's body over a second (the background swelling in under it) */
    {"WARM PAD", {72, 12, 50, 20, 0, 80, 10, 0}, {80, 104, 120, 92}, 16, 0, FX(0, 55, 0, 0),
     XP(P_LRATE + 1, 60, P_LD_SHP + 1, 24, P_SPREAD + 1, 48)},
    /* keys */
    {"TOUCH LEAD", {CSW_SAW, 0, 24, 0, 6, 58, 14, 0}, {6, 88, 112, 50}, 30, 1, FX(0, 40, 0, 0),
     XP(P_ATDST + 1, MD_CUT, P_LRATE + 1, 88, P_GLIDE + 1, 14)},
    {"GLIDE LEAD", {60, 0, 40, -10, 8, 56, 10, 20}, {20, 90, 112, 70}, 34, 1, FX(0, 40, 0, 0),
     XP(P_ATDST + 1, MD_VIB, P_LRATE + 1, 86, P_GLIDE + 1, 24, P_LFADE + 1, 90)},
    {"BRASS PAD", {CSW_SAW, 0, 60, 0, 0, 44, 30, -20}, {70, 110, 116, 96}, 56, 0, FX(0, 20, 0, 0),
     XP(P_ATDST + 1, MD_CUT, P_VINT + 1, 16, P_SPREAD + 1, 32, P_E2ATK + 1, 96)},
    {"SOFT BRASS", {40, 0, 70, 0, 0, 58, 16, 0}, {44, 86, 104, 64}, 40, 0, FX(0, 20, 0, 0),
     XP(P_ATDST + 1, MD_CUT, P_VINT + 1, 16, P_SPREAD + 1, 32)},
    {"BUZZ BRASS", {44, 0, 50, 0, 0, 66, 26, 20}, {30, 84, 100, 56}, 46, 0, FX(0, 0, 0, 0),
     XP(P_ATDST + 1, MD_CUT, P_VINT + 1, 12, P_SPREAD + 1, 32)},
    /* organs */
    {"ORGAN", {80, 12, 10, 0, 16, 108, 12, 0}, {0, 40, 127, 24}, 0, 0, FX(0, 20, 0, 0)},
    {"REED ORGAN", {CSW_PLS, 12, 30, 0, 26, 96, 12, 0}, {10, 48, 127, 30}, 6, 0, FX(0, 20, 0, 0),
     XP(P_MAC2 + 1, 30, P_VINT + 1, 8, P_SPREAD + 1, 32)},
    {"PIPE ORGAN", {88, 12, 36, 0, 4, 96, 12, 0}, {30, 60, 127, 86}, 0, 0, FX(0, 20, 0, 0)},
    /* leads (and VOCAL PAD) */
    /* the solo synth lead: channel II an octave up under channel I, each key's pressure its brilliance,
     * velocity the IL, the wheel bringing the octave forward; the aftertouch's vibrato */
    {"SYNTH LEAD", {CSW_SAW, 12, 30, -24, 4, 72, 16, 40}, {10, 92, 110, 50}, 42, 1, FX(0, 40, 0, 0),
     XP(P_ATDST + 1, MD_VIB, P_LRATE + 1, 88, P_GLIDE + 1, 16)},
    /* a square lead: LFO 2's slow PWM drifting at LFO 3's pace, pressure on the filter and the level */
    {"SQUARE LEAD", {CSW_PLS, 0, 10, 0, 22, 70, 18, 0}, {4, 88, 116, 44}, 26, 1, FX(0, 40, 0, 0),
     XP(P_GLIDE + 1, 20, P_LRATE + 1, 88, P_ATDST + 1, MD_VIB, P_LFADE + 1, 84)},
    /* the original's vocal pad: pulse and sine an octave apart through the high- and low-pass band, the
     * band swaying (LFO 3 on CUT and the HPF opposite ways), LFO 2's PWM, each key's pressure swelling its note */
    {"VOCAL PAD", {76, 12, 60, 0, 30, 48, 36, 0}, {96, 92, 120, 100}, -8, 0, FX(0, 60, 0, 0),
     XP(P_LRATE + 1, 80, P_ATDST + 1, MD_VIB, P_L2RATE + 1, 40)},
    /* (showcase) the ribbon lead: the ribbon bends up to an octave either way, a key's pressure opens the filter and
     * lifts the pitch a little, velocity the filter envelope; the aftertouch's vibrato */
    {"RIBBON LEAD", {72, 0, 20, 0, 10, 60, 2, 60}, {4, 76, 110, 50}, 30, 1, FX(0, 40, 0, 0),
     XP(P_ATDST + 1, MD_VIB, P_LRATE + 1, 88, P_LFADE + 1, 84)},
    /* basses */
    {"PUNCH BASS", {CSW_SAW, -12, 30, 0, 0, 30, 20, 0}, {0, 80, 90, 24}, 40, 1, FX(0, 0, 0, 0),
     XP(P_ATDST + 1, MD_CUT)},
    {"FUNKY BASS", {CSW_PLS, -12, 10, 0, 6, 20, 16, 0}, {20, 78, 80, 20}, 8, 1, FX(0, 20, 0, 0),
     XP(P_VINT + 1, 8)},
    {"SOLID BASS", {CSW_SAW, -12, 50, -20, 0, 40, 12, 0}, {12, 80, 100, 30}, 30, 1, FX(0, 0, 0, 0),
     XP(P_ATDST + 1, MD_CUT)},
    /* a pluck, a pad, a stab */
    /* a poly synth pluck: velocity on the filter envelope and its IL, high notes shorter */
    {"SOFT PLUCK", {CSW_SAW, 0, 40, 0, 6, 48, 20, 40}, {0, 104, 20, 80}, 46, 0, FX(0, 20, 0, 0),
     XP(P_VINT + 1, 8, P_SPREAD + 1, 32)},
    /* the slow swelling pad: detuned saws, the filter envelope rising with the attack, ENV 2 a longer
     * swell on top, LFO 2 swaying the balance, each key's pressure opening and detuning its note */
    {"SWELL PAD", {CSW_SAW, 0, 80, 0, 4, 50, 20, 0}, {100, 118, 120, 110}, 36, 0, FX(0, 45, 0, 0),
     XP(P_VINT + 1, 24, P_LRATE + 1, 82, P_ATDST + 1, MD_VIB, P_L2RATE + 1, 30)},
    /* the poly brass stab: velocity on the filter envelope's depth and its IL, the key follow, each
     * key's pressure pulling channel II sharp; the aftertouch's brilliance */
    {"BIG BRASS", {CSW_SAW, 0, 56, 0, 0, 70, 24, 0}, {10, 84, 100, 40}, 46, 0, FX(0, 0, 0, 0),
     XP(P_ATDST + 1, MD_CUT, P_VINT + 1, 12, P_SPREAD + 1, 32, P_ED_PIT + 1, -6)},
    /* (Stallion, specs/touch R32) a key pressed harder bends its own note up: POLY AT -> PITCH, +2 semitones at 127 (two
     * slots, 18 and 4: AMT is squared, 18 alone is 190 cents; its cables in STAMPEDE_PATCHES): the original's own polyphonic
     * aftertouch on a brass lead. Last in the table (STAMPEDE_PATCHES points at it) */
    {"PRESS BEND", {CSW_SAW, 0, 54, 0, 12, 70, 18, 0}, {32, 98, 108, 70}, 36, 0, FX(0, 20, 0, 0),
     XP(P_SPREAD + 1, 32)},
};
#undef STA_FX0
/* (Stallion) the presets' matrix patching (core.h preset_patch_t): slot, source, destination, amount */
static const pcable_t STAC_VBRASS[] = {
    CB(1, VEL, E1 + 7, 30),                          /* IL: a harder strike starts the filter higher */
    CB(2, VEL, E1AMT, 36), CB(3, DC, E1AMT, -18),    /* the filter envelope: x 0.75 soft .. x 1.25 hard */
    CB(4, VEL, ERATE, 16),
    CB(5, PAT, CUT, 20),                             /* each key's pressure: its own brilliance */
};
static const pcable_t STAC_TSTR[] = {
    CB(1, PAT, CUT, 24), CB(2, PAT, AMP, 14),
    CB(3, PAT, E1 + 4, -10),                         /* HPF down: fuller */
    CB(4, PAT, PIT2, 5),                             /* channel II up to 15 cents sharp */
    CB(5, LFO3, CUT, 4),                             /* (strings pass) LFO 3: the tone breathes */
};
static const pcable_t STAC_MORPH[] = {
    CB(1, LFO2, E1 + 3, 12),                         /* the channels' balance swaying */
    CB(2, LFO3, CUT, 6),                             /* the filter breathing */
    CB(3, VEL, CUT, 6),
    CB(4, AT, CUT, 14),                              /* pressure: it opens */
};
static const pcable_t STAC_RIBBON[] = {
    CB(1, RIB, PITCH, 45),                           /* the ribbon: +-11.9 semitones */
    CB(2, PAT, CUT, 24), CB(3, PAT, PITCH, 6),       /* pressure: brighter, up to 21 cents up */
    CB(4, VEL, E1AMT, 30), CB(5, DC, E1AMT, -15),
};
static const pcable_t STAC_PRESS_BEND[] = {CB(1, PAT, PITCH, 18), CB(2, PAT, PITCH, 4)};
/* LIFE-BEGIN (the life pass: each preset a little movement and touch, by its kind; MATRIX-SHOWCASE.md) */
static const pcable_t LIFE_STA_1[] = {   /* BRASS SWELL (brass) */
    CB(1, ENV2, CUT, 10),         /* ENV 2 (the filter envelope's times): more of the slow rise */
    CB(2, VEL, E1AMT, 16),        /* velocity: the swell deeper */
    CB(3, AT, PIT2, 3),           /* pressure: the section pulls sharp */
    CB(4, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
    CB(5, MAC2, E1 + 3, 30),      /* TIMBR: BAL over to channel II */
};
static const pcable_t LIFE_STA_2[] = {   /* BRASS (brass) */
    CB(1, ENV2, CUT, 10),         /* ENV 2 (the filter envelope's times): more of the slow rise */
    CB(2, VEL, E1AMT, 16),        /* velocity: the swell deeper */
    CB(3, AT, PIT2, 3),           /* pressure: the section pulls sharp */
    CB(4, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
    CB(5, MAC2, E1 + 3, 30),      /* TIMBR: BAL over to channel II */
};
static const pcable_t LIFE_STA_3[] = {   /* BRIGHT BRASS (brass) */
    CB(1, ENV2, CUT, 10),         /* ENV 2 (the filter envelope's times): more of the slow rise */
    CB(2, VEL, E1AMT, 16),        /* velocity: the swell deeper */
    CB(3, AT, PIT2, 3),           /* pressure: the section pulls sharp */
    CB(4, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
    CB(5, MAC2, E1 + 3, 30),      /* TIMBR: BAL over to channel II */
};
static const pcable_t LIFE_STA_4[] = {   /* MELLOW BRASS (brass) */
    CB(1, ENV2, CUT, 10),         /* ENV 2 (the filter envelope's times): more of the slow rise */
    CB(2, VEL, E1AMT, 16),        /* velocity: the swell deeper */
    CB(3, AT, PIT2, 3),           /* pressure: the section pulls sharp */
    CB(4, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
    CB(5, MAC2, E1 + 3, 30),      /* TIMBR: BAL over to channel II */
};
static const pcable_t LIFE_STA_6[] = {   /* OCTAVE BRASS (brass) */
    CB(2, VEL, E1AMT, 16),        /* velocity: the swell deeper */
    CB(3, AT, PIT2, 3),           /* pressure: the section pulls sharp */
    CB(4, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
    CB(5, MAC2, E1 + 3, 30),      /* TIMBR: BAL over to channel II */
};
static const pcable_t LIFE_STA_7[] = {   /* STRINGS (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, PIT2, 2),         /* LFO 2 (1 Hz): the second oscillator beating slowly */
    CB(3, VEL, CUT, 12),          /* velocity: brighter (the strings pass: the bite follows the strike) */
    CB(4, MAC2, E1 + 3, 30),      /* TIMBR: BAL over to channel II */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_STA_10[] = {   /* PULSE PAD (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, SHAPE, 6),        /* LFO 2 (1 Hz): a slow PWM / wave motion */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 3, 30),      /* TIMBR: BAL over to channel II */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_STA_11[] = {   /* SWEEP PAD (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, PIT2, 2),         /* LFO 2 (1 Hz): the second oscillator beating slowly */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 3, 30),      /* TIMBR: BAL over to channel II */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_STA_12[] = {   /* WARM PAD (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, SHAPE, 6),        /* LFO 2 (1 Hz): a slow PWM / wave motion */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 3, 30),      /* TIMBR: BAL over to channel II */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
    CB(6, ENV2, E1 + 3, -50),     /* ENV 2 (slow): BAL back from channel II's octave to channel I's body */
};
static const pcable_t LIFE_STA_13[] = {
    CB(1, VEL, CUT, 10),          /* velocity: brighter */
    CB(2, AT, PIT2, 3),           /* pressure: a little sharp as it opens */
    CB(3, KEY, CUT, -6),
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MW, E1 + 3, 30),        /* the wheel: channel II in */
};
static const pcable_t LIFE_STA_14[] = {
    CB(1, VEL, CUT, 10),          /* velocity: brighter */
    CB(2, AT, CUT, 12),           /* pressure: brighter as the vibrato comes */
    CB(3, KEY, CUT, -6),
    CB(4, MAC2, E1 + 3, 30),      /* TIMBR: BAL over to channel II */
};
static const pcable_t LIFE_STA_15[] = {
    CB(1, VEL, CUT, 12),          /* velocity: the swell brighter */
    CB(2, VEL, ERATE, 8),         /* harder: quicker */
    CB(3, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(4, MAC2, E1 + 3, 30),      /* TIMBR: BAL over to channel II */
};
static const pcable_t LIFE_STA_16[] = {
    CB(1, VEL, CUT, 10),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 12),        /* velocity: the envelope deeper */
    CB(3, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(4, MAC2, E1 + 3, 30),      /* TIMBR: BAL over to channel II */
};
static const pcable_t LIFE_STA_17[] = {
    CB(1, VEL, CUT, 12),          /* velocity: brighter, more ring */
    CB(2, VEL, E1AMT, 16),        /* velocity: the envelope deeper */
    CB(3, KEY, ERATE, 8),         /* high notes quicker */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
};
static const pcable_t LIFE_STA_18[] = {   /* ORGAN (organ) */
    CB(1, LFO2, PIT2, 2),         /* LFO 2: the second oscillator beating slowly */
    CB(2, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(3, MAC2, E1 + 3, 30),      /* TIMBR: BAL over to channel II */
    CB(4, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_STA_19[] = {   /* REED ORGAN (organ) */
    CB(1, LFO2, PIT2, 2),         /* LFO 2: the second oscillator beating slowly */
    CB(2, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(3, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_STA_20[] = {   /* PIPE ORGAN (organ) */
    CB(1, LFO2, PIT2, 2),         /* LFO 2: the second oscillator beating slowly */
    CB(2, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(3, MAC2, E1 + 3, 30),      /* TIMBR: BAL over to channel II */
    CB(4, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_STA_21[] = {   /* SYNTH LEAD (lead) */
    CB(1, PAT, CUT, 20),          /* the key's pressure: its brilliance */
    CB(2, ENV2, PITCH, 5),        /* ENV 2: a small scoop into the note */
    CB(3, VEL, E1 + 7, 24),       /* velocity: the IL higher */
    CB(4, LFO3, CUT, 4),          /* LFO 3: a slow shimmer */
    CB(5, MW, E1 + 3, 30),        /* the wheel: BAL over to channel II's octave */
};
static const pcable_t LIFE_STA_22[] = {   /* SQUARE LEAD (lead) */
    CB(1, LFO2, SHAPE, 10),       /* LFO 2 (slow): the PWM */
    CB(2, LFO3, L2RATE, 14),      /* LFO 3: the PWM's pace drifts */
    CB(3, AT, CUT, 14),           /* pressure: brighter */
    CB(4, VEL, E1AMT, 24),        /* velocity: the filter envelope deeper */
    CB(5, AT, AMP, 6),            /* pressure: a swell */
};
static const pcable_t LIFE_STA_23[] = {   /* VOCAL PAD (pad) */
    CB(1, LFO2, SHAPE, 8),        /* LFO 2: the PWM */
    CB(2, LFO3, CUT, 6),          /* LFO 3: the band moving up .. */
    CB(3, LFO3, HPF, -6),         /* .. its low edge the other way: a vowel-like sway */
    CB(4, PAT, AMP, 10),          /* the key's pressure: its note swells */
    CB(5, PAT, CUT, 12),          /* .. and opens */
};
static const pcable_t LIFE_STA_25[] = {
    CB(1, VEL, CUT, 12),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 14),        /* velocity: the envelope deeper */
    CB(3, AT, CUT, 12),           /* pressure: it growls open */
};
static const pcable_t LIFE_STA_26[] = {   /* FUNKY BASS (bass) */
    CB(1, VEL, CUT, 12),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 12),        /* velocity: the envelope deeper */
    CB(3, ENV2, E1 + 6, 10),      /* ENV 2: a resonant bite on the attack */
    CB(4, MAC2, E1 + 3, 30),      /* TIMBR: BAL over to channel II */
};
static const pcable_t LIFE_STA_27[] = {
    CB(1, VEL, CUT, 12),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 14),        /* velocity: the envelope deeper */
    CB(3, AT, CUT, 10),           /* pressure: brighter */
};
static const pcable_t LIFE_STA_28[] = {
    CB(1, VEL, CUT, 14),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 16),        /* velocity: the envelope deeper */
    CB(3, KEY, ERATE, 8),         /* high notes shorter */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
};
static const pcable_t LIFE_STA_29[] = {   /* SWELL PAD (pad) */
    CB(1, ENV2, CUT, 20),         /* ENV 2: a slower swell than the filter envelope's */
    CB(2, LFO3, CUT, 5),          /* LFO 3: the filter breathes */
    CB(3, PAT, CUT, 16),          /* the key's pressure: its brilliance */
    CB(4, PAT, PIT2, 4),          /* .. and channel II sharper */
    CB(5, LFO2, E1 + 3, 10),      /* LFO 2: the balance sways */
};
static const pcable_t LIFE_STA_30[] = {
    CB(1, VEL, CUT, 10),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 14),        /* velocity: the envelope deeper */
    CB(3, AT, CUT, 10),           /* pressure: brighter */
};
/* LIFE-END */
static const preset_patch_t STAMPEDE_PATCHES[] = {
    PPATCH(&STAMPEDE_PRESETS[1], LIFE_STA_1, P_LRATE + 1, 86, P_LFADE + 1, 84, P_E2DEC + 1, 106, P_E2REL + 1, 78),    /* BRASS SWELL */
    PPATCH(&STAMPEDE_PRESETS[2], LIFE_STA_2, P_LRATE + 1, 86, P_LFADE + 1, 84, P_E2DEC + 1, 102, P_E2REL + 1, 54),    /* BRASS */
    PPATCH(&STAMPEDE_PRESETS[3], LIFE_STA_3, P_LRATE + 1, 86, P_LFADE + 1, 84, P_E2DEC + 1, 98, P_E2REL + 1, 70),    /* BRIGHT BRASS */
    PPATCH(&STAMPEDE_PRESETS[4], LIFE_STA_4, P_LRATE + 1, 86, P_LFADE + 1, 84, P_E2DEC + 1, 102, P_E2REL + 1, 54),    /* MELLOW BRASS */
    PPATCH(&STAMPEDE_PRESETS[5], STAC_VBRASS, P_E2REL + 1, 86),            /* VEL BRASS */
    PPATCH(&STAMPEDE_PRESETS[6], LIFE_STA_6, P_LRATE + 1, 86, P_LFADE + 1, 84, P_E2DEC + 1, 96, P_E2REL + 1, 40),    /* OCTAVE BRASS */
    PPATCH(&STAMPEDE_PRESETS[7], LIFE_STA_7, P_LFADE + 1, 70),    /* STRINGS */
    PPATCH(&STAMPEDE_PRESETS[8], STAC_TSTR),              /* TOUCH STRING */
    PPATCH(&STAMPEDE_PRESETS[9], STAC_MORPH),             /* DRIFT PAD */
    PPATCH(&STAMPEDE_PRESETS[10], LIFE_STA_10),    /* PULSE PAD */
    PPATCH(&STAMPEDE_PRESETS[11], LIFE_STA_11),    /* SWEEP PAD */
    PPATCH(&STAMPEDE_PRESETS[12], LIFE_STA_12, P_E2ATK + 1, 90, P_E2DEC + 1, 110, P_E2SUS + 1, 127),    /* WARM PAD */
    PPATCH(&STAMPEDE_PRESETS[13], LIFE_STA_13),    /* TOUCH LEAD */
    PPATCH(&STAMPEDE_PRESETS[14], LIFE_STA_14),    /* GLIDE LEAD */
    PPATCH(&STAMPEDE_PRESETS[15], LIFE_STA_15),    /* BRASS PAD */
    PPATCH(&STAMPEDE_PRESETS[16], LIFE_STA_16),    /* SOFT BRASS */
    PPATCH(&STAMPEDE_PRESETS[17], LIFE_STA_17),    /* BUZZ BRASS */
    PPATCH(&STAMPEDE_PRESETS[18], LIFE_STA_18),    /* ORGAN */
    PPATCH(&STAMPEDE_PRESETS[19], LIFE_STA_19),    /* REED ORGAN */
    PPATCH(&STAMPEDE_PRESETS[20], LIFE_STA_20),    /* PIPE ORGAN */
    PPATCH(&STAMPEDE_PRESETS[21], LIFE_STA_21, P_LFADE + 1, 84),    /* SYNTH LEAD */
    PPATCH(&STAMPEDE_PRESETS[22], LIFE_STA_22, P_L2RATE + 1, 34),    /* SQUARE LEAD */
    PPATCH(&STAMPEDE_PRESETS[23], LIFE_STA_23, P_L3RATE + 1, 30),    /* VOCAL PAD */
    PPATCH(&STAMPEDE_PRESETS[24], STAC_RIBBON),           /* RIBBON LEAD */
    PPATCH(&STAMPEDE_PRESETS[25], LIFE_STA_25),    /* PUNCH BASS */
    PPATCH(&STAMPEDE_PRESETS[26], LIFE_STA_26),    /* FUNKY BASS */
    PPATCH(&STAMPEDE_PRESETS[27], LIFE_STA_27),    /* SOLID BASS */
    PPATCH(&STAMPEDE_PRESETS[28], LIFE_STA_28),    /* SOFT PLUCK */
    PPATCH(&STAMPEDE_PRESETS[29], LIFE_STA_29, P_E2ATK + 1, 110, P_E2DEC + 1, 100, P_E2SUS + 1, 110, P_E2REL + 1, 100),    /* SWELL PAD */
    PPATCH(&STAMPEDE_PRESETS[30], LIFE_STA_30, P_E2REL + 1, 70),    /* BIG BRASS */
    PPATCH(&STAMPEDE_PRESETS[NELEM(STAMPEDE_PRESETS) - 1u], STAC_PRESS_BEND),             /* PRESS BEND */
};

static const engine_t ENG_STAMPEDE = {
    "STAMPEDE", {"CH I/II", "VCF"},
    {
        {"WAVE", F_INT, 0, 127, CSW_SAW, N_CSWAVE, 0},
        {"FEET", F_SEMI, -24, 24, 0, 0, 0},
        {"DTUNE", F_PCT, 0, 127, 50, 0, 0},
        {"BAL", F_BIPCT, -64, 63, 0, 0, 0},
        {"HPF", F_CUTOFF, 0, 127, 0, 0, 0},
        {"LPF", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"IL", F_INT, -64, 127, 0, 0, 0},   /* (below 0: it starts under the cutoffs as set) */
    },
    STAMPEDE_PRESETS, sizeof(STAMPEDE_PRESETS) / sizeof(STAMPEDE_PRESETS[0]), 1, stampede_note_on, stampede_render,
    0xFA60, {P_E5, P_E6, P_E2, P_E4}, 0, 0, 0, 0, 0, 0,
    1, 0, 5,                                         /* (two VCOs, channel I's and II's; the matrix's HPF: HPF) */
};
