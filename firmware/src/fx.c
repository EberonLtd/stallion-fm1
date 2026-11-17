/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Changes: Copyright (C) 2026 Eberon Limited (John Gorman) */
/* Effects: per-track DIST insert, then sends into three shared buses (chorus, tempo delay, reverb).
 * Stereo dry mix; the chorus and the reverb come back in stereo, the delay in the middle. */
#if CLIPPER_SYNTH                /* Clipper: the line at half the rate (22.05 kHz, its repeats are low-passed anyway):
                                  * the same 1.49 s in half the memory (64 KB for the wavetables) */
#define DLY_LEN 32768u
#define DLY_SPAN (2u * DLY_LEN)  /* samples the line holds */
#else
#define DLY_LEN 65536u           /* 1.49 s: 1/4 down to 41 BPM, 1/8 dotted down to 31 (clamped below) */
#define DLY_SPAN DLY_LEN
#endif
#define CHO_LEN 2048u
#define PART_MAX 880000                                 /* (1.0) a part's sum, ~27 x full scale: x LEVEL_Q12[127] (9713)
                                                         * >> 2 stays in 32 bits */
#define MIX_MAX 2000000                                 /* (1.0) ~61 x full scale: far into the limiter, never past 32 bits */
static int16_t dly_buf[DLY_LEN] __attribute__((section(".pool")));
static int16_t cho_buf[CHO_LEN] __attribute__((section(".pool")));
/* the reverb: two input diffusers, then four delay lines mixed by a Hadamard matrix (a feedback delay
 * network: every echo feeds all four, so it thickens instead of ringing like a comb), damped in the
 * loop, one line slowly modulated (no metallic tone on long tails); left and right take different lines */
#define REV_MOD 12               /* samples the modulated line moves (+-) */
static const uint16_t REV_LINE[4] = {1559, 1931, 2389, 2791};   /* 35..63 ms, coprime */
static const uint16_t REV_AP[2] = {556, 441};
#if CLIPPER_SYNTH                /* (1.0) in the pool, which has room in the synth (48 KB free): .lowram put line 0, the
                                  * left channel's own, right above the second core's stacks */
static int16_t rev_line[1559 + 1931 + 2389 + 2791 + REV_MOD + 2] __attribute__((section(".pool")));
#else
static int16_t rev_line[1559 + 1931 + 2389 + 2791 + REV_MOD + 2] __attribute__((section(".lowram")));   /* (the pool is
                                                                      * full; .lowram: the SRAM below the RAM region, app.ld) */
#endif
static int16_t rev_ap[556 + 441] __attribute__((section(".pool")));
static struct {
    uint32_t dly_w, cho_w, cho_ph, rev_ph;
    int32_t dly_lp, dly_acc;
    uint16_t line_i[4], ap_i[2];
    int32_t line_lp[4];
    uint32_t dl_cur, dl_old;            /* (0.9) the delay's TIME now and the one it fades from (DL_XF samples) */
    uint32_t dl_x;                      /* .. samples into that fade (DL_XF: none) */
    int32_t cdep;                       /* (0.9) the chorus's DEPTH now (glides: a jump moved its taps at once) */
    int32_t crate;                      /* (1.1) its RATE now (glides: a jump turned the taps' speed at once, a corner) */
#if CLIPPER_SYNTH
    uint32_t dm_cur, dm_old, dm_x;      /* (1.0) the delay's MODE now, the one it fades from, samples into that fade */
    uint32_t tw_ph, tf_ph;              /* (1.0) TAPE: the wow's and the flutter's phases */
    int32_t tp0, tpd;                   /* .. the swing of the tap (Q8 samples, more delay) at the block's start, a step */
#endif
} fx;
#define DL_XF 1024u                     /* a new delay TIME: the old tap fades out as the new one fades in, 23 ms (a jump
                                         * of the read point was a crack) */

/* DIST: low cut -> drive (1x..8x, exponential) -> asymmetric soft clip
 * (a little bias = even harmonics) -> tone low-pass that closes with drive ->
 * make-up gain. State per part (track_t dist_*). */
/* (Clipper 0.9) DIST turned on or off while the part sounds: the dry and the driven cross-fade over 2.9 ms (SL_SLOPE;
 * it switched at once: a crack on a held chord); a part starting from silence (fresh): at once, as before, bit for bit */
static void track_dist(track_t *t, int32_t *b, uint32_t n, uint32_t fresh)
{
#if CLIPPER_SYNTH
    int32_t d = t->engine == ENGI_MARE ? 0 : t->p[P_DIST];    /* (MARE: DIST drives its ladders, eng_mare.c) */
#else
    int32_t d = t->p[P_DIST];
#endif
    int32_t i, g, k, mk, bias = 2400, b0, tw = d ? 32768 : 0, w = t->dist_w;
    if (!d && (!w || fresh)) {
        t->dist_on = 0;
        t->dist_w = 0;
        return;
    }
    if (d)
        t->dist_last = (int16_t)d;
    else
        d = t->dist_last;                               /* (fading out: the amount it had) */
    if (!t->dist_on) {                                  /* coming on: no stale high-pass state (a thump) */
        t->dist_on = 1;
        t->dist_hp = b[0];
        t->dist_lp1 = t->dist_lp2 = 0;
        w = fresh ? 32768 : 0;
    }
    g = 4096 + d * d * 2;                                /* Q12: 1x .. ~9x, gentle at first */
    k = 32000 - d * 95;                                  /* tone: transparent at low drive .. ~3 kHz, Q15 */
    mk = 30000 - d * 120;                                /* make-up */
    b0 = softclip(bias);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], y;
        t->dist_hp += (x - t->dist_hp + 64) >> 7;           /* ~55 Hz low cut: keep the bass out of the clipper */
        x = clamp(x - t->dist_hp, -230000, 230000);         /* (x >> 2) * g fits 32 bits; the clip is flat out there */
        y = softclip(MULQ12R(x, g, 2) + bias) - b0;         /* (as (x >> 2) * g >> 10 in 32 bits: no overflow for loud
                                                             * poly; SLOOP Pro #4: rounded, no step of 4 x the drive) */
        t->dist_lp1 += mulq15(y - t->dist_lp1, k);         /* two poles: tames the fizz */
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        y = mulq15(t->dist_lp2, mk);
        if (w != tw)
            w += clamp(tw - w, -SL_SLOPE, SL_SLOPE);
        b[i] = w == 32768 ? y : b[i] + (int32_t)(((int64_t)(y - b[i]) * w) >> 15);
    }
    t->dist_w = w;
    if (!w)
        t->dist_on = 0;                                 /* (faded out: off) */
}

/* master: peak limiter in front of the soft clipper, a 35 ms hold then ~46 ms release (SLOOP 2.6), threshold where tanh is still nearly linear,
 * so chords get quieter instead of crushed. SLOOP 2.6: it looks one block ahead (lim_block): the output is the mix
 * CTL samples late (0.73 ms) and the gain glides over each block to what the loudest sample of it and of the next one
 * needs, so a hot kick or bass is already turned down when it comes. Before, the gain caught up in ~4 samples and the
 * first ones went through the tanh knee at up to 4x: a crackle on the beats with the MASTER up (in headphones most) */
#define LIM_T 18000
#define OUT_FRAC 7                      /* bits under Q15 the DAC takes (audio.c: out << OUT_FRAC + out_lo) */
typedef struct {
    int32_t dl[CTL], dr[CTL];           /* the mix of the last block (the delay) */
    uint32_t w;                         /* its oldest sample (the next out) */
    int32_t env;                        /* the peak the gain fits under LIM_T (LIM_T: none) */
    int32_t g;                          /* the gain at the end of the last block, Q15 (32768: 1) */
    uint32_t hold;                      /* blocks before the release starts (LIM_HOLD after a peak near env) */
} lim_t;
/* (2.6, a whistle on loud basses) the release waits LIM_HOLD blocks after the last peak near the envelope: a held bass
 * (down to ~29 Hz, a period under 35 ms) keeps one gain, instead of the gain dipping and creeping back at every cycle
 * (a sawtooth at the bass's pitch with a corner at every block: tones around 1.4 kHz and up) */
#define LIM_HOLD 48u
#define LIM_REL 11                      /* the release after the hold: ~46 ms (12 before the hold: ~93 ms; the same loudness) */
static lim_t lim_dac = {.env = LIM_T, .g = 32768}, lim_usb = {.env = LIM_T, .g = 32768};
static int8_t lim_lo[2][CTL];           /* the delay of the bits under Q15 (lim_block's fl, fr: lim_dac's alone) */
/* (Clipper 0.9) the gain's way over a block: an S curve (smoothstep at (i + 1) / 32), not a straight line: no corner in
 * the gain at the blocks' joints (a straight ramp put one at each, a tick on every loud attack). Each block still ends
 * at the gain its look-ahead asks for; the samples going out were in the last block's look-ahead (g0 fits them) */
static const uint16_t LIM_S[CTL] = {94, 368, 810, 1408, 2150, 3024, 4018, 5120, 6318, 7600, 8954, 10368, 11830, 13328,
                                    14850, 16384, 17918, 19440, 20938, 22400, 23814, 25168, 26450, 27648, 28750, 29744,
                                    30618, 31360, 31958, 32400, 32674, 32768};
/* l, r: n (<= CTL) samples in; out: the n samples of CTL before, limited (before the knee). fl, fr (lim_dac, or 0):
 * the samples' OUT_FRAC bits under Q15 (x + f / 2^OUT_FRAC, -64 .. 64), delayed with them (lim_lo); 0 where the gain
 * is under 1 */
static void lim_block(lim_t *s, int32_t *l, int32_t *r, int8_t *fl, int8_t *fr, uint32_t n)
{
    uint32_t i, w = s->w;
    int32_t pk = 0, a, g0 = s->g, g1, dg, e = s->env;
    for (i = 0; i < CTL; i++) {                         /* the samples going out now and the ones after them (the
                                                         * rest of the delay, then this block): CTL ahead */
        a = s->dl[i] < 0 ? -s->dl[i] : s->dl[i], pk = a > pk ? a : pk;
        a = s->dr[i] < 0 ? -s->dr[i] : s->dr[i], pk = a > pk ? a : pk;
    }
    for (i = 0; i < n; i++) {
        a = l[i] < 0 ? -l[i] : l[i], pk = a > pk ? a : pk;
        a = r[i] < 0 ? -r[i] : r[i], pk = a > pk ? a : pk;
    }
    if ((int64_t)pk * 17 >= (int64_t)e * 16)           /* a peak within ~0.5 dB of the envelope: the hold again */
        s->hold = LIM_HOLD;
    if (s->hold)
        s->hold--;
    else if (e > LIM_T)                                 /* the release (after the hold), a block at a time */
        e -= (int32_t)((((int64_t)(e - LIM_T) * (int32_t)n) >> LIM_REL) + 1);
    if (e < LIM_T)
        e = LIM_T;
    if (pk > e)
        e = pk, s->hold = LIM_HOLD;
    s->env = e;
    g1 = e > LIM_T ? (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)e) : 32768;
    s->g = g1;
    dg = (g1 - g0) / (int32_t)(n ? n : 1u);             /* (towards 0: the gain never above both ends' bound) */
    for (i = 0; i < n; i++) {
        int32_t xl = s->dl[w], xr = s->dr[w], g = n == CTL ? g0 + (int32_t)(((int64_t)(g1 - g0) * LIM_S[i]) >> 15)
                                                           : g0 + dg * (int32_t)(i + 1u);
        if (fl) {
            int8_t el = lim_lo[0][w], er = lim_lo[1][w];
            lim_lo[0][w] = fl[i], lim_lo[1][w] = fr[i];
            fl[i] = el, fr[i] = er;
        }
        s->dl[w] = l[i], s->dr[w] = r[i];
        w = (w + 1u) & (CTL - 1u);
        if (i + 1u == n)
            g = g1;
        if (g < 32768) {                                /* (64-bit: |x| may be far above Q15. 2.6 fix: x >> 4 first cut
                                                         * the limited signal to 12 bits, a whistle on loud basses) */
            xl = (int32_t)(((int64_t)xl * g) >> 15);
            xr = (int32_t)(((int64_t)xr * g) >> 15);
            if (fl)                                     /* (limited: Q15 alone) */
                fl[i] = fr[i] = 0;
        }
        l[i] = xl, r[i] = xr;
    }
    s->w = w;
}
static volatile uint8_t fx_lowcut;     /* settings: 12 dB/oct ~110 Hz for the small speaker */
static int32_t lc_l1, lc_l2, lc_r1, lc_r2, dc_l, dc_r, dce_l, dce_r;

/* DC blocker (~2 Hz), always on: a leaky integrator of the input (Q6 state) subtracted from it.
 * The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next one, so no
 * part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (It was rounded, and a rounded step of (x - dc) / 4096 stops moving at |x - dc| < 2048: a
 * constant offset of up to +-31 stayed at the output after silence.) */
static inline int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = (x << 6) - *dc + *err, d = e >> 12;
    *err = e - (d << 12);
    *dc += d;
    return x - ((*dc + 32) >> 6);
}

static int32_t lce[4];
static inline int32_t lowcut1(int32_t x, int32_t *lc, int32_t *err)   /* x minus its one-pole low-pass */
{
    int32_t e = x - *lc + *err, d = e >> 6;
    *err = e - (d << 6);
    *lc += d;
    return x - *lc;
}

/* the output stage: linear up to KNEE (a clean low end: no tanh harmonics on a loud sine), above it
 * a tanh knee with the same slope at the joint, to full scale */
#define KNEE 16384
static inline int32_t knee(int32_t x)
{
    int32_t a = x < 0 ? -x : x;
    if (a <= KNEE)
        return x;
    a = KNEE + (softclip((a - KNEE) * 2) >> 1);
    return x < 0 ? -a : a;
}

/* the USB audio input at full level (menu USB AUDIO = FULL): the same output stage with its own state,
 * fed as if MASTER were all the way up, so the recording level does not follow the knob. The DAC path
 * (master_pre, lim_dac) is untouched. audio.c sets usb_full_now for a render while the computer records. */
static uint8_t usb_full;                 /* menu USB AUDIO: 0 MASTER (follows the knob), 1 FULL (panel.c lights_word) */
static uint8_t usb_full_now;             /* this render fills usb_out (audio.c) */
static int32_t usb_out[2u * CTL];
/* the visualiser's input (audio.c fills the scope with it): the mix as if MASTER were all the way up, so the
 * picture does not follow the volume knob, even at 0 (2.4). The same soft knee as the output, no limiter state */
static int16_t vis_tap[2u * CTL];
/* the DAC's bits under each sample of mix_block's out (Q15), left then right: out 2^OUT_FRAC + out_lo is the output
 * with OUT_FRAC more bits, through the MASTER and the knee (out_lo added at the knee's input slope; the DC blocker and
 * the low cut run on the Q15 part; 0 while the limiter's gain is under 1). -64 .. 64 */
static int8_t out_lo[2][CTL];
static struct { int32_t dc[2], dce[2], lc[4], lce[4]; } uo;
static void master_pre_usb(int32_t *l, int32_t *r)      /* (USB AUDIO = FULL: its own state) */
{
    *l = dc_block(*l, &uo.dc[0], &uo.dce[0]);
    *r = dc_block(*r, &uo.dc[1], &uo.dce[1]);
    if (fx_lowcut) {
        *l = lowcut1(*l, &uo.lc[0], &uo.lce[0]);
        *l = lowcut1(*l, &uo.lc[1], &uo.lce[1]);
        *r = lowcut1(*r, &uo.lc[2], &uo.lce[2]);
        *r = lowcut1(*r, &uo.lc[3], &uo.lce[3]);
    }
}

/* the output stage before the limiter: the DC blocker, the low cut */
static inline void master_pre(int32_t *l, int32_t *r)
{
    *l = dc_block(*l, &dc_l, &dce_l);
    *r = dc_block(*r, &dc_r, &dce_r);
    if (fx_lowcut) {                  /* two one-pole high-passes, error feedback as dc_block (the */
        *l = lowcut1(*l, &lc_l1, &lce[0]);          /* rounded step stopped at |x - lc| < 32: an offset) */
        *l = lowcut1(*l, &lc_l2, &lce[1]);
        *r = lowcut1(*r, &lc_r1, &lce[2]);
        *r = lowcut1(*r, &lc_r2, &lce[3]);
    }
}



static uint32_t delay_samples(void)
{
    uint32_t s = dly_samples((uint32_t)song.g[G_DTIME]);
    return s < 16u ? 16u : s >= DLY_SPAN - 4u ? DLY_SPAN - 4u : s;
}

#if CLIPPER_SYNTH
/* Clipper 0.2: a plate reverb (G_RTYPE = PLATE), after J. Dattorro, "Effect Design, Part 1" (JAES 45/9, 1997): a
 * pre-delay (G_RPRE), a bandwidth low-pass and four input diffusers, then a tank of two halves feeding each other (a
 * figure eight); in each half a modulated diffuser (moving reflections: long tails do not ring), a delay, the damping
 * low-pass (DAMP), the decay (SIZE), a second diffuser and a delay. The output is taken from seven taps across the
 * tank a side, as in the paper: dense from the start, wide, smooth.
 * It runs at half the rate (22.05 kHz, as the Lexicon 224 ran at 20 kHz): the paper's lengths (29.76 kHz) x 0.741,
 * 33 KB of lines (the punch-in ring's memory, gone in the synth); two input samples averaged into one tank step, the
 * output between the last two steps. */
enum { PL_AP1, PL_AP2, PL_AP3, PL_AP4, PL_MA, PL_DA1, PL_AA, PL_DA2, PL_MB, PL_DB1, PL_AB, PL_DB2, PL_PRE, PL_N };
/* the modulated diffusers: +-PL_EXC samples on a PL_LFO_INC sine. The paper's +-12 at ~1 Hz (here) shifted the pitch of the
 * recirculating tail enough that its copies beat: the wet pulsed 7 to 29 % at 2..4 Hz on a held note, worst on HALL's
 * long tail (players: "a helicopter"; ROOM, without this tank, under 1 %). +-5 at 1/3 Hz moves a seventh as fast: the
 * tail still drifts (no metallic ring), the pulse is under 5 % (tests/plate_test.c) */
#ifndef PL_EXC
#define PL_EXC 5
#endif
#ifndef PL_LFO_INC
#define PL_LFO_INC 64928u          /* ~0.33 Hz at 22.05 kHz */
#endif
static const uint16_t PL_LEN[PL_N] = {105, 79, 281, 205, 498 + PL_EXC + 2, 3299, 1334, 2756, 673 + PL_EXC + 2, 3124,
                                      1968, 2343, 2206};
