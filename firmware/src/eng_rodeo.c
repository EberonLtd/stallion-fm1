/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 zvenson (OberVave: the oscillators, the hard sync, the X-MOD, the filter), 3dSam (Clipper) */
/* RODEO: the voice of an 8-voice poly of 1979-84, after zvenson's OberVave (GPL-3.0-only, github.com/zvenson/obervave,
 * the same lineage: Felucca, SLOOP, Dinghy): its oscillators (naive saw / pulse, polyBLEP corrections one sample
 * late, a wrap and a hard-sync reset the same event), its X-MOD (OSC 2 -> OSC 1, linear) and its trapezoidal SVF
 * (the band-pass state saturating softly: a warm resonance that settles, never runs away) in its six modes. The rest
 * is Clipper's: ENV 1 (ENV DEST FLT the filter's contour), the LFOs, the matrix, VOICE (MONO, UNISON, UNI2 for
 * OberVave's DUAL), glide, VINTAGE (each voice card's tuning, drift and cutoff), SPREAD and the effects.
 * EDIT 1 (OSC)  W1  W2  OSC2  DT/XM        EDIT 2 (VCF)  MIX  CUT  RES  MODE
 *   W1    OSC 1: OFF, SAW, PLS, BOTH (saw + pulse); OFF still runs it, the master of the sync
 *   W2    OSC 2: OFF, SAW, PLS, BOTH, NOISE (white, in OSC 2's place), and SYSAW SYPLS SYBTH: hard-synced to OSC 1
 *   OSC2  OSC 2's interval, -24 .. +24 semitones
 *   DT/XM right of 0 DTUNE: OSC 2 a little above, up to +50 cents (squared: fine near 0); left of 0 XMOD: OSC 2
 *         bends OSC 1's frequency at audio rate, linearly (OberVave's X-MOD): bells, metal, growls
 *   MIX   -64 OSC 1 alone, 0 both (-3 dB each), +63 OSC 2 alone
 *   CUT RES  the SVF: RES up to 121 rings and settles; from 124 to 127 it sings, as the originals at full resonance,
 *         bounded by the band-pass state's soft clip (ROD_SOSC2, ROD_SOSC4); ENV DEST FLT
 *   MODE  LP12, HP12, BP12, NOTCH, LP24 (a second, plain stage), MORPH (the 1970s SVF voice's LP .. notch .. HP pot)
 * EDIT 3 (Stallion) KYBD 0..100 %: how far the filter follows the key (rod_kt). 0 (the default, every factory sound): none,
 *         the cutoff fixed while the notes move, as measured on the originals (the 1970s SVF voice 0.03 +- 0.03
 *         oct/oct, two units of the 1981 4-pole model 0.02 +- 0.02); 86 % (ROD_KYB_OLD) the
 *         tracking every sound had before, ROD_KTRK; 100 % one octave of cutoff an octave of the keys
 *         PW0: the width the pulse rests at (SHAPE moves it from there as before): 46.8 %, 49.5 % (the default), 53 %
 *         (every sound's before); the SVF-voice-type sounds 46.8, the others 49.5 (ROD_PW_REST)
 * SHAPE (LFO / ENV DEST SHP, MW / AT SHP, the matrix, TIMBR): the pulse width, PW0 at the middle (ROD_PW_REST), ~4 % at either
 * side (OberVave's PW and PWM: TIMBR sets the width a preset rests at); with a synced OSC 2: OSC 2's pitch, up to
 * +-32 semitones (OberVave's ENV > O2: ENV DEST SHP sweeps the sync); in MORPH: the morph, LP at the middle, notch a
 * quarter out, HP at either end (ENV / LFO to SHP sweep it). The drive: FX > DIST, the INSERT.
 * Not ported (Clipper has its own): OberVave's envelopes, LFO, arpeggiator, sequencer, effects, voice allocation.
 * Voice state: ph[0] OSC 1, ph[1] OSC 2, ph[2] the noise; s[0], s[1] the SVF, s[2], s[3] LP24's second stage, s[4],
 * s[5] OSC 1's and OSC 2's sample still open to a correction (s[5]: also the X-MOD's source) */
enum { OW_OFF, OW_SAW, OW_PLS, OW_BOTH, OW_NOISE, OW_SYSAW, OW_SYPLS, OW_SYBTH };
enum { OM_LP12, OM_HP12, OM_BP12, OM_NOTCH, OM_LP24, OM_MORPH };
static const char *const N_OW1[] = {"OFF", "SAW", "PLS", "BOTH"};
static const char *const N_OW2[] = {"OFF", "SAW", "PLS", "BOTH", "NOISE", "SYSAW", "SYPLS", "SYBTH"};
static const char *const N_OM[] = {"LP12", "HP12", "BP12", "NOTCH", "LP24", "MORPH"};
static const char *const N_OPW[] = {"46.8", "49.5", "53"};   /* (Stallion) PW0, % (ROD_PW_REST) */
static const param_desc_t ROD_DTUNE = {"DTUNE", F_INT, -64, 63, 19, 0, 0};   /* = edit[3], its label by its side */
static const param_desc_t ROD_XMOD = {"XMOD", F_INT, -64, 63, 19, 0, 0};

static const param_desc_t *rodeo_desc(const track_t *t, uint32_t k)
{
    return k == 3u ? (t->p[P_E3] < 0 ? &ROD_XMOD : &ROD_DTUNE) : 0;
}

static void rodeo_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (v->env < (1 << 17)) {                        /* from silence, or a tail below -42 dB (voice.c voice_start keeps
                                                      * such a voice's phases as "sounding": REL 109's tail outlasts 4 s,
                                                      * the same note again took it, DTUNE had moved OSC 2 anywhere: AIR
                                                      * CHOIR's POLY notes -8 .. +7 dB, UNI4 up to +11.5 over POLY at
                                                      * random): the filter clear (the oscillators run on) */
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = v->s[4] = v->s[5] = v->s[6] = 0;
        if (!v->ph[2]) {                             /* the first note: free-running, they start anywhere */
            v->ph[2] = 0x12345678u * (v->note + 1u) + 0x9E3779B9u;
            v->ph[0] = noise32((int32_t *)&v->ph[2]);
            v->ph[1] = noise32((int32_t *)&v->ph[2]);
        }
        v->ph[1] = v->ph[0] + 0x40000000u;           /* (Stallion) OSC 2 a quarter on from OSC 1, as STAMPEDE's channel II:
                                                      * left where the last note (or another engine's voice) had them,
                                                      * near half a turn they cancelled, a POLY note down at random (BP
                                                      * SWEEP PAD: UNI4 +9.1 dB over it, the draw moved by one more
                                                      * preset); a quarter is the mean level of random phases, every
                                                      * time. A note over an audible voice keeps its phases (free-running) */
    }
}

