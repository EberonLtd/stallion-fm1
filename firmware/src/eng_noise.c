/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* NOISE: from analog-style noise to shift-register noise, through a resonant filter.
 *
 * MODE picks the source:
 *   ANLG  xorshift white noise, plus random impulses (DENS: their density, 0 = none)
 *   DUST  the impulses only: crackle, drops (through a resonant filter)
 *   LFSR  a long shift register (LEN: 23 down to 7 bits) clocked CLK semitones above the key
 *   META  one cycle of a short register sequence per period of the key (LEN: which one): a metallic tone
 * COLR tilts the source from white (0) through pink (64, -3 dB/oct) to brown (127, -6 dB/oct).
 * FREQ / RES: a trapezoidal SVF that morphs from low-pass (RES 0) to a narrow band-pass (RES 127);
 * TRK key-tracks FREQ (127 = one semitone a semitone, around C4). DRFT: a slow random wander of FREQ
 * (and the clock), the level following it. CRSH (ANLG, DUST): sample-and-hold and fewer bits.
 * The track's FLT moves FREQ, SHP moves COLR, PIT the key (so FREQ with TRK, and the clock).
 *
 * State: s[0] s[1] the filter, s[2] the random generator (seeded by the note and the unison detune: a
 * note from silence sounds the same each time), s[3] the drift target and its countdown, s[4..6] the pink
 * poles, s[7] the brown pole; ph[0] the clock phase (LFSR, META) or the hold count (CRSH), ph[1] the
 * register, the held sample or META's DC blocker, ph[2] the drift. */
static const char *const N_NOISE_MODE[] = {"ANLG", "DUST", "LFSR", "META"};
enum { NZ_ANLG, NZ_DUST, NZ_LFSR, NZ_META };
static const char *const N_NOISE_LONG[] = {"L23", "L20", "L17", "L15", "L13", "L11", "L9", "L7", 0};
static const char *const N_NOISE_META[] = {"127", "127B", "93", "63", "63B", "31", "15", "7", 0};
static const param_desc_t NOISE_LONG = {"LEN", F_INT, 0, 127, 0, N_NOISE_LONG, 0};   /* = edit[5] but the names */
static const param_desc_t NOISE_META = {"LEN", F_INT, 0, 127, 0, N_NOISE_META, 0};
static const param_desc_t NOISE_CLK = {"CLK", F_INT, 0, 127, 0, 0, "st"};           /* = edit[7]: above the key */

/* Galois taps of the long registers (maximal length: 2^n - 1 clocks) */
static const uint32_t NOISE_TAPS[8] = {0x420000u, 0x90000u, 1u << 16 | 1u << 13, 0x6000u, 0x100Du, 0x500u, 0x110u, 0x60u};

/* META: one period of a register's output bit (bit i of the words, from the state 1), its length,
 * and the levels of a 1 and a 0 (no DC, the larger at 16384). 7-bit taps 0x60 and 0x78, the 15-bit
 * register with its feedback from bits 0 and 6 (period 93), 6-bit 0x30 and 0x2D, 5-bit, 4-bit, 3-bit */
static const struct { uint32_t w[4]; uint8_t len; int16_t hi, lo; } NOISE_SEQ[8] = {
    {{0xD13C50C1u, 0x91C2F95Cu, 0xA58DED6Cu, 0x01FD533Bu}, 127, 16128, -16384},
    {{0xDBD165F1u, 0x9E46A1FDu, 0x84C15692u, 0x019B1CEBu}, 127, 16128, -16384},
    {{0x41008001u, 0x00492402u, 0x00824824u, 0x00000000u}, 93, 16384, -3404},
    {{0x27179461u, 0x03F566EDu, 0x00000000u, 0x00000000u}, 63, 15872, -16384},
    {{0x75A6C487u, 0x02FCA8CFu, 0x00000000u, 0x00000000u}, 63, 15872, -16384},
    {{0x05763E69u, 0x00000000u, 0x00000000u, 0x00000000u}, 31, 15360, -16384},
    {{0x00000F59u, 0x00000000u, 0x00000000u, 0x00000000u}, 15, 14336, -16384},
    {{0x0000001Du, 0x00000000u, 0x00000000u, 0x00000000u}, 7, 12288, -16384},
};

static const param_desc_t *noise_desc(const track_t *t, uint32_t k)
{
    int32_t mode = t->p[P_E0];
    if (k == 5u && mode >= NZ_LFSR)
        return mode == NZ_LFSR ? &NOISE_LONG : &NOISE_META;
    if (k == 7u && mode >= NZ_LFSR)
        return &NOISE_CLK;
    return 0;
}

/* phase increment of pitch p (1/16 semitone) past the table's top, up to one step a sample */
static uint32_t noise_inc(int32_t p)
{
    uint32_t sh = 0, inc;
    while (p > 2047) {
        p -= 192;
        sh++;
    }
    inc = pitch_inc((uint32_t)clamp(p, 0, 2047));
    return sh && inc >= 0x80000000u >> (sh - 1u) ? 0xFFFFFFFFu : inc << sh;
}
/* (after Clipper 1.2) the same, unbounded (Q32: 2^32 = one clock a sample). LFSR: a clock past the sample rate steps the
 * register more than once a sample (the sequence read further on, as the register would be at that time): its pitch
 * goes on rising with the keys. It used to stop at one step a sample, so every note above it played the same pitch
 * (NZ ARCADE: TONE +84 semitones, the whole upper half of the keyboard; clipper-fm1 #10) */
