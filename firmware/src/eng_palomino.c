/* SPDX-License-Identifier: GPL-3.0-only */
/* PALOMINO: a voice after the early-1980s flagship analog poly's (its manual and service notes: two VCOs, cross mod,
 * sync, the 4-pole OTA filter chip switchable 12 / 24 dB). No code or sample of its maker's.
 * EDIT 1 (VCO)  W1  W2  VCO2  XMOD        EDIT 2 (VCF)  MIX  CUT  RES  MODE
 *   W1    VCO-1: TRI, SAW, PLS, SQR, then SINE, NOISE
 *   W2    VCO-2: SINE, SAW, PLS, NOISE, then TRI, SQR (the original's six on both: the first four of each as before, the
 *         stored values keep their meaning). Under SYNC VCO-1's phase runs on whatever it plays (NOISE too), so VCO-2
 *         still restarts at VCO-1's pitch; VCO-2's NOISE has no phase to restart. XMOD: VCO-2's value, whichever its
 *         wave (NOISE: a random FM of VCO-1); it bends no NOISE on VCO-1
 *   VCO2  VCO-2's interval, -24 .. +24 semitones (and 5 cents above: the original tunes its VCOs, then leaves them
 *         a little apart); VINTAGE and UNISON spread it further. Below -24 LOW (the original's VCO-2 LOW range): VCO-2
 *         8 octaves down, still following the keys (C4 about 1 Hz), in the mixer and the cross mod as ever: XMOD a slow
 *         vibrato or wobble of VCO-1, SYNC as before (SHAPE moves it there too)
 *   XMOD  cross mod: VCO-2 bends VCO-1's pitch at audio rate, exponentially (the service notes calibrate it in
 *         octaves), up to about 6 octaves at the top: metal, bells, growls
 *   MIX   the source mixer: -64 VCO-1 alone, 0 both, +63 VCO-2 alone
 *   CUT RES  the OTA filter (dsp.c ota_tick, the OTA chip's): the bass thins as the resonance rises, it sings at the top;
 *         keyboard tracking about half (the matrix's KEY > CUT moves it: AMT -32 none, +32 twice); ENV DEST FLT its
 *         envelope; KEY > ERATE the envelopes' key follow (the original's KEY FOLLOW). (Tuning) the key
 *         tracking measured 35 % (C2 .. C6 noise, CUT 64); now x 1.5, about half as this says
 *   MODE  24DB, 12DB (stage 2's output, the loop still closed at stage 4: the original's -12 dB/oct), and both with
 *         SYNC: VCO-2 slaved to VCO-1 (band-limited resets), and SHAPE then moves VCO-2's pitch (up to +-32
 *         semitones) instead of the pulse width: an ENV or LFO on SHP (or TIMBR) sweeps the sync
 * LFO > MOD P>VCO (the original's VCO MODULATOR switch): the LFO's and ENV's pitch modulation (LFO DEST PIT, ENV DEST
 * PIT) to BOTH VCOs (as before), VCO1 or VCO2 alone: an ENV on VCO-2 under SYNC is the original's sync sweep, an LFO
 * on one VCO a beating interval. The rest of the pitch (the bend, the matrix's PITCH, MW / AT VIB, MOTN, the glide,
 * VINTAGE) moves both
 * The pulse width: square (49 %: PAL_PW0) at SHAPE's middle, narrower as it moves either way (LFO / ENV to SHP: the
 * PWM); the high-pass is the sound's FX > FILTER (FILT above 0), as the original's comes after the mixer.
 * Voice state: ph[0] VCO-1, ph[1] VCO-2, ph[2] VCO-2's noise; s[0..3] the filter, s[4] the mixed sample waiting a
 * sample (a sync reset corrects the sample before it: polyBLEP), s[5] VCO-2's last value (the cross mod of the next
 * step), s[6] VCO-1's noise (its own generator: the two NOISEs mixed are not one) */
enum { PV1_TRI, PV1_SAW, PV1_PLS, PV1_SQR, PV1_SIN, PV1_NOIS };   /* (appended: the stores keep their meaning) */
enum { PV2_SIN, PV2_SAW, PV2_PLS, PV2_NOIS, PV2_TRI, PV2_SQR };
enum { PVM_24, PVM_12, PVM_24S, PVM_12S };
static const char *const N_PV1[] = {"TRI", "SAW", "PLS", "SQR", "SINE", "NOISE"};
static const char *const N_PV2[] = {"SINE", "SAW", "PLS", "NOISE", "TRI", "SQR"};
static const char *const N_PVM[] = {"24DB", "12DB", "24+SY", "12+SY"};

/* VOICE 2 > DTUNE: VCO-2's fine tune (+0 .. +15 cents, 40 the old fixed +5). RES stops short of oscillation, as the
 * original's is calibrated to. SHAPE narrows the pulse either way from the square (above the middle as its PW does;
 * below it the inverse width, the same spectrum), so an LFO about the middle is a full PWM */
#ifndef PAL_RES_TOP
#define PAL_RES_TOP 117
#endif
/* PALOMINO's constants as tune hooks: a host build may set them (-DPAL_KTRK=..: for parameter sweeps on the
 * host); the device build never does, its code is the same.
 * PAL_KTRK the filter's key tracking (12: about half, as the header says);
 * PAL_PWK SHAPE -> the pulse width; PAL_XMK XMOD -> the cross mod's depth; PAL_DTK VOICE 2 > DTUNE -> VCO-2's fine
 * tune (205 at 40: +5 cents); PAL_SYNR the sync's pitch range (SHAPE's deviation >> it); PAL_KNEE the output's
 * soft clip knee */