/* UNISON (voice.c unison_spread): OSC 1 and OSC 2 moved by r (Q32) of a turn of the note, OSC 2 by its share (OSC2,
 * DTUNE above): as if started that much sooner, their relative phase as the voice had it; the noise from its own place
 * (never 0: the xorshift would stop). (Before: ph[1] random of its own, voice.c uni_phases, kept by the voice for the
 * notes it played after the stack: near half a turn the two oscillators cancelled, a POLY note down (BP SWEEP PAD
 * -5 dB) and UNISON / UNI4 up to +8.8 dB over it) */
static void rodeo_spread(track_t *t, voice_t *v, uint32_t r)
{
    int32_t dt = clamp(t->p[P_E3], -64, 63);
    v->ph[0] += r;
    v->ph[1] += uni_turns(r, v->pitch16, clamp(t->p[P_E2], -24, 24) * 16 + (dt > 0 ? (dt * dt * 2048 / 3969) >> 8 : 0));
    v->ph[2] ^= r;
    if (!v->ph[2])
        v->ph[2] = 1u;
}

/* the naive wave: bit 0 the saw, bit 1 the pulse (saw(ph) - saw(ph + pw): mean 0); -98304 .. 98303 */
static inline int32_t rod_naive(uint32_t ph, uint32_t pw, uint32_t w)
{
    int32_t x = 0;
    if (w & 1u)
        x = (int32_t)(ph >> 16) - 32768;
    if (w & 2u)
        x += (int32_t)(ph >> 16) - (int32_t)((ph + pw) >> 16);
    return x;
}
/* a jump of h at t (0..32767: 0..1 sample) before the current sample: + h t^2 / 2 on the one before, - h (1 - t)^2 / 2
 * on this one */
static inline void rod_event(int32_t *prev, int32_t *cur, int32_t h, int32_t t)
{
    int32_t u = 32767 - t, h16 = h >> 4;
    *prev += (h16 * ((t * t) >> 15)) >> 12;
    *cur -= (h16 * ((u * u) >> 15)) >> 12;
}
/* one sample of an oscillator: returns the one before (corrected); *pv then holds this one's. hw: the height of a wrap
 * (-65536 a saw component); rt >= 0: a sync reset rt before this sample */
static inline int32_t rod_step(uint32_t *php, int32_t *pv, uint32_t inc, uint32_t d, uint32_t pw, uint32_t w, int32_t hw,
                              int32_t rt)
{
    uint32_t old = *php, ph = old + inc;
    int32_t cur, out = *pv, q;
    if (rt < 0 || !d) {
        cur = rod_naive(ph, pw, w);
        if (ph < inc && d) {                         /* wrapped: the saw(s) drop */
            q = (int32_t)(ph / d);
            rod_event(&out, &cur, hw, q > 32767 ? 32767 : q);
        }
        if ((w & 2u) && ph + pw < inc && d) {        /* the subtracted saw wrapped: the pulse rises */
            q = (int32_t)((ph + pw) / d);
            rod_event(&out, &cur, 65536, q > 32767 ? 32767 : q);
        }
    } else {
        uint32_t at = ph - d * (uint32_t)rt;         /* the phase at the reset */
        ph = d * (uint32_t)rt;                       /* reset, then rt on */
        cur = rod_naive(ph, pw, w);
        if (at < old) {                              /* it wrapped on the way to the reset */
            q = (int32_t)(at / d) + rt;
            rod_event(&out, &cur, hw, q > 32767 ? 32767 : q);
        }
        if ((w & 2u) && at + pw < old + pw) {        /* ph + pw wrapped on the way */
            q = (int32_t)((at + pw) / d) + rt;
            rod_event(&out, &cur, 65536, q > 32767 ? 32767 : q);
        }
        rod_event(&out, &cur, rod_naive(0, pw, w) - rod_naive(at, pw, w), rt);
    }
    *php = ph;
    *pv = cur;
    return out;
}

/* the trapezoidal SVF (A. Simper) on Clipper's cutoff steps (SVF_G), coefficients Q13, damping k Q12 (2.0 = 8192,
 * below 0 at RES 127: held by the band-pass state's tanh) */
typedef struct { int32_t a1, a2, a3, k; } rod_svf_t;
static inline void rod_coef(rod_svf_t *c, int32_t cut, int32_t k)   /* cut 0..127 << 8 */
{
    int32_t i, g, den;
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + k)) >> 12);
    if (den < 64)
        den = 64;
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
    c->k = k;
}
static inline void rod_svf(const rod_svf_t *c, int32_t *s, int32_t in, int32_t *lp, int32_t *bp, int32_t *hp)
{
    int32_t v3 = in - s[1];
    int32_t v1 = (c->a1 * s[0] + c->a2 * v3) >> 13;
    int32_t v2 = s[1] + ((c->a2 * s[0] + c->a3 * v3) >> 13);
    int32_t n1 = 2 * v1 - s[0];
    s[0] = (uint32_t)(n1 + 24576) < 49152u ? n1 : softclip(n1 >> 1) << 1;   /* linear below +-0.75, then soft */
    s[1] = 2 * v2 - s[1];
    *lp = v2;
    *bp = v1;
    *hp = in - ((c->k * v1) >> 12) - v2;
}
static inline int32_t rod_svf_lp(const rod_svf_t *c, int32_t *s, int32_t in)   /* LP24's second stage, plain */
{
    int32_t v3 = in - s[1];
    int32_t v1 = (c->a1 * s[0] + c->a2 * v3) >> 13;
    int32_t v2 = s[1] + ((c->a2 * s[0] + c->a3 * v3) >> 13);
    s[0] = 2 * v1 - s[0];
    s[1] = 2 * v2 - s[1];
    return v2;
}

/* (Stallion) the filter follows the key nearly fully (was 9, half): 16 = 0.85-0.92 octave of cutoff an octave at
 * CUT 60-80, the 1970s SVF voice's sequenced line D1-G3 measures 1.0 on the original; 0 B */