#define PL_TOTAL (105 + 79 + 281 + 205 + 498 + PL_EXC + 2 + 3299 + 1334 + 2756 + 673 + PL_EXC + 2 + 3124 + 1968 + 2343 + 2206)
static int16_t pl_buf[PL_TOTAL] __attribute__((section(".pool")));
static struct {
    uint16_t base[PL_N], w[PL_N];
    int32_t bw, dmp[2], end[2], y0l, y0r, y1l, y1r, acc;
    uint32_t lph;
    uint8_t half, ready;
} pl;

static void plate_clear(void)
{
    uint32_t k, b = 0;
    for (k = 0; k < PL_TOTAL; k++)
        pl_buf[k] = 0;
    for (k = 0; k < PL_N; k++) {
        pl.base[k] = (uint16_t)b;
        pl.w[k] = 0;
        b += PL_LEN[k];
    }
    pl.bw = pl.dmp[0] = pl.dmp[1] = pl.end[0] = pl.end[1] = 0;
    pl.y0l = pl.y0r = pl.y1l = pl.y1r = pl.acc = 0;
    pl.half = 0;
    pl.ready = 1;
}
/* line k: d samples back (1 = the newest, PL_LEN[k] the oldest) */
static inline int32_t pl_tap(uint32_t k, uint32_t d)
{
    uint32_t i = pl.w[k] + PL_LEN[k] - d;
    return pl_buf[pl.base[k] + (i >= PL_LEN[k] ? i - PL_LEN[k] : i)];
}
static inline void pl_put(uint32_t k, int32_t x)
{
    pl_buf[pl.base[k] + pl.w[k]] = (int16_t)clamp(x, -32768, 32767);
    if (++pl.w[k] >= PL_LEN[k])
        pl.w[k] = 0;
}
/* the output: seven taps a side, as in the paper (its lengths x 0.741): each side four taps from the other half of the
 * tank, then three (negative) from its own, so the sides stay decorrelated. Every tap inside its line
 * (tests/plate_taps_test.c): the two groups were swapped, which put the left side's tap of 1417 on line AA (1334
 * long, the paper's tap is on AB, 1968): past its end pl_tap read a short delay, then for 83 samples the line before
 * it, so the left output jumped between the two each time AA wrapped, 16.5 times a second: a chop on PLATE, HALL and
 * SHIMMER, louder with the send */
static const struct { uint8_t k; uint16_t d; int8_t s; } PL_TAP_L[7] = {
    {PL_DB1, 197, 1}, {PL_DB1, 2203, 1}, {PL_AB, 1417, -1}, {PL_DB2, 1479, 1},
    {PL_DA1, 1474, -1}, {PL_AA, 139, -1}, {PL_DA2, 790, -1}};
static const struct { uint8_t k; uint16_t d; int8_t s; } PL_TAP_R[7] = {
    {PL_DA1, 262, 1}, {PL_DA1, 2687, 1}, {PL_AA, 910, -1}, {PL_DA2, 1980, 1},
    {PL_DB1, 1564, -1}, {PL_AB, 248, -1}, {PL_DB2, 90, -1}};
static inline int32_t pl_ap(uint32_t k, int32_t x, int32_t c)  /* a diffuser: an allpass over line k, c Q15 */
{
    int32_t d = pl_tap(k, PL_LEN[k]), v = x - mulq15(d, c);
    pl_put(k, v);
    return d + mulq15(v, c);
}
static inline int32_t pl_mod_ap(uint32_t k, int32_t x, int32_t c, int32_t pos)   /* pos: the delay, Q8 samples */
{
    uint32_t di = (uint32_t)pos >> 8;
    int32_t a = pl_tap(k, di), b = pl_tap(k, di + 1u), d = a + (((b - a) * (pos & 255)) >> 8), v = x - mulq15(d, c);
    pl_put(k, v);
    return d + mulq15(v, c);
}
/* (1.0) SHIMMER: the tank's output an octave up, back into its input (the Eno / Lanois shimmer): a delay line read
 * twice as fast as it is written by two taps half a window apart, each faded in and out, at 22.05 kHz. The fades are
 * equal-power (a half sine: the two taps sin and cos): a reverb tail is diffuse, its two taps half a window apart
 * unrelated, so their powers add; triangles (whose gains add to 1) dipped the power 3 dB at each crossing, ~49 times
 * a second: a helicopter flutter on every note (tests/shimmer_test.c). Scaled by sqrt(2/3): the triangles' mean
 * power, so the octave line's loop gain (SH_GAIN) is where it was */
#define PS_LEN 1024u                                   /* 46 ms */
#define PS_WIN 896u                                    /* the window the taps sweep, samples */
static int16_t ps_buf[PS_LEN] __attribute__((section(".pool")));
static struct { uint32_t w, ph; int32_t lp; } ps;
/* SHIM on ROOM: the octave line at the tank's rate too, fed the lines' last outputs (src), the mean of two a step; its
 * output between the last two steps, into the room's input */
static struct { int32_t src, acc, y0, y1; uint8_t half; } ps_rm;
static int32_t shimmer_step(int32_t in)
{
    uint32_t k;
    int32_t y = 0;
    ps_buf[ps.w & (PS_LEN - 1u)] = (int16_t)clamp(in, -32768, 32767);
    for (k = 0; k < 2u; k++) {                         /* delay = WIN - phase: it shrinks one a step (twice the speed) */
        uint32_t ph = (ps.ph + k * (PS_WIN / 2u)) % PS_WIN, d = PS_WIN - ph + 2u;
        int32_t g = (sine_i(ph * (0x80000000u / PS_WIN)) * 26755) >> 15;   /* sin(pi ph / WIN) x sqrt(2/3) */
        y += (ps_buf[(ps.w - d) & (PS_LEN - 1u)] * g) >> 15;   /* (the two taps' squares sum to 2/3) */
    }
    ps.w++;
    ps.ph = (ps.ph + 1u) % PS_WIN;
    ps.lp += mulq15(y - ps.lp, 16000);                 /* (a little softer: the octave of a bright tail is harsh) */
    return ps.lp;
}
/* one tank step (22.05 kHz) on x: the left and right outputs. (1.0) bw: the input's bandwidth (HALL darker), shim:
 * the octave-up share of the tail fed back in (SHIMMER, SHIM), Q15 */
static void plate_step(int32_t x, int32_t decay, int32_t damp, uint32_t pre, int32_t bw, int32_t shim, int32_t *yl,
                       int32_t *yr)
{
    int32_t a, b, ma, mb;
    pl_put(PL_PRE, x);
    x = pre ? pl_tap(PL_PRE, pre) : x;
    if (shim)
        x += mulq15(shimmer_step((pl.end[0] + pl.end[1]) >> 1), shim);
    pl.bw += mulq15(x - pl.bw, bw);                             /* bandwidth (~6 kHz at 22.05; HALL ~3.5 kHz) */
    x = pl_ap(PL_AP1, pl.bw, 24576);                            /* 0.75 */
    x = pl_ap(PL_AP2, x, 24576);
    x = pl_ap(PL_AP3, x, 20480);                                /* 0.625 */
    x = pl_ap(PL_AP4, x, 20480);
    pl.lph += PL_LFO_INC;
    ma = ((498 << 8) + ((osc_sine(pl.lph) * PL_EXC) >> 7));
    mb = ((673 << 8) + ((osc_sine(pl.lph + 0x40000000u) * PL_EXC) >> 7));
    a = pl_mod_ap(PL_MA, x + mulq15(pl.end[1], decay), -22938, ma);   /* -0.7: the decay diffusion */
    b = pl_mod_ap(PL_MB, x + mulq15(pl.end[0], decay), -22938, mb);
    pl_put(PL_DA1, a);
    pl_put(PL_DB1, b);
    a = pl_tap(PL_DA1, PL_LEN[PL_DA1]);
    b = pl_tap(PL_DB1, PL_LEN[PL_DB1]);
    pl.dmp[0] += mulq15(a - pl.dmp[0], damp);
    pl.dmp[1] += mulq15(b - pl.dmp[1], damp);
    a = pl_ap(PL_AA, mulq15(pl.dmp[0], decay), 16384);          /* 0.5 */
    b = pl_ap(PL_AB, mulq15(pl.dmp[1], decay), 16384);
    pl_put(PL_DA2, a);
    pl_put(PL_DB2, b);
    pl.end[0] = pl_tap(PL_DA2, PL_LEN[PL_DA2]);
    pl.end[1] = pl_tap(PL_DB2, PL_LEN[PL_DB2]);
    {
        int32_t l = 0, r = 0;
        uint32_t k;
        for (k = 0; k < 7u; k++) {
            int32_t tl = pl_tap(PL_TAP_L[k].k, PL_TAP_L[k].d), tr = pl_tap(PL_TAP_R[k].k, PL_TAP_R[k].d);
            l += PL_TAP_L[k].s > 0 ? tl : -tl;
            r += PL_TAP_R[k].s > 0 ? tr : -tr;
        }
        *yl = l, *yr = r;
    }
}
#ifndef SH_DECAY
#define SH_DECAY 27000
#define SH_DAMP 28000
#define SH_GAIN 8192
#endif
/* SHIM (G_RSHIM, 0..127) on any TYPE: the octave line's share, SH_GAIN at 127, and the loop's limits with it, from none
 * at 0 to SH_DECAY / SH_DAMP at 127 (the more octave fed back, the less tail it may ride on). SHIMMER is HALL at 127,
 * bit for bit, whatever SHIM says (its knob already all the way up) */
#ifndef RS_GAIN
#define RS_GAIN 12000                                           /* ROOM's: the octave line's share at 127, */
#define RS_DECAY 27000                                          /* its loop gain, damping at 127 */
#define RS_DAMP 28000
#define RS_SRC 2                                                /* (the four lines' outputs' sum >> RS_SRC in) */
#endif
static inline void ps_skip(uint32_t k)                          /* the octave line's windows k steps on, nothing in */
{
    ps.w += k;
    ps.ph = (ps.ph + k) % PS_WIN;
}
static inline int32_t shim_amt(void)
{
    return song.g[G_RTYPE] == RT_SHIMMER ? 127 : song.g[G_RSHIM];
}
static inline int32_t shim_lim(int32_t x, int32_t lim, int32_t a)
{
    lim = 32767 - (32767 - lim) * a / 127;
    return x > lim ? lim : x;
}
/* the plate over a block: rev_in (the reverb sends) -> + wet_l, wet_r */
#define PL_QUIET (FS / 2u / CTL)                                 /* blocks: half a second of silence in and out */
#define PL_QUIET_LVL 2                                          /* silence out: the tail's last bits (-84 dBFS) */
#define PL_LONG (FS * 20u / CTL)                                /* 20 s without a send (HALL's longest tail: -60 dB in */
#define PL_LONG_LVL 16                                          /* 10 s): rounding's limit cycles too (-66 dBFS) */
static uint32_t pl_quiet, pl_silent;
static void plate_run(const int32_t *rev_in, int32_t *wet_l, int32_t *wet_r, uint32_t n, int32_t rg0, int32_t rg1)
{
    int32_t rt = song.g[G_RTYPE], hall = rt >= RT_HALL, sa = shim_amt();
    int32_t decay = hall ? 16384 + song.g[G_RSIZE] * 104          /* (1.0) HALL: 0.5 .. 0.9, RT60 ~2 .. 10 s */
                         : 6554 + song.g[G_RSIZE] * 155;        /* 0.2 .. 0.8: RT60 ~0.8 .. 5 s (twice a half) */
    int32_t damp = 32767 - song.g[G_RDAMP] * 193;               /* the damping low-pass: open .. 0.25 */
    if (sa) {                                                   /* (the octave line feeds the tank: kept under a loop gain
                                                                 * of 1, the top always damped a little) */
        decay = shim_lim(decay, SH_DECAY, sa);
        damp = shim_lim(damp, SH_DAMP, sa);
    }
    uint32_t pre = (uint32_t)clamp(song.g[G_RPRE] + (hall ? 18 : 0), 0, 100) * 2205u / 100u;   /* ms -> 22.05 kHz
                                                                 * samples (HALL: 18 ms more, a big room's) */
    int32_t bw = hall ? 18000 : 26000, shim = SH_GAIN * sa / 127;   /* (SHIMMER, SHIM: some of the tail an octave up) */
    if (shim && pl_silent >= PL_LONG) {                         /* (SHIMMER, SHIM: 20 s without a send, the tail long gone:
                                                                 * the octave line off, else its rounding's limit cycles (up
                                                                 * to 24 LSB on HALL) keep the tank from resting; its windows
                                                                 * run on, as they did: the same tail after) */
        shim = 0;
        ps_skip(n >> 1);
    }
    uint32_t i;
    int32_t any = 0, pk = 0;
    static uint32_t pre_cur;                                    /* (1.0) the pre-delay glides (a jump: a step in) */
    if (!pl.ready)
        plate_clear(), pre_cur = pre;
    for (i = 0; i < n; i++)                                     /* nothing sent and the tail gone: the tank rests (a
                                                                 * sound with REV 0 on PLATE / HALL paid for it in full) */
        any |= rev_in[i];
    if (!any && pl_quiet >= PL_QUIET) {
        pre_cur = pre;                                          /* (its lines silent: no glide to wait for) */
        pl.lph += (n >> 1) * PL_LFO_INC;                        /* (the diffusers' sine runs on: the same tail after) */
        pl.half ^= n & 1u;
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t x = mulq15(rev_in[i], 13000), wl, wr;           /* (as the room's input) */
        int32_t g = rg0 + (((rg1 - rg0) * (int32_t)i) >> CTL_LOG2);
        if (!pl.half) {
            pl.acc = x;
            wl = (pl.y0l + pl.y1l) >> 1;                        /* between the last two steps */
            wr = (pl.y0r + pl.y1r) >> 1;
        } else {
            int32_t yl, yr;
            pre_cur += pre_cur < pre ? 1u : pre_cur > pre ? (uint32_t)-1 : 0u;
            plate_step((pl.acc + x) >> 1, decay, damp, pre_cur, bw, shim, &yl, &yr);
            pl.y0l = pl.y1l, pl.y0r = pl.y1r;
            pl.y1l = (yl * 19661) >> 14, pl.y1r = (yr * 19661) >> 14;   /* x 1.2: the paper's 0.6, as loud as ROOM */
            wl = pl.y0l, wr = pl.y0r;
        }
        pk |= (wl < 0 ? -wl : wl) | (wr < 0 ? -wr : wr);
        if (g != 32767)
            wl = mulq15(wl, g), wr = mulq15(wr, g);
        wet_l[i] += wl;
        wet_r[i] += wr;
        pl.half ^= 1u;
    }
    pl_silent = any ? 0u : pl_silent + 1u;
    pl_quiet = any || pk > (pl_silent >= PL_LONG ? PL_LONG_LVL : PL_QUIET_LVL) ? 0u : pl_quiet + 1u;
}
#endif

#if CLIPPER_SYNTH
/* (1.0) the half-rate line read q (Q8) samples back: entry e ~ sample 2e, linear between entries */
static inline int32_t dly_rd(uint32_t q)
{
    uint32_t s = (fx.dly_w << 8) - q, e = s >> 9, fr = (s >> 1) & 255u;
    int32_t a = dly_buf[e & (DLY_LEN - 1u)], b = dly_buf[(e + 1u) & (DLY_LEN - 1u)];
    return a + (((b - a) * (int32_t)fr) >> 8);
}
/* (1.0) the taps of a MODE at a TIME d, sample i of the block: left, right, and the one fed back */
static inline void dly_taps(uint32_t m, uint32_t d, uint32_t i, int32_t *l, int32_t *r, int32_t *f)
{
    if (m == DM_PING) {
        uint32_t d2 = 2u * d;
        if (d2 > DLY_SPAN - 4u)
            d2 = DLY_SPAN - 4u;
        *l = dly_rd(d << 8);
        *r = *f = dly_rd(d2 << 8);
    } else if (m == DM_TAPE) {
        uint32_t q = (d << 8) + (uint32_t)(fx.tp0 + fx.tpd * (int32_t)i);
        if (q > ((DLY_SPAN - 4u) << 8))
            q = (DLY_SPAN - 4u) << 8;
        *l = *r = *f = dly_rd(q);
    } else {
        uint32_t p = fx.dly_w - d, e = p >> 1;
        int32_t a = dly_buf[e & (DLY_LEN - 1u)];
        *l = *r = *f = (p & 1u) ? (a + dly_buf[(e + 1u) & (DLY_LEN - 1u)]) >> 1 : a;
    }
}
#define TW_INC 1558299u                 /* (a block's turn: 0.5 Hz, the wow) */
#define TF_INC 20257887u                /* (6.5 Hz, the flutter) */
#endif

static inline int32_t lp_step(int32_t d, int32_t k)   /* (1.0) mulq15(d, k), d past 16 bits in 64 */
{
    return d > 65535 || d < -65535 ? (int32_t)(((int64_t)d * k) >> 15) : mulq15(d, k);
}
/* process the three buses for one block; sends in, wet out (stereo). The LFOs (chorus, reverb line)
 * are computed per block and ramped: no sine per sample */
