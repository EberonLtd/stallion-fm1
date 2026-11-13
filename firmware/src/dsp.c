/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Changes: Copyright (C) 2026 Eberon Limited (John Gorman) */
/* Shared DSP building blocks for the Felucca engines (all fixed point).
 * Voice output convention: add sample * amp to out[], where a full-scale
 * oscillator at amp = 1.0 (Q15 32767) contributes VOICE_FS. */
#define VOICE_FS 24000           /* per-voice level: one voice peaks near -6 dBFS before the master */

static inline int32_t mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
/* a * k / 65536 without 64-bit: a up to 2^31, k Q16 */
static inline int32_t mulq16(int32_t a, uint32_t k)
{
    return (int32_t)(((a >> 16) * (int32_t)k) + (int32_t)(((uint32_t)(a & 0xFFFF) * k) >> 16));
}
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
/* (Stallion) a struct's copy in words, inline: what runs from RAM (hal/fm1_hot.h) cannot call the flash's memcpy, which
 * a struct assignment compiles to (byte by byte, libc.c). n: bytes, a multiple of 4; d and s word-aligned */
typedef uint32_t __attribute__((may_alias)) word_alias_t;
static inline __attribute__((always_inline)) void copy32(void *d, const void *s, uint32_t n)
{
    word_alias_t *dw = d;
    const word_alias_t *sw = s;
    for (n >>= 2; n; n--)
        *dw++ = *sw++;
}
/* x * g / 4096 rounded to the nearest, in 32 bits wherever ((x >> k) * g) >> (12 - k) fits (the same bound): the k low
 * bits of x go in apart instead of being dropped. The mix's gains (fx.c: a part's LEVEL, mute / duck, the MASTER, DIST's
 * drive) took x >> k first and lost them: a floor to steps of 2^k x the gain (a part's sum to 16 LSB of the output at
 * MASTER 4096), signal-correlated distortion that stays as loud while a note fades out (SLOOP Pro #4). k, a constant:
 * ~6 ops */
#define MULQ12R(x, g, k) \
    ((((x) >> (k)) * (g) + ((((x) & ((1 << (k)) - 1)) * (g) + (1 << ((k) - 1))) >> (k)) + (1 << (11 - (k)))) >> (12 - (k)))

/* sine, linearly interpolated between the 1024 table points (plain lookup: THD -55 dB). SINE has 1025 points
 * (SINE[1024] = SINE[0] = 0, tools/gen_tables.py): i + 1 needs no wrap, bit for bit as the masked index was */
static inline int32_t sine_i(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = SINE[i], b = SINE[i + 1u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}
static inline int32_t osc_sine(uint32_t ph) { return sine_i(ph); }

/* polyBLEP residual (Q15) around a wrap of a phase accumulator */
static inline int32_t blep(uint32_t ph, uint32_t inc)
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d)
        return 0;
    if (ph < inc) {
        x = (int32_t)(ph / d);                       /* 0..32767 */
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -(int32_t)((0xFFFFFFFFu - ph) / d);       /* -32767..0 */
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}

static inline int32_t osc_saw(uint32_t ph, uint32_t inc)
{
    return ((int32_t)(ph >> 16) - 32768) - blep(ph, inc);
}

static inline int32_t osc_pulse(uint32_t ph, uint32_t inc, uint32_t pw)
{
    return (osc_saw(ph, inc) - osc_saw(ph + pw, inc)) >> 1;
}

static inline int32_t osc_tri(uint32_t ph)
{
    int32_t s = (int32_t)(ph >> 15);                 /* 0..131071 */
    return s < 65536 ? s - 32768 : 98303 - s;
}

/* Clipper 0.2: a band-limited triangle. Its corners (the slope turning at phase 0 and 1/2) get the polyBLAMP
 * residual (the integral of polyBLEP's: (1 - x)^3 / 6 on the two samples around a corner, x their distance from
 * it), scaled by the change of slope, 8 x 32768 x inc / 2^32 a sample. The naive one folds its partials back at
 * -12 dB an octave, audible from the 4th octave up; this one is as clean as the polyBLEP saw. */