#ifndef ROD_KTRK
#define ROD_KTRK 16
#endif
/* (Stallion) KYBD (P_OKYB, EDIT 3) k % on d (the note from C4, 1/16 semitone): cutoff steps (Q8). ROD_KYB_OLD % is
 * ROD_KTRK, the tracking before KYBD, exactly (the gain, Q10, ROD_KTRK << 10: ROD_KYB_C rounded up, 86 x it over ROD_KTRK << 16 by
 * 22, under the 64 the >> 6 drops; a host-tuned ROD_KTRK may round its default by 1/1024); 100 %: 18.6 steps a
 * sixteenth, one octave of cutoff an octave (the SVF_G scale: 14.02 steps an octave); 0: none. Per block, multiplies
 * and shifts (no division) */
#define ROD_KYB_OLD 86
#define ROD_KYB_C ((ROD_KTRK * (1 << 16) + ROD_KYB_OLD - 1) / ROD_KYB_OLD)
static inline int32_t rod_kt(int32_t d, int32_t k)
{
    return (d * ((k * ROD_KYB_C) >> 6)) >> 10;
}
/* (Stallion) the "square" a little off the middle, as an analog VCO's (an exact 50 %: evens -50 .. -66 dB, a hollow
 * digital square). (Stallion) EDIT 3 PW0, per sound, the width the pulse rests at (ROD_PW_REST): 46.8 % the 1970s
 * SVF voice's (measured on the original: 46.75 %, the PW knob at its middle assumed), 49.5 % the 1981 4-pole model's
 * (two units measured, 49.5-50 % and 49-49.25 %; the 1979 and 1983 models unmeasured, the same taken),
 * ROD_PW0, the default (INIT, a new sound); 53 % the width every sound had before (the SVF voice's held C1 read as
 * evens -18 .. -19 dB) */
#ifndef ROD_PW0
#define ROD_PW0 0x7EB851ECu
#endif
static const uint32_t ROD_PW_REST[3] = {0x77AE147Bu, ROD_PW0, 0x87AE147Bu};
/* (Stallion) the resonance: RES 0 .. 121 takes the SVF's damping k from 2.0 (8192) down by RES * ROD_RESK / 127
 * (Q12). Like ROD_KTRK, ROD_PW0, ROD_KF and ROD_LADDER above, a host build may set it (-DROD_RESK=.., for tuning
 * on the host); the device build never does, so these #ifndef lines cost it nothing */
#ifndef ROD_RESK
#define ROD_RESK 8400
#endif
/* (rodeo-types) LP24 as the 1981 / 1983 models' 4-pole filter chip: four equal poles (two SVF stages at k 2) and the
 * resonance a feedback tap from the output round all four (one sample late), so the bass falls as RES rises (DC 1 /
 * (1 + 4 kf)), not a resonant 2-pole into a plain one. ROD_LADDER 0: the old cascade (for the before / after renders) */
#ifndef ROD_KF
#define ROD_KF 30                                     /* RES 121: loop gain 3.5 of the 4 that sings */
#endif
#ifndef ROD_LADDER
#define ROD_LADDER 1
#endif
/* (Stallion) the top of RES sings, as the originals do at full resonance (measured on the original: both units of the
 * 1981 model a clear sine ~11 dB over the note; the 1970s SVF voice self-oscillates too): RES 0 .. 121 as before, each
 * step past 121 takes the 2-pole SVF's damping down by ROD_SOSC2 (negative from RES 124 on: it rings up, bounded by the
 * band-pass state's soft clip) and LP24's loop gain up by ROD_SOSC4 (Q10: 3.5 at 121, 4.9 at 124, 6.2 at 127; the loop
 * one sample late sings from ~4.7, not 4: measured, rodeo_test; bounded by the same clip in the loop's first stage).
 * Every factory sound sits at RES 66 or under: none moves */