static void fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in, int32_t *wet_l,
                     int32_t *wet_r, uint32_t n)
{
    uint32_t i, k, dl = delay_samples(), dlo;
    int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240;
    int32_t dmix = song.g[G_DMIX] * 258;
    int32_t g = 17000 + song.g[G_RSIZE] * 104, lpk = 32767 - song.g[G_RDAMP] * 200;   /* loop gain (RT60 ~0.4..4 s), damping */
    int32_t cdepth = song.g[G_CDEPTH] * 6, cdep0;
    int32_t ca0, ca1, cb0, cb1, ma, mb, dca, dcb;
    int plate = CLIPPER_SYNTH && song.g[G_RTYPE] >= RT_PLATE;   /* Clipper: the plate (1.0: HALL, SHIMMER: its tank)
                                                                 * instead of the room */
    const uint32_t L0 = REV_LINE[0] + REV_MOD + 2u, B1 = L0, B2 = B1 + REV_LINE[1], B3 = B2 + REV_LINE[2];
    int32_t rg0 = 32767, rg1 = 32767;                   /* (1.0) the reverb's share over the block (a change of type) */
#if CLIPPER_SYNTH
    int32_t rsh = 0;                                    /* SHIM on ROOM: the octave line's share, Q15 */
    {   /* a change between the room and the plate's tank (PLATE, HALL, SHIMMER share it: they change on the fly): the
         * old one fades out over 6 blocks (1.0: it was cut, a click on a ringing tail), then the other starts from
         * silence (no old tail bursting out of its lines) */
        static int fam_used = -1, was_shim;
        static uint8_t rv_fade;
        int shim = shim_amt() != 0, flip = 0;
        if (fam_used < 0)
            fam_used = plate;
        if (plate != fam_used) {
            if (!rv_fade)
                rv_fade = 6;
            rg0 = rv_fade * 5461, rg1 = (rv_fade - 1) * 5461;   /* (32767 / 6) */
            plate = fam_used;                           /* (this block: the old one) */
            if (!--rv_fade) {
                if (fam_used)
                    for (k = 0; k < NELEM(rev_line); k++)
                        rev_line[k] = 0;
                else
                    pl.ready = 0;
                fam_used = !fam_used;
                flip = 1;
            }
        }
        if ((shim && !was_shim) || flip) {              /* (the octave line from silence; SHIM: the room and the tank
                                                         * share it, the new one starts without the old one's) */
            memset(ps_buf, 0, sizeof ps_buf);
            ps.lp = 0;
            memset(&ps_rm, 0, sizeof ps_rm);
        }
        was_shim = shim;
        if (!plate && song.g[G_RTYPE] == RT_ROOM && song.g[G_RSHIM]) {   /* SHIM on ROOM (not while it fades out) */
            rsh = RS_GAIN * song.g[G_RSHIM] / 127;
            g = shim_lim(g, RS_DECAY, song.g[G_RSHIM]);
            lpk = shim_lim(lpk, RS_DAMP, song.g[G_RSHIM]);
        }
    }
#endif
    if (!fx.dl_cur)
        fx.dl_cur = dl, fx.dl_x = DL_XF, fx.cdep = cdepth, fx.crate = song.g[G_CRATE] & 127;
    if (dl != fx.dl_cur && fx.dl_x >= DL_XF)            /* (0.9) a new TIME: the fade starts (one at a time) */
        fx.dl_old = fx.dl_cur, fx.dl_cur = dl, fx.dl_x = 0;
    dl = fx.dl_cur, dlo = fx.dl_old;
#if CLIPPER_SYNTH
    {   /* (1.0) a new MODE: the fade starts (one at a time); TAPE's swing at both ends of the block */
        uint32_t dm = song.g[G_DMODE] <= DM_TAPE ? song.g[G_DMODE] : DM_MONO;
        int32_t dw = (int32_t)((dl * 197u) >> 8) + 1536, t1;   /* (wow: 0.3 % of TIME + 6 samples; flutter: 2) */
        if (fx.dm_x == 0 && fx.dm_old == fx.dm_cur)
            fx.dm_x = DL_XF;                            /* (from the start: nothing to fade) */
        if (dm != fx.dm_cur && fx.dm_x >= DL_XF)
            fx.dm_old = fx.dm_cur, fx.dm_cur = dm, fx.dm_x = 0;
        fx.tp0 = dw + 512 + ((dw * osc_sine(fx.tw_ph)) >> 15) + ((512 * osc_sine(fx.tf_ph)) >> 15);
        fx.tw_ph += TW_INC, fx.tf_ph += TF_INC;
        t1 = dw + 512 + ((dw * osc_sine(fx.tw_ph)) >> 15) + ((512 * osc_sine(fx.tf_ph)) >> 15);
        fx.tpd = (t1 - fx.tp0) >> CTL_LOG2;
    }
#endif
    cdep0 = fx.cdep;                                    /* (0.9) DEPTH glides: ~60 ms over its range */
    fx.cdep += clamp(cdepth - fx.cdep, -10, 10);
    /* (1.1) RATE glides a step a block (~90 ms over its range): the taps move at the LFO's slope, and a jump of RATE
     * turned their speed at once, a corner in the wet sound as large as DEPTH x the change x the slope there (5 -> 120 at
     * DEPTH 127: up to 3.8 x the corners of the steady sound at 120). A step: 5.4 % of the rate, under them */
    fx.crate += clamp((song.g[G_CRATE] & 127) - fx.crate, -1, 1);
    {   /* the chorus' two read points (Q8 samples back) and line 0's extra length, at both ends of the block */
        int32_t s0 = osc_sine(fx.cho_ph), s1, m0 = osc_sine(fx.rev_ph), m1;
        fx.cho_ph += LFO_INC[fx.crate];
        fx.rev_ph += LFO_INC[30];
        s1 = osc_sine(fx.cho_ph);
        m1 = osc_sine(fx.rev_ph);
        ca0 = (400 << 8) + ((s0 + 32768) * cdep0 >> 8), ca1 = (400 << 8) + ((s1 + 32768) * fx.cdep >> 8);
        cb0 = (400 << 8) + ((32767 - s0) * cdep0 >> 8), cb1 = (400 << 8) + ((32767 - s1) * fx.cdep >> 8);
        ma = (REV_MOD << 8) + ((m0 * REV_MOD) >> 7), mb = (REV_MOD << 8) + ((m1 * REV_MOD) >> 7);
        dca = (ca1 - ca0) >> CTL_LOG2, dcb = (cb1 - cb0) >> CTL_LOG2;
    }
    for (i = 0; i < n; i++) {
        int32_t yl, yr, a, o0, o1, o2, o3;
#if !CLIPPER_SYNTH
        int32_t x;
#endif
        /* chorus: two modulated short delays, 5..15 ms, the LFO half a turn apart: left and right move apart */
        cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(cho_in[i] >> 1, -32768, 32767);
        {
            int32_t r0 = ca0 + dca * (int32_t)i, r1 = cb0 + dcb * (int32_t)i;
            uint32_t i0 = (uint32_t)r0 >> 8, i1 = (uint32_t)r1 >> 8;
            int32_t c0 = cho_buf[(fx.cho_w - i0) & (CHO_LEN - 1u)], c1 = cho_buf[(fx.cho_w - i0 - 1u) & (CHO_LEN - 1u)];
            int32_t d0 = cho_buf[(fx.cho_w - i1) & (CHO_LEN - 1u)], d1 = cho_buf[(fx.cho_w - i1 - 1u) & (CHO_LEN - 1u)];
            yl = (c0 + (((c1 - c0) * (r0 & 255)) >> 8)) << 1;
            yr = (d0 + (((d1 - d0) * (r1 & 255)) >> 8)) << 1;
        }
        fx.cho_w++;
        /* delay with a low-passed feedback (in the middle) */
#if CLIPPER_SYNTH
        {   /* half rate: an entry is the mean of two samples (entry e ~ sample 2e); a position between two entries
             * reads between them. (1.0) MONO: one tap in the middle; PING: the echoes left (TIME) and right (2 x
             * TIME), the feedback from the right; TAPE: the tap swung by wow and flutter, the feedback saturating.
             * A change of MODE: the old one's taps fade out as the new one's fade in (DL_XF) */
            int32_t xl, xr, xf, v, ol, or_, of;
            uint32_t m = fx.dm_cur;
            dly_taps(m, dl, i, &xl, &xr, &xf);
            if (fx.dl_x < DL_XF) {                      /* (0.9) the old TIME's taps fading out */
                dly_taps(m, dlo, i, &ol, &or_, &of);
                xl = ol + (((xl - ol) * (int32_t)fx.dl_x) >> 10);
                xr = or_ + (((xr - or_) * (int32_t)fx.dl_x) >> 10);
                xf = of + (((xf - of) * (int32_t)fx.dl_x) >> 10);
                fx.dl_x++;
            }
            if (fx.dm_x < DL_XF) {                      /* (1.0) the old MODE's taps fading out */
                dly_taps(fx.dm_old, dl, i, &ol, &or_, &of);
                xl = ol + (((xl - ol) * (int32_t)fx.dm_x) >> 10);
                xr = or_ + (((xr - or_) * (int32_t)fx.dm_x) >> 10);
                xf = of + (((xf - of) * (int32_t)fx.dm_x) >> 10);
                fx.dm_x++;
            }
            fx.dly_lp += mulq15(xf - fx.dly_lp, col);
            v = (dly_in[i] >> 1) + mulq15(fx.dly_lp, fb);
            if (m == DM_TAPE)
                v = softclip(v);                        /* (a tape's: the repeats round off as they pile up) */
            if (fx.dly_w & 1u)
                dly_buf[(fx.dly_w >> 1) & (DLY_LEN - 1u)] = (int16_t)clamp((fx.dly_acc + v) >> 1, -32768, 32767);
            else
                fx.dly_acc = v;
            fx.dly_w++;
            yl += mulq15(xl << 1, dmix);
            yr += mulq15(xr << 1, dmix);
        }
#else
        x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)];
        if (fx.dl_x < DL_XF) {                          /* (0.9) the old TIME's tap fading out */
            int32_t xo = dly_buf[(fx.dly_w - dlo) & (DLY_LEN - 1u)];
            x = xo + (((x - xo) * (int32_t)fx.dl_x) >> 10);
            fx.dl_x++;
        }
        fx.dly_lp += mulq15(x - fx.dly_lp, col);
        dly_buf[fx.dly_w & (DLY_LEN - 1u)] =
            (int16_t)clamp((dly_in[i] >> 1) + mulq15(fx.dly_lp, fb), -32768, 32767);
        fx.dly_w++;
        x = mulq15(x << 1, dmix);
        yl += x;
        yr += x;
#endif
        /* reverb: two diffusers, then the four lines (Clipper: or the plate, after the loop) */
        if (plate) {
            wet_l[i] = yl;
            wet_r[i] = yr;
            continue;
        }
        a = mulq15(rev_in[i], 13000);
#if CLIPPER_SYNTH
        if (rsh) {                                      /* SHIM: the lines' last outputs an octave up, into the input */
            int32_t y;
            if (!ps_rm.half) {
                ps_rm.acc = ps_rm.src;
                y = (ps_rm.y0 + ps_rm.y1) >> 1;
            } else {
                ps_rm.y0 = ps_rm.y1;
                ps_rm.y1 = shimmer_step((ps_rm.acc + ps_rm.src) >> 1);
                y = ps_rm.y0;
            }
            ps_rm.half ^= 1u;
            a += mulq15(y, rsh);
        }
#endif
        {
            int16_t *c = rev_ap;
            for (k = 0; k < 2u; k++) {
                int32_t b = c[fx.ap_i[k]], v = a + (b >> 1);
                c[fx.ap_i[k]] = (int16_t)clamp(v, -32768, 32767);
                a = b - (v >> 1);
                if (++fx.ap_i[k] >= REV_AP[k])
                    fx.ap_i[k] = 0;
                c += REV_AP[k];
            }
        }
        {
            int16_t *c = rev_line;
            int32_t s0, s1, d0, d1, r = ma + (((mb - ma) * (int32_t)i) >> CTL_LOG2);   /* (between the two: never below 0) */
            uint32_t ri = fx.line_i[0] + ((uint32_t)r >> 8), rj;
            if (ri >= L0)                                   /* (the oldest sample is at line_i: reading */
                ri -= L0;                                   /* past it shortens line 0 by 0..2 REV_MOD) */
            rj = ri + 1u >= L0 ? 0u : ri + 1u;
            o0 = c[ri] + (((c[rj] - c[ri]) * (r & 255)) >> 8);
            o1 = c[B1 + fx.line_i[1]];
            o2 = c[B2 + fx.line_i[2]];
            o3 = c[B3 + fx.line_i[3]];
            s0 = o0 + o1, d0 = o0 - o1, s1 = o2 + o3, d1 = o2 - o3;   /* Hadamard / 2: each feeds all four */
            /* (1.0) lp_step: the lines at full scale (SIZE and the sends up, DAMP 0) swing the step past 65536, and
             * x 32767 overflowed 32 bits: the tail burst into a crackle. Within 65536, as before, bit for bit */
            fx.line_lp[0] += lp_step(((s0 + s1) >> 1) - fx.line_lp[0], lpk);
            fx.line_lp[1] += lp_step(((d0 + d1) >> 1) - fx.line_lp[1], lpk);
            fx.line_lp[2] += lp_step(((s0 - s1) >> 1) - fx.line_lp[2], lpk);
            fx.line_lp[3] += lp_step(((d0 - d1) >> 1) - fx.line_lp[3], lpk);
            c[fx.line_i[0]] = (int16_t)clamp(mulq15(fx.line_lp[0], g) + a, -32768, 32767);
            c[B1 + fx.line_i[1]] = (int16_t)clamp(mulq15(fx.line_lp[1], g) - a, -32768, 32767);
            c[B2 + fx.line_i[2]] = (int16_t)clamp(mulq15(fx.line_lp[2], g) + a, -32768, 32767);
            c[B3 + fx.line_i[3]] = (int16_t)clamp(mulq15(fx.line_lp[3], g) - a, -32768, 32767);
            if (++fx.line_i[0] >= L0) fx.line_i[0] = 0;
            if (++fx.line_i[1] >= REV_LINE[1]) fx.line_i[1] = 0;
            if (++fx.line_i[2] >= REV_LINE[2]) fx.line_i[2] = 0;
            if (++fx.line_i[3] >= REV_LINE[3]) fx.line_i[3] = 0;
#if CLIPPER_SYNTH
            if (rsh)
                ps_rm.src = (o0 + o1 + o2 + o3) >> RS_SRC;   /* (SHIM: the next sample's) */
#endif
        }
        if (rg0 != 32767 || rg1 != 32767) {             /* (1.0) fading out (a change of type) */
            int32_t g = rg0 + (((rg1 - rg0) * (int32_t)i) >> CTL_LOG2);
            o0 = mulq15(o0, g), o1 = mulq15(o1, g), o2 = mulq15(o2, g), o3 = mulq15(o3, g);
        }
        wet_l[i] = yl + o0 + o2;
        wet_r[i] = yr + o1 - o3;
    }
#if CLIPPER_SYNTH
    if (plate)
        plate_run(rev_in, wet_l, wet_r, n, rg0, rg1);
#endif
}

/* one block of the whole mix (shared with tests/hostsim.c): events -> each part
 * -> dist -> SLICER -> level / pan / sends -> drums (-> SLICER) -> buses -> master; out: stereo Q15 */
static void events_block(uint32_t n);                    /* seq.c */
static int32_t send_c[CTL], send_d[CTL], send_r[CTL], wet_l[CTL], wet_r[CTL], mix_l[CTL], mix_r[CTL], part_buf[CTL];

/* ---- mute / solo: a track that goes silent fades out over ~6 ms (and back in) */
#define MUTE_STEP 4096                                  /* Q15 per block: 8 blocks */
static int32_t gain_next(track_t *t)                    /* the track's mute gain at the end of this block */
{
    int32_t to = trk_silent(t) ? 32767 : 0, a = t->att;
    t->att = a < to ? (to - a > MUTE_STEP ? a + MUTE_STEP : to) : (a - to > MUTE_STEP ? a - MUTE_STEP : to);
    return 32767 - t->att;
}

/* ---- DUCK: every kick (the drum track's KICK / KICK 2) dips the synth parts, which come back over an
 * eighth note: depth G_DUCK, the curve (1 - t / T)^2. drum_on sets drums.kick. */
static struct {
    uint32_t t;                                         /* units since the kick */
    int32_t g0, g1;                                     /* the parts' gain at the block start, end (Q15) */
} duck = {0xFFFFFFFFu, 32767, 32767};

static void duck_block(uint32_t adv)
{
    int32_t depth = CLIPPER_SYNTH ? 0 : song.g[G_DUCK] * 258, x;   /* (Clipper: its slot is the reverb TYPE) */
    uint32_t len = BEAT_U / 2u;
    duck.g0 = duck.g1;
    if (drums.kick) {
        drums.kick = 0;
        duck.t = 0;
    }
    if (!depth || duck.t >= len) {
        duck.g1 = 32767;
        return;
    }
    x = 32767 - (int32_t)((duck.t << 10) / (len >> 5));      /* 1 - t / T, Q15 (t < len < 2^21) */
    if (x < 0)
        x = 0;
    duck.g1 = 32767 - mulq15(depth, mulq15(x, x));
    duck.t = duck.t + adv < duck.t ? 0xFFFFFFFFu : duck.t + adv;
}

/* ---- the DJ filter: v < 0 a low-pass closing, > 0 a high-pass opening, 0 off. The cutoff glides
 * to the knob (no zipper); at 0 it opens fully, then the filter is bypassed. On the master (G_FILT)
 * and, SLOOP 2.4, on each track (P_TFLT: a synth part's mono signal, the drum track's left, right,
 * reverb send and, 2.5, delay send) */
typedef struct {
    int32_t cut;                                        /* now, 0..127 << 8 (CUTOFF_HZ index) */
    int8_t mode;                                        /* -1 LP, 1 HP, 0 off */
    uint8_t seed;                                       /* (SLOOP 2.6) bit per channel: the LP's state from its next sample */
    int32_t z[4][2];                                    /* the SVF states, per channel */
    int32_t e[4][2];                                    /* what each state's >> 13 left out, added to its next sample */
} djf_t;
static djf_t djf, tflt[NTRK];

/* the knob v -> this block's coefficients in *c; 0 = bypassed (nothing to do) */
static int djf_block(djf_t *f, int32_t v, tsvf_t *c)
{
    int32_t to;
    if ((v < 0 && f->mode > 0) || (v > 0 && f->mode < 0))
        v = 0;                                          /* (Clipper 0.9) from one side to the other (a CC, the editor):
                                                         * open this side first, then the other from open (a jump from
                                                         * the low-pass to the high-pass was a crack) */
    if (v < 0 && f->mode >= 0) {                        /* (switching side: from open) */
        f->mode = -1;
        f->cut = 127 << 8;
        memset(f->z, 0, sizeof f->z), memset(f->e, 0, sizeof f->e);
        f->seed = 15;                                   /* (SLOOP 2.6) the low-pass starts where the signal is: from rest
                                                         * its output began at 0 under a sounding note (a click) */
    } else if (v > 0 && f->mode <= 0) {
        f->mode = 1;
        f->cut = 0;
        memset(f->z, 0, sizeof f->z), memset(f->e, 0, sizeof f->e);
    }
    if (!f->mode)
        return 0;
    to = f->mode < 0 ? (v < 0 ? (127 << 8) + v * 90 * 4 : 127 << 8) : (v > 0 ? v * 90 * 4 : 0);
    f->cut += clamp(to - f->cut, -384, 384);            /* ~1.5 index a block */
    if (!v && f->cut == to) {
        f->mode = 0;                                    /* fully open again: off */
        return 0;
    }
    tsvf_coef(c, f->cut, 40);
    return 1;
}
/* n samples of channel ch through the filter (x within +-140000) */
static void djf_run(djf_t *f, const tsvf_t *c, int32_t *b, uint32_t n, uint32_t ch)
{
    uint32_t i;
    int32_t *z = f->z[ch], *e = f->e[ch];
    if ((f->seed >> ch) & 1u) {
        f->seed &= (uint8_t)~(1u << ch);
        if (f->mode < 0 && n) {
            z[0] = 0;                                   /* at rest on this input: the output is it (DC: exact) */
            z[1] = clamp(b[0], -140000, 140000);
        }
    }
    for (i = 0; i < n; i++) {
        int32_t x = clamp(b[i], -140000, 140000), v3 = x - z[1];
        int32_t s1 = c->a1 * z[0] + c->a2 * v3 + e[0], s2 = c->a2 * z[0] + c->a3 * v3 + e[1];
        int32_t v1 = s1 >> 13, d2 = s2 >> 13, y = z[1] + d2;
        e[0] = s1 - (v1 << 13), e[1] = s2 - (d2 << 13);
        z[0] = clamp(2 * v1 - z[0], -150000, 150000);
        z[1] = clamp(2 * y - z[1], -150000, 150000);
        b[i] = f->mode < 0 ? y : x - y;
    }
}
/* a track's filter on a signal at 4x its level (b >> 2 into the filter's range, back << 2) */
static void tflt_run(djf_t *f, const tsvf_t *c, int32_t *b, uint32_t n, uint32_t ch)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        b[i] >>= 2;
    djf_run(f, c, b, n, ch);
    for (i = 0; i < n; i++)
        b[i] <<= 2;
}