static inline int32_t blamp_res(uint32_t dist, uint32_t d)       /* (1 - x)^3 / 6 Q15, x = dist / (d << 15) */
{
    int32_t c = 32768 - (int32_t)(dist / d);                     /* 1 - x, Q15 */
    c = (((c * c) >> 15) * c) >> 15;
    return (c * 5461) >> 15;                                     /* / 6 */
}
static inline int32_t osc_tri_bl(uint32_t ph, uint32_t inc)
{
    int32_t s = osc_tri(ph), D = (int32_t)(inc >> 14);           /* the change of slope at a corner */
    uint32_t d = inc >> 15, q;
    if (!d)
        return s;
    q = ph;                                                      /* the bottom corner (slope up: + D) */
    if (q < inc)
        s += (D * blamp_res(q, d)) >> 15;
    else if (~q < inc)
        s += (D * blamp_res(~q, d)) >> 15;
    q = ph - 0x80000000u;                                        /* the top corner (slope down: - D) */
    if (q < inc)
        s -= (D * blamp_res(q, d)) >> 15;
    else if (~q < inc)
        s -= (D * blamp_res(~q, d)) >> 15;
    return s;
}

/* (1.1) the same oscillators with their band-limiting out of line: blep and osc_tri_bl's corners only change the
 * samples around a wrap or a corner (a divide each), but inlined they put that code, for every wave and oscillator,
 * in the sample loop. Here the loop keeps only the compares that find those samples; the samples themselves call
 * blep or osc_tri_bl, so the result is theirs exactly. ANALOG renders with them (eng_analog.c); the others keep
 * blep, osc_saw ... as they are */
static __attribute__((noinline)) int32_t blep_far(uint32_t ph, uint32_t inc) { return blep(ph, inc); }
static __attribute__((noinline)) int32_t osc_tri_bl_far(uint32_t ph, uint32_t inc) { return osc_tri_bl(ph, inc); }
static inline int32_t osc_saw_o(uint32_t ph, uint32_t inc)
{
    int32_t s = (int32_t)(ph >> 16) - 32768;
    return ph < inc || ph > 0xFFFFFFFFu - inc ? s - blep_far(ph, inc) : s;
}
static inline int32_t osc_pulse_o(uint32_t ph, uint32_t inc, uint32_t pw)
{
    return (osc_saw_o(ph, inc) - osc_saw_o(ph + pw, inc)) >> 1;
}
static inline int32_t osc_tri_bl_o(uint32_t ph, uint32_t inc)
{
    uint32_t q = ph - 0x80000000u;
    return ph < inc || ~ph < inc || q < inc || ~q < inc ? osc_tri_bl_far(ph, inc) : osc_tri(ph);
}

/* tanh(x / 32768) * 32767, linearly interpolated (a plain lookup gives
 * 256-unit steps, audible on quiet signals) */