#ifndef ROD_SOSC2
#define ROD_SOSC2 80
#endif
#ifndef ROD_SOSC4
#define ROD_SOSC4 450
#endif
static void rodeo_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *ev = vm_ev(t, m);              /* the EDIT values: the voice's own or the part's */
    uint32_t w1 = (uint32_t)clamp(ev[0], 0, 3), w2 = (uint32_t)clamp(ev[1], 0, 7), mode = (uint32_t)clamp(ev[7], 0, 5);
    uint32_t sync = w2 >= OW_SYSAW, nois = w2 == OW_NOISE, i, inc2, pw, d2;
    uint32_t inc1 = m->vo1 ? hy_inc(m->pitch16, m->fine, m->vo1) : m->inc, d1 = inc1 >> 15;   /* (vo1, vo2: each its
                                                      * own pitch) */
    uint32_t ph1 = v->ph[0], ph2 = v->ph[1], nz = v->ph[2];
    int32_t dev = m->shape - (64 << 8), adev = dev < 0 ? -dev : dev, dt = clamp(ev[3], -64, 63), mix = clamp(ev[4], -64, 63);
    int32_t xq = dt < 0 ? clamp((dt * dt) >> 5, 0, 127) : 0, mq = mode == OM_MORPH ? (adev >> 7 > 128 ? 128 : adev >> 7) : 0;
    int32_t g1 = clamp(mix <= 0 ? 11469 - mix * 77 : 11469 - mix * 182, 0, 16384);   /* Q14: -3 dB each at 0 */
    int32_t g2 = clamp(mix >= 0 ? 11469 + mix * 78 : 11469 + mix * 179, 0, 16384);
    int32_t cut = (ev[5] << 8) + m->cutoff + rod_kt(m->pitch16 - 60 * 16, t->p[P_OKYB]);   /* (+ the key: KYBD) */
    int32_t pv1 = v->s[4], pv2 = v->s[5], hw1, hw2, kf, res = clamp(ev[6], 0, 127), top = res > 121 ? res - 121 : 0;
    rod_svf_t c1, c2;
    if (sync)
        w2 -= 4u;                                    /* (SYSAW .. SYBTH: SAW .. BOTH, synced) */
    else if (nois)
        w2 = 0;
    hw1 = (w1 & 1u ? -65536 : 0) + (w1 & 2u ? -65536 : 0);
    hw2 = (w2 & 1u ? -65536 : 0) + (w2 & 2u ? -65536 : 0);
    pw = ROD_PW_REST[clamp(t->p[P_OPW], 0, 2)];        /* (PW0: the rest width) */
    pw += sync || mode == OM_MORPH ? 0u : (uint32_t)(clamp(dev, -8192, 8192) * 241000);
    inc2 = hy_inc(m->pitch16 + clamp(ev[2], -24, 24) * 16 + (sync ? dev >> 5 : 0), m->fine,
                  (dt > 0 ? dt * dt * 2048 / 3969 : 0) + m->vo2);
    d2 = inc2 >> 15;
    res -= top;                                      /* (0 .. 121, as before; top: the steps past it, ROD_SOSC2) */
    rod_coef(&c1, cut, ROD_LADDER && mode == OM_LP24 ? 8192 : 8192 - res * ROD_RESK / 127 - top * ROD_SOSC2);
    if (mode == OM_LP24)
        rod_coef(&c2, cut, ROD_LADDER ? 8192 : 5793);
    kf = ROD_LADDER && mode == OM_LP24 ? res * ROD_KF + top * ROD_SOSC4 : 0;   /* Q10: the loop gain */
    for (i = 0; i < n; i++) {
        int32_t s1, s2, x, lp, bp, hp, y, rt = -1;
        uint32_t ie = inc1, d = d1;
        if (xq) {                                    /* X-MOD: OSC 2 -> OSC 1, linear, never below inc / 16 */
            int64_t k = (int64_t)(inc1 >> 8 > 0x200000u ? 0x200000 : (int32_t)(inc1 >> 8)) * (((pv2 >> 8) * xq) >> 6) + inc1;
            ie = k < (int64_t)(inc1 >> 4) ? inc1 >> 4 : k > 0x70000000 ? 0x70000000u : (uint32_t)k;
            d = ie >> 15;
        }
        s1 = rod_step(&ph1, &pv1, ie, d, pw, w1, hw1, -1);
        if (sync && ph1 < ie && d) {                 /* OSC 1 wrapped: OSC 2 restarts there */
            rt = (int32_t)(ph1 / d);
            rt = rt > 32767 ? 32767 : rt;
        }
        if (nois) {
            s2 = pv2;                                /* (one sample late, as the oscillators) */
            pv2 = (int32_t)(noise32((int32_t *)&nz) >> 16) - 32768;
        } else {
            s2 = rod_step(&ph2, &pv2, inc2, d2, pw, w2, hw2, rt);
        }
        x = ((s1 * g1) >> 14) + ((s2 * g2) >> 14) - ((kf * v->s[6]) >> 10);
        x = soft_knee(x, 49152);                     /* (two BOTHs: the input rounds off, ~+-65536) */
        rod_svf(&c1, v->s, x, &lp, &bp, &hp);
        switch (mode) {
        case OM_LP12: y = lp; break;
        case OM_HP12: y = hp; break;
        case OM_BP12: y = bp; break;
        case OM_NOTCH: y = hp + lp; break;
        case OM_MORPH: y = lp + (((hp - lp) * mq) >> 7); break;
        default: y = v->s[6] = rod_svf_lp(&c2, v->s + 2, lp); break;
        }
        y = clamp(y, -65000, 65000);
        out[i] += voice_amp(y, m, i);
    }
    v->ph[0] = ph1;
    v->ph[1] = ph2;
    v->ph[2] = nz;
    v->s[4] = pv1;
    v->s[5] = pv2;
}

/* (Stallion 1.0) the factory sounds: the 1979 / 1981 / 1983 models' programs that made the sound famous (the 1983
 * model's factory list: brass ensemble, strings, pulse-width pads, clav keys, organs, solos, basses, plucks; the big
 * detuned saw chords of 1984-era rock and the stabs of Minneapolis funk), rebuilt here; dry (no sends, no arp),
 * centred.
 * (rodeo-types) Each preset follows one of the originals, tuned to that instrument alone. The 1970s SVF voice (12 dB
 * SVF, the LP-notch-HP pot, BP; bass, sweeps, raw and vocal): MORPH PAD, SYNTH CHOIR, FUNKY KEYS, BP SWEEP PAD, SOLO
 * LEAD, PULSE LEAD, SYNTH BASS, SVF BASS, MORPH PLUCK. The 1979 model (12 dB SVF, brassy, sync, X-MOD): SAW STAB, FUNK
 * STAB, SWELL BRASS, TOUCH BRASS, WHEEL STRING, XMOD KEYS, SYNTH PIANO, XMOD LEAD, SYNC LEAD, UNISON LEAD, SYNC PLUCK.
 * The 1981 / 1983 models (the 4-pole filter chip's LP24; pads, the big brass): INIT, BRASS ENSMBL, COMP BRASS,
 * STRINGS, SLOW STRINGS, PWM PAD, VINTAGE PD, CLAV (its 2-pole), BRASS STAB, POLY SWEEP, FAT BASS, POLY PLUCK */
#define ROD_FX0 FX(0, 0, 0, 0)
#define ROD_PW47 (int8_t)(P_OPW + 1), 0                 /* (Stallion) PW0 46.8 %: the SVF-voice-type sounds
                                                      * (ROD_PW_REST) */