#if CLIPPER_SYNTH
/* INSERT (Clipper 0.6: SLOOP 2.6's, after Felucca 1.5 by Leo Kuroshita, Discussions #78 and #177): one effect per
 * sound (the instrument and its layer each their own, kept with the sound: presets, projects), after DIST and before
 * the sound's FILTER, with a dry / wet MIX. TYPE (P_ITYPE, N_ITYPE; stored, append-only) and its three values A B C
 * (P_IA .. P_IC, what they mean: params.c ins_desc):
 *   SOFT HARD FOLD FUZZ  DRIVE (1x .. ~13x, squared), TONE (two one-pole low-passes at F_CUTOFF), LEVEL (F_DB, as the
 *                        track's LEVEL: 112 is 0 dB). A slow low cut in front (~27 Hz), a DC blocker after (FUZZ is
 *                        lopsided). SOFT tanh, HARD a flat clip at a voice's level, FOLD a triangle folder (the wave
 *                        reflected at +-16384: every fold more harmonics), FUZZ a hard top and a soft, halved bottom;
 *   CRUSH                BITS (1 .. 16, of a voice's full scale), RATE (a held sample, 689 Hz .. 44.1 kHz, INS_HOLD:
 *                        aliasing as the old samplers), LPF (two one-pole low-passes at F_CUTOFF after them);
 *   PHASR                RATE (F_LFOHZ), DEPTH (the sweep around ~850 Hz: 0 still .. the whole 150 Hz .. 4.8 kHz),
 *                        FDBK (0 .. 0.7): four all-pass stages, mono, per track;
 *   FLANG CHOR           RATE, DEPTH, FDBK: a 256-sample line per track (5.8 ms); FLANG swept 0.25 .. 5 ms (a squared
 *                        triangle), feedback 0 .. 0.85; CHOR a sine around 3.2 ms (+-2.3 ms at DEPTH 127), feedback
 *                        0 .. 0.4. A DC blocker on the swept copy (a clipped loud input is lopsided).
 * (1.1, the effects of a modern poly's multi-effect the INSERT did not have, after its PONY types: stored, append-only)
 *   PITCH                PITCH (-24 .. +24 semitones), FINE (-50 .. +50 cents, 5 a step), FDBK (0 .. 0.6: the shifted
 *                        sound shifted again, a rising or falling cascade): TripDeck's pitch shifter (ins_pitch);
 *   PIT2                 PTCH1, PTCH2 (two voices of the same line), BAL (0 the first alone .. 127 the second alone);
 *                        the wet of both is the shifted sound only (MIX blends the dry back in);
 *   PHAS6                RATE, DEPTH, FDBK: CS-1's phaser, six all-pass stages swept by a sine over 100 Hz .. 5 kHz
 *                        (ins_phase6): three notches, deeper and wider than PHASR's two;
 *   DELAY                TIME (2 .. 92 ms, DI_MS), FDBK (0 .. 0.9), TONE (a low-pass on the echoes): a short delay of
 *                        the sound's own (slapback, doubling, comb) beside the long one of the DLY send; its wet is dry
 *                        + echo (MIX is the echo's level); the part runs on while echoes ring (ins_ringing);
 *   LOCMP                COMP (the threshold), LOFI (0 clean .. 6 bits at 3.7 kHz), TONE: a lo-fi compressor, the
 *                        compressor of Airwindows Pop3 then CRUSH's bits and held sample (ins_locomp).
 * The swept ones are the pedals': their wet is (dry + swept) / 2, the notches need both, so MIX 100 % is the whole
 * effect and less a lighter one. MIX (P_IMIX) blends dry and wet linearly; MIX 0 or TYPE OFF: the track bit for bit
 * as before (nothing runs). MIX glides (2.9 ms, SL_SLOPE); a TYPE change fades the old one out, clears the state,
 * fades the new one in; so does a start from MIX 0 (no stale line or filter).
 * State in the pool (1.1: was .bss, 580 B; the line of PITCH and DELAY makes it 4.6 KB a sound); noinline, its own
 * loops */
enum { IT_OFF, IT_SOFT, IT_HARD, IT_FOLD, IT_FUZZ, IT_CRUSH, IT_PHASER, IT_FLANGER, IT_CHORUS,
       IT_PONY1, IT_PONY2, IT_PONY12,           /* (0.8: the PONY chorus, pony_chorus below: the INSERT passes them) */
       IT_PITCH, IT_PITCH2, IT_PHASER6, IT_DELAY, IT_LOCOMP,   /* (1.1) */
       IT_SDLY, IT_PDLY,                        /* the stereo analog delay, its ping-pong (ins_sdly) */
       IT_N };
_Static_assert(IT_SDLY == VC_IT_SDLY && IT_PDLY == VC_IT_SDLY + 1, "voice.c vc_voice charges STDLY / PPDLY by number");
_Static_assert(IT_N == sizeof N_ITYPE / sizeof N_ITYPE[0], "the INSERT's types as params.c names them");
_Static_assert(P_IA == P_ITYPE + 1 && P_IC == P_ITYPE + 3 && P_IMIX == P_ITYPE + 4, "the INSERT's values in order");
#define IL_LEN 256u                     /* FLANG / CHOR: the line, samples (a power of 2: masked) */
enum { SD_PH, SD_WI, SD_TQ, SD_U, SD_A1, SD_A2, SD_OP, SD_OC, SD_B1, SD_B2, SD_DC, SD_Y, SD_ST };   /* a line's state
                                         * (ins_sdly; PH WI TQ: line 1's, the clock and TIME both lines share) */
#define IN_LEN 2048u                    /* (1.1) PITCH, PIT2, DELAY: the line, samples (a power of 2: masked) */
#define PH_ST 4u                        /* PHASR: all-pass stages (two notches, the classic pedal's) */
static const uint8_t INS_HOLD[16] = {64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1};   /* CRUSH RATE */
/* PHASR: the stages' coefficient over the sweep (Q14), 33 points */
static const int16_t PH_A[33] = {
    -16038, -15998, -15955, -15906, -15853, -15793, -15727, -15653, -15572, -15481, -15381, -15270, -15147, -15012,
    -14862, -14696, -14514, -14313, -14092, -13849, -13582, -13290, -12970, -12621, -12240, -11825, -11375, -10886,
    -10356, -9784, -9166, -8500, -7783,
};
typedef struct {
    int32_t w;                          /* the wet share now, Q15 */
    uint8_t type;                       /* the type running */
    uint8_t cnt;                        /* CRUSH: samples left of the held one */
    uint16_t wp;                        /* FLANG / CHOR: the line's next write */
    uint16_t fill;                      /* (0.9) FLANG / CHOR: samples before the wet comes in (the line full first) */
    uint32_t ph;                        /* the sweep's phase */
    int32_t hp, lp1, lp2, dc, hold;     /* low cut (Q8), the two low-passes, DC blocker (Q8), CRUSH's held sample */
    int32_t px[PH_ST], py[PH_ST], fb;   /* PHASR: each stage's last input and output, the fed back */
    /* (1.1) */
    int32_t in;                         /* PITCH, PIT2, DELAY: the line's input, fading in from a start (Q15) */
    int32_t ph1, ph2;                   /* PITCH, PIT2: each voice's heads in the window (16.16); DELAY: ph1 the delay
                                         * now (Q8 samples, gliding to TIME) */
    int32_t acc, ring;                  /* DELAY: the first of the pair the line keeps as one; the loudest written lately */
    int32_t cp, cg;                     /* LOCMP: the compressor's state (Pop3's p) and the gain the last block ended on */
    int32_t x6[6], y6[6], a6;           /* PHAS6: each stage's last input and output; the coefficient now (Q15) */
    int32_t bb[2][SD_ST];               /* STDLY / PPDLY: each line's state (ins_sdly) */
    int32_t sdy[2][CTL];                /* .. PPDLY: each line's output, the last block (the other line's feedback) */
    uint16_t sdh;                       /* .. blocks an echo may still be on its way (ring held) */
    int16_t line[IN_LEN];               /* FLANG / CHOR (the first IL_LEN), PITCH, PIT2, DELAY: at a quarter (headroom);
                                         * STDLY / PPDLY on another engine than MARE: two lines of IN_LEN / 2 */
} ins_t;
/* (1.1: in the pool, 4.6 KB a sound: tools/build.py keeps 8 KiB of it spare, the RAM region has less room) */
static ins_t ins[NPART] __attribute__((section(".pool")));   /* the parts (Clipper: the instrument and its layer) */

static inline int32_t ins_mixq(int32_t m) { return m >= 127 ? 32768 : m <= 0 ? 0 : m * 258; }   /* MIX -> Q15 */
static inline int32_t ins_mix(int32_t x, int32_t y, int32_t w) { return w == 32768 ? y : x + mulq16(y - x, (uint32_t)w << 1); }
static inline uint32_t ins_tri(uint32_t q) { uint32_t t = q >> 16; return t < 32768u ? t << 1 : (65535u - t) << 1; }
/* the one-pole low-pass (topology-preserving) coefficient of cutoff index c, Q16: G = g / (1 + g), g = tan(pi fc / FS) */
static int32_t ins_lpk(int32_t c)
{
    uint32_t g = SVF_G[clamp(c, 0, 127)];
    return (int32_t)((g << 16) / (4096u + g));
}
/* the drives' and CRUSH's shape of v (a voice's full scale 32768; v already through DRIVE) */
static inline int32_t ins_shape(uint32_t ty, int32_t v, uint32_t sh)
{
    switch (ty) {
    case IT_SOFT:
        return softclip(v);
    case IT_HARD:
        return clamp(v, -24000, 24000);
    case IT_FOLD: {
        int32_t u = (int32_t)((uint32_t)(v + 16384) & 0xFFFFu) - 32768;   /* a triangle of period 65536: +-16384 */
        u = 16384 - (u < 0 ? -u : u);
        return u + (u >> 1);                            /* x 1.5 (no divide) */
    }
    case IT_FUZZ:
        return v > 0 ? (v > 20000 ? 20000 : v) : softclip(v) >> 1;
    case IT_CRUSH:                                      /* sh: 16 - BITS; to the nearest step */
        return sh ? ((v + (1 << (sh - 1u))) >> sh) << sh : v;
    default:
        return v;
    }
}
static inline uint32_t ins_bits_sh(int32_t a) { return 15u - (uint32_t)(clamp(a, 0, 127) >> 3); }   /* BITS 1..16 */

/* SOFT .. FUZZ: low cut -> DRIVE -> shape -> TONE -> LEVEL -> DC blocker; CRUSH: held sample -> bits -> LPF. The state
 * in locals for the block (b may alias it as far as the compiler knows: no load / store a sample), written back */
static __attribute__((noinline)) void ins_drive(ins_t *s, const int16_t *p, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, ty = s->type, sh = ins_bits_sh(p[P_IA]);
    uint32_t hold = INS_HOLD[clamp(p[P_IB], 0, 127) >> 3];
    int32_t a = clamp(p[P_IA], 0, 127), g = 4096 + a * a * 3;   /* DRIVE, Q12: 1x .. 12.8x, gentle at first */
    int32_t w = s->w, lp1 = s->lp1, lp2 = s->lp2, x, y, v;
    if (ty == IT_CRUSH) {
        uint32_t k = (uint32_t)ins_lpk(p[P_IC]), cnt = s->cnt;
        int32_t hd = s->hold;
        for (i = 0; i < n; i++) {
            x = b[i];
            w += clamp(tw - w, -SL_SLOPE, SL_SLOPE);
            if (!cnt) {
                hd = ins_shape(IT_CRUSH, clamp(x, -262144, 262143), sh);
                cnt = hold;
            }
            cnt--;
            y = hd << 4;                                /* (x16: the low cutoffs' rounding leaves no offset) */
            v = mulq16(y - lp1, k);                     /* LPF: two one-pole low-passes */
            y = v + lp1;
            lp1 = y + v;
            v = mulq16(y - lp2, k);
            y = v + lp2;
            lp2 = y + v;
            b[i] = ins_mix(x, (y + 8) >> 4, w);
        }
        s->cnt = (uint8_t)cnt;
        s->hold = hd;
    } else {
        uint32_t k = (uint32_t)ins_lpk(p[P_IB]);
        int32_t lvl = LEVEL_Q12[p[P_IC] & 127], hp = s->hp, dc = s->dc;
        for (i = 0; i < n; i++) {
            x = b[i];
            w += clamp(tw - w, -SL_SLOPE, SL_SLOPE);
            hp += ((x << 8) - hp) >> 8;                 /* ~27 Hz low cut (Q8: no offset left by the rounding) */
            v = clamp(x - (hp >> 8), -230000, 230000);
            y = ins_shape(ty, ((v >> 3) * g) >> 9, 0) << 4;   /* (>> 3 first: 32 bits; x16 as CRUSH) */
            v = mulq16(y - lp1, k);                     /* TONE: two one-pole low-passes */
            y = v + lp1;
            lp1 = y + v;
            v = mulq16(y - lp2, k);
            y = v + lp2;
            lp2 = y + v;
            y = (((y + 8) >> 4) * lvl) >> 12;           /* LEVEL */
            dc += ((y << 8) - dc) >> 9;                 /* DC blocker, ~14 Hz (FUZZ's lopsided halves) */
            b[i] = ins_mix(x, y - (dc >> 8), w);
        }
        s->hp = hp;
        s->dc = dc;
    }
    s->w = w;
    s->lp1 = lp1;
    s->lp2 = lp2;
}

/* PHASR, FLANG, CHOR: the sweep at RATE (one LFO per state), DEPTH, FDBK; the state in locals for the block */
static __attribute__((noinline)) void ins_swept(ins_t *s, const int16_t *p, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, inc = LFO_INC[clamp(p[P_IA], 0, 127)] / CTL, dep = (uint32_t)clamp(p[P_IB], 0, 127);
    int32_t c = clamp(p[P_IC], 0, 127), w = s->w, dc = s->dc;
    if (s->type == IT_PHASER) {                         /* the coefficients a block at a time (at its middle) */
        uint32_t tr = ins_tri(s->ph + (n >> 1) * inc), q = 32768u + (uint32_t)(((int32_t)tr - 32768) * (int32_t)dep / 127);
        int32_t a = PH_A[q >> 11] + (((PH_A[(q >> 11) + 1u] - PH_A[q >> 11]) * (int32_t)(q & 2047u)) >> 11);
        int32_t fbk = c * 180, fb = s->fb;              /* 0 .. 0.7, Q15 */
        int32_t x0 = s->px[0], x1 = s->px[1], x2 = s->px[2], x3 = s->px[3];
        int32_t y0 = s->py[0], y1 = s->py[1], y2 = s->py[2], y3 = s->py[3];
        _Static_assert(PH_ST == 4u, "ins_swept: four all-pass stages");
        for (i = 0; i < n; i++) {
            int32_t x = b[i], u = clamp((x + 4) >> 3, -16383, 16383) + ((fb * fbk + 16384) >> 15), y;
            w += clamp(tw - w, -SL_SLOPE, SL_SLOPE);
            /* y = a (u - y1) + u1, four times */
            y = clamp(x0 + ((a * (u - y0) + 8192) >> 14), -65535, 65535), x0 = u, y0 = y, u = y;
            y = clamp(x1 + ((a * (u - y1) + 8192) >> 14), -65535, 65535), x1 = u, y1 = y, u = y;
            y = clamp(x2 + ((a * (u - y2) + 8192) >> 14), -65535, 65535), x2 = u, y2 = y, u = y;
            y = clamp(x3 + ((a * (u - y3) + 8192) >> 14), -65535, 65535), x3 = u, y3 = y;
            fb = clamp(y, -32767, 32767);
            y <<= 2;                                    /* (y: at an eighth) */
            dc += ((y << 8) - dc) >> 9;                 /* DC blocker, ~14 Hz (a clipped loud input is lopsided) */
            b[i] = ins_mix(x, (x >> 1) + y - (dc >> 8), w);   /* (dry + all-passed) / 2 */
        }
        s->px[0] = x0, s->px[1] = x1, s->px[2] = x2, s->px[3] = x3;
        s->py[0] = y0, s->py[1] = y1, s->py[2] = y2, s->py[3] = y3;
        s->fb = fb;
        s->ph += n * inc;
    } else {
        int32_t chorus = s->type == IT_CHORUS, fbk = c * (chorus ? 103 : 219);   /* 0 .. 0.4 / 0 .. 0.85, Q15 */
        uint32_t dq = dep * 516u, ph = s->ph, wp = s->wp; /* DEPTH, Q16 */
        int16_t *line = s->line;
        for (i = 0; i < n; i++, ph += inc) {
            int32_t x = b[i], r0, r1, v, fr;
            uint32_t d;                                 /* the delay, Q16 samples */
            if (s->fill)
                s->fill--;                              /* (0.9: an empty line read back: a step when the sound reaches
                                                         * the delay; the wet waits for the line) */
            if (!s->fill)
                w += clamp(tw - w, -SL_SLOPE, SL_SLOPE);
            if (chorus) {
                d = (uint32_t)((140 << 16) + ((osc_sine(ph) * (int32_t)dep * 50) >> 5));   /* 3.2 +- 2.3 ms */
            } else {
                uint32_t tr = ins_tri(ph), sw = (tr * tr) >> 16;   /* longer near the short end */
                d = (11u << 16) + 209u * (((sw >> 1) * dq) >> 15);  /* 0.25 .. 5 ms */
            }
            r0 = (int32_t)(wp - (d >> 16)) & (int32_t)(IL_LEN - 1u);
            r1 = (r0 - 1) & (int32_t)(IL_LEN - 1u);
            fr = (int32_t)((d >> 1) & 0x7FFFu);
            v = line[r0] + (((line[r1] - line[r0]) * fr) >> 15);
            line[wp] = (int16_t)clamp((x >> 2) + softclip(mulq15(v, fbk)), -32767, 32767);
            wp = (wp + 1u) & (IL_LEN - 1u);
            v <<= 1;                                    /* (v: at a quarter) */
            dc += ((v << 8) - dc) >> 9;                 /* DC blocker (the line's clip on a loud lopsided wave) */
            b[i] = ins_mix(x, (x >> 1) + v - (dc >> 8), w);   /* (dry + delayed) / 2 */
        }
        s->ph = ph;
        s->wp = (uint16_t)wp;
    }
    s->w = w;
    s->dc = dc;
}