#ifndef PAL_KTRK
#define PAL_KTRK 12
#endif
#ifndef PAL_PWK
#define PAL_PWK 62625                              /* (tuned) SHAPE's end 3.35 %, as measured on the original's
                                                   * and its successor's PW knob at its end (65000: 1.63 %) */
#endif
/* PAL_PW0 the square's rest width: 49 % (the original / its successor measured 49.5 % and 48.5 %; an exact 50 % has no
 * even harmonics, a hollow digital square): SQR, and PLS at SHAPE's middle, h2 about -30 dB. PLS fades it out
 * towards SHAPE's ends, so the narrowest pulse stays PAL_PWK's either way */
#ifndef PAL_PW0
#define PAL_PW0 0x7D70A3D7u
#endif
#define PAL_PW0S ((0x80000000u - PAL_PW0) >> 15)       /* (the rest offset per step of |w| from SHAPE's end) */
#ifndef PAL_XMK
#define PAL_XMK 3
#endif
#ifndef PAL_DTK
#define PAL_DTK 205
#endif
#ifndef PAL_SYNR
#define PAL_SYNR 5
#endif
#ifndef PAL_KNEE
#define PAL_KNEE 16000
#endif
static void palomino_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (v->env < (1 << 17)) {                        /* from silence, or a tail below -42 dB (as RODEO's): the filter
                                                      * clear (VCO-1 runs on) */
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = v->s[4] = v->s[5] = 0;
        if (!v->ph[2])
            v->ph[2] = 0x2545F491u + v->note * 2654435761u;
        if (!v->s[6])
            v->s[6] = (int32_t)(0x6A09E667u + v->note * 2246822519u);
        v->ph[1] = v->ph[0];                         /* (Stallion) VCO-2 in step with VCO-1, as MARE's: left where the
                                                      * last note had it, near half a turn the two cancelled, a POLY
                                                      * note -5 .. +3 dB at random (a strings and a keys preset;
                                                      * tests/tail_level_test.c). In step, not a quarter on: VCO-2 is 5
                                                      * cents above, so from a quarter it drifts toward half a turn
                                                      * within the note (-1.6 dB on average under the random level, -4.6
                                                      * at most); in step +0.5 on average. A note over an audible voice
                                                      * keeps its phases (voice.c) */
    }
}

/* UNISON (voice.c unison_spread): voice j of a stack of n (in DETUNE's order) started o = j^2 / 2n of a turn of the note
 * after the lead's VCO-1, VCO-2 by its share of the same time (VCO2 above VCO-1, its 5 cents rounded to 1/16 semitone;
 * LOW 8 octaves down: 1/256): the lead's own relation of the two, a POLY note's, on every voice. The quadratic phases
 * (Newman's) sum to the mean of random phases on the fundamental, |sum| = sqrt n, and stay near it as DETUNE moves the
 * voices apart linearly; the same level at every note, the stack as wide. No lead (a sample's pending start: none here)
 * r (Q32) as the other engines. Each NOISE from its own place (never 0: note-on reads 0 as "unseeded"). (Before:
 * voice.c uni_phases, VCO-2 at a random phase of its own on each voice, VCO-1 where the voice's last note left it plus
 * r: the same UNISON note -15 .. +7 dB over POLY at random, 8 dB apart on average over 24 draws) */
#ifndef PAL_UNI_TAB
#define PAL_UNI_TAB \
    {0, 3386947196u}, {0, 644583122u, 3511252161u}, {0, 3752129289u, 3044707972u, 553421372u}, \
    {0, 3996437893u, 911027184u, 1459443274u, 308481181u, 3703422932u, 3252598912u, 1182796801u}
#endif
static const uint32_t PAL_UNI_O[4][8] = {PAL_UNI_TAB};
static uint32_t pal_uni_off(uint32_t j, uint32_t n)
{
    return PAL_UNI_O[n <= 2u ? 0 : n == 3u ? 1 : n == 4u ? 2 : 3][j & 7u];
}
static void palomino_spread(track_t *t, voice_t *v, uint32_t r, const voice_t *lead, uint32_t j, uint32_t n)
{
    int32_t iv = t->p[P_E2];
    uint32_t o = r, a0 = v->ph[0], a1 = v->ph[1];
    if (lead && lead != v && n > 1u)
        o = pal_uni_off(j, n), a0 = lead->ph[0], a1 = lead->ph[1];
    v->ph[0] = a0 + o;
    v->ph[1] = a1 + (iv < -24 ? o >> 8 : uni_turns(o, v->pitch16, clamp(iv, -24, 24) * 16 + 1));
    v->ph[2] ^= r;
    if (!v->ph[2])
        v->ph[2] = 1u;
    v->s[6] = (int32_t)((uint32_t)v->s[6] ^ (r << 16 | r >> 16));
    if (!v->s[6])
        v->s[6] = 1;
}

static inline int32_t pal_naive2(uint32_t w, uint32_t ph, uint32_t pw)   /* VCO-2 without its polyBLEP (a sync step) */
{
    switch (w) {
    case PV2_SIN: return osc_sine(ph);
    case PV2_SAW: return (int32_t)(ph >> 16) - 32768;
    case PV2_PLS: return ((int32_t)(ph >> 16) - (int32_t)((ph + pw) >> 16)) >> 1;
    case PV2_TRI: return osc_tri(ph);
    case PV2_SQR: return ((int32_t)(ph >> 16) - (int32_t)((ph + PAL_PW0) >> 16)) >> 1;
    default: return 0;                               /* (NOISE: no phase, no step) */
    }
}