#define ROD_SE(a) P_M1SRC + 1, MS_ENV2, P_M1DST + 1, MT_SHAPE, P_M1AMT + 1, (a) /* ENV 2 -> SHAPE (a synced OSC 2) */
static const preset_t RODEO_PRESETS[] = {
    /* {W1, W2, OSC2, DT/XM, MIX, CUT, RES, MODE}, {A D S R}, ENV -> FILTER, mono; extras */
    {"INIT", {OW_SAW, OW_OFF, 0, 0, 0, 127, 0, OM_LP24}, {0, 64, 127, 20}, 0, 0, ROD_FX0},
    /* brass and stabs */
    {"BRASS ENSMBL", {OW_SAW, OW_SAW, 0, 22, -6, 32, 0, OM_LP24}, {54, 86, 100, 60}, 44, 0, ROD_FX0, XP(P_VINT + 1, 80, P_SPREAD + 1, 90, P_E2ATK + 1, 56, P_E2SUS + 1, 50)},
    {"SAW STAB", {OW_SAW, OW_SAW, 0, 36, -6, 104, 18, OM_LP12}, {0, 112, 0, 70}, 18, 0, ROD_FX0, XP(P_VINT + 1, 80, P_SPREAD + 1, 90)},
    {"FUNK STAB", {OW_SAW, OW_SAW, 12, 18, -6, 68, 8, OM_LP12}, {4, 80, 80, 50}, 38, 0, ROD_FX0,
     XP(P_LRATE + 1, 78, P_LD_PIT + 1, 2, P_VINT + 1, 80, P_SPREAD + 1, 90)},
    /* the big swelling brass: ENV 2 a slow swell on the filter, velocity on the envelope's depth,
     * pressure opening it and pulling OSC 2 sharp */
    {"SWELL BRASS", {OW_SAW, OW_SAW, 0, 18, -6, 32, 26, OM_LP12}, {80, 96, 110, 80}, 40, 0, ROD_FX0,
     XP(P_VINT + 1, 80, P_LRATE + 1, 86, P_LD_PIT + 1, 1, P_LFADE + 1, 84)},
    {"COMP BRASS", {OW_SAW, OW_SAW, 0, 24, -6, 62, 18, OM_LP24}, {30, 76, 110, 50}, 8, 0, ROD_FX0, XP(P_VINT + 1, 80, P_SPREAD + 1, 90, P_E2ATK + 1, 40, P_E2SUS + 1, 30)},
    /* (matrix showcase: RODEO_PATCHES) the brass section played: velocity on the filter envelope's depth and speed,
     * pressure pulling OSC 2 sharp */
    {"TOUCH BRASS", {OW_SAW, OW_SAW, 0, 22, -6, 40, 22, OM_LP12}, {48, 86, 110, 60}, 2, 0, ROD_FX0, XP(P_VINT + 1, 80, P_SPREAD + 1, 90, P_E2ATK + 1, 60, P_E2SUS + 1, 45)},
    /* strings and pads */
    {"STRING ENS", {OW_SAW, OW_SAW, 0, 34, -6, 70, 8, OM_LP12}, {64, 100, 118, 96}, 6, 0, ROD_FX0,
     XP(P_LRATE + 1, 82, P_LD_PIT + 1, 1, P_VINT + 1, 80, P_SPREAD + 1, 100)},
    /* (showcase) the wheel opens the strings: brighter, wider detune and a faster ensemble LFO together */
    {"WHEEL STRING", {OW_SAW, OW_SAW, 0, 16, -6, 82, 24, OM_LP12}, {42, 96, 116, 76}, 12, 0, ROD_FX0,
     XP(P_LRATE + 1, 84, P_LD_PIT + 1, 1, P_VINT + 1, 80, P_MWDST + 1, MD_OFF)},
    {"SLOW STRINGS", {OW_SAW, OW_PLS, 0, 30, -6, 70, 14, OM_LP24}, {70, 104, 120, 104}, 6, 0, ROD_FX0,
     XP(P_LRATE + 1, 82, P_LD_PIT + 1, 1, P_VINT + 1, 80, P_SPREAD + 1, 90)},
    /* (showcase) cross-mod keys: OSC 2 a twelfth up bending OSC 1, ENV 2 and velocity on the depth, high notes shorter */
    {"XMOD KEYS", {OW_PLS, OW_SAW, 19, -8, -64, 86, 28, OM_LP12}, {0, 96, 30, 70}, 38, 0, ROD_FX0, XP(P_E2DEC + 1, 80, P_SPREAD + 1, 90)},
    {"PWM PAD", {OW_PLS, OW_PLS, 0, 22, -6, 78, 26, OM_LP24}, {56, 100, 118, 108}, 24, 0, ROD_FX0,
     XP(P_LRATE + 1, 50, P_LD_SHP + 1, 30, P_VINT + 1, 80, P_SPREAD + 1, 90)},
    /* (showcase) the SVF voice's morph pad: LFO 2 sways the filter between low-pass, notch and high-pass, the wheel morphs it
     * further */
    {"SUS PAD", {OW_SAW, OW_PLS, 0, 26, -6, 60, 20, OM_LP24}, {40, 100, 116, 90}, 18, 0, ROD_FX0,
     XP(P_VINT + 1, 80, P_SPREAD + 1, 90)},
    {"VINTAGE PD", {OW_SAW, OW_SAW, 0, 24, -6, 47, 15, OM_LP24}, {55, 72, 100, 93}, 30, 0, FX(0, 27, 0, 31),
     XP(P_VINT + 1, 127, P_SPREAD + 1, 120)},   /* raw: chorus, reverb and SPREAD; a lone note
                                                 * centred by voice.c */
    {"HOLLOW PAD", {OW_PLS, OW_SAW, 0, 30, -6, 64, 40, OM_MORPH}, {70, 104, 118, 100}, 10, 0, ROD_FX0,
     XP(P_LRATE + 1, 56, P_LD_SHP + 1, 24, P_VINT + 1, 80, P_SPREAD + 1, 90)},
    /* keys */
    {"POWER STAB", {OW_SAW, OW_SAW, 0, 30, -6, 70, 22, OM_LP24}, {0, 88, 50, 40}, 30, 0, ROD_FX0,
     XP(P_VINT + 1, 80, P_SPREAD + 1, 90)},
    /* (Stallion) the 8-voice brass stab: an open 2-pole, ENV 2 the filter's own fast contour (LIFE_ROD_16: ENV 2 -> CUT) */
    {"BRASS STAB", {OW_SAW, OW_SAW, 0, 24, -6, 66, 16, OM_LP24}, {0, 92, 70, 46}, 10, 0, ROD_FX0,
     XP(P_VINT + 1, 80, P_SPREAD + 1, 90, P_E2DEC + 1, 62)},
    {"CHORD STAB", {OW_SAW, OW_SAW, 12, 30, -6, 96, 20, OM_LP12}, {0, 96, 30, 50}, 20, 0, ROD_FX0,
     XP(P_VINT + 1, 80, P_SPREAD + 1, 90)},
    {"PULSE KEYS", {OW_PLS, OW_PLS, 0, 20, -6, 60, 24, OM_LP12}, {0, 92, 40, 50}, 30, 0, ROD_FX0,
     XP(P_LRATE + 1, 60, P_LD_SHP + 1, 20, P_SPREAD + 1, 90)},
    /* organs */
    /* (Stallion) the SVF voice's band-pass sweep pad: BP12, LFO 1 sweeping the cutoff slowly */
    {"BP SWEEP PAD", {OW_SAW, OW_PLS, 0, 24, -6, 52, 66, OM_BP12}, {66, 100, 118, 108}, 0, 0, ROD_FX0,
     XP(P_VINT + 1, 80, P_SPREAD + 1, 100, P_LRATE + 1, 30, P_LD_FLT + 1, 24)},
    /* (Stallion) an X-MOD lead: OSC 2 a fifth up bending OSC 1 a little, a growl, no bell */
    {"DRIVE LEAD", {OW_OFF, OW_SYSAW, 12, 0, 63, 80, 20, OM_LP12}, {0, 80, 110, 44}, 20, 1, ROD_FX0,
     XP(P_GLIDE + 1, 20, P_LRATE + 1, 88, P_LD_PIT + 1, 1, P_E2DEC + 1, 90)},
    /* (Stallion) the 1981 model's 4-pole sweep: ENV 2 a slow rise on the filter (LIFE_ROD_21) */
    {"POLY SWEEP", {OW_SAW, OW_SAW, 0, 30, -6, 52, 36, OM_LP24}, {44, 100, 118, 82}, 0, 0, ROD_FX0,
     XP(P_VINT + 1, 80, P_SPREAD + 1, 100, P_E2ATK + 1, 96, P_E2SUS + 1, 60)},
    /* leads */
    {"GRIT BASS", {OW_SAW, OW_SAW, -12, 20, -6, 50, 18, OM_LP24}, {0, 76, 90, 24}, 40, 0, ROD_FX0,
     XP(P_VOICE + 1, V_UNISON, P_GLIDE + 1, 10, P_VINT + 1, 80)},
    {"SOLO LEAD", {OW_SAW, OW_SAW, 0, 20, -6, 68, 18, OM_LP12}, {0, 80, 90, 40}, 30, 1, ROD_FX0,
     XP(P_GLIDE + 1, 24, P_LRATE + 1, 88, P_LD_PIT + 1, 1, P_LFADE + 1, 84)},
    {"UNISON LEAD", {OW_SAW, OW_SAW, 0, 10, -6, 82, 33, OM_LP12}, {0, 72, 110, 50}, 25, 0, ROD_FX0,
     XP(P_VOICE + 1, V_UNISON, P_GLIDE + 1, 30, P_VINT + 1, 80)},
    {"PULSE LEAD", {OW_PLS, OW_PLS, 0, 10, -6, 86, 28, OM_LP12}, {4, 80, 110, 44}, 16, 1, ROD_FX0,
     XP(P_GLIDE + 1, 30, P_LRATE + 1, 88, P_LD_PIT + 1, 2, P_LFADE + 1, 84)},
    /* basses */
    {"SYNTH BASS", {OW_SAW, OW_PLS, -12, 23, -6, 52, 12, OM_LP12}, {0, 72, 80, 26}, 54, 1, ROD_FX0, XP(P_GLIDE + 1, 20, ROD_PW47)},
    /* (Stallion) the SVF voice's bass: the 53 % pulse and a saw a hair apart, 12 dB, a quick filter fall */
    {"SVF BASS", {OW_PLS, OW_SAW, 0, 14, -20, 62, 48, OM_LP12}, {0, 70, 90, 20}, 36, 1, ROD_FX0, XP(P_GLIDE + 1, 10, ROD_PW47)},
    {"FAT BASS", {OW_BOTH, OW_SAW, -12, 26, -6, 66, 12, OM_LP24}, {0, 72, 70, 24}, 54, 1, ROD_FX0},
    /* plucks */
    /* a sync pluck: OSC 2 synced, ENV 2 sweeping it down, velocity how far */
    {"SYNC PLUCK", {OW_OFF, OW_SYSAW, 7, 0, 63, 78, 26, OM_LP12}, {0, 96, 20, 70}, 12, 0, ROD_FX0, XP(P_E2DEC + 1, 70, P_SPREAD + 1, 90)},
    /* a detuned saw pluck: velocity on the envelope's depth and speed, LFO 3 drifting the detune */
    {"BRIGHT PLUCK", {OW_BOTH, OW_SAW, 0, 28, -6, 56, 24, OM_LP24}, {0, 90, 16, 64}, 36, 0, ROD_FX0,
     XP(P_VINT + 1, 80, P_SPREAD + 1, 90)},
    /* the SVF voice's morph pluck: ENV 2 sweeping the filter from low-pass through notch, velocity further */
    {"MORPH PLUCK", {OW_SAW, OW_PLS, 12, 16, -10, 68, 50, OM_MORPH}, {0, 84, 10, 64}, 36, 0, ROD_FX0, XP(P_E2DEC + 1, 70, P_SPREAD + 1, 90, ROD_PW47)},
};
#undef ROD_FX0
#undef ROD_SE