/* ---- (1.1) PITCH, PIT2: the pitch shifter of TripDeck (firmware/src/fx.h ps_run, ps_rd), Copyright (C) 2026
 * anonymousacademe (https://github.com/anonymousacademe/FM-1-TripDeck), GPL-3.0-only as this file. Two read heads half
 * a window (1024 samples, 23 ms) apart on a line written at the sound's rate; the heads' delay moves by a step a sample
 * (1 - the ratio: 0 the same pitch, -1 an octave up, +1/2 an octave down), so they read it at another speed; each is
 * faded by a triangle, silent at the window's ends where it jumps. Here per sound, mono, its line in ins_t, the ratio
 * from PS_SEMI and PS_CENT, a fade-in of the line's input (a start: the heads never cross a step into the empty line),
 * PITCH's FDBK and PIT2's second voice on the same line. */
#define IPS_WIN (1024 << 16)              /* the window, 16.16 samples */
/* a b / 32768 rounded towards 0 (a feedback through it dies away to 0, no -1 left going round) */
static inline int32_t mulq15z(int32_t a, int32_t b)
{
    int32_t m = a * b;
    return (m + ((m >> 31) & 32767)) >> 15;
}
static const int32_t PS_SEMI[49] = {     /* 2^(s / 12), Q16, s = -24 .. +24 (the knob over 49 even parts: N_ISEMI) */
    16384, 17358, 18390, 19484, 20643, 21870, 23170, 24548, 26008, 27554, 29193, 30929, 32768, 34716, 36781, 38968,
    41285, 43740, 46341, 49097, 52016, 55109, 58386, 61858, 65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193,
    104032, 110218, 116772, 123715, 131072, 138866, 147123, 155872, 165140, 174960, 185364, 196386, 208064, 220436,
    233544, 247431, 262144,
};
static const int16_t PS_CENT[21] = {     /* 2^(c / 1200) - 1, Q16, c = -50 .. +50 in 5s (N_ICENT) */
    -1866, -1682, -1497, -1312, -1126, -940, -753, -565, -377, -189, 0, 190, 380, 570, 761, 953, 1146, 1338, 1532, 1726,
    1920,
};
/* how far the heads move a sample (16.16) for the knobs' semitones and cents (cents -1: none) */
static int32_t ps_rate(int32_t semi, int32_t cent)
{
    int32_t r = PS_SEMI[(clamp(semi, 0, 127) * 49) >> 7];
    if (cent >= 0)
        r += (int32_t)(((int64_t)r * PS_CENT[(clamp(cent, 0, 127) * 21) >> 7]) >> 16);
    return 65536 - r;
}
/* the line d (16.16) samples before the last written (w: the next write) */
static inline int32_t ps_rd(const int16_t *line, uint32_t w, int32_t d)
{
    uint32_t pos = w - 1u - ((uint32_t)d >> 16);
    int32_t f = (d >> 8) & 255, a = line[pos & (IN_LEN - 1u)], b = line[(pos - 1u) & (IN_LEN - 1u)];
    return a + (((b - a) * f) >> 8);
}
/* one voice: its heads moved by rate, the two faded together */
static inline int32_t ps_voice(const int16_t *line, uint32_t w, int32_t *ph, int32_t rate)
{
    int32_t d1 = *ph + rate, d2, w1;
    if (d1 >= IPS_WIN)
        d1 -= IPS_WIN;
    else if (d1 < 0)
        d1 += IPS_WIN;
    *ph = d1;
    d2 = d1 + (IPS_WIN >> 1);
    if (d2 >= IPS_WIN)
        d2 -= IPS_WIN;
    w1 = (d1 < (IPS_WIN >> 1) ? d1 : IPS_WIN - d1) >> 17;   /* a triangle: 0 at the window's ends, 256 in its middle */
    return (ps_rd(line, w, d1) * w1 + ps_rd(line, w, d2) * (256 - w1)) >> 8;
}
static __attribute__((noinline)) void ins_pitch(ins_t *s, const int16_t *p, int32_t *b, uint32_t n, int32_t tw)
{
    int two = s->type == IT_PITCH2;
    int32_t c = clamp(p[P_IC], 0, 127), r1 = ps_rate(p[P_IA], two ? -1 : p[P_IB]), r2 = two ? ps_rate(p[P_IB], -1) : 0;
    int32_t fbk = two ? 0 : c * 155, bal = c >= 127 ? 128 : c;   /* PITCH: FDBK 0 .. 0.6 (Q15); PIT2: BAL */
    int32_t w = s->w, in = s->in, ph1 = s->ph1, ph2 = s->ph2, fb = s->fb;
    uint32_t i, wp = s->wp;
    int16_t *line = s->line;
    for (i = 0; i < n; i++) {
        int32_t x = b[i], y;
        w += clamp(tw - w, -SL_SLOPE, SL_SLOPE);
        in = in + SL_SLOPE < 32768 ? in + SL_SLOPE : 32768;
        line[wp & (IN_LEN - 1u)] = (int16_t)clamp(mulq15(clamp(x >> 2, -32767, 32767), in) + mulq15z(fb, fbk), -32767, 32767);
        wp++;
        y = ps_voice(line, wp, &ph1, r1);
        if (two)
            y += ((ps_voice(line, wp, &ph2, r2) - y) * bal) >> 7;
        fb = y;
        b[i] = ins_mix(x, y << 2, w);                   /* (y: at a quarter) */
    }
    s->w = w, s->in = in, s->ph1 = ph1, s->ph2 = ph2, s->fb = fb, s->wp = (uint16_t)wp;
}

/* ---- (1.1) PHAS6: the phaser of CS-1 (firmware/src/cs1_fx.h fx_phaser, its PH_A table: firmware/gen/cs1_tables.h,
 * tools/gen_tables.py), Copyright (C) 2026 twochannels (https://github.com/twochannels/CS-1), GPL-3.0-only as this
 * file: six first-order all-passes (y = -a x + x1 + a y1) swept by one sine LFO, the coefficient from PH6_A once a
 * block and slid across it, feedback round the chain. Here per sound and mono (CS-1's is stereo, the right a quarter
 * turn behind), RATE from LFO_INC as PHASR's, and the wet (dry + all-passed) / 2 with a DC blocker, as PHASR's. */
static const int16_t PH6_A[128] = {      /* (1 - tan(pi f / FS)) / (1 + tan(pi f / FS)), Q15, f = 100 x 50^(i / 127) Hz */
    32303, 32289, 32274, 32259, 32243, 32227, 32210, 32193, 32175, 32157, 32138, 32118, 32098, 32078, 32056, 32034,
    32012, 31988, 31964, 31939, 31914, 31887, 31860, 31832, 31804, 31774, 31743, 31712, 31679, 31646, 31611, 31576,
    31539, 31502, 31463, 31423, 31382, 31339, 31296, 31251, 31204, 31157, 31107, 31057, 31005, 30951, 30896, 30839,
    30781, 30720, 30658, 30594, 30529, 30461, 30392, 30320, 30246, 30170, 30092, 30012, 29930, 29845, 29757, 29668,
    29575, 29480, 29382, 29282, 29179, 29073, 28963, 28851, 28736, 28617, 28496, 28371, 28242, 28110, 27975, 27835,
    27692, 27546, 27395, 27240, 27081, 26918, 26751, 26579, 26403, 26223, 26037, 25847, 25652, 25453, 25248, 25038,
    24823, 24602, 24377, 24145, 23908, 23665, 23417, 23163, 22902, 22636, 22363, 22084, 21798, 21506, 21207, 20901,
    20589, 20270, 19943, 19609, 19268, 18920, 18564, 18200, 17828, 17449, 17061, 16665, 16261, 15848, 15427, 14996,
};
static __attribute__((noinline)) void ins_phase6(ins_t *s, const int16_t *p, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, k, inc = LFO_INC[clamp(p[P_IA], 0, 127)] / CTL;
    int32_t dep = clamp(p[P_IB], 0, 127), fbk = clamp(p[P_IC], 0, 127) * 180;   /* FDBK 0 .. 0.7, Q15 */
    int32_t idx = 64 + ((osc_sine(s->ph + n * inc) * dep) >> 16), a1 = PH6_A[clamp(idx, 0, 127)];
    int32_t w = s->w, fb = s->fb, dc = s->dc, a = s->a6 ? s->a6 : a1, ad = (a1 - a) / (int32_t)n;
    for (i = 0; i < n; i++) {
        int32_t x = b[i], u = clamp((x + 4) >> 3, -16383, 16383) + mulq15(fb, fbk), y;
        w += clamp(tw - w, -SL_SLOPE, SL_SLOPE);
        for (k = 0; k < 6u; k++) {                      /* (the states held under 60000: a Q15 product of more wraps) */
            y = clamp(-mulq15(u, a) + s->x6[k] + mulq15(s->y6[k], a), -60000, 60000);
            s->x6[k] = u, s->y6[k] = y, u = y;
        }
        fb = clamp(u, -32767, 32767);
        a += ad;
        y = u << 2;                                     /* (u: at an eighth) */
        dc += ((y << 8) - dc) >> 9;                     /* DC blocker, ~14 Hz */
        b[i] = ins_mix(x, (x >> 1) + y - (dc >> 8), w);   /* (dry + all-passed) / 2 */
    }
    s->ph += n * inc;
    s->w = w, s->fb = fb, s->dc = dc, s->a6 = a1;
}

/* ---- (1.1) DELAY: a short delay of the sound's own. The line at half the rate (two samples kept as their mean: 92 ms
 * in IN_LEN), read between its samples at the delay now, which glides to TIME (an eighth of a sample a sample: a
 * turned TIME bends the echoes as a tape would, no click); TONE a low-pass on the echoes, inside the feedback (at x16:
 * its rounding leaves no offset); the feedback through softclip, rounded towards 0 (the echoes die away to silence). ring: the loudest sample written lately, falling by half in ~130 ms (longer than the
 * longest TIME: the gaps between echoes); mix_part1 runs the part on while it is over 3 (ins_ringing). */
static const uint8_t DI_MS[32] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20, 22, 25, 28, 31, 34, 37, 41, 45, 49, 53,
                                  58, 63, 68, 73, 78, 83, 88, 92};   /* TIME, ms (the knob over 32 even parts: N_IDMS) */
static __attribute__((noinline)) void ins_delay(ins_t *s, const int16_t *p, int32_t *b, uint32_t n, int32_t tw)
{
    int32_t tgt = DI_MS[(clamp(p[P_IA], 0, 127) * 32) >> 7] * 11290;   /* Q8 samples (44.1 a ms) */
    int32_t fbk = clamp(p[P_IB], 0, 127) * 232;         /* 0 .. 0.9, Q15 */
    uint32_t i, k = (uint32_t)ins_lpk(p[P_IC]), wp = s->wp, odd = s->cnt;
    int32_t w = s->w, in = s->in, d = s->ph1 ? s->ph1 : tgt, acc = s->acc, lp = s->lp1, pk = 0;
    int16_t *line = s->line;
    _Static_assert(92 * 11290 < (int32_t)(IN_LEN - 3u) * 512, "DELAY: the longest TIME inside the line");
    for (i = 0; i < n; i++) {
        int32_t x = b[i], back, e, a0, a1, tap, v;
        w += clamp(tw - w, -SL_SLOPE, SL_SLOPE);
        in = in + SL_SLOPE < 32768 ? in + SL_SLOPE : 32768;
        d += clamp(tgt - d, -32, 32);
        back = d >> 1;                                  /* in the line's samples, Q8 */
        e = (int32_t)wp - 1 - (back >> 8);
        a0 = line[(uint32_t)e & (IN_LEN - 1u)], a1 = line[(uint32_t)(e - 1) & (IN_LEN - 1u)];
        tap = a0 + (((a1 - a0) * (back & 255)) >> 8);
        v = mulq16((tap << 4) - lp, k);                 /* TONE: a one-pole low-pass (topology-preserving), x16 */
        tap = v + lp;
        lp = tap + v;
        tap = (tap + 8) >> 4;
        v = clamp(mulq15(clamp(x >> 2, -32767, 32767), in) + softclip(mulq15z(tap, fbk)), -32767, 32767);
        if (odd) {
            v = (acc + v) >> 1;
            line[wp & (IN_LEN - 1u)] = (int16_t)v;
            wp++;
            v = v < 0 ? -v : v;
            pk = v > pk ? v : pk;
        } else {
            acc = v;
        }
        odd ^= 1u;
        b[i] = ins_mix(x, x + (tap << 2), w);           /* dry + the echo (tap: at a quarter) */
    }
    s->ring -= (s->ring >> 8) + (s->ring > 0);
    s->ring = pk > s->ring ? pk : s->ring;
    s->w = w, s->in = in, s->ph1 = d, s->acc = acc, s->lp1 = lp, s->wp = (uint16_t)wp, s->cnt = (uint8_t)odd;
}

/* ---- STDLY, PPDLY: a stereo analog delay after the voice's filters, the idea of the bucket-brigade
 * lines in the instrument MARE follows (no one's code): two lines, the left and the right of the part (its mid and side)
 * in. A line is a fixed number of stages (MARE: 2048, two lines in its engine arena; another engine: 1024, the
 * INSERT's own line halved) clocked at stages / TIME, so a longer time is a lower clock and a darker echo by itself:
 * two one-pole low-passes before the line and two after it at SD_FC x the clock (16 kHz at most), the line read between
 * its last two stages (the clock's fraction); each stage written as the input was at the clock's instant (between two
 * samples: no jitter where the clock is near the rate). MARE's response at 428 ms (a 4.8 kHz clock): -1.3 dB at
 * 500 Hz, -5 dB at 1 kHz, -16 dB at 2 kHz; at 105 ms -0.5 dB at 1 kHz (tests/stereo_delay_test.c). One clock for both
 * lines: the same tone left and right (centred); SPACE (C, line 2's time against line 1's, x 2^(-8/16) .. x 2^(8/16))
 * reads the other line short of its last stage, the longer line at TIME. TIME (A) 35 .. 780 ms (SD_MS; under stages /
 * 44.1 kHz the clock stays at the rate and the lines are read short); FDBK (B) 0 .. x 1.15 (past 1 at the top: the line
 * sings, held by a tanh in the loop, a slow DC blocker in it; ~0.7 a repeat at 80); MIX the dry / wet crossfade. STDLY:
 * each line its own side, its own feedback. PPDLY (ping-pong): the feedback crossed, line 1 into line 2 and line 2
 * into line 1 (each side's input its own): a side's echo comes back on the other side, SPACE apart; a centred sound
 * stays centred. TIME glides (SD_GLIDE: at most half a ms of delay a ms, easing in at the end), each line's read point
 * glides (half a stage a clock at most): a turned TIME or SPACE bends the echoes as a tape would, no click. The filters
 * at x16 and the DC blocker at x4096: no offset left by their rounding, a tail dies to exact 0. The part's side buffer
 * (voice.c part_side) carries the stereo out; mix_part1 leaves it unscaled (no DIST / INSERT level match on it). ring
 * as DELAY's: the part runs on while echoes are left (ins_ringing) */
static inline int32_t sd_mul(int32_t a, int32_t k) { return (int32_t)(((int64_t)a * k) >> 16); }   /* (one multiply) */
static const uint16_t SD_SPR[9] = {4096, 4277, 4467, 4664, 4871, 5087, 5312, 5547, 5793};   /* SPACE: 2^(k / 16), Q12 */
static const uint16_t SD_MS[32] = {35, 39, 43, 47, 52, 58, 64, 71, 78, 86, 95, 105, 116, 129, 142, 157, 174, 192, 212, 235,
                                   259, 287, 317, 350, 387, 428, 473, 523, 578, 638, 706, 780};   /* (params.c N_ISDMS) */