static void palomino_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *ev = vm_ev(t, m);              /* the EDIT values: the voice's own or the part's */
    const int16_t *p = t->p;
    uint32_t w1 = (uint32_t)clamp(ev[0], 0, 5), w2 = (uint32_t)clamp(ev[1], 0, 5), mode = (uint32_t)clamp(ev[7], 0, 3);
    uint32_t i, ph1 = v->ph[0], ph2 = v->ph[1], nz = v->ph[2], nz1 = (uint32_t)v->s[6], inc = m->inc, inc1, inc2, pw, pw2;
    uint32_t pt = (uint32_t)clamp(p[P_PTGT], PT_BOTH, PT_VCO2);
    int32_t sync = mode >= PVM_24S, low = ev[2] < -24, dev = m->shape - (64 << 8), xm = clamp(ev[3], 0, 127) * PAL_XMK;
    int32_t mix = clamp(ev[4], -64, 63), g1 = clamp((64 - mix) * 256, 0, 32767), g2 = clamp((64 + mix) * 256, 0, 32767);
    int32_t cut = (ev[5] << 8) + m->cutoff + (v->pitch16 - 60 * 16) * PAL_KTRK, pend = v->s[4], y2p = v->s[5];
    int32_t ls[4] = {v->s[0], v->s[1], v->s[2], v->s[3]}, w = clamp(dev * 2, -127 * 256, 127 * 256);
    ladder_t lad;
    __typeof__(&ota_tick) ota = HOT_FAR(ota_tick);   /* (Stallion: PONY's filter, in RAM: hal/fm1_hot.h) */
    pw = sync ? PAL_PW0 : 0x80000000u - (uint32_t)(w * PAL_PWK) - (uint32_t)(32768 - (w < 0 ? -w : w)) * PAL_PW0S;
                                                     /* (tuning) both ways from the square (PAL_PW0 at the
                                                      * middle), 2.6 % .. 97.4 % in phase: an LFO on SHP sweeps the
                                                      * whole cycle (was one way, clamped at the square half the
                                                      * time, floor 5 %) */
    pw2 = pw;
    if ((pt == PT_VCO2 && m->pmod) || m->vo1)        /* P>VCO VCO2: VCO-1 without the LFO's and ENV's pitch (+ its own:
                                                      * the BEND page's VCO switches, the matrix's PIT1) */
        inc = hy_inc(m->pitch16, m->fine, (pt == PT_VCO2 ? -m->pmod : 0) + m->vo1);
    inc2 = hy_inc(m->pitch16 + (low ? 0 : clamp(ev[2], -24, 24) * 16) + (sync ? dev >> PAL_SYNR : 0), m->fine,
                  clamp(p[P_DETUNE], 0, 127) * PAL_DTK / 40 - (pt == PT_VCO1 ? m->pmod : 0) + m->vo2);   /* (VCO1: VCO-2 without it) */
    if (low)                                         /* LOW: 8 octaves down */
        inc2 >>= 8;
    ota_coef(&lad, cut, ev[6] > PAL_RES_TOP ? PAL_RES_TOP : ev[6]);
    for (i = 0; i < n; i++) {
        int32_t y1, y2, x, y, a, corr = 0;
        uint32_t old1 = ph1;
        inc1 = inc;                                  /* the cross mod: VCO-2's last value, in octaves (Q10) */
        if (xm) {
            int32_t o = (y2p * xm) >> 11, oc = o >> 10;
            uint64_t k = (uint64_t)inc * ((1u << 30) + FM6_EXP2[o & 1023]) >> 30;
            k = oc >= 0 ? k << oc : k >> -oc;
            inc1 = k > 0x60000000u ? 0x60000000u : (uint32_t)k;
        }
        ph1 += inc1;
        ph2 += inc2;
        if (sync && ph1 < old1 && inc1 >= 4096u) {   /* VCO-1 wrapped: VCO-2 restarts, by the fraction (q12: the
                                                      * overshoot / the increment); its step h band-limited over this
                                                      * sample and the one before (pend) */
            uint32_t q12 = ph1 / (inc1 >> 12), at;
            int32_t h, tq, b;
            q12 = q12 > 4095u ? 4095u : q12;
            at = ph2 - (inc2 >> 12) * q12;
            ph2 = (inc2 >> 12) * q12;
            h = pal_naive2(w2, 0, pw2) - pal_naive2(w2, at, pw2);   /* the step: new - old */
            tq = (int32_t)(q12 << 3), b = 32768 - tq;
            corr = -((((b * b) >> 16) * h) >> 15);                  /* this sample: - h (1 - t)^2 / 2 */
            pend += mulq15(((((tq * tq) >> 16) * h) >> 15), g2);     /* the one before: + h t^2 / 2 */
        }
        switch (w2) {
        case PV2_SIN: y2 = osc_sine(ph2); break;
        case PV2_SAW: y2 = osc_saw(ph2, inc2); break;
        case PV2_PLS: y2 = osc_pulse(ph2, inc2, pw2); break;
        case PV2_TRI: y2 = osc_tri_bl(ph2, inc2); break;
        case PV2_SQR: y2 = osc_pulse(ph2, inc2, PAL_PW0); break;
        default:
            nz = nz * 1664525u + 1013904223u;
            y2 = (int32_t)(nz >> 16) - 32768;
            break;
        }
        y2 += corr;
        switch (w1) {
        case PV1_TRI: y1 = osc_tri_bl(ph1, inc1); break;
        case PV1_SAW: y1 = osc_saw(ph1, inc1); break;
        case PV1_PLS: y1 = osc_pulse(ph1, inc1, pw); break;
        case PV1_SQR: y1 = osc_pulse(ph1, inc1, PAL_PW0); break;
        case PV1_SIN: y1 = osc_sine(ph1); break;
        default:
            nz1 = nz1 * 22695477u + 1u;              /* (another generator than VCO-2's) */
            y1 = (int32_t)(nz1 >> 16) - 32768;
            break;
        }
        y2p = y2;
        x = mulq15(y1, g1) + mulq15(y2, g2);
        y = ota(&lad, pend, ls, (int)(mode & 1u)) >> 1;   /* (the sample before: a sync may still correct this one) */
        pend = x;
        a = y < 0 ? -y : y;
        if (a > PAL_KNEE) {
            a = PAL_KNEE + (softclip((a - PAL_KNEE) * 2) >> 1);
            y = y < 0 ? -a : a;
        }
        out[i] += mulq15(mulq15(y << 1, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph1;
    v->ph[1] = ph2;
    v->ph[2] = nz;
    v->s[6] = (int32_t)nz1;
    v->s[0] = ls[0], v->s[1] = ls[1], v->s[2] = ls[2], v->s[3] = ls[3];
    v->s[4] = pend;
    v->s[5] = y2p;
}

/* (Stallion 1.0) the factory sounds: the original's own factory patches (its 64-patch sheet: strings, brass, keys,
 * organs, leads, basses) rebuilt from their descriptions; dry (no sends, no arp), centred */
#define PAL_FX0 FX(0, 0, 0, 0)
static const preset_t PALOMINO_PRESETS[] = {
    /* {W1, W2, VCO2, XMOD, MIX, CUT, RES, MODE}, {A D S R}, ENV -> FILTER, mono; extras */
    {"INIT", {PV1_SAW, PV2_SAW, 0, 0, -64, 127, 0, PVM_24}, {0, 64, 127, 20}, 0, 0, PAL_FX0},
    /* strings and pads */
    {"LOW STRINGS", {PV1_SAW, PV2_SAW, -12, 0, -10, 100, 6, PVM_24}, {70, 100, 116, 94}, 8, 0, PAL_FX0,
     XP(P_DETUNE + 1, 80, P_LRATE + 1, 82, P_LD_PIT + 1, 1, P_TFLT + 1, 26)},
    {"MID STRINGS", {PV1_SAW, PV2_SAW, 0, 0, 0, 80, 6, PVM_12}, {66, 100, 118, 92}, 6, 0, PAL_FX0,
     XP(P_DETUNE + 1, 90, P_LRATE + 1, 84, P_LD_PIT + 1, 1)},
    {"HIGH STRINGS", {PV1_SAW, PV2_SAW, 12, 0, 0, 92, 4, PVM_12}, {64, 100, 118, 90}, 6, 0, PAL_FX0,
     XP(P_DETUNE + 1, 90, P_LRATE + 1, 86, P_LD_PIT + 1, 1, P_TFLT + 1, 40)},
    {"SOFT STRINGS", {PV1_SAW, PV2_PLS, 0, 0, 0, 96, 4, PVM_12}, {80, 104, 120, 100}, 4, 0, PAL_FX0,
     XP(P_LRATE + 1, 60, P_LD_SHP + 1, 24, P_VINT + 1, 24, P_TFLT + 1, 34)},
    {"SYNTH CHOIR", {PV1_PLS, PV2_PLS, 0, 0, 24, 70, 78, PVM_24}, {96, 108, 110, 106}, 6, 0, PAL_FX0,
     XP(P_LRATE + 1, 70, P_LD_SHP + 1, 30, P_LD_PIT + 1, 1)},
    {"RESO PAD", {PV1_SAW, PV2_SAW, 12, 0, 20, 84, 70, PVM_24S}, {50, 110, 100, 96}, -30, 0, PAL_FX0,
     XP(P_VINT + 1, 20)},
    {"FAT FIFTHS", {PV1_SAW, PV2_SAW, 7, 0, 12, 78, 34, PVM_24}, {12, 82, 102, 54}, 30, 0, PAL_FX0},
    /* brass */
    {"LOW BRASS", {PV1_SAW, PV2_SAW, 0, 0, 0, 46, 18, PVM_24}, {32, 104, 118, 68}, 12, 0, PAL_FX0,
     XP(P_DETUNE + 1, 80, P_VINT + 1, 20)},
    {"HIGH BRASS", {PV1_SAW, PV2_SAW, 12, 0, -10, 50, 12, PVM_24}, {36, 86, 110, 58}, 14, 0, PAL_FX0,
     XP(P_DETUNE + 1, 80)},
    {"SYNTH BRASS", {PV1_SAW, PV2_SAW, 0, 0, 0, 36, 46, PVM_12}, {60, 100, 110, 56}, 14, 0, PAL_FX0,
     XP(P_DETUNE + 1, 100, P_VINT + 1, 24)},
    /* (matrix showcase: PALOMINO_PATCHES) TIMBR morphs the pad: the mix over to VCO-2's pulse, cross mod and resonance
     * coming in; LFO 2 drifts the mix */
    {"MORPH PAD", {PV1_SAW, PV2_PLS, 7, 0, -40, 70, 20, PVM_12}, {60, 100, 118, 94}, 10, 0, PAL_FX0,
     XP(P_L2RATE + 1, 30)},
    /* (showcase) the brass played: velocity on the filter envelope's depth and speed, the key follow on the envelopes'
     * time, pressure pulling VCO-2 sharp */
    {"SQR BASS", {PV1_SQR, PV2_SAW, -12, 0, -10, 40, 24, PVM_24}, {0, 64, 40, 20}, 48, 1, PAL_FX0,
     XP(P_GLIDE + 1, 0)},
    /* keys */
    {"POLY KEYS", {PV1_SAW, PV2_PLS, 0, 0, 0, 60, 18, PVM_12}, {0, 90, 40, 50}, 30, 0, PAL_FX0,
     XP(P_DETUNE + 1, 60)},
    {"SAW PAD", {PV1_SAW, PV2_SAW, 0, 0, 0, 72, 8, PVM_12}, {80, 104, 120, 104}, 6, 0, PAL_FX0,
     XP(P_DETUNE + 1, 112, P_LRATE + 1, 80, P_LD_PIT + 1, 1, P_SPREAD + 1, 90)},
    {"SQR LEAD", {PV1_SQR, PV2_SAW, 12, 0, -20, 74, 22, PVM_24}, {6, 80, 110, 50}, 22, 1, PAL_FX0,
     XP(P_GLIDE + 1, 24, P_LRATE + 1, 88, P_LD_PIT + 1, 1, P_LFADE + 1, 84)},
    {"DARK PAD", {PV1_SAW, PV2_PLS, 0, 0, 0, 34, 52, PVM_24}, {60, 110, 120, 104}, 50, 0, PAL_FX0,
     XP(P_DETUNE + 1, 80, P_SPREAD + 1, 90)},
    /* organs */
    {"POLY BRASS", {PV1_SAW, PV2_SAW, 0, 0, 0, 50, 12, PVM_24}, {30, 90, 104, 60}, 44, 0, PAL_FX0,
     XP(P_DETUNE + 1, 90, P_E2ATK + 1, 70, P_E2SUS + 1, 50)},
    {"SEQ BASS", {PV1_SAW, PV2_PLS, -12, 0, 0, 36, 40, PVM_24}, {0, 60, 30, 20}, 50, 1, PAL_FX0,
     XP(P_GLIDE + 1, 0)},
    {"MOTION PAD", {PV1_SAW, PV2_PLS, 0, 0, 0, 46, 40, PVM_24}, {70, 104, 120, 104}, 10, 0, PAL_FX0,
     XP(P_DETUNE + 1, 70, P_SPREAD + 1, 90)},
    /* leads */
    {"SYNC LEAD", {PV1_SAW, PV2_SAW, 12, 0, 40, 100, 10, PVM_24S}, {2, 80, 116, 44}, 10, 1, PAL_FX0,
     XP(P_M1SRC + 1, MS_ENV2, P_M1DST + 1, MT_SHAPE, P_M1AMT + 1, 40, P_E2DEC + 1, 84)},
    /* (showcase) the sync lead played: ENV 2 sweeps the synced VCO-2, velocity sets how far, pressure pushes it up */
    {"OCTAVE LEAD", {PV1_PLS, PV2_SAW, 12, 0, 10, 80, 18, PVM_24}, {2, 80, 110, 50}, 18, 1, PAL_FX0,
     XP(P_GLIDE + 1, 18, P_LRATE + 1, 88, P_LD_PIT + 1, 1, P_LFADE + 1, 84)},
    {"SOFT LEAD", {PV1_SQR, PV2_TRI, 0, 0, -20, 60, 10, PVM_24}, {6, 80, 116, 50}, 10, 1, PAL_FX0,
     XP(P_GLIDE + 1, 16, P_LRATE + 1, 88, P_LD_PIT + 1, 2, P_LFADE + 1, 84)},
    {"FAT LEAD", {PV1_SAW, PV2_SAW, 0, 0, 24, 82, 40, PVM_24}, {0, 104, 86, 44}, 30, 1, PAL_FX0,
     XP(P_DETUNE + 1, 40, P_GLIDE + 1, 24, P_VOICE + 1, V_UNISON, P_LD_PIT + 1, 1)},
    /* the cross-mod lead: VCO-2 an octave down bending the saw, ENV 2 biting at the attack, pressure
     * growling the cross mod deeper */
    {"SAW LEAD", {PV1_SAW, PV2_SAW, 0, 0, 0, 84, 16, PVM_24}, {0, 80, 110, 44}, 20, 1, PAL_FX0,
     XP(P_GLIDE + 1, 16, P_DETUNE + 1, 40, P_LRATE + 1, 88, P_LD_PIT + 1, 1)},
    /* basses */
    {"SAW BASS", {PV1_SAW, PV2_SAW, -12, 0, 0, 40, 30, PVM_24}, {0, 66, 24, 12}, 44, 1, PAL_FX0},
    {"SUB BASS", {PV1_SQR, PV2_SIN, -12, 0, 0, 40, 6, PVM_24}, {0, 70, 80, 14}, 10, 1, PAL_FX0},
    {"PWM STRINGS", {PV1_PLS, PV2_PLS, 0, 0, 0, 72, 6, PVM_12}, {64, 100, 118, 100}, 6, 0, PAL_FX0,
     XP(P_DETUNE + 1, 70, P_LRATE + 1, 50, P_LD_SHP + 1, 30, P_SPREAD + 1, 90)},
    /* the sync bass: VCO-2 synced, ENV 2 sweeping it down from high, velocity setting how far */
    {"SYNC BASS", {PV1_SAW, PV2_SAW, 0, 0, 30, 50, 20, PVM_24S}, {0, 66, 30, 12}, 30, 1, PAL_FX0, XP(P_E2DEC + 1, 60)},
    /* plucks (and a pad and a stab) */
    {"POLY PLUCK", {PV1_SAW, PV2_PLS, 0, 0, 0, 40, 30, PVM_24}, {0, 74, 0, 54}, 48, 0, PAL_FX0, XP(P_ALLOC + 1, 2)},
    /* the original's two pulses, a slow LFO 2 on their width whose rate LFO 3 drifts, the wheel deeper */
    {"PWM PAD", {PV1_PLS, PV2_PLS, 0, 0, 0, 110, 26, PVM_24}, {46, 100, 102, 110}, 0, 0, PAL_FX0,
     XP(P_DETUNE + 1, 60, P_VINT + 1, 20, P_L2RATE + 1, 50, P_TFLT + 1, 30)},
    /* the poly brass stab: two saws, velocity on the filter envelope, the key follow shortening the high
     * notes, ENV 2 a resonant bite, pressure pulling VCO-2 sharp */
    {"RIPPLE PAD", {PV1_SAW, PV2_SIN, 0, 10, 0, 70, 20, PVM_24}, {70, 104, 116, 104}, 8, 0, PAL_FX0,
     XP(P_DETUNE + 1, 40, P_SPREAD + 1, 90)},
};
#undef PAL_FX0

/* (Stallion) the matrix showcase (core.h preset_patch_t): slot, source, destination, amount */
static const pcable_t PALC_MORPH[] = {
    CB(1, MAC2, E1 + 4, 40),                         /* MIX: over to VCO-2 */
    CB(2, MAC2, XMOD, 24),                           /* cross mod */
    CB(3, MAC2, E1 + 6, 20),                         /* RES */
    CB(4, LFO2, E1 + 4, 8),                          /* the mix drifting */
};
static const pcable_t PALC_TBRASS[] = {
    CB(1, VEL, CUT, 12),                             /* velocity: brighter */
    CB(2, VEL, E1AMT, 14),                           /* velocity: the pluck deeper */
    CB(3, AT, CUT, 8),                               /* pressure: brighter */
    CB(4, KEY, CUT, -8),
};
static const pcable_t PALC_TSYNC[] = {
    CB(1, VEL, CUT, 10),                             /* velocity: brighter */
    CB(2, AT, CUT, 12),                              /* pressure: brighter */
    CB(3, LFO2, SHAPE, 8),                           /* LFO 2: a slow PWM on VCO-1 */
    CB(4, MW, E1 + 4, 24),                           /* the wheel: more of the octave */
};
static const pcable_t PALC_BSTAB[] = {
    CB(1, LFO3, XMOD, 14),                           /* LFO 3: the cross mod rising and falling, a slow ripple */
    CB(2, LFO2, CUT, 4),
    CB(3, VEL, CUT, 6),
    CB(4, MW, XMOD, 30),                             /* the wheel: more cross mod */
    CB(5, KEY, CUT, -8),
};
/* LIFE-BEGIN (the life pass: each preset a little movement and touch, by its kind; MATRIX-SHOWCASE.md) */
static const pcable_t LIFE_PAL_1[] = {   /* LOW STRINGS (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, PIT2, 2),         /* LFO 2 (1 Hz): the second oscillator beating slowly */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 4, 12),      /* TIMBR: the MIX over to VCO-2 (12) */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_PAL_2[] = {   /* MID STRINGS (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, PIT2, 2),         /* LFO 2 (1 Hz): the second oscillator beating slowly */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 4, 12),      /* TIMBR: the MIX over to VCO-2 (12) */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_PAL_3[] = {   /* HIGH STRINGS (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, PIT2, 2),         /* LFO 2 (1 Hz): the second oscillator beating slowly */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 4, 12),      /* TIMBR: the MIX over to VCO-2 (12) */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_PAL_4[] = {   /* SOFT STRINGS (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, SHAPE, 6),        /* LFO 2 (1 Hz): a slow PWM / wave motion */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 4, 12),      /* TIMBR: the MIX over to VCO-2 (12) */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_PAL_5[] = {   /* SYNTH CHOIR (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, SHAPE, 6),        /* LFO 2 (1 Hz): a slow PWM / wave motion */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 4, 12),      /* TIMBR: the MIX over to VCO-2 (12) */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_PAL_6[] = {   /* RESO PAD (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, PIT2, 2),         /* LFO 2 (1 Hz): the second oscillator beating slowly */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 4, 12),      /* TIMBR: the MIX over to VCO-2 (12) */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_PAL_7[] = {   /* FAT FIFTHS (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, PIT2, 2),         /* LFO 2 (1 Hz): the second oscillator beating slowly */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 4, 12),      /* TIMBR: the MIX over to VCO-2 (12) */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_PAL_8[] = {   /* LOW BRASS (brass) */
    CB(1, ENV2, CUT, 30),         /* ENV 2: the bright blat of the attack */
    CB(2, VEL, E1AMT, 16),        /* velocity: the swell deeper */
    CB(3, AT, PIT2, 3),           /* pressure: the section pulls sharp */
    CB(4, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
    CB(5, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to VCO-2 */
};
static const pcable_t LIFE_PAL_9[] = {   /* HIGH BRASS (brass) */
    CB(1, ENV2, CUT, 30),         /* ENV 2: the bright blat of the attack */
    CB(2, VEL, E1AMT, 16),        /* velocity: the swell deeper */
    CB(3, AT, PIT2, 3),           /* pressure: the section pulls sharp */
    CB(4, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
    CB(5, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to VCO-2 */
};
static const pcable_t LIFE_PAL_10[] = {   /* SYNTH BRASS (brass) */
    CB(1, ENV2, CUT, 30),         /* ENV 2: the bright blat of the attack */
    CB(2, VEL, E1AMT, 16),        /* velocity: the swell deeper */
    CB(3, AT, PIT2, 3),           /* pressure: the section pulls sharp */
    CB(4, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
    CB(5, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to VCO-2 */
};
static const pcable_t LIFE_PAL_13[] = {
    CB(1, VEL, CUT, 12),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 14),        /* velocity: the envelope deeper */
    CB(3, KEY, ERATE, 8),         /* high notes shorter */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
};
static const pcable_t LIFE_PAL_14[] = {
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, PIT2, 2),         /* LFO 2: the second oscillator beating slowly */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 4, 12),      /* TIMBR: the MIX over to VCO-2 */
};
static const pcable_t LIFE_PAL_15[] = {
    CB(1, VEL, CUT, 10),          /* velocity: brighter */
    CB(2, AT, CUT, 14),           /* pressure: brighter */
    CB(3, MW, E1 + 4, 30),        /* the wheel: VCO-2 (the octave saw) in */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
};
static const pcable_t LIFE_PAL_16[] = {
    CB(1, LFO3, CUT, 8),          /* LFO 3: the filter breathes */
    CB(2, LFO2, SHAPE, 10),       /* LFO 2: a slow PWM on VCO-2 */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 4, 12),      /* TIMBR: the MIX over to VCO-2 */
};
static const pcable_t LIFE_PAL_17[] = {
    CB(1, ENV2, CUT, 30),         /* ENV 2: the brass swell and fall */
    CB(2, VEL, CUT, 10),          /* velocity: brighter */
    CB(3, AT, PIT2, 4),           /* pressure: VCO-2 a little sharp */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
};
static const pcable_t LIFE_PAL_18[] = {
    CB(1, VEL, CUT, 14),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 16),        /* velocity: the envelope deeper */
    CB(3, KEY, CUT, -8),
};
static const pcable_t LIFE_PAL_19[] = {
    CB(1, LFO3, CUT, 24),         /* LFO 3 (0.4 Hz): the filter sweeping up and down */
    CB(2, LFO2, SHAPE, 10),       /* LFO 2: a slow PWM on VCO-2 */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 4, 12),      /* TIMBR: the MIX over to VCO-2 */
};
static const pcable_t LIFE_PAL_20[] = {   /* SYNC LEAD (lead) */
    CB(2, VEL, CUT, 10),          /* velocity: brighter */
    CB(3, AT, AMP, 8),            /* pressure: a swell */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to VCO-2 */
};
static const pcable_t LIFE_PAL_22[] = {   /* SOFT LEAD (lead) */
    CB(1, ENV2, PITCH, 6),        /* ENV 2: a pitch blip on the attack */
    CB(2, VEL, CUT, 10),          /* velocity: brighter */
    CB(3, AT, AMP, 8),            /* pressure: a swell */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to VCO-2 */
};
static const pcable_t LIFE_PAL_23[] = {   /* FAT LEAD (lead) */
    CB(1, ENV2, PITCH, 6),        /* ENV 2: a pitch blip on the attack */
    CB(2, VEL, CUT, 10),          /* velocity: brighter */
    CB(3, AT, AMP, 8),            /* pressure: a swell */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to VCO-2 */
};
static const pcable_t LIFE_PAL_24[] = {
    CB(1, VEL, CUT, 10),          /* velocity: brighter */
    CB(2, AT, CUT, 12),           /* pressure: brighter */
};
static const pcable_t LIFE_PAL_25[] = {   /* SAW BASS (bass) */
    CB(1, VEL, CUT, 12),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 12),        /* velocity: the envelope deeper */
    CB(3, ENV2, E1 + 6, 10),      /* ENV 2: a resonant bite on the attack */
    CB(4, MAC2, E1 + 4, 12),      /* TIMBR: the MIX over to VCO-2 (12) */
};
static const pcable_t LIFE_PAL_26[] = {   /* SUB BASS (bass) */
    CB(1, VEL, CUT, 12),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 12),        /* velocity: the envelope deeper */
    CB(3, ENV2, E1 + 6, 10),      /* ENV 2: a resonant bite on the attack */
    CB(4, MAC2, E1 + 4, 12),      /* TIMBR: the MIX over to VCO-2 (12) */
};
static const pcable_t LIFE_PAL_27[] = {
    CB(1, LFO2, SHAPE, 8),        /* LFO 2: a second, slower pulse-width sweep */
    CB(2, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
};
static const pcable_t LIFE_PAL_28[] = {   /* SYNC BASS */
    CB(1, ENV2, SHAPE, 30),       /* ENV 2: the synced VCO-2 swept down from high */
    CB(2, VEL, SHAPE, 12),        /* .. from higher, harder */
    CB(3, VEL, CUT, 10),          /* velocity: brighter */
    CB(4, MW, SHAPE, 16),         /* the wheel: the sync held higher */
    CB(5, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
};
static const pcable_t LIFE_PAL_29[] = {   /* POLY PLUCK (keys) */
    CB(1, VEL, CUT, 14),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 16),        /* velocity: the envelope deeper */
    CB(3, KEY, ERATE, 8),         /* high notes shorter */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 4, 30),      /* TIMBR: the MIX over to VCO-2 */
};
static const pcable_t LIFE_PAL_30[] = {   /* PWM PAD */
    CB(1, LFO2, SHAPE, 14),       /* LFO 2 (slow): the PWM */
    CB(2, LFO3, L2RATE, 14),      /* LFO 3: the PWM's pace drifts */
    CB(3, LFO3, CUT, 5),          /* LFO 3: the filter breathes */
    CB(4, MW, SHAPE, 12),         /* the wheel: deeper PWM */
    CB(5, AT, CUT, 10),           /* pressure: brighter */
};
/* LIFE-END */
static const preset_patch_t PALOMINO_PATCHES[] = {
    PPATCH(&PALOMINO_PRESETS[1], LIFE_PAL_1, P_LFADE + 1, 70),    /* LOW STRINGS */
    PPATCH(&PALOMINO_PRESETS[2], LIFE_PAL_2, P_LFADE + 1, 70),    /* MID STRINGS */
    PPATCH(&PALOMINO_PRESETS[3], LIFE_PAL_3, P_LFADE + 1, 70),    /* HIGH STRINGS */
    PPATCH(&PALOMINO_PRESETS[4], LIFE_PAL_4),    /* SOFT STRINGS */
    PPATCH(&PALOMINO_PRESETS[5], LIFE_PAL_5, P_LFADE + 1, 70),    /* SYNTH CHOIR */
    PPATCH(&PALOMINO_PRESETS[6], LIFE_PAL_6),    /* RESO PAD */
    PPATCH(&PALOMINO_PRESETS[7], LIFE_PAL_7),    /* FAT FIFTHS */
    PPATCH(&PALOMINO_PRESETS[8], LIFE_PAL_8, P_E2ATK + 1, 24, P_E2DEC + 1, 86, P_E2SUS + 1, 50, P_LFADE + 1, 84),    /* LOW BRASS */
    PPATCH(&PALOMINO_PRESETS[9], LIFE_PAL_9, P_E2ATK + 1, 34, P_E2DEC + 1, 82, P_E2SUS + 1, 56, P_LFADE + 1, 84),    /* HIGH BRASS */
    PPATCH(&PALOMINO_PRESETS[10], LIFE_PAL_10, P_E2ATK + 1, 64, P_E2DEC + 1, 110, P_E2SUS + 1, 54, P_LFADE + 1, 84),    /* SYNTH BRASS */
    PPATCH(&PALOMINO_PRESETS[11], PALC_MORPH),         /* MORPH PAD */
    PPATCH(&PALOMINO_PRESETS[12], PALC_TBRASS),        /* SQR BASS */
    PPATCH(&PALOMINO_PRESETS[13], LIFE_PAL_13),    /* POLY KEYS */
    PPATCH(&PALOMINO_PRESETS[14], LIFE_PAL_14),    /* SAW PAD */
    PPATCH(&PALOMINO_PRESETS[15], LIFE_PAL_15),    /* SQR LEAD */
    PPATCH(&PALOMINO_PRESETS[16], LIFE_PAL_16),    /* DARK PAD */
    PPATCH(&PALOMINO_PRESETS[17], LIFE_PAL_17),    /* POLY BRASS */
    PPATCH(&PALOMINO_PRESETS[18], LIFE_PAL_18),    /* SEQ BASS */
    PPATCH(&PALOMINO_PRESETS[19], LIFE_PAL_19),    /* MOTION PAD */
    PPATCH(&PALOMINO_PRESETS[20], LIFE_PAL_20, P_LRATE + 1, 88, P_LD_PIT + 1, 1, P_LFADE + 1, 84),    /* SYNC LEAD */
    PPATCH(&PALOMINO_PRESETS[21], PALC_TSYNC),         /* OCTAVE LEAD */
    PPATCH(&PALOMINO_PRESETS[22], LIFE_PAL_22),    /* SOFT LEAD */
    PPATCH(&PALOMINO_PRESETS[23], LIFE_PAL_23, P_LFADE + 1, 80, P_LRATE + 1, 88),    /* FAT LEAD */
    PPATCH(&PALOMINO_PRESETS[24], LIFE_PAL_24),    /* SAW LEAD */
    PPATCH(&PALOMINO_PRESETS[25], LIFE_PAL_25),    /* SAW BASS */
    PPATCH(&PALOMINO_PRESETS[26], LIFE_PAL_26),    /* SUB BASS */
    PPATCH(&PALOMINO_PRESETS[27], LIFE_PAL_27),    /* PWM STRINGS */
    PPATCH(&PALOMINO_PRESETS[28], LIFE_PAL_28),    /* SYNC BASS */
    PPATCH(&PALOMINO_PRESETS[29], LIFE_PAL_29),    /* POLY PLUCK */
    PPATCH(&PALOMINO_PRESETS[30], LIFE_PAL_30),    /* PWM PAD */
    PPATCH(&PALOMINO_PRESETS[31], PALC_BSTAB),         /* RIPPLE PAD */
};

static const engine_t ENG_PALOMINO = {
    "PALOMINO", {"VCO", "VCF"},
    {
        {"W1", F_ENUM, 0, 5, PV1_SAW, N_PV1, 0},
        {"W2", F_ENUM, 0, 5, PV2_SAW, N_PV2, 0},
        {"VCO2", F_SEMIL, -25, 24, 0, 0, 0},       /* (-25: LOW) */
        {"XMOD", F_PCT, 0, 127, 0, 0, 0},
        {"MIX", F_BIPCT, -64, 63, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"MODE", F_ENUM, 0, 3, PVM_24, N_PVM, 0},
    },
    PALOMINO_PRESETS, sizeof(PALOMINO_PRESETS) / sizeof(PALOMINO_PRESETS[0]), 1, palomino_note_on, palomino_render,
    0xFC60, {P_E5, P_E6, P_E3, P_E4}, 0, 0, 0, 0, 0, 0,
    1, 4, 0,                                         /* (two VCOs; the matrix's XMOD: XMOD) */
};