static inline int32_t softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = TANH_Q15[256];                           /* continuous with the table */
    else
        y = TANH_Q15[i] + (((TANH_Q15[i + 1] - TANH_Q15[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}

static inline uint32_t noise32(int32_t *st)
{
    uint32_t s = (uint32_t)*st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = (int32_t)s;
    return s;
}

/* Trapezoidal SVF (A. Simper), unconditionally stable. Coefficients per
 * block in Q13; signals stay within +-150000 so products fit in 32 bits. */
typedef struct { int32_t a1, a2, a3; } tsvf_t;
static inline void tsvf_coef(tsvf_t *c, int32_t cut, int32_t reso)   /* cut: 0..127 << 8 */
{
    int32_t i, g, den, k = 8192 - reso * 7600 / 127;   /* damping 2.0 .. ~0.15 (Q12) */
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)                                       /* between table points: sweeps without 128 steps */
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
}

static inline int32_t tsvf_lp(const tsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    return v2;
}

/* Clipper: a four-pole transistor ladder (the Moog type), after V. Zavalishin's TPT ladder ("The Art of VA
 * Filter Design", ch. 5): four one-pole trapezoidal low-passes, the feedback solved in the same sample (zero-delay
 * feedback: the cutoff and the resonance stay right up to the top), and a tanh at the ladder's input (its
 * differential pair): a hot signal drives it warm, the resonance saturates softly, and from k = 4 it sings on its
 * own. The passband is partly kept at high resonance (the input x (1 + k / 2)), as on the later Moogs.
 * Inside: signals Q12 (no dead band even at 30 Hz, where G is 1/500), coefficients Q16, products and sums in
 * 64 bits (as FM6, PHYS), rounded; G <= 0.75 (g <= 3, 17.6 kHz): the states stay within 3 times the tanh's
 * ceiling (65536), 8e8 in Q12. */
typedef struct { int32_t G, H, G4, inv, k; } ladder_t;   /* G, 1 - G, G^4, 1 / (1 + k G^4): Q16; k: Q12 */
static inline int32_t mul_sh(int32_t a, int32_t b, int sh) { return (int32_t)(((int64_t)a * b) >> sh); }
static inline void ladder_coef(ladder_t *c, int32_t cut, int32_t res)   /* cut 0..127 << 8, res 0..127 */
{
    int32_t i, g, G, G2;
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    g = clamp(g, 1, 3 * 4096);
    G = (int32_t)(((uint32_t)g << 16) / (uint32_t)(4096 + g));   /* g / (1 + g) */
    G2 = mul_sh(G, G, 16);
    c->G = G;
    c->H = 65536 - G;
    c->G4 = mul_sh(G2, G2, 16);
    c->k = clamp(res, 0, 127) * 16900 / 127;                     /* 0 .. 4.13 */
    c->inv = (int32_t)(0xFFFFFFFFu / (uint32_t)(65536 + mul_sh(c->k, c->G4, 12)));   /* (32 bits: the FM-1 has
                                                                                      * no 64-bit divide) */
}
/* one sample: x (clamped to +-131072); s[4] the stages (Q12); tap 1: stage 2's output, the loop still closed from
 * stage 4 (SLOPE 12, as the OTA's) */
static inline int32_t ladder_tap(const ladder_t *c, int32_t x, int32_t *s, int tap)
{
    int64_t in = (int64_t)clamp(x, -131072, 131072) << 12, S, y, u;
    int32_t G = c->G, uq, v, j, out = 0;
    in += (in * c->k) >> 13;                                     /* x (1 + k / 2) */
    S = ((int64_t)G * s[0] >> 16) + s[1];
    S = ((G * S) >> 16) + s[2];
    S = ((G * S) >> 16) + s[3];
    S = (c->H * S) >> 16;
    y = ((((int64_t)c->G4 * in) >> 16) + S) * c->inv >> 16;    /* the ladder's output, solved */
    u = in - ((c->k * y) >> 12);                                 /* the ladder's input (Q12) */
    {                                                            /* tanh at 65536: on u / 2 (softclip's range), the
                                                                  * bits below a unit added back (its slope ~1 there) */
        int64_t h = u >> 13;
        if (h <= -65536 || h >= 65536)
            uq = softclip(h < 0 ? -65536 : 65536) << 13;
        else
            uq = (softclip((int32_t)h) << 13) + (int32_t)(u - (h << 13));
    }
    for (j = 0; j < 4; j++) {
        v = (int32_t)(((int64_t)G * (uq - s[j]) + 32768) >> 16);
        uq = v + s[j];
        s[j] = uq + v;
        if (j == 1)
            out = uq;
    }
    return ((tap ? out : uq) + 2048) >> 12;
}
static inline int32_t ladder_tick(const ladder_t *c, int32_t x, int32_t *s)
{
    return HOT_FAR(ladder_tap)(c, x, s, 0);         /* (Stallion: in RAM, called from the flash too: hal/fm1_hot.h) */
}

/* SLOPE 48: four poles more after a 4-pole MODEL, at its cutoff (its G), their own state. The resonance is the
 * first's: these have a fixed feedback of 1, a quarter of the way to singing (linear, the same ZDF solve): four bare
 * poles would take 12 dB off the first one's peak (G^4 = -1/4 at the cutoff), these 3.5 (2 / (1 - 1/4) against 2 at
 * DC), a bump of 2 dB two thirds of an octave under it, and above it 48 dB an octave. The passband made up
 * at the output (x 2: the states see the input's level). Signals Q12 as the ladder's, the input clamped to +-131072 */
typedef struct { int32_t G, H, G4, inv; } pole4_t;      /* Q16 */
static inline void pole4_coef(pole4_t *c, int32_t G)
{
    int32_t G2 = mul_sh(G, G, 16);
    c->G = G;
    c->H = 65536 - G;
    c->G4 = mul_sh(G2, G2, 16);
    c->inv = (int32_t)(0xFFFFFFFFu / (uint32_t)(65536 + c->G4));   /* 1 / (1 + G^4) */
}
static inline int32_t pole4_tick(const pole4_t *c, int32_t x, int32_t *s)
{
    int64_t in = (int64_t)clamp(x, -131072, 131072) << 12, S, y;
    int32_t G = c->G, uq, v, j;
    S = ((int64_t)G * s[0] >> 16) + s[1];
    S = ((G * S) >> 16) + s[2];
    S = ((G * S) >> 16) + s[3];
    S = (c->H * S) >> 16;
    y = ((((int64_t)c->G4 * in) >> 16) + S) * c->inv >> 16;
    uq = (int32_t)(in - y);
    for (j = 0; j < 4; j++) {
        v = (int32_t)(((int64_t)G * (uq - s[j]) + 32768) >> 16);
        uq = v + s[j];
        s[j] = uq + v;
    }
    return (uq + 1024) >> 11;                                    /* x 2 */
}

/* The OTA filter: the 4-pole OTA filter chip's idea (the 1980s DCO poly, in a module on its later model; the flagship
 * poly), its public datasheet (a second source's) and service notes, no code of anyone's. Four OTA stages, each a one-pole
 * low-pass whose input is a bare differential pair (a tanh of the stage's input less its output), the resonance fed
 * back from stage 4 through another OTA (it soft-clips: the self-oscillation stays round), and the input only partly
 * compensated (x (1 + k / 4), the ladder x (1 + k / 2)): the bass drops as the resonance rises, about 8 dB at the top,
 * the DCO poly's thin, glassy resonance. From k = 4 it sings (RES 127: 4.25). The same ZDF solve as the ladder; the tanh
 * on stage 1 (the stage that sees the jumps; the other three see the filtered signal: saturating them too costs
 * three more tanh a sample and a voice) and on the feedback. tap 1 gives stage 2's output with the loop still closed
 * from stage 4: the flagship poly's -12 dB/oct (the same peak, a gentler slope). Coefficients: a ladder_t. */
static inline int64_t clamp64(int64_t x, int64_t lo, int64_t hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline void ota_coef(ladder_t *c, int32_t cut, int32_t res)    /* cut 0..127 << 8, res 0..127 */
{
    ladder_coef(c, cut, 0);
    c->k = clamp(res, 0, 127) * 17400 / 127;                     /* 0 .. 4.25 */
    c->inv = (int32_t)(0xFFFFFFFFu / (uint32_t)(65536 + mul_sh(c->k, c->G4, 12)));
}
static inline int32_t ota_sat(int64_t d)        /* an OTA's tanh, its knee at 49152 (Q12 in, Q12 out) */
{
    int64_t h = d >> 12;
    int32_t y;
    if (h <= -98304 || h >= 98304) {
        y = softclip(h < 0 ? -65536 : 65536);
        return (y + (y >> 1)) << 12;
    }
    y = softclip((int32_t)((h * 21845) >> 15));                  /* x 2/3: the knee at softclip's 32768 */
    return ((y + (y >> 1)) << 12) + (int32_t)(d - (h << 12));    /* x 3/2; the bits below a unit added back */
}
static inline int32_t ota_tick(const ladder_t *c, int32_t x, int32_t *s, int tap)
{
    int64_t in = (int64_t)clamp(x, -131072, 131072) << 12, S, y;
    int32_t G = c->G, uq, v, j, out = 0;
    in += (in * c->k) >> 14;                                     /* x (1 + k / 4) */
    S = ((int64_t)G * s[0] >> 16) + s[1];
    S = ((G * S) >> 16) + s[2];
    S = ((G * S) >> 16) + s[3];
    S = (c->H * S) >> 16;
    y = ((((int64_t)c->G4 * in) >> 16) + S) * c->inv >> 16;
    uq = (int32_t)clamp64(in - (((int64_t)c->k * ota_sat(y)) >> 12), -(163840LL << 12), 163840LL << 12);
    v = (int32_t)(((int64_t)G * ota_sat(((int64_t)uq - s[0]) >> 1) + 16384) >> 15);   /* (its knee twice the
                                                                  * feedback's: the feedback sets the oscillation's level,
                                                                  * stage 1 does not drag its pitch down) */
    uq = v + s[0];
    s[0] = uq + v;
    for (j = 1; j < 4; j++) {
        v = (int32_t)(((int64_t)G * (uq - s[j]) + 32768) >> 16);
        uq = v + s[j];
        s[j] = uq + v;
        if (j == 1)
            out = uq;
    }
    return ((tap ? out : uq) + 2048) >> 12;
}

/* Clipper 0.9: the filter MODELs of FX > FILTER > MODL (P_FMODEL), for the engines with a low-pass of their own
 * (ANALOG, WAVE, VECTOR): their cutoff and resonance, another circuit. SVF and MOOG as before, bit for bit; then
 *   MS20   two one-pole low-passes (12 dB) with the feedback through a saturation, after the Korg MS-20's Sallen-Key
 *          (Zavalishin's Korg-35 idea, ZDF as the ladder): bright, a screaming resonance that bites back on loud notes
 *   DIODE  four poles, the first at half the cutoff (the diode ladder's coupled first stage, the TB-303's idea): 18 dB
 *          or so, a rubbery resonance, the input driven
 *   SEM    the state-variable low-pass driven softly, its resonance short of oscillating (the Oberheim SEM's)
 *   NOTCH BP HP  the same SVF's other outputs (the SEM's mode knob)
 *   OTA    the 4-pole OTA filter chip's four stages (ota_tick above): the bass thins as the resonance rises, a round
 *          self-oscillation (the 1980s DCO poly's and flagship poly's filter)
 * fmod_tick takes the engine's signal (full scale) and gives the filtered one at half scale, as tsvf_lp(x / 2) and
 * ladder_tick(x) / 2 did. State: sv[2] (the SVF family) and ls[4] (the ladder family; ls[8] with SLOPE 48), the
 * engine's. SLOPE (fmod_slope, after fmod_coef; 24 when not called): LADDER, OTA, DIODE at 12 (stage 2's output, the
 * loop closed from stage 4), 24 (as before, bit for bit) or 48 (pole4_tick after them, ls[4..7]); the others keep
 * their own. FM_PAIR: the MODEL filter and FILTER 2 together (fmod_pair below), for the engines' loops */
typedef struct { int32_t G, H, G1, H1, PG, inv, k; } zdf_t;   /* Q16; k Q12 */
typedef struct { int32_t s[4]; int32_t *sd; } f2st_t;   /* FILTER 2's state; SPLIT: where its next side sample goes */
typedef struct fmod {
    uint8_t model, slope;                              /* slope: FS_12 .. FS_48 */
    tsvf_t sv;
    int32_t k;                                         /* the SVF's damping (Q12) */
    ladder_t lad;
    zdf_t z;
    pole4_t x4;                                        /* (SLOPE 48) */
    uint8_t route;                                     /* (FM_PAIR) F2_SER .. F2_SPLIT */
    const struct fmod *p1, *p2;                        /* .. the MODEL filter, FILTER 2 */
    f2st_t *st;
} fmod_t;
#define FM_PAIR 15
static int32_t fmod_pair(const fmod_t *f, int32_t x, int32_t *sv, int32_t *ls);
static inline int32_t zdf_sat(int64_t u)               /* the ladder's tanh, at 65536 (Q12) */
{
    int64_t h = u >> 13;
    if (h <= -65536 || h >= 65536)
        return softclip(h < 0 ? -65536 : 65536) << 13;
    return (softclip((int32_t)h) << 13) + (int32_t)(u - (h << 13));
}
static inline int32_t fmod_g(int32_t cut)              /* the prewarped gain g (Q12), as the ladder's */
{
    int32_t i, g;
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    return clamp(g, 1, 3 * 4096);
}
static inline void fmod_coef(fmod_t *f, int32_t model, int32_t cut, int32_t res)   /* cut 0..127 << 8, res 0..127 */
{
    f->model = (uint8_t)model;
    f->slope = FS_24;
    res = clamp(res, 0, 127);
    if (model == FM_LADDER) {
        ladder_coef(&f->lad, cut, res);
    } else if (model == FM_OTA) {
        ota_coef(&f->lad, cut, res);
    } else if (model == FM_SK || model == FM_DIODE) {
        zdf_t *z = &f->z;
        int32_t g = fmod_g(cut), G, G2;
        G = (int32_t)(((uint32_t)g << 16) / (uint32_t)(4096 + g));
        z->G = G, z->H = 65536 - G;
        G2 = mul_sh(G, G, 16);
        if (model == FM_SK) {
            z->k = res * 9000 / 127;                   /* 0 .. 2.2: it sings from 2 */
            z->PG = G2;
        } else {
            int32_t g1 = g >> 1;
            z->G1 = (int32_t)(((uint32_t)g1 << 16) / (uint32_t)(4096 + g1));
            z->H1 = 65536 - z->G1;
            z->k = res * 19000 / 127;                  /* 0 .. 4.6 */
            z->PG = mul_sh(mul_sh(z->G1, G2, 16), G, 16);
        }
        z->inv = (int32_t)(0xFFFFFFFFu / (uint32_t)(65536 + mul_sh(z->k, z->PG, 12)));
    } else {
        int32_t r = model == FM_WARM ? res * 100 / 127 : res;   /* (WARM: short of oscillating) */
        tsvf_coef(&f->sv, cut, r);
        f->k = 8192 - r * 7600 / 127;
    }
}
/* (after fmod_coef) the SLOPE of the 4-pole models; the others keep 24 (their own) */
static inline void fmod_slope(fmod_t *f, int32_t slope)
{
    if (f->model != FM_LADDER && f->model != FM_OTA && f->model != FM_DIODE)
        return;
    f->slope = (uint8_t)clamp(slope, FS_12, FS_48);
    if (f->slope == FS_48)
        pole4_coef(&f->x4, f->model == FM_DIODE ? f->z.G : f->lad.G);
}
static inline int32_t fmod_tick(const fmod_t *f, int32_t x, int32_t *sv, int32_t *ls)
{
    switch (f->model) {
    case FM_PAIR:
        return fmod_pair(f, x, sv, ls);
    case FM_LADDER:
        if (f->slope == FS_24)
            return ladder_tick(&f->lad, x, ls) >> 1;
        x = HOT_FAR(ladder_tap)(&f->lad, x, ls, f->slope == FS_12);   /* (Stallion: in RAM, called from the flash too) */
        return (f->slope == FS_48 ? pole4_tick(&f->x4, x, ls + 4) : x) >> 1;
    case FM_OTA:
        x = ota_tick(&f->lad, x, ls, f->slope == FS_12);
        return (f->slope == FS_48 ? pole4_tick(&f->x4, x, ls + 4) : x) >> 1;
    case FM_SK: {                                    /* two poles, the feedback saturated */
        const zdf_t *z = &f->z;
        int64_t in = (int64_t)clamp(x, -131072, 131072) << 12, S, y;
        int32_t u, v;
        in += (in * z->k) >> 14;                       /* (a little of the passband back) */
        S = ((((int64_t)z->G * ls[0]) >> 16) * z->H >> 16) + ((int64_t)z->H * ls[1] >> 16);
        y = ((((int64_t)z->PG * in) >> 16) + S) * z->inv >> 16;
        u = (int32_t)clamp64(in - (((int64_t)z->k * zdf_sat(y)) >> 12), -(int64_t)400000000, 400000000);
        v = (int32_t)(((int64_t)z->G * (u - ls[0]) + 32768) >> 16);
        u = v + ls[0], ls[0] = u + v;
        v = (int32_t)(((int64_t)z->G * (u - ls[1]) + 32768) >> 16);
        u = v + ls[1], ls[1] = u + v;
        return (u + 4096) >> 13;
    }
    case FM_DIODE: {                                   /* four poles, the first at half the cutoff */
        const zdf_t *z = &f->z;
        int64_t in = (int64_t)clamp(x, -131072, 131072) << 12, S, T, y;
        int32_t u, v, j, o = 0;
        in += (in * z->k) >> 13;
        T = (((int64_t)z->G * ls[1]) >> 16) + ls[2];
        T = ((z->G * T) >> 16) + ls[3];
        T = (z->H * T) >> 16;
        S = mul_sh(mul_sh(mul_sh(z->G, z->G, 16), z->G, 16), z->H1, 16);
        S = ((S * (int64_t)ls[0]) >> 16) + T;
        y = ((((int64_t)z->PG * in) >> 16) + S) * z->inv >> 16;
        u = zdf_sat(in - ((z->k * y) >> 12));
        v = (int32_t)(((int64_t)z->G1 * (u - ls[0]) + 32768) >> 16);
        u = v + ls[0], ls[0] = u + v;
        for (j = 1; j < 4; j++) {
            v = (int32_t)(((int64_t)z->G * (u - ls[j]) + 32768) >> 16);
            u = v + ls[j], ls[j] = u + v;
            if (j == 1)
                o = u;
        }
        if (f->slope == FS_24)
            return (u + 4096) >> 13;
        if (f->slope == FS_12)
            return (o + 4096) >> 13;
        return pole4_tick(&f->x4, (u + 2048) >> 12, ls + 4) >> 1;
    }
    case FM_SVF:
        return tsvf_lp(&f->sv, x >> 1, &sv[0], &sv[1]);
    default: {                                         /* SEM NOTCH BP HP: one SVF, its outputs */
        const tsvf_t *c = &f->sv;
        int32_t in = x >> 1, v1, v2, v3;
        if (f->model == FM_WARM) {                      /* (driven softly) */
            int32_t a = in < 0 ? -in : in;
            if (a > 12000)
                in = in < 0 ? -(12000 + (softclip((a - 12000) * 2) >> 1)) : 12000 + (softclip((a - 12000) * 2) >> 1);
        }
        v3 = in - sv[1];
        v1 = (c->a1 * sv[0] + c->a2 * v3) >> 13;
        v2 = sv[1] + ((c->a2 * sv[0] + c->a3 * v3) >> 13);
        sv[0] = clamp(2 * v1 - sv[0], -150000, 150000);
        sv[1] = clamp(2 * v2 - sv[1], -150000, 150000);
        switch (f->model) {
        case FM_NOTCH: return in - ((f->k * v1) >> 12);
        case FM_BP: return (f->k * v1) >> 12;
        case FM_HP: return in - ((f->k * v1) >> 12) - v2;
        default: return v2;
        }
    }
    }
}

/* FILTER 2 beside the MODEL filter (engines.c f2_pair): the pair is one more model for the engine's loop, which calls
 * fmod_tick as before (without FILTER 2 nothing of it is there: the loop as it was, bit for bit). x the engine's signal
 * (full scale), the MODEL filter's states sv / ls as before, FILTER 2's in st (the SVF family two of its four); SER:
 * FILTER 2 after it, PAR: the two on x, their sum (-3 dB: two bands about as loud as one), SPLIT: their middle, the
 * side ((first - second) / 2: the first left, the second right) into st->sd, a sample a call (voice.c vrender gives it
 * the voice's level and adds it to the part's side). Half scale, as fmod_tick */
static int32_t fmod_pair(const fmod_t *f, int32_t x, int32_t *sv, int32_t *ls)
{
    int32_t y = fmod_tick(f->p1, x, sv, ls), y2;
    if (f->route == F2_SER)
        return fmod_tick(f->p2, y << 1, f->st->s, f->st->s);
    y2 = fmod_tick(f->p2, x, f->st->s, f->st->s);
    if (f->route == F2_PAR)
        return (y + y2) * 23 >> 5;
    *f->st->sd++ = (y - y2) >> 1;
    return (y + y2) >> 1;
}

/* amplitude ramp over the block. Blocks are always CTL long, so x / CTL is a
 * shift rounded towards zero (-Os would keep a hardware divide per sample) */
#define CTL_LOG2 5
#if (1 << CTL_LOG2) != CTL
#error "CTL_LOG2 does not match CTL"
#endif
static inline int32_t amp_at(const vmod_t *m, uint32_t i)
{
    int32_t x = (m->amp1 - m->amp0) * (int32_t)i;
    return m->amp0 + ((x + ((x >> 31) & (CTL - 1))) >> CTL_LOG2);
}