#define SD_FC 50u                       /* the low-passes' cutoff, % of the clock */
#define SD_GLIDE 93                     /* TIME's glide, Q8 ms a block of CTL (0.73 ms): a slope of 1/2 */
static __attribute__((noinline)) void ins_sdly(ins_t *s, const int16_t *p, int32_t *b, uint32_t n, int32_t tw)
{
    track_t *t = &trk[(uint32_t)(s - ins) % NPART];
    int16_t *ln[2];
    uint32_t nst, mask, c, i, src = 1, f, x, step, rcp, ut[2], al;
    int32_t k, fbk = clamp(p[P_IB], 0, 127) * 297, w = s->w, pk = 0, pki = 0, *q0 = s->bb[0], tq;   /* (FDBK: Q15) */
    int32_t sp = (clamp(p[P_IC], 0, 127) * 17 >> 7) - 8, tg = SD_MS[(clamp(p[P_IA], 0, 127) * 32) >> 7] << 8;   /* Q8 ms */
    int pp = p[P_ITYPE] == IT_PDLY, sided = part_side_on, fresh = 0;
#if CLIPPER_SYNTH
    if (t->engine == ENGI_MARE && (uint32_t)(t - trk) < NPART) {
        mar_part_t *A = mar_arena(t);
        ln[0] = A->line[0], ln[1] = A->line[1], nst = MTD_N, src = 2;
    } else
#endif
    {
        ln[0] = s->line, ln[1] = s->line + IN_LEN / 2u, nst = IN_LEN / 2u;
    }
    mask = nst - 1u;
    if (s->cnt != src) {                                /* (a start, or the lines moved: from rest) */
        memset(s->bb, 0, sizeof s->bb);
        memset(s->sdy, 0, sizeof s->sdy);
        s->sdh = 0;
        memset(ln[0], 0, 2u * nst * sizeof(int16_t));   /* (both lines: one after the other) */
        s->cnt = (uint8_t)src;
        q0[SD_TQ] = tg, fresh = 1;
    }
    tq = q0[SD_TQ] += clamp((tg - q0[SD_TQ] + 7) >> 3, -SD_GLIDE, SD_GLIDE);   /* (easing in at the end: no kink) */
    f = nst * 256000u / (uint32_t)tq;                   /* the clock, Hz: the stages over TIME */
    al = f < 44100u ? nst : (uint32_t)tq * 441u / 2560u;   /* the longer line's stages (a short TIME: fewer, at the rate) */
    f = f < 44100u ? f : 44100u;
    step = (f << 16) / 44100u;                          /* Q16 clocks a sample */
    rcp = (1u << 31) / step;                            /* (the time since a clock: its Q16 fraction x rcp >> 16, Q15) */
    x = (f * SD_FC / 100u > 16000u ? 16000u : f * SD_FC / 100u) * 5836u / 10000u;   /* 2 pi fc / fs, Q12 */
    k = (int32_t)((x << 16) / (4096u + x));             /* the one-poles' coefficient, Q16 */
    ut[0] = ut[1] = al << 8;                            /* each line's stages, Q8: the shorter one SPACE short */
    ut[sp > 0 ? 0 : 1] = (al << 20) / SD_SPR[sp < 0 ? -sp : sp];
    for (c = 0; c < 2u; c++) {
        ut[c] = ut[c] < 512u ? 512u : ut[c] > (nst - 1u) << 8 ? (nst - 1u) << 8 : ut[c];
        if (fresh)
            s->bb[c][SD_U] = (int32_t)ut[c];
    }
    {   /* a line at a time over the block, its state in locals (its input first: the dry side faded in, into yb); then
         * the mix. PPDLY: each line fed by the other's output of the last block (sdy: 0.73 ms more each way round, the two
         * ways alike: a mono sound stays exactly mono) */
        int32_t yb[2][CTL], lin = s->in;
        uint32_t ph = 0, wi = 0;
        for (c = 0; c < 2u; c++) {
            int32_t *q = s->bb[c], *y = yb[c], a1 = q[SD_A1], a2 = q[SD_A2], op = q[SD_OP], oc = q[SD_OC];
            int32_t b1 = q[SD_B1], b2 = q[SD_B2], dc = q[SD_DC], yv = q[SD_Y];
            const int32_t *fs = pp ? s->sdy[c ^ 1u] : 0;   /* (PPDLY: the other line's last block, both ways alike) */
            lin = s->in;
            for (i = 0; i < n; i++) {
                int32_t sd = sided ? part_side[i] : 0;
                int32_t v;
                lin = lin + SL_SLOPE < 32768 ? lin + SL_SLOPE : 32768;
                y[i] = v = mulq15(clamp((c ? b[i] - sd : b[i] + sd) >> 2, -32767, 32767), lin);
                v = v < 0 ? -v : v;
                pki = v > pki ? v : pki;                /* (the loudest in) */
            }
            ph = (uint32_t)q0[SD_PH], wi = (uint32_t)q0[SD_WI];
            for (i = 0; i < n; i++) {
                int32_t x = y[i] + softclip(mulq15(fs ? fs[i] : yv, fbk)), a2o = a2, o;   /* (softclip: tanh, slope 1) */
                a1 += sd_mul((clamp(x, -32767, 32767) << 4) - a1, k);   /* (the filters at x16: their rounding leaves no
                                                         * offset, the tail dies to 0) */
                a2 += sd_mul(a1 - a2, k);
                ph += step;
                if (ph >= 65536u) {                     /* a clock: a stage in, the oldest out */
                    int16_t *l = ln[c];
                    int32_t v, a0, uu = q[SD_U];
                    uint32_t r;
                    ph -= 65536u;
                    v = clamp((a2 - sd_mul(a2 - a2o, (int32_t)(((ph * rcp) >> 16) << 1)) + 8) >> 4, -32767, 32767);
                    l[wi & mask] = (int16_t)v;          /* (the stage at the clock's instant, between two samples: no
                                                         * jitter where a clock near the rate skips a sample) */
                    uu += clamp(((int32_t)ut[c] - uu + 15) >> 4, -128, 128);   /* (the read point glides, half a stage
                                                         * a clock at most, read between two stages) */
                    q[SD_U] = uu;
                    r = wi + 1u - ((uint32_t)uu >> 8), a0 = l[r & mask];
                    op = oc, oc = a0 + (((l[(r - 1u) & mask] - a0) * (uu & 255)) >> 8);
                    wi++;
                }
                o = op + (((oc - op) * (int32_t)(ph >> 2)) >> 14);
                b1 += sd_mul((o << 4) - b1, k);
                b2 += sd_mul(b1 - b2, k);
                dc += ((b2 << 8) - dc) >> 9;            /* (the loop's DC: a slow high-pass, x 4096: no offset left) */
                y[i] = yv = clamp((b2 - (dc >> 8) + 8) >> 4, -32767, 32767);
            }
            q[SD_A1] = a1, q[SD_A2] = a2, q[SD_OP] = op, q[SD_OC] = oc, q[SD_B1] = b1, q[SD_B2] = b2, q[SD_DC] = dc;
            q[SD_Y] = yv;
        }
        s->in = lin;
        q0[SD_PH] = (int32_t)ph, q0[SD_WI] = (int32_t)wi;
        for (i = 0; i < n; i++) {
            int32_t sd = sided ? part_side[i] : 0, x0 = b[i] + sd, x1 = b[i] - sd, y0 = yb[0][i], y1 = yb[1][i];
            s->sdy[0][i] = y0, s->sdy[1][i] = y1;
            y0 = y0 < 0 ? -y0 : y0, y1 = y1 < 0 ? -y1 : y1;
            pk = y0 > pk ? y0 : pk, pk = y1 > pk ? y1 : pk;   /* (ring: the loudest out lately) */
            w += clamp(tw - w, -(SL_SLOPE >> 4), SL_SLOPE >> 4);   /* (MIX a crossfade: the dry fades over ~46 ms, no
                                                         * step when the type starts or stops) */
            x0 = ins_mix(x0, yb[0][i] << 2, w), x1 = ins_mix(x1, yb[1][i] << 2, w);   /* MIX: dry -> wet */
            b[i] = (x0 + x1) >> 1;
            part_side[i] = (x0 - x1) >> 1;
        }
    }
    part_side_on = 1;
    if (pki > 3)                                        /* (something went in: its echo is ahead for a line's time, a
                                                         * quiet one too: ring held over 3 until it is out) */
        s->sdh = (uint16_t)(((uint32_t)tq * 11u >> 11) + 8u);   /* (TIME in blocks of CTL, and some) */
    else if (s->sdh)
        s->sdh--;
    s->ring -= (s->ring >> 8) + (s->ring > 0);
    s->ring = pk > s->ring ? pk : s->ring;
    s->ring = s->sdh && s->ring < 4 ? 4 : s->ring;
    s->w = w;
}

/* ---- (1.1) LOCMP: a lo-fi compressor (a known effect type's idea: a compressor, then the lo-fi). The compressor is
 * Airwindows Pop3's law, Copyright (c) 2018 Chris Johnson (https://github.com/airwindows/airwindows, MIT: the notice in
 * LICENSES/MIT-Airwindows.txt), as Lunar Modulator's Squash > Snap describes and ports it (engines/src/fx_squash.cc,
 * Copyright (c) 2026 ip2k, https://github.com/ip2k/lunar-modulator, MIT: LICENSES/MIT-Lunar-Modulator.txt): a state p
 * that moves towards T / |x| by the attack while |x| is over the threshold T = (1 - COMP)^4, else towards 1 by the
 * release; the gain (1 - R) + p R, R = 0.75 (Pop3's Ratio 0.5). Pop3's defaults: attack 25.5 ms, release 46.8 ms. Here
 * in fixed point and a block at a time (the block's peak, its steps at once: one divide a block, the chip has no
 * divider), the gain ramped across the block, Pop3's gate left out, a makeup gain from COMP (LC_MAKEUP: what a -6 dB
 * peak loses, three quarters of it back). Then CRUSH's held sample and bits from LOFI and two low-passes at TONE. */
static const uint16_t LC_MAKEUP[17] = {  /* Q12, COMP 0, 8 .. 127 */
    4096, 4096, 4096, 4437, 5242, 6160, 7163, 8197, 9182, 10035, 10695, 11142, 11404, 11529, 11575, 11585, 11585,
};
static __attribute__((noinline)) void ins_locomp(ins_t *s, const int16_t *p, int32_t *b, uint32_t n, int32_t tw)
{
    int32_t a = clamp(p[P_IA], 0, 127), lo = clamp(p[P_IB], 0, 127), u = (127 - a) * 258, uu = mulq15(u, u);
    int32_t thr = mulq15(uu, uu), pk = 0, tg, g1, dg, g, w = s->w, lp1 = s->lp1, lp2 = s->lp2, hd = s->hold, x, y, v;
    uint32_t i, sh = (uint32_t)(lo * 10) / 127u, hold = lo ? INS_HOLD[15 - (lo * 8) / 127] : 1u, cnt = s->cnt;
    uint32_t k = (uint32_t)ins_lpk(p[P_IC]);
    thr = thr < 33 ? 33 : thr;                          /* (-60 dB of a voice's full scale at the most) */
    for (i = 0; i < n; i++) {
        x = b[i] < 0 ? -b[i] : b[i];
        pk = x > pk ? x : pk;
    }
    tg = pk > thr ? (int32_t)(((uint32_t)thr << 15) / (uint32_t)pk) : 32768;
    s->cp += mulq15(tg - s->cp, pk > thr ? 920 : 504);  /* a block's steps of 1 / 1125 and 1 / 2064 a sample */
    g1 = (int32_t)(((8192 + ((s->cp * 3) >> 2)) * (int32_t)LC_MAKEUP[a >> 3] + 2048) >> 12);   /* Q15, makeup in */
    g = s->cg, dg = (g1 - g) / (int32_t)n;
    for (i = 0; i < n; i++) {
        x = b[i];
        w += clamp(tw - w, -SL_SLOPE, SL_SLOPE);
        g += dg;
        y = (int32_t)(((int64_t)x * g) >> 15);
        if (!cnt) {
            hd = ins_shape(IT_CRUSH, clamp(y, -262144, 262143), sh);
            cnt = hold;
        }
        cnt--;
        y = hd << 4;                                    /* (x16, as CRUSH's) */
        v = mulq16(y - lp1, k);                         /* TONE: two one-pole low-passes */
        y = v + lp1;
        lp1 = y + v;
        v = mulq16(y - lp2, k);
        y = v + lp2;
        lp2 = y + v;
        b[i] = ins_mix(x, (y + 8) >> 4, w);
    }
    s->w = w, s->cg = g1, s->lp1 = lp1, s->lp2 = lp2, s->hold = hd, s->cnt = (uint8_t)cnt;
}

/* the state at rest for a start of type ty: the line cleared as far as ty reads it */
static void ins_clear(ins_t *s, int32_t ty)
{
    memset(s, 0, __builtin_offsetof(ins_t, line));
    if (ty == IT_FLANGER || ty == IT_CHORUS)
        memset(s->line, 0, IL_LEN * sizeof s->line[0]);
    else if (ty == IT_PITCH || ty == IT_PITCH2 || ty == IT_DELAY)
        memset(s->line, 0, sizeof s->line);
    else if (ty == IT_LOCOMP)
        s->cp = s->cg = 32768;                          /* (the compressor open: no dip at the start) */
    s->type = (uint8_t)ty;
}

/* the INSERT of the settings p on one channel b through state s: 1 while it runs (or fades out); 0: dry, bit for bit */
static __attribute__((noinline)) int ins_run(ins_t *s, const int16_t *p, int32_t *b, uint32_t n, int fresh)
{
    int32_t ty = clamp(p[P_ITYPE], 0, IT_N - 1), tw;
    if (ty >= IT_PONY1 && ty <= IT_PONY12)
        ty = IT_OFF;                                    /* the PONY chorus: not here (pony_chorus, after the FILTER) */
    if (s->type != ty && !s->w)                         /* faded out: the new type from rest */
        s->type = (uint8_t)ty, s->fill = 0;
    tw = s->type == ty && ty ? ins_mixq(p[P_IMIX]) : 0;
    if (!s->w && !s->fill) {
        if (!tw)
            return 0;                                   /* dry: bit for bit */
        ins_clear(s, ty);                               /* starting: no stale line or filter */
        if ((ty == IT_FLANGER || ty == IT_CHORUS) && !fresh)
            s->fill = IL_LEN;                           /* (the part was silent: the empty line is its past, no wait) */
    }
    switch (s->type) {
    case IT_PHASER:
    case IT_FLANGER:
    case IT_CHORUS:
        ins_swept(s, p, b, n, tw);
        break;
    case IT_PITCH:
    case IT_PITCH2:
        ins_pitch(s, p, b, n, tw);
        break;
    case IT_PHASER6:
        ins_phase6(s, p, b, n, tw);
        break;
    case IT_DELAY:
        ins_delay(s, p, b, n, tw);
        break;
    case IT_LOCOMP:
        ins_locomp(s, p, b, n, tw);
        break;
    case IT_SDLY:
    case IT_PDLY:
        ins_sdly(s, p, b, n, tw);
        break;
    default:
        ins_drive(s, p, b, n, tw);
    }
    return s->w != 0 || s->fill != 0;                   /* (filling: running, the wet still out) */
}
/* (1.1) the INSERT's DELAY has echoes still ringing in its line (mix_part1: the part runs on until they die away) */
static int ins_ringing(const track_t *t)
{
    const ins_t *s = &ins[(uint32_t)(t - trk) % NPART];
    return t->ins_run && s->type >= IT_DELAY && s->ring > 3;   /* (DELAY, STDLY, PPDLY: LOCMP's ring is 0) */
}
/* a part's INSERT on its block b (mix_part): nothing while TYPE is OFF or MIX is 0 and faded */
static void track_insert(track_t *t, int32_t *b, uint32_t n, int fresh)
{
    t->ins_run = (uint8_t)HOT_FAR(ins_run)(&ins[(uint32_t)(t - trk) % NPART], t->p, b, n, fresh);
}
/* the INSERT at rest at once (mix_part: TYPE turned OFF while the part was silent, its fade never ran) */
static __attribute__((noinline)) void ins_rest(track_t *t)
{
    memset(&ins[(uint32_t)(t - trk) % NPART], 0, sizeof(ins_t));
    t->ins_run = 0;
}

/* ---- the PONY chorus (Clipper 0.8): the stereo BBD chorus of the 1980s DCO poly, its idea and its published
 * measurements (no code of anyone's): one line, two taps whose delays a triangle LFO sweeps in opposite directions,
 * each tap low-passed as a BBD's, left = dry + tap 1, right = dry + tap 2.
 * (Stallion) measured on the original: I 0.532 Hz, II 0.867 Hz, the taps' own
 * ranges (left 1.65 .. 5.46 ms, right 1.21 .. 4.81 ms: + the wet's filters ~0.075 ms, the 1.72 .. 5.53 / 1.29 .. 4.88
 * measured), each tap through two one-poles (TONE 117: ~2.8 kHz together), the wet 1.6 x the dry (the level with the
 * chorus on ~4 dB over off, as the original's). I+II: 9.75 Hz, 3.3 .. 3.7 ms, shallow (the original has no third mode:
 * PONY's own).
 * Its mode: the PONY engine's CHOR, else the INSERT's types PONY1 PONY2 JUN12 (any sound; then MIX the wet, A WIDTH,
 * B TONE, C NOISE: the hiss). The hiss (PONY's CHAR HISS, the INSERT's NOISE): two noises, one a tap (uncorrelated),
 * after the taps' filters, flat to 2 kHz, ~+7 dB at 4..8 kHz, falling above ~11 kHz, ~63 dB under a held chord; PONY's
 * runs while the chorus is on, notes or not (mix_part1 keeps the part on: pch_hiss). Mode changes fade out (2.9 ms),
 * clear the line, fill it (5.8 ms), fade in. 556 B a part (.bss);
 * the stereo half (left - right) in pch_side for mix_part1's level loop; off: nothing runs */