static uint64_t noise_inc64(int32_t p)
{
    uint32_t sh = 0;
    while (p > 2047) {
        p -= 192;
        sh++;
    }
    return (uint64_t)pitch_inc((uint32_t)clamp(p, 0, 2047)) << (sh < 20u ? sh : 20u);
}
#define NZ_STEPS_MAX 64u                              /* register steps a sample at most (64 x 44.1 kHz) */

static uint32_t isqrt32(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x)
        b >>= 2;
    while (b) {
        if (x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

static void noise_note_on(track_t *t, voice_t *v)
{
    uint32_t seed = (v->note + 1u) * 0x9E3779B9u ^ (uint32_t)v->fine * 0x85EBCA6Bu;   /* UNISON: each its own */
    (void)t;
    v->s[0] = v->s[1] = 0;
    v->s[2] = (int32_t)(seed ? seed : 1u);
    v->s[3] = 0;                                      /* a new drift target at once */
    v->s[4] = v->s[5] = v->s[6] = v->s[7] = 0;
    v->ph[0] = 0;
    v->ph[1] = 0;
    v->ph[2] = 0;
}

static void noise_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t mode = (uint32_t)p[P_E0] & 3u, sel = ((uint32_t)p[P_E5] >> 4) & 7u, i;
    int32_t res = p[P_E3], drft = p[P_E6], crsh = mode < NZ_LFSR ? p[P_E7] : 0;
    int32_t ic1 = v->s[0], ic2 = v->s[1], rng = v->s[2], b0 = v->s[4], b1 = v->s[5], b2 = v->s[6], br = v->s[7];
    uint32_t ph = v->ph[0], reg = v->ph[1];         /* state in locals: out[] may alias v->s[] */
    int32_t drift = (int32_t)v->ph[2], d16 = 0, gain = 32767, gq, colr, cut, k, a1, a2, a3, g, bpg, mix, cf, thr = 0;
    uint32_t inc = 0, taps = NOISE_TAPS[sel];
    uint64_t inc64 = 0;                               /* (after 1.2) LFSR: its clock, Q32, past the sample rate too */
    /* drift: a new random target every 0.7..3 s, followed with a 0.7 s lag */
    if (drft) {
        int32_t tg = v->s[3] >> 16, cnt = v->s[3] & 0xFFFF;
        if (--cnt <= 0) {
            uint32_t r = noise32(&rng);
            tg = (int32_t)(r >> 16) - 32768;
            cnt = 1024 + (int32_t)(r & 3071u);
        }
        v->s[3] = (int32_t)((uint32_t)tg << 16 | (uint32_t)cnt);
        drift += ((tg << 4) - drift) >> 10;           /* Q19 */
        d16 = ((drift >> 11) * drft * drft) >> 14;    /* up to about +-16 semitones */
        gain = 32767 - ((((32767 - (drift >> 4)) >> 1) * drft) >> 8);   /* down to -6 dB as it falls */
    }
    /* COLR (+ SHP), 0..127 << 8: white -> pink -> brown */
    colr = clamp((p[P_E1] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    cf = colr <= (64 << 8) ? colr << 1 : ((colr - (64 << 8)) * 520) >> 8;   /* the crossfade, Q15 */
    /* FREQ (+ FLT), key-tracked by TRK around C4, the drift: SVF cutoff units (256 an index, 0.86 st) */
    cut = (p[P_E2] << 8) + m->cutoff + (((m->pitch16 - 60 * 16) * p[P_E4] * 151) >> 10) + ((d16 * 4785) >> 8);
#if CLIPPER_SYNTH
    if (mode <= NZ_DUST && v->vel > 110)              /* the accent (voice.c: velocity over 110 opens the */
        cut -= (m->envq15 * 24) >> 7;                 /* cutoff up to 24 steps) undone: the band is the note's pitch */
#endif
    cut = clamp(cut, 0, 127 << 8);
    g = SVF_G[cut >> 8];
    if (cut < (127 << 8))
        g += ((SVF_G[(cut >> 8) + 1] - g) * (cut & 255)) >> 8;
    k = 8192 - res * 7800 / 127;                      /* damping, Q12: 2.0 .. 0.1 */
    a1 = (int32_t)((4096u << 13) / (uint32_t)(4096 + ((g * (g + k)) >> 12)));
    a2 = (a1 * g) >> 12;
    a3 = (a2 * g) >> 12;
    bpg = (int32_t)isqrt32((uint32_t)k << 12);        /* band-pass x 1.5 sqrt(damping): its level holds with the Q */
    mix = res * 258;                                  /* low-pass -> band-pass, Q15 */
    {   /* level: make up most of what the filter takes from white noise (less for pink and brown), +12 dB for
         * the sparse DUST, the drift's level; Q12, mk in 1/256 octave: 2^(mk / 256), at most x 16 */
        int32_t wf = 32767 - ((colr * 230) >> 8), mk = (((((127 << 8) - cut) >> 8) * 7) * wf) >> 15;
#if CLIPPER_SYNTH
        if (mode == NZ_META)                          /* META's tone: none for the filter (its energy */
            mk = 0;                                   /* sits in its harmonics, not spread as white noise's) */
#endif
        mk += 214 + (mode == NZ_DUST ? 512 : 0);
        if (mk > 1279)
            mk = 1279;
        gq = (4096 + ((mk & 255) << 4)) << (mk >> 8);
        gq = (gq * (gain >> 3)) >> 12;
        if (gq > 65535)
            gq = 65535;
    }
    if (mode <= NZ_DUST && p[P_E5]) {                 /* impulses: 1 .. 2000 a second */
        uint32_t e = (uint32_t)p[P_E5] * 22u, r = 1u << (e >> 8);   /* 2^(DENS * 11 / 127), Q8 fraction */
        r += (r * (e & 255u)) >> 8;
        thr = (int32_t)((r * 97391u) >> 16);          /* per 2^16 of a sample: rate * 65536 / FS, Q16 */
    }
    if (mode == NZ_LFSR) {
        uint32_t mask = taps;                         /* the register's own bits (MODE / LEN changes) */
        inc64 = noise_inc64(m->pitch16 + p[P_E7] * 16 + d16);
        mask |= mask >> 1;
        mask |= mask >> 2;
        mask |= mask >> 4;
        mask |= mask >> 8;
        mask |= mask >> 16;
        if (!(reg & mask))                            /* a new note: the register from the note's seed */
            reg = (uint32_t)rng | 1u;
        reg &= mask;
    } else if (mode == NZ_META) {
        inc = p[P_E7] || d16 ? noise_inc(m->pitch16 + p[P_E7] * 16 + d16) : m->inc;
    }
    for (i = 0; i < n; i++) {
        int32_t w, x, v1, v2, v3, y;
        switch (mode) {
        case NZ_DUST:
        case NZ_ANLG: {
            uint32_t r = noise32(&rng);
            w = mode == NZ_ANLG ? (int32_t)(r >> 17) - 16384 : 0;
            if ((int32_t)(r & 0xFFFFu) < thr) {       /* an impulse, random size and sign */
                int32_t a = 8192 + (int32_t)((r >> 18) & 0x7FFFu);
                w += (r & 0x20000u) ? a : -a;
            }
            break;
        }
        case NZ_LFSR: {
            uint64_t a = (uint64_t)ph + inc64;
            uint32_t ns = (uint32_t)(a >> 32) < NZ_STEPS_MAX ? (uint32_t)(a >> 32) : NZ_STEPS_MAX;
            ph = (uint32_t)a;
            while (ns--)                               /* (after 1.2: as many steps as clocks in this sample) */
                reg = (reg >> 1) ^ (-(reg & 1u) & taps);
            w = (reg & 1u) ? 12000 : -12000;
            break;
        }
        default: {
            uint32_t j = ((ph >> 16) * NOISE_SEQ[sel].len) >> 16;
            ph += inc;
            w = (NOISE_SEQ[sel].w[j >> 5] >> (j & 31u)) & 1u ? NOISE_SEQ[sel].hi : NOISE_SEQ[sel].lo;
            break;
        }
        }
        x = w;
        if (colr) {                                   /* pink: three leaky poles (Kellet), brown: a leaky integrator */
            int32_t pk;
            b0 = clamp(((b0 * 32690) >> 15) + ((w * 3245) >> 15), -65535, 65535);   /* (after Clipper 1.2: bounded: a held */
            b1 = clamp(((b1 * 31555) >> 15) + ((w * 9716) >> 15), -65535, 65535);   /* LFSR level grew them past 2^31 / 32690) */
            b2 = ((b2 * 18678) >> 15) + ((w * 17247) >> 14);
            pk = (b0 + b1 + b2 + ((w * 6055) >> 15)) >> 2;
            if (colr <= (64 << 8)) {
                x = w + mulq15(pk - w, cf);
            } else {
                br = clamp(((br * 32580) >> 15) + ((w * 3480) >> 15), -65535, 65535);
                x = pk + mulq15(br - pk, cf);
            }
        }
        /* the SVF (dsp.c tsvf_lp, with the band-pass), rounded: at low cutoffs the floor of the shifts
         * walked the integrators off zero (DC) */
        v3 = x - ic2;
        v1 = (a1 * ic1 + a2 * v3 + 4096) >> 13;
        v2 = ic2 + ((a2 * ic1 + a3 * v3 + 4096) >> 13);
        ic1 = clamp(2 * v1 - ic1, -150000, 150000);
        ic2 = clamp(2 * v2 - ic2, -150000, 150000);
        v1 = (v1 * bpg * 3) >> 13;
        y = v2 + (((v1 - v2) * (mix >> 3)) >> 12);
        y = soft_knee(((y >> 4) * gq) >> 8, 24000);
        if (mode == NZ_META) {                        /* the knee bends a lopsided tone: a DC blocker, ~14 Hz */
            reg += (uint32_t)(((y << 8) - (int32_t)reg) >> 9);
            y -= (int32_t)reg >> 8;
        }
        if (crsh) {                                   /* hold 1..16 samples, 16 .. 4 bits */
            if ((int32_t)--ph <= 0 || ph > 16u) {
                ph = 1u + (uint32_t)crsh / 8u;
                reg = (uint32_t)(y >> (crsh / 11) << (crsh / 11));
            }
            y = (int32_t)reg;
        }
        out[i] += voice_amp(y, m, i);
    }
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = rng;
    v->s[4] = b0;
    v->s[5] = b1;
    v->s[6] = b2;
    v->s[7] = br;
    v->ph[0] = ph;
    v->ph[1] = reg;
    v->ph[2] = (uint32_t)drift;
}

#if CLIPPER_SYNTH
/* (Stallion 1.0) synth sounds played on the keys, from the two pitched sides of the engine:
 *  - META: one cycle of a short register's sequence per key period, a buzz exactly on the note (the 7-step
 *    sequence: six harmonics, round; 15: falling; 31 to 127: a flat comb, as bright as a narrow pulse) through
 *    the resonant SVF: the subtractive basses, keys, brass, strings, pads and leads of a "periodic noise" voice.
 *    LEN 8 = 127, 40 = 93, 72 = 63B, 88 = 31, 104 = 15, 120 = 7. The longer registers alias above their pitch
 *    range (127: ~F3, 63: ~F4, 31: ~F5): the basses take them, the pads and leads the short ones.
 *  - ANLG at RES 104..127 and TRK 127: breath through a narrow band that sits on the note (FREQ 44; 58 an octave
 *    up): air pads and breathy synth leads. Measured within ~15 cents C4..C6, ~75 cents flat at C3, so they are
 *    pads and leads played from C4 up. The band is their pitch: no ENV 1 to the filter, AT to vibrato or off
 *    (P_ATDST), and MAC1 (BRITE) re-routed: its own +-40 cutoff steps cancelled (MAC1 -> CUT x -40) and the
 *    noise made whiter instead (MAC1 -> SHAPE, the COLR). Their movement: RES (the band's focus, MT_E1 + 3),
 *    COLR (SHAPE), the level, small vibratos.
 * Every one moves through its cables (NOISE_PATCHES). Dry, centred, no arp. */
#define NZ_VIB P_LD_PIT + 1, 1, P_LRATE + 1, 88, P_LFADE + 1, 90    /* a delayed vibrato, ~5 Hz */
#define NZ_SVIB P_LD_PIT + 1, 1, P_LRATE + 1, 80, P_LFADE + 1, 110  /* a slower, later one, ~3.4 Hz */
enum { NZL_127 = 8, NZL_93 = 40, NZL_63 = 72, NZL_31 = 88, NZL_15 = 104, NZL_7 = 120 };   /* META's LEN */
static const preset_t NOISE_PRESETS[] = {
    /* {MODE, COLR, FREQ, RES, TRK, DENS / LEN, DRFT, CRSH / CLK} */
    {"INIT", {NZ_ANLG, 64, 80, 30, 64, 0, 0, 0}, {10, 70, 90, 60}, 0, 0, FX(0, 0, 0, 0)},
    {"NOISE BASS", {NZ_META, 20, 34, 50, 64, NZL_127, 0, 0}, {0, 74, 96, 30}, 28, 1, FX(0, 0, 0, 0)},
    {"GRIT BASS", {NZ_META, 30, 40, 30, 80, NZL_93, 0, 0}, {0, 80, 100, 24}, 20, 1, FX(0, 0, 0, 0)},
    {"SUB BUZZ", {NZ_META, 30, 30, 10, 64, NZL_7, 0, 0}, {0, 90, 110, 30}, 10, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 10)},
    {"RESO BASS", {NZ_META, 10, 36, 96, 70, NZL_31, 0, 0}, {0, 70, 70, 26}, 40, 1, FX(0, 0, 0, 0)},
    {"WARM BASS", {NZ_META, 20, 38, 20, 64, NZL_15, 0, 0}, {2, 80, 110, 40}, 16, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 12)},
    {"SHIFT KEYS", {NZ_META, 20, 60, 30, 90, NZL_7, 0, 0}, {0, 76, 60, 50}, 24, 0, FX(0, 0, 0, 0)},
    {"BUZZ KEYS", {NZ_META, 30, 56, 60, 100, NZL_31, 0, 0}, {0, 70, 50, 46}, 30, 0, FX(0, 0, 0, 0)},
    {"SHIFT PLUCK", {NZ_META, 10, 50, 50, 100, NZL_15, 0, 0}, {0, 64, 30, 50}, 40, 0, FX(0, 0, 0, 0)},
    {"BUZZ BRASS", {NZ_META, 30, 50, 24, 90, NZL_31, 0, 0}, {12, 80, 100, 40}, 30, 0, FX(0, 0, 0, 0)},
    {"SOFT BRASS", {NZ_META, 20, 46, 16, 80, NZL_15, 0, 0}, {30, 80, 110, 50}, 20, 0, FX(0, 0, 0, 0)},
    {"BUZZ STRINGS", {NZ_META, 30, 62, 20, 80, NZL_31, 0, 0}, {70, 90, 120, 80}, 0, 0, FX(0, 0, 0, 0)},
    {"LOOP PAD", {NZ_META, 30, 54, 40, 80, NZL_15, 0, 0}, {90, 90, 120, 100}, 0, 0, FX(0, 0, 0, 0)},
    {"DARK PAD", {NZ_META, 64, 42, 30, 64, NZL_63, 0, 0}, {100, 100, 120, 100}, 0, 0, FX(0, 0, 0, 0)},
    {"RESO PAD", {NZ_META, 20, 60, 92, 80, NZL_31, 0, 0}, {80, 90, 120, 90}, 0, 0, FX(0, 0, 0, 0)},
    {"SHIFT LEAD", {NZ_META, 20, 76, 40, 100, NZL_15, 0, 0}, {2, 80, 110, 40}, 12, 1, FX(0, 0, 0, 0), XP(NZ_VIB, P_GLIDE + 1, 12)},
    {"BUZZ LEAD", {NZ_META, 0, 84, 70, 100, NZL_31, 0, 0}, {0, 76, 100, 30}, 18, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 14)},
    {"BIT LEAD", {NZ_META, 0, 70, 30, 100, NZL_7, 0, 0}, {0, 70, 120, 30}, 0, 1, FX(0, 0, 0, 0), XP(NZ_VIB, P_GLIDE + 1, 20)},
    {"RESO LEAD", {NZ_META, 10, 66, 96, 110, NZL_15, 0, 0}, {0, 80, 110, 40}, 30, 1, FX(0, 0, 0, 0), XP(NZ_VIB)},
    {"BREATH PAD", {NZ_ANLG, 40, 44, 116, 127, 0, 4, 0}, {80, 90, 120, 90}, 0, 0, FX(0, 0, 0, 0)},
    {"VAPOR PAD", {NZ_ANLG, 20, 58, 120, 127, 0, 0, 0}, {90, 100, 120, 100}, 0, 0, FX(0, 0, 0, 0)},
    {"AIR PAD", {NZ_ANLG, 50, 44, 112, 127, 0, 20, 0}, {90, 90, 120, 100}, 0, 0, FX(0, 0, 0, 0), XP(P_ATDST + 1, MD_VIB)},
    {"AIRY STRINGS", {NZ_ANLG, 20, 57, 100, 127, 0, 10, 0}, {70, 90, 120, 90}, 0, 0, FX(0, 0, 0, 0), XP(P_ATDST + 1, MD_VIB)},
    {"AIR CHOIR", {NZ_ANLG, 64, 44, 112, 127, 0, 6, 0}, {90, 90, 120, 100}, 0, 0, FX(0, 0, 0, 0), XP(NZ_SVIB)},
    {"TONAL NOISE", {NZ_ANLG, 64, 57, 120, 127, 0, 0, 0}, {10, 80, 110, 60}, 0, 0, FX(0, 0, 0, 0), XP(P_ATDST + 1, MD_VIB)},
    {"GHOST PAD", {NZ_ANLG, 30, 44, 127, 127, 0, 0, 0}, {90, 100, 127, 110}, 0, 0, FX(0, 0, 0, 0)},
    {"SOFT LEAD", {NZ_ANLG, 20, 44, 122, 127, 0, 0, 0}, {20, 80, 120, 40}, 0, 1, FX(0, 0, 0, 0), XP(NZ_VIB, P_GLIDE + 1, 10)},
    {"HUSH LEAD", {NZ_ANLG, 90, 44, 124, 127, 0, 0, 0}, {30, 80, 120, 50}, 0, 1, FX(0, 0, 0, 0), XP(NZ_VIB)},
    {"SOFT KEYS", {NZ_META, 20, 52, 20, 80, NZL_7, 0, 0}, {4, 84, 50, 60}, 16, 0, FX(0, 0, 0, 0)},
    {"MIST PAD", {NZ_ANLG, 50, 44, 104, 127, 0, 8, 0}, {90, 100, 120, 110}, 0, 0, FX(0, 0, 0, 0)},
    {"WISP LEAD", {NZ_ANLG, 10, 44, 126, 127, 0, 0, 0}, {4, 70, 120, 30}, 0, 1, FX(0, 0, 0, 0), XP(NZ_VIB, P_GLIDE + 1, 16)},
    {"BREATH LEAD", {NZ_ANLG, 40, 44, 116, 127, 0, 0, 0}, {10, 70, 120, 30}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 20, P_ATDST + 1, MD_VIB)},
};
/* the cables: each sound's movement (params.c PATCH_TABLES, tests/preset_patch_test.c) */
#define NZC_BRITE(a, b) CB(a, MAC1, CUT, -40), CB(b, MAC1, SHAPE, -28)   /* ANLG: BRITE whitens, the band stays */
static const pcable_t NZC_NBASS[] = {
    CB(1, VEL, E1AMT, 24),        /* harder: more filter envelope */
    CB(2, ENV2, E1 + 3, 14),      /* ENV 2 (its own, quicker): a resonant edge on the attack that settles */
    CB(3, LFO3, SHAPE, 16),       /* LFO 3 (0.4 Hz): the colour breathes */
    CB(4, MW, CUT, 20),           /* the wheel opens it */
};
static const pcable_t NZC_GRIT[] = {
    CB(1, ENV2, CUT, 14),         /* a snap on the cutoff, apart from the amp's */
    CB(2, LFO2, SHAPE, 8),        /* LFO 2 (0.7 Hz): the grit comes and goes */
    CB(3, VEL, CUT, 8),
    CB(4, AT, E1 + 3, 12),        /* pressure: more resonance */
};
static const pcable_t NZC_SUB[] = {
    CB(1, LFO3, CUT, 7),          /* LFO 3 (0.24 Hz): a slow breath on the cutoff */
    CB(2, VEL, E1AMT, 30),
    CB(3, MW, E1 + 3, 30),        /* the wheel: resonance */
    CB(4, RAND, CUT, 4),          /* each note a little its own */
};
static const pcable_t NZC_RESO[] = {
    CB(1, VEL, E1AMT, 30),
    CB(2, ENV2, E1 + 3, -16),     /* the resonance dips and comes back */
    CB(3, AT, CUT, 16),
    CB(4, LFO2, CUT, 4),          /* LFO 2 per note (0.4 Hz) */
};
static const pcable_t NZC_WARM[] = {
    CB(1, LFO3, SHAPE, 12),       /* LFO 3: the colour */
    CB(2, LFO2, CUT, 3),          /* LFO 2 (0.24 Hz): the cutoff */
    CB(3, LFO3, L2RATE, 10),      /* .. its rate drifting */
    CB(4, VEL, CUT, 10),
};
static const pcable_t NZC_SKEYS[] = {
    CB(1, VEL, E1AMT, 30),
    CB(2, VEL, CUT, 10),
    CB(3, ENV2, SHAPE, -20),      /* a whiter onset */
    CB(4, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz) */
    CB(5, AT, E1 + 3, 14),        /* pressure: resonance */
    CB(6, LFO2, E1 + 3, 6),       /* LFO 2 (0.7 Hz): the resonance, across it */
};
static const pcable_t NZC_BKEYS[] = {
    CB(1, VEL, E1AMT, 24),
    CB(2, LFO2, CUT, 4),          /* LFO 2 per note (1.6 Hz): a slow wah */
    CB(3, LFO3, L2RATE, 8),       /* .. its rate drifting */
    CB(4, MW, E1 + 3, 30),
};
static const pcable_t NZC_PLUCK[] = {
    CB(1, VEL, E1AMT, 30),
    CB(2, ENV2, E1 + 3, 20),      /* resonance on the attack */
    CB(3, RAND, CUT, 6),          /* each note a little its own */
    CB(4, LFO3, SHAPE, 16),       /* LFO 3 (0.4 Hz): the colour */
};
static const pcable_t NZC_BBRASS[] = {
    CB(1, ENV2, CUT, 14),         /* ENV 2: the brass swell of the filter */
    CB(2, VEL, E1AMT, 24),
    CB(3, VEL, ERATE, 12),        /* harder: quicker */
    CB(4, LFO2, PITCH, 2),        /* LFO 2 (3.4 Hz): a trace of vibrato */
    CB(5, AT, CUT, 10),
    CB(6, LFO3, SHAPE, 10),       /* LFO 3 (0.4 Hz): the colour */
};
static const pcable_t NZC_SBRASS[] = {
    CB(1, ENV2, CUT, 12),         /* the slow swell */
    CB(2, VEL, CUT, 8),
    CB(3, LFO3, SHAPE, 14),       /* LFO 3 (0.4 Hz): the colour */
    CB(4, MW, CUT, 16),
};
static const pcable_t NZC_BSTR[] = {
    CB(1, LFO2, PITCH, 3),        /* LFO 2 (2.7 Hz): a gentle vibrato */
    CB(2, LFO3, CUT, 4),
    CB(3, RAND, PITCH, 3),        /* each note a few cents its own: the chord thickens */
    CB(4, AT, CUT, 12),
    CB(5, MW, SHAPE, 20),
};
static const pcable_t NZC_LOOP[] = {
    CB(1, LFO3, CUT, 8),          /* LFO 3 (0.24 Hz) on the cutoff */
    CB(2, LFO2, E1 + 3, 10),      /* LFO 2 (0.5 Hz) on the resonance */
    CB(3, LFO3, L2RATE, 12),      /* .. and on LFO 2's rate */
    CB(4, VEL, CUT, 6),
    CB(5, AT, AMP, 8),
};
static const pcable_t NZC_DARK[] = {
    CB(1, ENV2, CUT, 14),         /* ENV 2: a slow opening */
    CB(2, LFO3, SHAPE, 14),
    CB(3, SH, CUT, 3),            /* LFO 2's S&H: a small drift */
    CB(4, MW, CUT, 24),
};
static const pcable_t NZC_RPAD[] = {
    CB(1, LFO3, CUT, 12),         /* LFO 3 (0.2 Hz): the resonant sweep */
    CB(2, LFO2, CUT, 4),          /* LFO 2 (0.75 Hz) across it */
    CB(3, AT, E1 + 3, 10),
    CB(4, VEL, SHAPE, -10),
};
static const pcable_t NZC_SLEAD[] = {
    CB(1, AT, CUT, 14),
    CB(2, MW, E1 + 3, 30),
    CB(3, ENV2, SHAPE, -24),      /* a whiter onset */
    CB(4, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz) */
};
static const pcable_t NZC_BLEAD[] = {
    CB(1, VEL, E1AMT, 24),
    CB(2, LFO2, CUT, 5),          /* LFO 2 per note (1 Hz) */
    CB(3, LFO3, L2RATE, 10),
    CB(4, AT, E1 + 3, 14),
};
static const pcable_t NZC_BIT[] = {
    CB(1, MW, SHAPE, 30),         /* the wheel darkens the colour */
    CB(2, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz) */
    CB(3, VEL, CUT, 8),
    CB(4, ENV2, PITCH, -4),       /* each note scoops up ~9 cents */
};
static const pcable_t NZC_RLEAD[] = {
    CB(1, VEL, E1AMT, 30),
    CB(2, AT, CUT, 16),
    CB(3, LFO2, E1 + 3, 6),       /* LFO 2 (1.2 Hz) on the resonance */
    CB(4, LFO3, SHAPE, 12),
    CB(5, MW, CUT, 20),
};
static const pcable_t NZC_BPAD[] = {
    CB(1, LFO3, E1 + 3, 6),       /* LFO 3: the band's focus breathes */
    CB(2, LFO2, AMP, 5),          /* LFO 2 (0.3 Hz): the level, a little */
    CB(3, AT, E1 + 3, 10),        /* pressure: a purer tone */
    NZC_BRITE(4, 5),
};
static const pcable_t NZC_VAPOR[] = {
    CB(1, ENV2, E1 + 3, -14),     /* ENV 2: airy at first, the tone forms */
    CB(2, LFO3, SHAPE, 16),
    CB(3, LFO2, PITCH, 3),        /* LFO 2 (3.4 Hz): a soft shimmer */
    NZC_BRITE(4, 5),
};
static const pcable_t NZC_CHOIR[] = {
    CB(1, LFO3, E1 + 3, 8),
    CB(2, SH, SHAPE, 6),          /* LFO 2's S&H: the colour wanders */
    CB(3, AT, AMP, 8),
    NZC_BRITE(4, 5),
};
static const pcable_t NZC_GHOST[] = {
    CB(1, LFO2, PITCH, 3),        /* LFO 2 (2 Hz): a slow vibrato */
    CB(2, LFO3, L2RATE, 14),      /* .. its rate drifting */
    CB(3, LFO3, AMP, 6),
    CB(4, MW, E1 + 3, -30),       /* the wheel: breathier */
    NZC_BRITE(5, 6),
};
static const pcable_t NZC_SOFT[] = {
    CB(1, ENV2, E1 + 3, -20),     /* a breath at the onset, then the tone */
    CB(2, AT, AMP, 10),           /* pressure swells it */
    CB(3, VEL, SHAPE, -12),       /* harder: whiter */
    NZC_BRITE(4, 5),
};
static const pcable_t NZC_HUSH[] = {
    CB(1, LFO3, E1 + 3, 10),      /* LFO 3 (0.4 Hz): the focus */
    CB(2, AT, SHAPE, -20),        /* pressure: brighter */
    CB(3, LFO2, AMP, 4),          /* LFO 2 (4 Hz): a soft tremolo */
    NZC_BRITE(4, 5),
};
static const pcable_t NZC_SOFTK[] = {
    CB(1, VEL, E1AMT, 30),
    CB(2, VEL, SHAPE, -16),       /* harder: brighter colour */
    CB(3, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz) */
    CB(4, AT, AMP, 8),
};
static const pcable_t NZC_MIST[] = {
    CB(1, LFO3, E1 + 3, 10),      /* LFO 3 (0.18 Hz) on the focus */
    CB(2, LFO2, SHAPE, 10),       /* LFO 2 (0.55 Hz) on the colour */
    CB(3, LFO3, L2RATE, 10),
    CB(4, AT, E1 + 3, 10),
    NZC_BRITE(5, 6),
};
static const pcable_t NZC_WISP[] = {
    CB(1, ENV2, E1 + 3, -24),     /* a breath at the onset */
    CB(2, LFO3, SHAPE, 12),
    CB(3, VEL, E1 + 3, -8),       /* harder: breathier */
    NZC_BRITE(4, 5),
};
#define NZP(i, c, ...) PPATCH(&NOISE_PRESETS[i], c, __VA_ARGS__)
static const preset_patch_t NOISE_PATCHES[] = {
    NZP(1, NZC_NBASS, P_E2DEC + 1, 40, P_E2REL + 1, 30, P_MWDST + 1, MD_OFF),
    NZP(2, NZC_GRIT, P_E2DEC + 1, 40, P_E2REL + 1, 30, P_L2RATE + 1, 50, P_ATDST + 1, MD_OFF),
    NZP(3, NZC_SUB, P_L3RATE + 1, 30, P_MWDST + 1, MD_OFF),
    NZP(4, NZC_RESO, P_E2DEC + 1, 60, P_E2SUS + 1, 40, P_L2RATE + 1, 40, P_L2MODE + 1, L2_KEY),
    NZP(5, NZC_WARM, P_L2RATE + 1, 30),
    NZP(6, NZC_SKEYS, P_E2DEC + 1, 50, P_E2REL + 1, 40, P_L2RATE + 1, 50, P_ATDST + 1, MD_OFF),
    NZP(7, NZC_BKEYS, P_L2RATE + 1, 66, P_L2MODE + 1, L2_KEY, P_MWDST + 1, MD_OFF),
    NZP(8, NZC_PLUCK, P_E2DEC + 1, 40, P_E2REL + 1, 40),
    NZP(9, NZC_BBRASS, P_E2ATK + 1, 20, P_E2SUS + 1, 80, P_E2REL + 1, 40, P_L2RATE + 1, 80),
    NZP(10, NZC_SBRASS, P_E2ATK + 1, 40, P_E2DEC + 1, 80, P_E2SUS + 1, 90, P_MWDST + 1, MD_OFF),
    NZP(11, NZC_BSTR, P_L2RATE + 1, 76, P_MWDST + 1, MD_OFF, P_ATDST + 1, MD_OFF),
    NZP(12, NZC_LOOP, P_L3RATE + 1, 30, P_L2RATE + 1, 44, P_ATDST + 1, MD_OFF),
    NZP(13, NZC_DARK, P_E2ATK + 1, 110, P_E2DEC + 1, 100, P_E2SUS + 1, 60, P_L2RATE + 1, 30),
    NZP(14, NZC_RPAD, P_L3RATE + 1, 26, P_L2RATE + 1, 52, P_ATDST + 1, MD_OFF),
    NZP(15, NZC_SLEAD, P_E2DEC + 1, 60, P_E2REL + 1, 40, P_ATDST + 1, MD_VIB, P_MWDST + 1, MD_OFF),
    NZP(16, NZC_BLEAD, P_L2RATE + 1, 56, P_L2MODE + 1, L2_KEY, P_ATDST + 1, MD_OFF),
    NZP(17, NZC_BIT, P_E2DEC + 1, 30, P_E2REL + 1, 20, P_MWDST + 1, MD_OFF),
    NZP(18, NZC_RLEAD, P_L2RATE + 1, 60, P_ATDST + 1, MD_OFF, P_MWDST + 1, MD_OFF),
    NZP(19, NZC_BPAD, P_L2RATE + 1, 34, P_ATDST + 1, MD_OFF),
    NZP(20, NZC_VAPOR, P_E2DEC + 1, 100, P_E2REL + 1, 80, P_L2RATE + 1, 80, P_ATDST + 1, MD_VIB),
    NZP(23, NZC_CHOIR, P_L2RATE + 1, 40, P_ATDST + 1, MD_OFF),
    NZP(25, NZC_GHOST, P_L2RATE + 1, 70, P_ATDST + 1, MD_VIB, P_MWDST + 1, MD_OFF),
    NZP(26, NZC_SOFT, P_E2DEC + 1, 40, P_E2REL + 1, 30, P_ATDST + 1, MD_VIB),
    NZP(27, NZC_HUSH, P_L2RATE + 1, 82, P_ATDST + 1, MD_VIB),
    NZP(28, NZC_SOFTK, P_ATDST + 1, MD_OFF),
    NZP(29, NZC_MIST, P_L3RATE + 1, 24, P_L2RATE + 1, 46, P_ATDST + 1, MD_OFF),
    NZP(30, NZC_WISP, P_E2DEC + 1, 30, P_E2REL + 1, 30, P_ATDST + 1, MD_VIB),
};
#undef NZP
#undef NZC_BRITE
#undef NZ_VIB
#undef NZ_SVIB
#else
/* {MODE, COLR, FREQ, RES, TRK, DENS, DRFT, CRSH / CLK} */
static const preset_t NOISE_PRESETS[] = {
    {"WIND", {NZ_ANLG, 64, 70, 100, 127, 0, 70, 0}, {90, 90, 110, 90}, 0, 0, FX(0, 30, 20, 80)},
    {"RAIN", {NZ_DUST, 30, 88, 80, 64, 105, 40, 0}, {10, 90, 127, 70}, 0, 0, FX(0, 0, 20, 70)},
    /* a 7-bit register (127 steps) clocked 84 semitones (7 octaves) above the key: one period per key period,
     * a pitched chip-style buzz */
    {"NZ ARCADE", {NZ_LFSR, 0, 127, 0, 127, 112, 0, 84}, {0, 80, 0, 50}, 0, 0, FX(0, 0, 10, 20)},
    {"NZ METAL", {NZ_META, 0, 100, 40, 100, 32, 0, 0}, {0, 75, 40, 60}, 10, 0, FX(0, 20, 30, 45)},
    /* SLOOP 2.5 */
    {"OCEAN", {NZ_ANLG, 100, 50, 20, 0, 0, 110, 0}, {100, 90, 127, 100}, 0, 0, FX(0, 20, 0, 70)},
    {"VINYL", {NZ_DUST, 0, 110, 10, 0, 70, 20, 0}, {0, 64, 127, 40}, 0, 0, FX(0, 0, 0, 10)},
    {"HISS", {NZ_ANLG, 0, 115, 0, 0, 0, 0, 0}, {20, 64, 127, 40}, 0, 0, FX(0, 0, 0, 20)},
    {"RISER", {NZ_ANLG, 30, 60, 110, 127, 0, 0, 0}, {100, 90, 127, 60}, 0, 0, FX(0, 20, 30, 60)},
    {"NZ SNARE", {NZ_ANLG, 20, 90, 30, 64, 0, 0, 0}, {0, 60, 0, 44}, 0, 0, FX(0, 0, 0, 25)},
    {"BITCRUSH", {NZ_ANLG, 40, 90, 40, 64, 0, 0, 100}, {0, 70, 110, 40}, 0, 0, FX(0, 0, 20, 30)},
    {"RADIO", {NZ_LFSR, 20, 70, 90, 64, 0, 60, 48}, {10, 70, 120, 50}, 0, 0, FX(0, 10, 30, 40)},
};
#endif