/* (Stallion) the matrix showcase (core.h preset_patch_t): slot, source, destination, amount */
static const pcable_t RODC_TBRASS[] = {
    CB(1, VEL, E1AMT, 40), CB(2, DC, E1AMT, -20),    /* the filter envelope: x 0.7 soft .. x 1.3 hard */
    CB(3, VEL, ERATE, 16),
    CB(4, AT, PIT2, 6),                              /* pressure: OSC 2 up to 21 cents sharp */
    CB(5, ENV2, CUT, 56),                            /* (review-rodeo prototype) ENV 2: the filter's own contour */
};
static const pcable_t RODC_WSTR[] = {
    CB(1, MW, CUT, 20), CB(2, MW, E1 + 3, 20), CB(3, MW, L1RATE, 16),   /* cutoff, DTUNE, the LFO's rate */
};
static const pcable_t RODC_XKEYS[] = {
    CB(1, ENV2, E1 + 3, -24),                        /* XMOD deeper at the strike, decaying */
    CB(2, VEL, E1 + 3, -10),
    CB(3, KEY, ERATE, 14),
};
static const pcable_t RODC_MORPH[] = {
    CB(1, LFO2, CUT, 6), CB(2, MW, CUT, 30), CB(3, VEL, CUT, 10),
};
/* LIFE-BEGIN (the life pass: each preset a little movement and touch, by its kind; MATRIX-SHOWCASE.md) */
static const pcable_t LIFE_ROD_1[] = {   /* BRASS ENSMBL (brass) */
    CB(1, ENV2, CUT, 56),         /* (review-rodeo prototype) ENV 2: the filter's own contour, the brass swell and fall */
    CB(2, VEL, E1AMT, 16),        /* velocity: the swell deeper */
    CB(3, AT, PIT2, 3),           /* pressure: the section pulls sharp */
    CB(4, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
    CB(5, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to OSC 2 */
};
static const pcable_t LIFE_ROD_2[] = {   /* SAW STAB (keys) */
    CB(1, VEL, CUT, 14),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 16),        /* velocity: the envelope deeper */
    CB(3, KEY, ERATE, 8),         /* high notes shorter */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to OSC 2 */
};
static const pcable_t LIFE_ROD_3[] = {   /* FUNK STAB (keys) */
    CB(1, VEL, CUT, 14),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 16),        /* velocity: the envelope deeper */
    CB(3, KEY, ERATE, 8),         /* high notes shorter */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to OSC 2 */
};
static const pcable_t LIFE_ROD_4[] = {   /* SWELL BRASS (brass) */
    CB(1, ENV2, CUT, 32),         /* ENV 2 (slow): the swell */
    CB(2, VEL, E1AMT, 24),        /* velocity: the filter envelope deeper */
    CB(3, AT, CUT, 14),           /* pressure: brighter */
    CB(4, AT, PIT2, 3),           /* .. and the section pulls sharp */
    CB(5, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
};
static const pcable_t LIFE_ROD_5[] = {   /* COMP BRASS (brass) */
    CB(1, ENV2, CUT, 36),         /* (review-rodeo prototype) ENV 2: the filter's own contour, a short blat */
    CB(2, VEL, E1AMT, 16),        /* velocity: the swell deeper */
    CB(3, AT, PIT2, 3),           /* pressure: the section pulls sharp */
    CB(4, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
    CB(5, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to OSC 2 */
};
static const pcable_t LIFE_ROD_7[] = {
    CB(1, LFO3, CUT, 6),          /* LFO 3: the filter breathes */
    CB(2, VEL, CUT, 6),           /* velocity: brighter */
};
static const pcable_t LIFE_ROD_9[] = {   /* SLOW STRINGS (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, SHAPE, 6),        /* LFO 2 (1 Hz): a slow PWM / wave motion */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to OSC 2 */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_ROD_11[] = {   /* PWM PAD (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, SHAPE, 6),        /* LFO 2 (1 Hz): a slow PWM / wave motion */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to OSC 2 */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_ROD_14[] = {
    CB(1, LFO3, CUT, 6),          /* LFO 3: the filter breathes */
    CB(2, VEL, CUT, 6),           /* velocity: brighter */
};
static const pcable_t LIFE_ROD_15[] = {
    CB(1, VEL, CUT, 14),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 16),        /* velocity: the envelope deeper */
    CB(3, KEY, ERATE, 8),         /* high notes shorter */
};
static const pcable_t LIFE_ROD_16[] = {   /* BRASS STAB (brass) */
    CB(1, ENV2, CUT, 58),         /* ENV 2: the filter's own contour, the blat */
    CB(2, VEL, CUT, 14),          /* velocity: brighter */
    CB(3, AT, PIT2, 3),           /* pressure: the section pulls sharp */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
};
static const pcable_t LIFE_ROD_17[] = {
    CB(1, VEL, CUT, 14),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 16),        /* velocity: the envelope deeper */
    CB(3, KEY, ERATE, 8),         /* high notes shorter */
};
static const pcable_t LIFE_ROD_18[] = {
    CB(1, VEL, CUT, 12),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 14),        /* velocity: the envelope deeper */
    CB(3, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
};
static const pcable_t LIFE_ROD_19[] = {   /* BP SWEEP PAD (pad) */
    CB(1, LFO2, PIT2, 2),         /* LFO 2: the second oscillator beating slowly */
    CB(2, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to OSC 2 */
    CB(3, MW, CUT, 20),           /* the wheel: the band higher */
};
static const pcable_t LIFE_ROD_20[] = {
    CB(1, ENV2, SHAPE, 40),       /* ENV 2: the sync swept down after each note */
    CB(2, VEL, CUT, 10),          /* velocity: brighter */
    CB(3, MW, SHAPE, 30),         /* the wheel: the sync up, the scream */
};
static const pcable_t LIFE_ROD_21[] = {   /* POLY SWEEP (pad) */
    CB(1, ENV2, CUT, 63),         /* ENV 2: the slow swell of the filter (AMT tops at 63; it loaded as 63 already) */
    CB(2, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(3, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to OSC 2 */
    CB(4, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_ROD_22[] = {
    CB(2, VEL, CUT, 10),          /* velocity: brighter */
    CB(3, VEL, E1AMT, 12),        /* velocity: the envelope deeper */
    CB(4, AT, CUT, 10),           /* pressure: it growls open */
};
static const pcable_t LIFE_ROD_23[] = {   /* SOLO LEAD (lead) */
    CB(1, ENV2, PITCH, 6),        /* ENV 2: a pitch blip on the attack */
    CB(2, VEL, CUT, 10),          /* velocity: brighter */
    CB(3, AT, AMP, 8),            /* pressure: a swell */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to OSC 2 */
};
static const pcable_t LIFE_ROD_24[] = {   /* UNISON LEAD (lead) */
    CB(1, ENV2, PITCH, 6),        /* ENV 2: a pitch blip on the attack */
    CB(2, VEL, CUT, 10),          /* velocity: brighter */
    CB(3, AT, AMP, 8),            /* pressure: a swell */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to OSC 2 */
};
static const pcable_t LIFE_ROD_25[] = {   /* PULSE LEAD (lead) */
    CB(1, ENV2, PITCH, 6),        /* ENV 2: a pitch blip on the attack */
    CB(2, VEL, CUT, 10),          /* velocity: brighter */
    CB(3, AT, AMP, 8),            /* pressure: a swell */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to OSC 2 */
};
static const pcable_t LIFE_ROD_26[] = {   /* SYNTH BASS (bass) */
    CB(1, VEL, CUT, 12),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 12),        /* velocity: the envelope deeper */
    CB(3, ENV2, E1 + 6, 10),      /* ENV 2: a resonant bite on the attack */
    CB(4, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to OSC 2 */
};
static const pcable_t LIFE_ROD_27[] = {   /* SVF BASS (bass) */
    CB(1, VEL, CUT, 12),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 12),        /* velocity: the envelope deeper */
    CB(3, ENV2, E1 + 6, 10),      /* ENV 2: a resonant bite on the attack */
    CB(4, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to OSC 2 */
};
static const pcable_t LIFE_ROD_28[] = {   /* FAT BASS (bass) */
    CB(1, VEL, CUT, 12),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 12),        /* velocity: the envelope deeper */
    CB(3, ENV2, E1 + 6, 10),      /* ENV 2: a resonant bite on the attack */
    CB(4, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to OSC 2 */
};
static const pcable_t LIFE_ROD_29[] = {   /* SYNC PLUCK (keys) */
    CB(1, ENV2, SHAPE, 34),       /* ENV 2: the synced OSC 2 swept down */
    CB(2, VEL, SHAPE, 14),        /* .. from higher, harder */
    CB(3, VEL, E1AMT, 20),        /* velocity: the filter envelope deeper */
    CB(4, KEY, ERATE, 10),        /* high notes shorter */
    CB(5, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
};
static const pcable_t LIFE_ROD_30[] = {
    CB(1, VEL, CUT, 14),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 16),        /* velocity: the envelope deeper */
    CB(3, KEY, ERATE, 8),         /* high notes shorter */
};
static const pcable_t LIFE_ROD_31[] = {   /* MORPH PLUCK (keys) */
    CB(1, ENV2, SHAPE, 24),       /* ENV 2: the morph swept at the pluck */
    CB(2, VEL, SHAPE, 10),        /* .. further, harder */
    CB(3, VEL, CUT, 12),          /* velocity: brighter */
    CB(4, LFO2, SHAPE, 6),        /* LFO 2: the morph sways under the tail */
    CB(5, KEY, ERATE, 8),         /* high notes shorter */
};
/* LIFE-END */
static const preset_patch_t RODEO_PATCHES[] = {
    PPATCH(&RODEO_PRESETS[1], LIFE_ROD_1, P_LRATE + 1, 86, P_LD_PIT + 1, 1, P_LFADE + 1, 84, P_E2DEC + 1, 88),    /* BRASS ENSMBL */
    PPATCH(&RODEO_PRESETS[2], LIFE_ROD_2),    /* SAW STAB */
    PPATCH(&RODEO_PRESETS[3], LIFE_ROD_3),    /* FUNK STAB */
    PPATCH(&RODEO_PRESETS[4], LIFE_ROD_4, P_E2ATK + 1, 96, P_E2DEC + 1, 100, P_E2SUS + 1, 90, P_E2REL + 1, 80),    /* SWELL BRASS */
    PPATCH(&RODEO_PRESETS[5], LIFE_ROD_5, P_LRATE + 1, 86, P_LD_PIT + 1, 1, P_LFADE + 1, 84, P_E2DEC + 1, 76),    /* COMP BRASS */
    PPATCH(&RODEO_PRESETS[6], RODC_TBRASS, P_E2DEC + 1, 92),            /* TOUCH BRASS */
    PPATCH(&RODEO_PRESETS[7], LIFE_ROD_7, P_LFADE + 1, 70),    /* STRING ENS */
    PPATCH(&RODEO_PRESETS[8], RODC_WSTR, P_SPREAD + 1, 90),              /* WHEEL STRING */
    PPATCH(&RODEO_PRESETS[9], LIFE_ROD_9, P_LFADE + 1, 70),    /* SLOW STRINGS */
    PPATCH(&RODEO_PRESETS[10], RODC_XKEYS),            /* XMOD KEYS */
    PPATCH(&RODEO_PRESETS[11], LIFE_ROD_11),    /* PWM PAD */
    PPATCH(&RODEO_PRESETS[12], RODC_MORPH),            /* SUS PAD */
    PPATCH(&RODEO_PRESETS[14], LIFE_ROD_14, P_LFADE + 1, 70),    /* HOLLOW PAD */
    PPATCH(&RODEO_PRESETS[15], LIFE_ROD_15),    /* POWER STAB */
    PPATCH(&RODEO_PRESETS[16], LIFE_ROD_16),    /* BRASS STAB */
    PPATCH(&RODEO_PRESETS[17], LIFE_ROD_17),    /* CHORD STAB */
    PPATCH(&RODEO_PRESETS[18], LIFE_ROD_18),    /* PULSE KEYS */
    PPATCH(&RODEO_PRESETS[19], LIFE_ROD_19, ROD_PW47),    /* BP SWEEP PAD */
    PPATCH(&RODEO_PRESETS[20], LIFE_ROD_20),    /* DRIVE LEAD */
    PPATCH(&RODEO_PRESETS[21], LIFE_ROD_21),    /* POLY SWEEP */
    PPATCH(&RODEO_PRESETS[22], LIFE_ROD_22, P_LRATE + 1, 88, P_LD_PIT + 1, 1, P_LFADE + 1, 84),    /* GRIT BASS */
    PPATCH(&RODEO_PRESETS[23], LIFE_ROD_23, ROD_PW47),    /* SOLO LEAD */
    PPATCH(&RODEO_PRESETS[24], LIFE_ROD_24, P_LRATE + 1, 88, P_LD_PIT + 1, 1, P_LFADE + 1, 84),    /* UNISON LEAD */
    PPATCH(&RODEO_PRESETS[25], LIFE_ROD_25, ROD_PW47),    /* PULSE LEAD */
    PPATCH(&RODEO_PRESETS[26], LIFE_ROD_26),    /* SYNTH BASS */
    PPATCH(&RODEO_PRESETS[27], LIFE_ROD_27),    /* SVF BASS */
    PPATCH(&RODEO_PRESETS[28], LIFE_ROD_28),    /* FAT BASS */
    PPATCH(&RODEO_PRESETS[29], LIFE_ROD_29),    /* SYNC PLUCK */
    PPATCH(&RODEO_PRESETS[30], LIFE_ROD_30),    /* BRIGHT PLUCK */
    PPATCH(&RODEO_PRESETS[31], LIFE_ROD_31),    /* MORPH PLUCK */
};
#undef ROD_PW47

static const engine_t ENG_RODEO = {
    "RODEO", {"OSC", "VCF"},
    {
        {"W1", F_ENUM, 0, 3, OW_SAW, N_OW1, 0},
        {"W2", F_ENUM, 0, 7, OW_SAW, N_OW2, 0},
        {"OSC2", F_SEMI, -24, 24, 0, 0, 0},
        {"DT/XM", F_INT, -64, 63, 19, 0, 0},
        {"MIX", F_BIPCT, -64, 63, -6, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 80, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"MODE", F_ENUM, 0, 5, OM_LP24, N_OM, 0},
    },
    RODEO_PRESETS, sizeof(RODEO_PRESETS) / sizeof(RODEO_PRESETS[0]), 1, rodeo_note_on, rodeo_render,
    0xEC48, {P_E5, P_E6, P_ED_FLT, P_REL}, 0, 0, rodeo_desc, 0, 0, 0,
    1, 0, 0,                                         /* (two oscillators) */
};