#define PCH_LEN 256u                     /* the line: 5.8 ms (a power of 2: masked), at an eighth (headroom) */
#define PCH_HISS 345                    /* the hiss at CHAR HISS (INSERT NOISE 127): ~-63 dB under a held chord */
#define JH_F 32159                      /* .. its filter: 2 sin(pi 7.2 kHz / FS), the damping 1 / Q (0.4) */
#define JH_Q 13107
#define PCH_DRY 2700                     /* the dry at full wet: 1 - 0.08 (the level on ~+4 dB over off) */
typedef struct {
    int32_t w;                          /* the wet now, Q15 */
    uint32_t ph, nz;                    /* the LFO's phase; the hiss */
    int32_t l1, l2, m1, m2;             /* each tap's two low-passes */
    int32_t h1, h2, g1, g2;             /* each tap's hiss: its low-pass, its band-pass (a Chamberlin filter) */
    uint16_t wp;                        /* the line's next write */
    uint8_t mode;                       /* running: 1 I, 2 II, 3 I+II */
    uint16_t fill;                      /* samples left before it fades in (the line full of the sound first) */
    uint8_t idle;                       /* the part was silent (mix_part1 did not run it): a new mode at once */
    int16_t line[PCH_LEN];
} pch_t;
static pch_t jc[NPART];
static int32_t pch_side[CTL];
static const uint32_t PCH_INC[3] = {51812u, 84438u, 949577u};   /* 0.532, 0.867, 9.75 Hz at 44.1 kHz */
static int32_t pch_mode(const track_t *t, int32_t *wet, int32_t *width, int32_t *tone, int32_t *hiss)
{
    const int16_t *p = t->p;
    int32_t m = t->engine == ENGI_PONY ? clamp(p[P_E7], 0, 3) : 0;
    *wet = 32768, *width = 127, *tone = PCH_TONE, *hiss = m && (p[P_JCHR] & JQ_HISS) ? PCH_HISS : 0;
    if (!m && p[P_ITYPE] >= IT_PONY1 && p[P_ITYPE] <= IT_PONY12) {
        int32_t c = clamp(p[P_IC], 0, 127);
        m = p[P_ITYPE] - IT_PONY1 + 1;
        *wet = ins_mixq(p[P_IMIX]);
        *width = clamp(p[P_IA], 0, 127), *tone = clamp(p[P_IB], 0, 127);
        *hiss = (c * c * PCH_HISS) >> 14;                /* (a gentle curve: 127 the BBD's) */
    }
    return *wet ? m : 0;
}
#if BOARD_SMK37                                         /* (Stallion: on the SMK-37 Pro pch_mode runs from the flash, a */
#define PCH_MODE HOT_FAR(pch_mode)                        /* call a block, its RAM code area full with PONY's CHAR: */
#else                                                   /* tools/build.py HOT_NOT_SMK37) */
#define PCH_MODE pch_mode
#endif
/* PONY's chorus hiss on: the part runs on in silence (mix_part1), the hiss heard */
static inline int pch_hiss(const track_t *t)
{
    return t->engine == ENGI_PONY && t->p[P_E7] && (t->p[P_JCHR] & JQ_HISS);
}
/* the part silent: a chorus whose mode is no longer asked for stops at once (nothing to fade: the next note dry) */
static void pch_idle(const track_t *t)
{
    pch_t *c = &jc[(uint32_t)(t - trk) % NPART];
    int32_t a, b, d, e;
    c->idle = 1;
    if (c->mode && PCH_MODE(t, &a, &b, &d, &e) != c->mode)
        c->w = 0, c->mode = 0;
}
/* the chorus on the part's block b (mono in, its mid back in b, its side in pch_side): 1 when it ran */
static __attribute__((noinline)) int pony_chorus(track_t *t, int32_t *b, uint32_t n)
{
    pch_t *c = &jc[(uint32_t)(t - trk) % NPART];
    int32_t wet, width, tone, hiss, m = PCH_MODE(t, &wet, &width, &tone, &hiss), tw, w, wq, l1, l2, m1, m2, k;
    uint32_t i, ph, wp, inc, base1, span1, base2, span2;
    int16_t *line = c->line;
    if (c->mode != m && (!c->w || c->idle)) {           /* faded out (or silent): the new mode from rest */
        memset(c, 0, sizeof *c);
        c->mode = (uint8_t)m;
        c->nz = 0x2545F491u;
        c->fill = PCH_LEN;                               /* (an empty line read back: a step when the sound reaches it) */
    }
    c->idle = 0;
    if (!c->mode)
        return 0;
    tw = c->mode == m ? wet : 0;
    k = ins_lpk(tone) >> 1;                             /* Q15 */
    inc = PCH_INC[c->mode - 1u];
    if (c->mode == 3u)
        base1 = base2 = 9535488u, span1 = span2 = 18u;  /* 145.5 + 0 .. 17.7 samples (Q16) */
    else
        base1 = 4770611u, span1 = 168u, base2 = 3499622u, span2 = 159u;   /* left 72.8 + 0 .. 168, right 53.4 + 0 .. 159 */
    w = c->w, ph = c->ph, wp = c->wp, l1 = c->l1, l2 = c->l2, m1 = c->m1, m2 = c->m2;
    width = width * 258;
    for (i = 0; i < n; i++, ph += inc) {
        int32_t x = b[i], tr = (int32_t)ins_tri(ph), v1, v2, r0, fr, g, md, sd, t1, t2;
        uint32_t d1 = base1 + span1 * (uint32_t)tr, d2 = base2 + span2 * (uint32_t)(65535 - tr);
        if (c->fill)
            c->fill--;
        else
            w += clamp(tw - w, -SL_SLOPE, SL_SLOPE);
        r0 = (int32_t)(wp - (d1 >> 16)) & (int32_t)(PCH_LEN - 1u), fr = (int32_t)((d1 >> 1) & 0x7FFFu);
        v1 = line[r0] + (((line[(r0 - 1) & (int32_t)(PCH_LEN - 1u)] - line[r0]) * fr) >> 15);
        r0 = (int32_t)(wp - (d2 >> 16)) & (int32_t)(PCH_LEN - 1u), fr = (int32_t)((d2 >> 1) & 0x7FFFu);
        v2 = line[r0] + (((line[(r0 - 1) & (int32_t)(PCH_LEN - 1u)] - line[r0]) * fr) >> 15);
        l1 += ((v1 - l1) * k) >> 15;                    /* the BBD's dark: two poles a tap */
        m1 += ((l1 - m1) * k) >> 15;
        l2 += ((v2 - l2) * k) >> 15;
        m2 += ((l2 - m2) * k) >> 15;
        t1 = m1, t2 = m2;
        if (hiss) {                                     /* two noises (a quarter), each through a resonant 2-pole
                                                         * low-pass at ~7.2 kHz (Q 2.5): its rise, its fall */
            int32_t n1, n2;
            c->nz = c->nz * 1664525u + 1013904223u;
            n1 = ((int32_t)(c->nz >> 16) - 32768) >> 2;
            c->nz = c->nz * 1664525u + 1013904223u;
            n2 = ((int32_t)(c->nz >> 16) - 32768) >> 2;
            c->h1 += (c->g1 * JH_F) >> 15;
            c->g1 += ((n1 - c->h1 - ((c->g1 * JH_Q) >> 15)) * JH_F) >> 15;
            c->h2 += (c->g2 * JH_F) >> 15;
            c->g2 += ((n2 - c->h2 - ((c->g2 * JH_Q) >> 15)) * JH_F) >> 15;
            t1 += (c->h1 * hiss) >> 20;
            t2 += (c->h2 * hiss) >> 20;
        }
        line[wp] = (int16_t)clamp(x >> 3, -32767, 32767);   /* (1.0: an eighth, was a quarter: 8 loud voices clipped) */
        wp = (wp + 1u) & (PCH_LEN - 1u);
        wq = (w * 13107) >> 11;                         /* the wet x 6.4: 1.6 x the dry (the taps at an eighth) */
        g = 32768 - ((w * PCH_DRY) >> 15);
        /* (1.0: 64-bit products: a loud chord's x passes 2^16, and x * g wrapped round: a crack at a chord change) */
        md = x + (int32_t)(((int64_t)(t1 + t2) * wq) >> 15);   /* dry + 1.6 (tap 1 + tap 2) / 2 */
        sd = (int32_t)(((int64_t)(((int64_t)(t1 - t2) * wq) >> 15) * width) >> 15);   /* 1.6 (tap 1 - tap 2) / 2 */
        b[i] = (int32_t)(((int64_t)md * g) >> 15);
        pch_side[i] = (int32_t)(((int64_t)sd * g) >> 15);
    }
    c->w = w, c->ph = ph, c->wp = (uint16_t)wp, c->l1 = l1, c->l2 = l2, c->m1 = m1, c->m2 = m2;
    if (!w && c->mode != m)
        c->mode = 0;                                    /* (faded out: the next block takes the new one) */
    return 1;
}
/* (Stallion) PONY's CHAR HPF: the high-pass after the voices' sum (on the original, after the VCAs, before the chorus),
 * not in each voice (eng_pony.c): HPF 0 a ~+10 dB shelf under ~70 Hz, 1 flat, 2 and 3 a one-pole high-pass at ~240,
 * ~760 Hz (each voice's before). b the part's block, sd SPREAD's side (or 0), each its own state */
#define JQ_SHK 186                      /* (HPF 0: a one-pole at ~40 Hz, its output x 2.16 added: +10 dB under it, */
#define JQ_SHG 70854                    /* ~+5 dB at 70 Hz, ~+2 dB at 150 Hz) */
static int32_t jq_hp[NPART][2];
static __attribute__((noinline)) void pony_hpf(track_t *t, int32_t *b, int32_t *sd, uint32_t n)
{
    int32_t *st = jq_hp[(uint32_t)(t - trk) % NPART], pos = clamp(t->p[P_E4], 0, 3), k = 0, j, i;
    if (pos == 1) {
        st[0] = st[1] = 0;
        return;
    }
    k = !pos ? JQ_SHK : pos == 2 ? 1120 : 3360;
    for (j = 0; j < 2; j++, b = sd) {
        int32_t s = st[j];
        if (!b)
            break;
        for (i = 0; i < (int32_t)n; i++) {
            s += (int32_t)(((int64_t)(b[i] - s) * k) >> 15);
            b[i] = pos ? b[i] - s : b[i] + (int32_t)(((int64_t)s * JQ_SHG) >> 15);
        }
        st[j] = s;
    }
}
#endif

/* (0.7, SLOOP 2.7's, after JIANT FM 0.6.8, github.com/juanjiant-bit/JIANT) LEVEL and PAN glide: a knob's detent (or a
 * new sound's trim) moves a quarter of the way each block of CTL samples (0.73 ms), ~3 ms in all, instead of a step a
 * detent. The last few units: at once */
static int32_t mix_glide(int32_t from, int32_t to)
{
    int32_t d = to - from;
    return d <= 8 && d >= -8 ? to : from + d / 4;
}

#if CLIPPER_SYNTH
/* Clipper 0.9, the part's duck: what cannot change under a sounding part without a crack (another filter MODEL, SLOPE,
 * FILTER 2's MODL or ROUTE: engines.c fx_want; another WAVE table, which is built in the voices' own memory, another FMX
 * patch: PTCH) waits for it: the part fades out over 2.9 ms (an S curve), holds silent while the change lands (a table
 * built: a few ms), fades in over 2.9 ms. A silent part: at once; one fading out for a sound loaded: after its fade.
 * part_pending: what waits; part_apply: lands it (FMX: fmx_block loads its patch while dk_free) */
static uint32_t part_pending(track_t *t, int32_t fmw)
{
    uint32_t k = (uint32_t)(t - trk) % NPART, w = 0;
    if (!t->fm_eff)
        t->fm_eff = (uint8_t)fmw;
    if (!t->fx_eff)
        t->fx_eff = (uint8_t)fx_want(t);
    w |= fmw != t->fm_eff || fx_want(t) != t->fx_eff;
    if (ENGINES[t->engine % NENGINES] == &ENG_WAVE) {
        int16_t tab = (int16_t)((uint32_t)t->p[P_E0] % WT_NALL + 1u);
        if (!wt_tab_eff[k])
            wt_tab_eff[k] = tab;
        w |= (uint32_t)(wt_tab_eff[k] != tab) << 1;
    }
    if (ENGINES[t->engine % NENGINES] == &ENG_FMX && fmx_slot_seen[k] &&
        fmx_slot_seen[k] != clamp(t->p[P_E0], 0, (int32_t)FMX_NSLOT - 1) + 1 && !((fmx_reload >> k) & 1u))
        w |= 4u;
    return w;
}
static void part_apply(track_t *t, int32_t fmw)
{
    uint32_t k = (uint32_t)(t - trk) % NPART;
    if (t->fm_eff && t->fm_eff != fmw && ((1u << t->engine % 32u) & (1u << ENGI_VECTOR | 1u << ENGI_PONY))) {   /* (and
                                                         * PONY's, its MODL since Stallion: the same) */
        uint32_t i;                                     /* (after Clipper 1.2) VECTOR keeps one set of filter states for
                                                         * every MODEL (eng_vector.c s[0..3]): a ladder's (x 4096) read by
                                                         * an SVF burst at full scale; cleared while the part is silent.
                                                         * test-all: s[0..3] and fs[0..3], SLOPE 48's last stages; part_duck
                                                         * holds the part silent while the new filter settles) */
        for (i = 0; i < NVOICE; i++) {
            t->v[i].s[0] = t->v[i].s[1] = t->v[i].s[2] = t->v[i].s[3] = 0;
            t->v[i].fs[0] = t->v[i].fs[1] = t->v[i].fs[2] = t->v[i].fs[3] = 0;
        }
    }
    t->fm_eff = (uint8_t)fmw;
    t->fx_eff = (uint8_t)fx_want(t);
    if (ENGINES[t->engine % NENGINES] == &ENG_WAVE)
        wt_tab_eff[k] = (int16_t)((uint32_t)t->p[P_E0] % WT_NALL + 1u);
    else
        wt_tab_eff[k] = 0;
}
static void part_duck(track_t *t, int32_t *b, int32_t *b2, uint32_t n, int32_t fmw)   /* (1.0: b2, SPREAD's side, too) */
{
    uint32_t k;
    if (t->dk_phase == 2u) {                            /* held silent until the change has landed (50 ms at most) */
        uint32_t p = (uint32_t)(t - trk) % NPART;
        int ready = !HOT_FAR(part_pending)(t, fmw) && (ENGINES[t->engine % NENGINES] != &ENG_WAVE || wt_have == wt_tab_eff[p] - 1) &&
                    (ENGINES[t->engine % NENGINES] != &ENG_VECTOR || t->dk_hold >= 12u) &&   /* (after Clipper 1.3)
                                                         * VECTOR's filter starts from rest (part_apply): under the chord
                                                         * it rings up to twice its level for a few ms (DIODE -> LADDR at
                                                         * CUT 20: 1461, steady 767), heard in the fade-in; held 12 blocks
                                                         * (8.7 ms) more, it has settled */
                    (ENGINES[t->engine % NENGINES] != &ENG_FMX || t->dk_hold >= 16u);   /* (Stallion) FMX: the
                                                         * held voices' operator envelopes keep the old patch's levels and
                                                         * slew to the new patch's sustains at its decay rates: a bright
                                                         * blip 10 .. 15 ms after the patch lands (clean_test PTCH 5 -> 6:
                                                         * 699, steady 103; held 8 blocks: 656); held 16 blocks (11.6 ms)
                                                         * more, silent, it has settled: 392, steady 131 */
        for (k = 0; k < n; k++)
            b[k] = 0;
        if (b2)
            for (k = 0; k < n; k++)
                b2[k] = 0;
        if (ready || ++t->dk_hold > 68u)
            t->dk_phase = 3, t->dk_pos = 0, t->dk_hold = 0;
        return;
    }
    for (k = 0; k < n; k++) {
        int32_t x = t->dk_phase == 1u ? 32768 - t->dk_pos * 256 : t->dk_pos * 256, x2 = (x * x) >> 15;
        int32_t g = 3 * x2 - 2 * ((x2 * x) >> 15);      /* smoothstep, Q15 */
        if (t->dk_pos >= 128u) {
            if (t->dk_phase == 1u) {
                b[k] = 0;
                if (b2)
                    b2[k] = 0;
            }
            continue;
        }
        b[k] = (int32_t)(((int64_t)b[k] * g) >> 15);
        if (b2)
            b2[k] = (int32_t)(((int64_t)b2[k] * g) >> 15);
        t->dk_pos++;
    }
    if (t->dk_pos >= 128u) {
        t->dk_phase = t->dk_phase == 1u ? 2u : 0u;      /* (out: held from the next block, the change lands there) */
        t->dk_pos = 0, t->dk_hold = 0;
    }
}
#endif

/* one synth part into the dry mix and the sends; a part with no voice sounding costs
 * the LFO tick and a cleared buffer only (after the DIST tail has run out) */