/* (after Clipper 1.2) UNISON (voice.c unison_spread): the phase moved by r (Q32) and the noise from its own
 * seed (the note's: every voice the same noise at DTUNE 0) */
static void noise_spread(track_t *t, voice_t *v, uint32_t r)
{
    (void)t;
    v->ph[0] += r;
    v->s[2] = (int32_t)((uint32_t)v->s[2] ^ r);
    if (!v->s[2])
        v->s[2] = 1;
}

static const engine_t ENG_NOISE = {
    .name = "NOISE",
    .page_title = {"SRC", "MOVE"},
    .edit = {
        {"MODE", F_ENUM, 0, 3, 0, N_NOISE_MODE, 0},
        {"COLR", F_PCT, 0, 127, 64, 0, 0},
        {"FREQ", F_CUTOFF, 0, 127, 80, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"TRK", F_PCT, 0, 127, 64, 0, 0},
        {"DENS", F_PCT, 0, 127, 0, 0, 0},           /* LFSR, META: the register (noise_desc) */
        {"DRFT", F_PCT, 0, 127, 0, 0, 0},
        {"CRSH", F_PCT, 0, 127, 0, 0, 0},           /* LFSR, META: the clock above the key */
    },
    .presets = NOISE_PRESETS,
    .npresets = NELEM(NOISE_PRESETS),
    .note_on = noise_note_on,
    .render = noise_render,
    .fil_page = 0,
    .color = 0xC618,
    .macro = {P_E2, P_E3, P_E1, P_REL},
    .desc = noise_desc,
};