static void mix_part1(track_t *t, uint32_t n)
{
    int32_t *b = part_buf;
    uint32_t i, fresh = !t->tail;                       /* (0.9) the part was silent: a change takes effect at once */
    int32_t g0 = 32767 - t->att, g1 = HOT_FAR(gain_next)(t);   /* (Stallion: HOT_FAR, hal/fm1_hot.h: mix_part1 runs
                                                         * from RAM, the rare calls it makes to the flash through it) */
#if CLIPPER_SYNTH
    int32_t fmw = clamp(t->p[P_FMODEL], FM_SVF, FM_LAST);
    if (HOT_FAR(part_pending)(t, fmw)) {                /* (0.9) a change that would crack the sound: the duck first
                                                         * (Stallion: in the flash, a call a block: the RAM code area
                                                         * full with PONY's CHAR, tools/build.py HOT) */
        if (fresh)
            HOT_FAR(part_apply)(t, fmw);                /* (silent: at once) */
        else if (t->xf_on)
            ;                                           /* (a sound loaded: the old one fades out as it was, the change
                                                         * lands after, the part silent) */
        else if (!t->dk_phase)
            t->dk_phase = 1, t->dk_pos = 0;
        else if (t->dk_phase == 3u)
            t->dk_phase = 1, t->dk_pos = (uint8_t)(128u - t->dk_pos);   /* (from where the fade-in got to) */
    }
    t->dk_free = (uint8_t)(fresh || t->dk_phase == 2u);
    if (t->dk_phase == 2u)
        HOT_FAR(part_apply)(t, fmw);
    if (track_render(t, b, n))
        t->tail = t->p[P_ITYPE] ? 64 : 16, t->ins_xt = 0;  /* blocks of DIST / INSERT state to run out after the last
                                                         * voice (the INSERT's: 46 ms, a flanger's feedback) */
    else if (ins_ringing(t))
        t->tail = t->tail ? t->tail : 1u;               /* (1.1) the DELAY's echoes: the part runs on (not fresh) */
    else if ((!t->tail || !(t->p[P_DIST] | t->p[P_ITYPE] | jc[(uint32_t)(t - trk) % NPART].w) || !--t->tail) &&
             !slicer_busy(t) &&
             !(t->ins_run && t->ins_res > 16u && t->ins_xt < 255u && ++t->ins_xt) &&   /* (the INSERT's DC blocker
                                                         * run out first, up to 255 blocks more: no step) */
             !pch_hiss(t)) {                             /* (Stallion: PONY's chorus hiss runs on in silence) */
        if (t->ins_run && !t->p[P_ITYPE])
            HOT_FAR(ins_rest)(t);                       /* TYPE OFF while silent: no fade-out left for the next note */
        pch_idle(t);                                     /* (0.8: the PONY chorus the same) */
        HOT_FAR(part_apply)(t, fmw);                    /* (0.9: silent: every change at once) */
        t->dk_phase = 0, t->dk_pos = 0;
        HOT_FAR(slicer_track)(t, 0, n);
        return;
    }
#else
    if (track_render(t, b, n))
        t->tail = 16;                                   /* blocks of DIST state to run out after the last voice */
    else if ((!t->tail || !t->p[P_DIST] || !--t->tail) && !slicer_busy(t)) {
        slicer_track(t, 0, n);                          /* (the SLICER's step clock runs on) */
        return;
    }
#endif
#if CLIPPER_SYNTH
    if (t->dk_phase)
        part_duck(t, b, part_side_on ? part_side : 0, n, fmw);
#endif
    if (!g0 && !g1) {                                   /* silent (MUTE / SOLO): the voices run, nothing is heard */
        HOT_FAR(slicer_track)(t, 0, n);
        return;
    }
    {
        int32_t lvl = LEVEL_Q12[t->p[P_LEVEL] ? clamp(t->p[P_LEVEL] + t->p[P_ED_FX], 1, (int32_t)NELEM(LEVEL_Q12) - 1) : 0],
                pan = t->p[P_PAN];                      /* (+ the sound's trim: 1/2 dB steps, as LEVEL's; past LEVEL 127's
                                                         * +7.5 dB for a quiet preset, Stallion: up to 190, +39 dB) */
#if CLIPPER_SYNTH
        {   /* (1.0) the expression (CC 11): x (cc + 1)^2 / 128^2, 127 unchanged */
            int32_t ex = 128 - part_ctl[ctl_part(t)].exoff;
            lvl = (int32_t)(((int64_t)lvl * (ex * ex)) >> 14);   /* (64 bits: a trim past LEVEL 127) */
        }
        pan = clamp(pan + t->panmod, -64, 64);         /* Clipper 0.4: + the matrix's PAN (voice.c) */
#endif
        int32_t gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
        int32_t c = t->p[P_CHOR] * 258, d = t->p[P_DLY] * 258, r = t->p[P_REV] * 258, pk = t->peak;
#if CLIPPER_SYNTH
        int32_t sa = 0;                                 /* (1.0) |the part| before DIST and the INSERT (SPREAD's side
                                                         * follows their level: they run on the mid) */
        {   /* (1.0) SPACE (voice.c mac_glide): more reverb and delay; + the matrix's DLY, REV, CHO (voice.c) */
            int32_t sp = mac_st[(uint32_t)(t - trk) % NTRK].q8[3] >> 8;
            r = clamp(t->p[P_REV] + sp + t->revmod, 0, 127) * 258, d = clamp(t->p[P_DLY] + sp / 2 + t->dlymod, 0, 127) * 258;
            c = clamp(t->p[P_CHOR] + t->chomod, 0, 127) * 258;
        }
#endif
        int32_t xmax, c0, d0, r0, dsc, dsd, dsr;
        int32_t ga = mulq15(g0, duck.g0), gb = mulq15(g1, duck.g1);   /* mute x duck, ramped over the block */
        if (!t->mix_on || fresh)                        /* (0.9) the sends glide as LEVEL does (a jump: a crack in the
                                                         * chorus's and the delay's wet) */
            t->snd_c = c, t->snd_d = d, t->snd_r = r;
        c0 = t->snd_c, d0 = t->snd_d, r0 = t->snd_r;
        t->snd_c = mix_glide(c0, c), t->snd_d = mix_glide(d0, d), t->snd_r = mix_glide(r0, r);
        dsc = t->snd_c - c0, dsd = t->snd_d - d0, dsr = t->snd_r - r0;
        xmax = c0 > d0 ? c0 : d0;
        xmax = xmax > r0 ? xmax : r0;
        xmax = xmax > c ? xmax : c, xmax = xmax > d ? xmax : d, xmax = xmax > r ? xmax : r;
        xmax = 0x7FFFFFFF / (xmax | 1);                 /* sends: loud chords at a high LEVEL */
#if CLIPPER_SYNTH
        if (t->engine == ENGI_PONY && (t->p[P_JCHR] & JQ_HPF))   /* (Stallion) PONY's CHAR HPF: after the voices' sum */
            HOT_FAR(pony_hpf)(t, b, part_side_on ? part_side : 0, n);
        if (part_side_on && t->p[P_ITYPE] < IT_SDLY && (t->p[P_DIST] | t->dist_w | t->p[P_ITYPE] | t->ins_run))
            for (i = 0; i < n; i++)
                sa += b[i] < 0 ? -(b[i] >> 6) : b[i] >> 6;
#endif
        track_dist(t, b, n, fresh);
#if CLIPPER_SYNTH
        if (t->p[P_ITYPE] | t->ins_run) {
            track_insert(t, b, n, fresh);               /* the INSERT (0.6): off, nothing */
            t->ins_res = (uint16_t)(b[n - 1u] < 0 ? (-b[n - 1u] > 65535 ? 65535 : -b[n - 1u])
                                                  : (b[n - 1u] > 65535 ? 65535 : b[n - 1u]));   /* (its tail's end) */
        }
#endif
        HOT_FAR(slicer_track)(t, b, n);                 /* slicer.c: before the level, pan and sends */
        {
            tsvf_t fc;                                  /* the track's FILTER (P_TFLT), after the SLICER */
            djf_t *f = &tflt[(uint32_t)(t - trk) % NTRK];
            if (djf_block(f, t->p[P_TFLT], &fc)) {
                tflt_run(f, &fc, b, n, 0);
#if CLIPPER_SYNTH
                if (part_side_on)                       /* (1.0) SPREAD's side through the same filter (its own state) */
                    tflt_run(f, &fc, part_side, n, 1);
#endif
            }
        }
#if CLIPPER_SYNTH
        if (part_side_on && sa) {                       /* (1.0) the side as loud as DIST / the INSERT left the mid */
            int32_t sb = 0, g, g0s = t->sd_g ? t->sd_g : 32768;
            for (i = 0; i < n; i++)
                sb += b[i] < 0 ? -(b[i] >> 6) : b[i] >> 6;
            while (sb > 65535 || sa > 65535)            /* (32 bits: no 64-bit divide) */
                sb >>= 1, sa >>= 1;
            g = (int32_t)(((uint32_t)sb << 15) / (uint32_t)(sa + 1));
            g = clamp(g, 0, 65535);
            t->sd_g = g;
            for (i = 0; i < n; i++) {
                int32_t gi = g0s + (((g - g0s) * (int32_t)i) >> CTL_LOG2);
                part_side[i] = (int32_t)(((int64_t)part_side[i] * gi) >> 15);
            }
        } else {
            t->sd_g = 0;
        }
        const int32_t *side = pony_chorus(t, b, n) ? pch_side : 0;   /* (0.8) the PONY chorus: stereo */
        if (part_side_on) {                             /* (1.0) + SPREAD's */
            if (side)
                for (i = 0; i < n; i++)
                    part_side[i] += pch_side[i];
            side = part_side;
        }
#endif
        int32_t lvl0, dl, gl0, dgl, gr0, dgr, bmx;          /* (0.7, after SLOOP 2.7) LEVEL (and a new sound's trim) and PAN
                                                         * glide (mix_glide), ramped sample by sample inside the block */
        if (!t->mix_on)
            t->lvl = lvl, t->gpl = gl, t->gpr = gr, t->mix_on = 1;
        lvl0 = t->lvl, gl0 = t->gpl, gr0 = t->gpr;
        t->lvl = mix_glide(lvl0, lvl), t->gpl = mix_glide(gl0, gl), t->gpr = mix_glide(gr0, gr);
        dl = (t->lvl - lvl0) >> CTL_LOG2, dgl = t->gpl - gl0, dgr = t->gpr - gr0;
        {   /* (Stallion) the part's bound: x LEVEL 127 (9713) as before; a trim past it, a tighter one (the same bound
             * after the gain, 32 bits: far into the limiter either way) */
            int32_t lvm = lvl0 > t->lvl ? lvl0 : t->lvl;
#if NVOICE > 8 && QUIET_MANY
            bmx = lvm > 9713 ? (int32_t)((0x7FFFFFFFu / (uint32_t)lvm) << 3) : PART_MAX * 2;
#else
            bmx = lvm > 9713 ? (int32_t)((0x7FFFFFFFu / (uint32_t)lvm) << 2) : PART_MAX;
#endif
        }
        for (i = 0; i < n; i++) {
            int32_t lv = lvl0 + dl * (int32_t)i;       /* (SLOOP Pro #4: rounded, all of b, in 32 bits as the >> 3 / >> 2
                                                         * first were: they floored the part to 2..9 LSB) */
#if NVOICE > 8 && QUIET_MANY                            /* Clipper: 12 / 16 loud voices: one more bit of headroom */
            int32_t bi = clamp(b[i], -bmx, bmx), x = MULQ12R(bi, lv, 3), a;
#else
            int32_t bi = clamp(b[i], -bmx, bmx), x = MULQ12R(bi, lv, 2), a;   /* (pre-shift: 8 loud voices,
                                                         * 1.0: bounded: x LEVEL 127 within 32 bits) */
#endif
            int32_t xs, g = ga + (((gb - ga) * (int32_t)i) >> CTL_LOG2);
            if (g < 32767) {                            /* (always, at rest: mulq15(32767, 32767) is 32766) */
                g >>= 3;                                /* (Q12: no 32-bit overflow) */
                x = MULQ12R(x, g, 4);
            }
            a = x < 0 ? -x : x;
            xs = clamp(x, -xmax, xmax);                 /* sends: mulq15 would overflow */
            if (a > pk)
                pk = a;
            c = c0 + ((dsc * (int32_t)i) >> CTL_LOG2), d = d0 + ((dsd * (int32_t)i) >> CTL_LOG2);
            r = r0 + ((dsr * (int32_t)i) >> CTL_LOG2);
            if (c)
                send_c[i] += mulq15(xs, c);
            if (d)
                send_d[i] += mulq15(xs, d);
            if (r)
                send_r[i] += mulq15(xs, r);
            gl = gl0 + ((dgl * (int32_t)i) >> CTL_LOG2), gr = gr0 + ((dgr * (int32_t)i) >> CTL_LOG2);   /* (0.7) */
#if CLIPPER_SYNTH
            if (side) {                                 /* the chorus's left - right, through the same level */
                int32_t si = clamp(side[i], -bmx, bmx);   /* (Stallion: bounded as b) */
#if NVOICE > 8 && QUIET_MANY
                int32_t sd = MULQ12R(si, lv, 3);
#else
                int32_t sd = MULQ12R(si, lv, 2);
#endif
                if (g < 32767)
                    sd = MULQ12R(sd, g, 4);             /* (g: Q12 above) */
                mix_l[i] += ((x + sd) * (gl >> 4) + 128) >> 8;
                mix_r[i] += ((x - sd) * (gr >> 4) + 128) >> 8;
                continue;
            }
#endif
            mix_l[i] += (x * (gl >> 4) + 128) >> 8;     /* (x may pass 2^19: PAN's Q12 to Q8, exact at rest: 64 a step;
                                                         * #4: x >> 4 floored the part to 16 LSB) */
            mix_r[i] += (x * (gr >> 4) + 128) >> 8;
        }
        t->peak = pk;
    }
}

/* Clipper 0.3: a sound fading out after a change renders, mixes and sends with its own values (voice.c
 * sound_fade_begin) */
static void mix_part(track_t *t, uint32_t n)
{
#if CLIPPER_SYNTH
    if (t->xf_snd && t->xf_on && t->xf) {
        int16_t keep[P_E0];
        memcpy(keep, t->p, sizeof keep);
        memcpy(t->p, t->p_old, sizeof keep);
        HOT_FAR(mix_part1)(t, n);                       /* (Stallion: in RAM, hal/fm1_hot.h) */
        memcpy(t->p, keep, sizeof keep);
        return;
    }
#endif
    HOT_FAR(mix_part1)(t, n);
}

/* ---- DUST: the master through an old sampler and a record. G_DUST 0..127 turns up together: drive
 * into a soft clip, a lower sample rate (held samples, 44.1 -> 11 kHz), fewer bits (15 -> 8), a
 * one-pole low-pass (open -> ~3 kHz), a little hiss and crackle. The hiss and the crackle are the
 * record turning: they fade in with PLAY and out (~0.1 s) at STOP, so a stopped SLOOP is silent.
 * Stereo, ~25 ops a sample. */
static struct {
    int32_t hl, hr, hn;                                 /* held samples, samples left to hold */
    int32_t ll, lr;                                     /* low-pass states */
    int32_t rnd, click;                                 /* noise state, a crackle decaying */
    int32_t bed;                                        /* hiss / crackle level, Q15: 0 stopped, 32767 playing */
} dust = {0, 0, 0, 0, 0, 0x2545F491, 0, 0};

static int32_t crush_bits(int32_t v, int32_t shift)    /* fewer bits, rounded toward 0: no DC from tails */
{
    return v >= 0 ? (v >> shift) << shift : -((-v >> shift) << shift);
}

static void dust_process(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t d = song.g[G_DUST], hold, shift, a, drive, hiss, i, bed0, bed1;
    uint32_t pc;
    if (!d) {
        dust.bed = 0;
        return;
    }
    hold = 1 + d * 3 / 127;
    shift = d / 18;
    a = 32767 - d * 165;                                /* one-pole coefficient, Q15 */
    drive = 4096 + d * 24;                              /* Q12: 1x .. 1.75x */
    hiss = d * 2;
    pc = (uint32_t)d * 7u;                              /* crackle: chance per sample, x 2^-22 */
    bed0 = dust.bed;                                    /* ~0.1 s from 0 to full (238 a block of 32) */
    bed1 = dust.bed = clamp(dust.bed + (song.playing ? 238 : -238), 0, 32767);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = l[i], y = r[i];
        int32_t bed = bed0 + (((bed1 - bed0) * i) >> CTL_LOG2);
        uint32_t nz = noise32(&dust.rnd);
        if (--dust.hn <= 0) {                           /* sample and hold, then the bits */
            dust.hn = hold;
            dust.hl = softclip(((x >> 2) * drive) >> 10);
            dust.hr = softclip(((y >> 2) * drive) >> 10);
            if (shift) {
                dust.hl = crush_bits(dust.hl, shift);
                dust.hr = crush_bits(dust.hr, shift);
            }
        }
        dust.ll += mulq15(dust.hl - dust.ll, a);
        dust.lr += mulq15(dust.hr - dust.lr, a);
        if ((nz >> 10) < pc)                            /* a speck of dust */
            dust.click = mulq15(((int32_t)(nz & 0x3FFu) - 512) * d / 8, bed);
        x = dust.ll + dust.click + mulq15(((int32_t)(nz >> 16) - 32768) * hiss >> 15, bed);
        y = dust.lr + dust.click + mulq15(((int32_t)(nz & 0xFFFFu) - 32768) * hiss >> 15, bed);
        dust.click -= dust.click >> 2;
        l[i] = x;
        r[i] = y;
    }
}

static void djf_process(int32_t *l, int32_t *r, uint32_t n)
{
    tsvf_t c;
    if (!HOT_FAR(djf_block)(&djf, song.g[G_FILT], &c))   /* (Stallion: in RAM, hal/fm1_hot.h) */
        return;
    HOT_FAR(djf_run)(&djf, &c, l, n, 0);
    HOT_FAR(djf_run)(&djf, &c, r, n, 1);
}

#include "punch.c"            /* PUNCH-IN FX on the whole mix (FX held + a white key) */
#if CLIPPER_SYNTH
#include "bus.c"              /* Clipper 0.3: the master EQ and compressor */
#endif
static int32_t master_cur = -1;                        /* the volume knob, ramped per sample (no zipper) */
static void mix_block(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t m0, m1;
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    HOT_FAR(events_block)(n);                           /* (Stallion: the mix runs from RAM, the sequencer from the flash:
                                                         * hal/fm1_hot.h) */
    duck_block(n * (uint32_t)song.g[G_BPM]);
    for (i = 0; i < NPART; i++)
        mix_part(&trk[i], n);
#if !CLIPPER_SYNTH || BOARD_SMK37                                  /* (Clipper 1.2: no drum machine: nothing to render; its sounds are
                                                     * out of the image) */
    drums.a0 = TDRUM->att;                              /* the drum track's mute / solo fade */
    drums.a1 = 32767 - HOT_FAR(gain_next)(TDRUM);
    {
        static int32_t dl[CTL], dr[CTL], dv[CTL], dd[CTL];
        tsvf_t fc;
        djf_t *f = &tflt[TRK_DRUM];
        if (n <= CTL && HOT_FAR(djf_block)(f, TDRUM->p[P_TFLT], &fc)) {   /* the drum track's FILTER: left, right, the sends */
            for (i = 0; i < n; i++)
                dl[i] = dr[i] = dv[i] = dd[i] = 0;
            HOT_FAR(slicer_drums)(dl, dr, dv, dd, n);
            HOT_FAR(tflt_run)(f, &fc, dl, n, 0);       /* (Stallion: in RAM, hal/fm1_hot.h) */
            HOT_FAR(tflt_run)(f, &fc, dr, n, 1);
            HOT_FAR(tflt_run)(f, &fc, dv, n, 2);
            HOT_FAR(tflt_run)(f, &fc, dd, n, 3);
            for (i = 0; i < n; i++) {
                mix_l[i] += dl[i];
                mix_r[i] += dr[i];
                send_r[i] += dv[i];
                send_d[i] += dd[i];
            }
        } else {
            HOT_FAR(slicer_drums)(mix_l, mix_r, send_r, send_d, n);   /* drums_render, through the SLICER when on */
        }
    }
#endif
    click_render(mix_l, mix_r, n);                      /* the metronome: its own voice (drums.c), any kit, any mute */
    fx_buses(send_c, send_d, send_r, wet_l, wet_r, n);
    for (i = 0; i < n; i++) {
        mix_l[i] += wet_l[i];
        mix_r[i] += wet_r[i];
    }
    dust_process(mix_l, mix_r, n);
    punch_process(mix_l, mix_r, n);
    djf_process(mix_l, mix_r, n);
#if CLIPPER_SYNTH
    bus_process(mix_l, mix_r, n);
#endif
    m1 = (int32_t)song.master_q12;
    m0 = master_cur < 0 ? m1 : master_cur;
    master_cur = m1;
    if (n > CTL)
        n = CTL;                                        /* (the buffers' size) */
    {
        int32_t *ol = wet_l, *orr = wet_r, *ul = send_d, *ur = send_r;   /* (the buses' buffers: free after fx_buses) */
        static uint8_t usb_was;
        for (i = 0; i < n; i++) {
            int32_t m = m0 + (((m1 - m0) * (int32_t)i) >> CTL_LOG2);
            int32_t xl = clamp(mix_l[i], -MIX_MAX, MIX_MAX), xr = clamp(mix_r[i], -MIX_MAX, MIX_MAX);
            /* x MASTER / 4096 as MULQ12R(x, m, 2) (1.0: bounded first: in 32 bits; #4: rounded): sl, sr at 2^-10 LSB
             * (+ a half of Q15), the Q15 sample rounded from them and its OUT_FRAC bits under it (-64 .. 64) */
            int32_t sl = (xl >> 2) * m + (((xl & 3) * m + 2) >> 2) + 512;
            int32_t sr = (xr >> 2) * m + (((xr & 3) * m + 2) >> 2) + 512;
            ol[i] = sl >> 10;
            orr[i] = sr >> 10;
            out_lo[0][i] = (int8_t)(((sl & 1023) - 508) >> 3);
            out_lo[1][i] = (int8_t)(((sr & 1023) - 508) >> 3);
            master_pre(&ol[i], &orr[i]);
            if ((i & 1u)) {                             /* the visualiser: every other sample, as audio.c reads it */
                vis_tap[2u * i] = (int16_t)knee(mix_l[i]);
                vis_tap[2u * i + 1u] = (int16_t)knee(mix_r[i]);
            }
            if (usb_full_now) {                         /* USB AUDIO = FULL: MASTER all the way up (4096) */
                ul[i] = mix_l[i], ur[i] = mix_r[i];     /* (#4: x 4096 / 4096, every bit; it was floored to 4 LSB) */
                master_pre_usb(&ul[i], &ur[i]);
            }
        }
        lim_block(&lim_dac, ol, orr, out_lo[0], out_lo[1], n);
        for (i = 0; i < n; i++) {
            out[2u * i] = knee(ol[i]);
            out[2u * i + 1u] = knee(orr[i]);
        }
        if (usb_full_now && !usb_was) {                 /* (a new recording: no old block in its delay) */
            memset(&lim_usb, 0, sizeof lim_usb);
            lim_usb.env = LIM_T, lim_usb.g = 32768, lim_usb.hold = 0;
        }
        usb_was = usb_full_now;
        if (usb_full_now) {
            lim_block(&lim_usb, ul, ur, 0, 0, n);
            for (i = 0; i < n; i++) {
                usb_out[2u * i] = knee(ul[i]);
                usb_out[2u * i + 1u] = knee(ur[i]);
            }
        }
    }
}
