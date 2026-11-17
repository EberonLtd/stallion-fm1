/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (Clipper) */
/* PONY (Clipper 0.8): the 1980s DCO poly it follows, one of the most loved sounds there is (its
 * idea: one DCO with a saw, a pulse whose width the LFO moves, a sub oscillator an octave down and noise; a high-pass
 * in four positions; a 24 dB resonant low-pass; and the BBD chorus, I, II or both). No code or sample of Roland's.
 * EDIT 1  WAVE  PW  SUB  NOIS        EDIT 2  HPF  CUT  RES  CHOR
 *   WAVE  SAW, PLS (the pulse), S+P (both, as the 106's two buttons)
 *   PW    the pulse's width, 48.5 % (the square) .. ~11 %; SHAPE moves it: LFO DEST SHP the PWM, one-sided as the
 *         instrument's (from the knob's width narrower and back, voice.c), MW / AT SHP, the matrix (folded at the
 *         square)
 *   SUB   the square an octave down; NOIS white noise
 *   HPF   0 a little bass boost, 1 flat, 2 and 3 a high-pass at ~240 and ~720 Hz (the 106's four positions)
 *   CUT RES  the OTA chip's four stages (dsp.c ota_tick: a round self-oscillation; here the passband held as the
 *         resonance rises, PAL_OTA's note below), keyboard tracking half; ENV DEST FLT its envelope (on PONY +-64 the
 *         whole cutoff range, voice.c: the inverted envelope holds the filter shut); FX > FILTER > SLOPE 12 (stage 2's
 *         output, the flagship poly's), 24, 48 (dsp.c pole4_tick after it); FX > FILTER 2 after or beside it (its LINK
 *         follows this CUT: engines.c f2_pair)
 *   CHOR  OFF, I (0.5 Hz, wide), II (0.8 Hz, deeper), I+II (8 Hz, shallow: the 60's both buttons): stereo, after the
 *         sound's INSERT and FILTER (fx.c pony_chorus: one per part; the INSERT's PONY types give it to any sound)
 * EDIT 1's WAVE OFF: neither saw nor pulse (the sub, the noise alone).
 * EDIT 3 (Stallion, PONY only; params.c P_JRNG ..), after the instrument's panel:
 *   RANGE  the DCO's 16', 8' (as before), 4': its increment halved or doubled, so exactly an octave (the sub follows)
 *   KYBD   the filter's key tracking, 0 .. 100 % (one octave of cutoff an octave of the keys); 43 % the old half,
 *         (note - C4) << 3 exactly (pony_kt)
 *   VCA    ENV (as before) or GATE: the amp on and off with the key, ~3 ms ramps, ENV 1 still on the filter
 *         (voice.c track_render, pony_gate)
 *   CHAR   the instrument's quirks, a bit each (core.h JQ_*; the editor a box each): CLEAN none, CLASSIC the ones the
 *         references fit (HISS HPF LEVER GATE NOISE), FULL all, CUSTOM another mix. Each:
 *     STEPS  the CUT knob (the editor, CC 74) lands at once, 128 steps, as the instrument's slider read by its CPU (the
 *            sound before STEPS); off: it glides to a new value over a few blocks (the voice's s[7], Q8)
 *     HISS   the chorus's own hiss while the chorus is on, notes or not (fx.c pony_chorus: two noises, one a tap)
 *     HPF    the HPF after the voices' sum, not in each voice (fx.c pony_hpf): 0 a ~+10 dB shelf under ~70 Hz
 *     LEVER  the wheel's vibrato from the LFO (its rate, its wave), not the separate ~5.5 Hz sine (pony_lfo)
 *     GATE   VCA GATE's ramps ~1 ms (not ~3 ms)
 *     NOISE  the noise through a ~5 kHz low-pass and a ~34 Hz high-pass (the voice's jq[0], jq[1])
 *     VCA    the VCA's curve: the level follows the envelope within ~2 dB above 30 %, falls ~26 dB a 10 % under it
 *            (fast percussive tails; pony_vamp)
 *     LFO    the LFO free-running (no restart on a phrase's first key), its delay a hold-off and a straight fade
 *            (not the RC rise: voice.c track_lfo_tick, pony_lfo)
 *     COARS  the CUT knob in the instrument's coarser steps, ~1.35 semitones (0.11 octave)
 *     TICK   the envelopes and the LFO a new value every ~4.2 ms, ~1 ms smoothed, as the instrument's CPU gives them
 *            (pony_amp, pony_lfo; the voice's jq[2], jq[3])
 * FX > FILTER > MODL: OTA by default (stored as FM_SVF's number, pony_model), LADDR Clipper's PONY filter as it was
 * (the Zavalishin ladder, >> 1: fmod_tick FM_LADDER at SLOPE 24), or any other model (fx.c part_apply clears the
 * states when it changes, as VECTOR's).
 * The oscillators are band-limited (polyBLEP: dsp.c osc_saw / osc_pulse).
 * Voice state: ph[0] the DCO, ph[1] the sub's flip-flop (its top bit: the sub is the DCO divided by two, as on the
 * original, its edges at the saw's resets whatever ph[1] held before), ph[2] the noise; s[0..3] the filter, s[4] the
 * HPF's low-pass, s[5] the bass boost's, s[6] VCA GATE's level (Q15), s[7] the CUT knob as it glides (Q8) */
enum { JW_SAW, JW_PLS, JW_SP, JW_OFF };
static const char *const N_JWAVE[] = {"SAW", "PLS", "S+P", "OFF"};   /* (OFF appended: stored values keep theirs) */
static const char *const N_JRNG[] = {"16'", "8'", "4'"};
static const char *const N_JVCA[] = {"ENV", "GATE"};
/* CHAR: its bits' names (the editor's boxes, JQ_* in order) and the sets the device shows (params.c F_CHAR) */
static const char *const N_JQBIT[] = {"STEPS", "HISS", "HPF", "LEVER LFO", "GATE 1 MS", "DARK NOISE", "VCA CURVE",
                                      "LFO DELAY", "COARSE CUT", "CPU TICK", 0};
static const char *const N_JCHAR[] = {"CLEAN", "CLASC", "FULL", "CUSTM"};
static int jq_char(int32_t v) { return !v ? 0 : v == (int32_t)JQ_CLASSIC ? 1 : v == (int32_t)JQ_FULL ? 2 : 3; }
/* the pulse: the square a little off 50 % (48.5 %: its even harmonics ~27 dB down, as the instrument's comparator
 * leaves them), PW 127 ~11 % (the instrument's knob at its end) */
#define PAL_SQ (0x80000000u - 64424509u)
#define PAL_NARROW 49500u
/* PAL_OTA: pony_render, the OTA alone (FX > FILTER > MODL OTA, no FILTER 2): the passband held as the resonance rises,
 * as the instrument's is (its fundamental within 0..+2 dB from RES 0 to self-oscillation, the whole sound ~8 dB
 * louder, the whistle 4..9 dB over the fundamental): the input x (1 + k) where dsp.c ota_tick gives x (1 + k / 4)
 * (other engines' OTA as before), the signal (1 + 3k / 8) lower through the filter and as much higher after it (the
 * oscillation, set by the feedback's saturation, that much louder against the notes). RES 0: as before, bit for bit */
#define JK_DEF 43                                  /* KYBD's default: the old half (pony_kt) */
/* KYBD k (0..100 %) on d (the note from C4, 1/16 semitone): cutoff steps (Q8). 0 .. JK_DEF to the old << 3 exactly,
 * on to 100: one octave of cutoff an octave (CUTOFF_HZ: 14.02 steps an octave, 18.69 << 8 / 16 a sixteenth) */
static inline int32_t pony_kt(int32_t d, int32_t k)
{
    int32_t g = k <= JK_DEF ? k * 512 / JK_DEF : 512 + (k - JK_DEF) * 12;   /* Q6: 512 = 8, 1196 = 18.69 */
    return (d * g) >> 6;
}
/* the MODEL PONY plays: its list has OTA where SVF is (params.c N_JFMODEL), so FM_SVF's number (P_FMODEL's default,
 * every PONY sound and store before) is OTA */
static inline int32_t pony_model(const track_t *t)
{
    int32_t m = fm_model(t);
    return m == FM_SVF ? FM_OTA : m == FM_OTA ? FM_SVF : m;
}
/* VCA GATE (pony_vamp, once a block after env_tick): the amp's level, up while the note is held (ATK, DEC, SUS), down
 * once let go or given up, a quarter a block (~3 ms; CHAR's GATE ~1 ms); an ended voice's at 0 (a sample's KILL_BLOCKS
 * fade: its last block ramps down) */
#define JG_STEP 8192
#define JG_STEP1 23800                                 /* (GATE: 1.38 blocks, ~1 ms) */
static inline int32_t pony_gate(voice_t *v, int32_t st)
{
    int32_t g = v->active ? clamp(v->s[6] + (v->stage - 1u < 2u ? st : -st), 0, 32767) : 0;
    v->s[6] = g;
    return g;
}
/* the amp (voice.c track_render, once a block, for VCA GATE or CHAR's VCA): GATE's level, else the envelope env (Q15)
 * through the VCA's curve (VCA: e under 0.3 as e^3 / 0.09, the same level at 0.3: -26 dB a 10 % at 0.1, the tail
 * quick), x the velocity (as voice.c's) */
static __attribute__((noinline)) int32_t pony_vamp(const track_t *t, voice_t *v, int32_t env)
{
    int32_t a = env;
    if (t->p[P_JVCA]) {
        a = pony_gate(v, (t->p[P_JCHR] & JQ_GATE) ? JG_STEP1 : JG_STEP);
    } else if (a < 9830) {
        a = (a * a) >> 15;
        a = (int32_t)(((int64_t)((a * env) >> 15) * 364089) >> 15);   /* (x 11.11: 1 / 0.09) */
    }
    return mulq15(a, v->vel * 258);
}
/* CHAR's TICK: the part's ~4.2 ms tick (185 samples) and the LFO through it (pony_lfo); each voice's envelope through
 * it (pony_amp, the engine's amp: the filter's envelope and the amp's alike). A tick sets the value the CPU gives
 * (held), each block moves half way to it (~1 ms) */
#define JT_TICK 185
typedef struct {
    int16_t acc;                                       /* samples since the last tick */
    uint8_t now;                                       /* this block a tick */
    int32_t lt, lo, rt, ro;                            /* the LFO (as the routes see it, faded): held, out; raw: .. */
} jtick_t;
static jtick_t jtick[NPART];
static int32_t pony_amp(struct track *t, voice_t *v, int32_t a)
{
    if (!(t->p[P_JCHR] & JQ_TICK)) {
        v->jq[2] = v->jq[3] = a;
        return a;
    }
    if (jtick[(uint32_t)(t - trk) % NPART].now)
        v->jq[2] = a;
    v->jq[3] += (v->jq[2] - v->jq[3]) >> 1;
    return v->jq[3];
}
/* (voice.c track_render, once a block and part, before its voices) the LFO as PONY's CHAR has it: lfo the LFO as its
 * routes see it (the delay's fade in), back the same or: LFO a hold-off (the first half of the fade's time) and a
 * straight fade (the RC rise not run: voice.c track_lfo_tick); TICK held from tick to tick; *vib (the wheel's
 * vibrato): LEVER the LFO itself (no fade: the lever's) */
static __attribute__((noinline)) int32_t pony_lfo(track_t *t, int32_t lfo, int32_t *vib, uint32_t n)
{
    jtick_t *k = &jtick[(uint32_t)(t - trk) % NPART];
    uint32_t q = (uint32_t)t->p[P_JCHR];
    int32_t raw = t->lfo_val;
    if (q & JQ_LFO) {
        int32_t f = t->lfo_fade;
        lfo = mulq15(raw, f < 16384 ? 0 : (f - 16384) * 2);
    }
    k->acc = (int16_t)(k->acc + (int32_t)n);
    k->now = k->acc >= JT_TICK;
    if (k->now)
        k->acc = (int16_t)(k->acc - JT_TICK);
    if (q & JQ_TICK) {
        if (k->now)
            k->lt = lfo, k->rt = raw;
        k->lo += (k->lt - k->lo) >> 1;
        k->ro += (k->rt - k->ro) >> 1;
        lfo = k->lo, raw = k->ro;
    } else {
        k->lt = k->lo = lfo, k->rt = k->ro = raw;
    }
    if (q & JQ_LEVER)
        *vib = raw;
    return lfo;
}
/* ENV DEST FLT on PONY (voice.c): the envelope as the filter sees it, its level in dB over a 46 dB span, 0 .. 1
 * (Q15): the instrument's envelope moves the cutoff evenly in octaves as the level falls evenly in dB, so an inverted
 * envelope holds the filter shut, then opens it steadily (the reference's harmonics 1, 2, 4, 8 loudest at ~95, 115,
 * 135, 165 ms), and a pluck closes it the same way. log2 from the top bit and a straight mantissa */
/* pony_note_on: a filter that sings (OTA, RES 120 up: k over 4) starts from silence with stage 1 at about its
 * oscillation's level (its ring settles in a period or two, no step bigger than its own), the circuit's own noise as
 * the instrument's: with no source (WAVE OFF, SUB and NOIS 0) it sounds within ~1 ms of the key, in tune with KYBD 100
 * (pony_ota_g); below RES 120, or another MODL, from 0 */
#define JF_SEED (1 << 28)
#define JF_SINGS 120
#define JF_DB 4288                                     /* 32768 x 6.02 / 46 dB */
static inline int32_t pony_fenv(int32_t e)
{
    int32_t n = 0, l;
    if (e <= 0)
        return 0;
    if (e >= 1 << 8)
        n = 8;
    if (e >= 1 << (n + 4))
        n += 4;
    if (e >= 1 << (n + 2))
        n += 2;
    if (e >= 1 << (n + 1))
        n += 1;
    l = ((n - 15) << 12) + (((e << 12) >> n) - 4096);   /* log2(e / 32768), Q12 */
    l = 32767 + ((l * JF_DB) >> 12);
    return l < 0 ? 0 : l;
}
/* PONY's OTA at its cutoff exactly (pony_render): g = tan(pi f / FS) from the cutoff as a pitch (CUTOFF_HZ is
 * 30 Hz x 2^(cut x 9.059 / 127): pitch_inc to 1/16 semitone, the fraction as the voice's fine tuning) instead of the Q12
 * table (its steps 0.3 .. 2.4 % below 2 kHz: the self-oscillating filter out of tune by up to 40 cents); the table
 * from cut 100 (3.7 kHz, its steps under 0.1 %). With KYBD 100 the singing filter plays in tune */
#ifndef JO_STRETCH
#define JO_STRETCH 4260u                               /* (c 0.065, Q16) */
#endif
static __attribute__((noinline)) void pony_ota_g(ladder_t *c, int32_t cut)   /* (in the flash, a call a block: the
                                                        * RAM code area is full; pony_render calls it by HOT_FAR) */
{
    uint32_t P = 92109u + (((uint32_t)(cut < 0 ? 0 : cut) * 14024u) >> 10), inc = pitch_inc(P >> 8), x, g;
    int32_t G, G2;
    inc += (inc >> 12) * (((P & 255u) * 3792u) >> 16);
    x = (uint32_t)(((uint64_t)inc * 205887u) >> 32);   /* pi f / FS, Q16 */
    g = x + ((x * (((x * JO_STRETCH) >> 16) + ((((x * x) >> 16) * 21845u) >> 16))) >> 16);   /* tan x ~ x (1 +
                                                         * x^2 / 3); the singing loop's pitch falls ~0.8 cent a 100 Hz
                                                         * under its cutoff: stretched back, x (1 + c x) */
    G = (int32_t)((g << 16) / (65536u + g));
    G2 = mul_sh(G, G, 16);
    c->G = G;
    c->H = 65536 - G;
    c->G4 = mul_sh(G2, G2, 16);
    c->inv = (int32_t)(0xFFFFFFFFu / (uint32_t)(65536 + mul_sh(c->k, c->G4, 12)));
}
static const char *const N_JHPF[] = {"0", "1", "2", "3"};
static const char *const N_JCHOR[] = {"OFF", "I", "II", "I+II"};

static void pony_note_on(track_t *t, voice_t *v)
{
    if (!v->env && !v->env_out) {                    /* from silence: the filter clear (a DCO: its phase runs on) */
        v->s[1] = v->s[2] = v->s[3] = v->s[4] = v->s[5] = v->s[6] = 0;
        v->jq[0] = v->jq[1] = v->jq[2] = v->jq[3] = 0;
        v->s[0] = t->p[P_E6] >= JF_SINGS && t->p[P_FMODEL] == FM_SVF ? JF_SEED : 0;   /* (JF_SEED; FM_SVF: OTA) */
        v->s[7] = t->p[P_E5] << 8;                   /* (the CUT knob's glide from where it is) */
        if (!v->ph[2])
            v->ph[2] = 0x2545F491u + v->note * 2654435761u;
    }
}

static void pony_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *ev = vm_ev(t, m);              /* the EDIT values: the voice's own or the part's */
    uint32_t i, ph = v->ph[0], fl = v->ph[1] & 0x80000000u, nz = v->ph[2], inc = m->inc, incs;
    uint32_t wave = (uint32_t)clamp(ev[0], 0, 3), hpf = (uint32_t)clamp(ev[4], 0, 3);
    int32_t w = ev[1] * 256 + (m->shape - (64 << 8)) * 2;   /* 0 square .. 127 * 256 narrow: the PW knob, then the
                                                     * LFO's PWM (one-sided, voice.c), MW / AT / the matrix */
    uint32_t pw;
    w = w < 0 ? (-w < 127 * 256 ? -w : 127 * 256) : w < 127 * 256 ? w : 127 * 256;   /* (folded at the square: a
                                                     * source swinging both ways never holds it flat) */
    pw = PAL_SQ - (uint32_t)w * PAL_NARROW;            /* 48.5 % .. ~11 % */
    int32_t sub = ev[2] * 200, nois = ev[3] * 160, hp = v->s[4], bb = v->s[5];
    int32_t kc = ev[5] << 8, cut, model = pony_model(t), own, xo = 1024;
    int32_t ls[8] = {v->s[0], v->s[1], v->s[2], v->s[3], v->fs[0], v->fs[1], v->fs[2], v->fs[3]};   /* (+ SLOPE 48's) */
    int32_t sl = fm_slope(t);                         /* (FX > FILTER > SLOPE: 12 stage 2's output, 48 four poles more) */
    fmod_t fm;                                        /* (FILTER 2: the OTA as a filter MODEL beside it, engines.c */
    f2pair_t f2;                                      /* f2_pair; without it the OTA here as before) */
    int pair;
    const ladder_t *lad = &fm.lad;
    const pole4_t *x4 = &fm.x4;
    uint32_t q = (uint32_t)t->p[P_JCHR];              /* CHAR (core.h JQ_*) */
    int32_t nl = v->jq[0], nh = v->jq[1];
    if (t->p[P_JRNG] != 1)                            /* RANGE 16', 4': the DCO an octave down, up */
        inc = !t->p[P_JRNG] ? inc >> 1 : inc > 0x3FFFFFFFu ? 0x7FFFFFFEu : inc << 1;
    incs = inc >> 1;
    if (q & JQ_HPF)
        hpf = 1u;                                     /* (CHAR's HPF: after the voices' sum, fx.c pony_hpf) */
    if (!(q & (JQ_STEPS | JQ_COARSE))) {              /* STEPS OFF: the knob glides, half the way a block, the last
                                                       * quarter step at once (a knob at rest: where it is) */
        int32_t c = v->s[7], d = kc - c;
        c = d > 64 || d < -64 ? c + (d >> 1) : kc;
        v->s[7] = c;
        kc = c;
    } else {
        if (q & JQ_COARSE)                            /* (COARS: steps of 1.542 (Q8 395), 0.11 octave) */
            kc = (((kc + 197) * 10619) >> 22) * 395;
        v->s[7] = kc;
    }
    cut = kc + m->cutoff + pony_kt(v->pitch16 - 60 * 16, t->p[P_JKYB]);
    fmod_coef(&fm, model, cut, ev[6]);
    if (model == FM_OTA && cut < 100 << 8)
        HOT_FAR(pony_ota_g)(&fm.lad, cut);
    fmod_slope(&fm, sl);
    pair = f2_pair(&fm, &f2, t, v, m, cut, out);
    own = pair || model != FM_OTA;                    /* (the OTA alone: its loop below) */
    if (!own)                                         /* the passband held as the resonance rises (PAL_OTA's note) */
        xo = 1024 + ((fm.lad.k * 3) >> 5);
    for (i = 0; i < n; i++) {
        int32_t s, y, a;
        if (wave == JW_SAW)
            s = osc_saw(ph, inc);
        else if (wave == JW_PLS)
            s = osc_pulse(ph, inc, pw);
        else if (wave == JW_SP)
            s = (osc_saw(ph, inc) + osc_pulse(ph, inc, pw)) * 5 >> 3;
        else
            s = 0;                                   /* WAVE OFF */
        if (sub)
            s += mulq15(osc_pulse(fl | ph >> 1, incs, 0x80000000u), sub);
        if (nois) {
            int32_t z;
            nz = nz * 1664525u + 1013904223u;
            z = (int32_t)(nz >> 16) - 32768;
            if (q & JQ_NOISE) {                      /* (NOISE: ~5 kHz low-pass, ~34 Hz high-pass) */
                nl += ((z - nl) * 16699) >> 15;
                nh += ((nl - nh) * 159) >> 15;
                z = nl - nh;
            }
            s += mulq15(z, nois);
        }
        ph += inc;
        if (ph < inc)                                /* the saw reset: the flip-flop turns */
            fl ^= 0x80000000u;
        s = s * 5 >> 3;                              /* (four sources: headroom) */
        if (hpf != 1u) {                             /* 0: bass boost (a low shelf ~120 Hz); 2, 3: a one-pole high-pass */
            if (!hpf) {
                bb += ((s - bb) * 560) >> 15;
                s += bb >> 1;
            } else {
                hp += ((s - hp) * (hpf == 2u ? 1120 : 3360)) >> 15;
                s -= hp;
            }
        }
        if (own) {
            y = fmod_tick(&fm, s, ls, ls);           /* (the SVF family: its two states in ls[0], ls[1]) */
        } else {
            y = ota_tick(lad, s, ls, sl == FS_12);
            if (sl == FS_48)
                y = pole4_tick(x4, y, ls + 4);
            y = (y * xo) >> 11;                       /* (RES 0: >> 1, as before) */
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
    if (pair)
        f2_pair_end(&f2, v);
    v->s[4] = hp;
    v->s[5] = bb;
    v->jq[0] = nl, v->jq[1] = nh;
}

/* (Stallion 1.0) the factory sounds: the original's classic patches (both versions) as their factory sheets describe them
 * (strings, brass, pianos, organs, basses, leads), rebuilt here; dry (no sends, no arp), centred */
#define PON_FX0 FX(0, 0, 0, 0)
#define PON_STEPS XP((int8_t)(P_JCHR + 1), JQ_CLASSIC | JQ_STEPS)   /* CHAR CLASSIC and STEPS: a hand-swept
                                                    * resonant filter in the instrument's 128 steps (the sound held:
                                                    * the same) */
static const preset_t PONY_PRESETS[] = {
    /* {WAVE, PW, SUB, NOIS, HPF, CUT, RES, CHOR}, {A D S R}, ENV -> FILTER, mono; extras. (ENV -> FILTER halved when
     * its span doubled to the whole cutoff range, voice.c: each sweep's top where it was) */
    {"INIT", {JW_SAW, 0, 0, 0, 1, 127, 0, 0}, {0, 64, 127, 20}, 0, 0, PON_FX0},
    /* strings and pads */
    {"STRINGS", {JW_SAW, 0, 24, 0, 2, 78, 0, 2}, {74, 100, 116, 92}, 3, 0, PON_FX0,
     XP(P_LRATE + 1, 84, P_LD_PIT + 1, 1)},
    /* (matrix showcase: PONY_PATCHES) the strings under the fingers: pressure opens the filter, swells the level, speeds
     * the ensemble's LFO; the aftertouch's own vibrato on top */
    {"TOUCH STRING", {JW_SAW, 0, 24, 0, 2, 80, 0, 2}, {74, 100, 116, 92}, 3, 0, PON_FX0,
     XP(P_LRATE + 1, 84, P_LD_PIT + 1, 1, P_ATDST + 1, MD_VIB)},
    {"DARK STRINGS", {JW_SAW, 0, 30, 0, 1, 64, 0, 2}, {84, 104, 118, 100}, 2, 0, PON_FX0,
     XP(P_LRATE + 1, 80, P_LD_PIT + 1, 1)},
    {"PWM STRINGS", {JW_SP, 50, 24, 0, 2, 78, 0, 1}, {72, 100, 116, 92}, 3, 0, PON_FX0,
     XP(P_LRATE + 1, 64, P_LD_SHP + 1, 36)},
    {"WARM PAD", {JW_SP, 40, 40, 0, 1, 70, 10, 2}, {84, 104, 120, 100}, 4, 0, PON_FX0,
     XP(P_LRATE + 1, 56, P_LD_SHP + 1, 30)},
    {"SYNTH CHOIR", {JW_PLS, 90, 0, 6, 2, 62, 36, 2}, {80, 100, 118, 96}, 0, 0, PON_FX0,
     XP(P_LRATE + 1, 88, P_LD_PIT + 1, 1, P_LD_SHP + 1, 10)},
    {"RESO SWEEP", {JW_SAW, 0, 30, 0, 1, 32, 70, 1}, {90, 112, 70, 104}, 22, 0, PON_FX0, PON_STEPS},   /* (STEPS:
                                                                                         * the hand sweep in steps) */
    {"BRASS+STRING", {JW_SAW, 0, 30, 0, 1, 62, 6, 1}, {60, 96, 110, 84}, 12, 0, PON_FX0},
    /* brass */
    {"SYNTH BRASS", {JW_SAW, 0, 40, 0, 1, 42, 10, 0}, {56, 86, 100, 62}, 20, 0, PON_FX0},
    /* (showcase) velocity brass: the filter envelope's depth and speed and the cutoff all follow the strike */
    {"VEL BRASS", {JW_SAW, 0, 40, 0, 1, 38, 10, 0}, {56, 86, 100, 62}, 20, 0, PON_FX0},
    {"PHASE BRASS", {JW_SP, 50, 30, 0, 1, 46, 10, 1}, {56, 86, 100, 62}, 19, 0, PON_FX0,
     XP(P_LRATE + 1, 60, P_LD_SHP + 1, 34)},
    {"FANFARE", {JW_SAW, 0, 60, 0, 1, 42, 16, 0}, {44, 80, 96, 56}, 20, 0, PON_FX0},
    /* keys */
    {"SYNTH PIANO", {JW_SP, 40, 20, 0, 2, 34, 10, 3}, {0, 96, 0, 74}, 22, 0, PON_FX0},
    {"TREMOLO EP", {JW_PLS, 20, 0, 0, 2, 52, 0, 3}, {0, 100, 20, 72}, 15, 0, PON_FX0,
     XP(P_LRATE + 1, 76, P_LD_AMP + 1, 40)},
    /* (Stallion: CLAV's place) the filter whistling alone, no oscillator: WAVE OFF, RES 125 (the filter sings,
     * pony_note_on), KYBD 100 (in tune: CUT 45, BRITE -1, RES 125 put C2 .. C6 within +7 cents of the notes), the
     * wheel its vibrato (WAH: the LFO on the cutoff), the bend on the filter (VCF BEND) */
    {"WHISTLE", {JW_OFF, 0, 0, 0, 1, 45, 125, 1}, {8, 80, 127, 50}, 0, 1, PON_FX0,
     XP((int8_t)(P_JKYB + 1), 100, (int8_t)(P_MAC1 + 1), -1, P_GLIDE + 1, 20, P_MWDST + 1, MD_WAH)},
    {"HARPSICHORD", {JW_PLS, 110, 40, 0, 3, 52, 6, 0}, {0, 90, 0, 66}, 10, 0, PON_FX0},
    /* organs */
    {"ORGAN", {JW_PLS, 0, 90, 0, 2, 92, 0, 2}, {0, 40, 127, 24}, 0, 0, PON_FX0},
    {"COMBO ORGAN", {JW_PLS, 80, 60, 0, 3, 88, 0, 0}, {0, 40, 127, 20}, 0, 0, PON_FX0,
     XP(P_LRATE + 1, 94, P_LD_PIT + 1, 2)},
    /* (showcase) the wheel as a PWM pad's one control: deeper, faster PWM and a brighter filter together */
    {"PWM WHEEL", {JW_PLS, 20, 30, 0, 1, 70, 10, 2}, {60, 100, 120, 90}, 3, 0, PON_FX0,
     XP(P_LRATE + 1, 56, P_LD_SHP + 1, 26, P_MWDST + 1, MD_OFF)},
    /* basses */
    {"PULSE BASS", {JW_PLS, 30, 127, 0, 0, 40, 24, 0}, {0, 72, 40, 26}, 20, 1, PON_FX0},
    {"SUB BASS", {JW_PLS, 0, 127, 0, 0, 30, 0, 0}, {0, 64, 127, 20}, 3, 1, PON_FX0},
    {"FUNK BASS", {JW_SAW, 0, 100, 0, 0, 40, 36, 0}, {10, 70, 50, 24}, 18, 1, PON_FX0, PON_STEPS},
    {"HOUSE BASS", {JW_SP, 64, 60, 0, 0, 46, 10, 3}, {0, 76, 60, 30}, 20, 1, PON_FX0},
    /* (showcase) TIMBR morphs the bass: up, a narrow pulse, less sub, more resonance and envelope; down, round and deep */
    {"MORPH BASS", {JW_PLS, 30, 100, 0, 0, 40, 20, 0}, {0, 72, 60, 26}, 18, 1, PON_FX0},
    /* leads */
    {"SAW LEAD", {JW_SAW, 0, 40, 0, 1, 56, 30, 0}, {2, 80, 110, 44}, 10, 1, PON_FX0,
     XP(P_GLIDE + 1, 30, P_LRATE + 1, 88, P_LD_PIT + 1, 2, P_LFADE + 1, 84)},
    {"PWM LEAD", {JW_PLS, 50, 40, 0, 1, 70, 10, 2}, {2, 80, 116, 44}, 5, 1, PON_FX0,
     XP(P_GLIDE + 1, 20, P_LRATE + 1, 62, P_LD_SHP + 1, 40)},
    /* a square lead with the sub under it, LFO 2's slow PWM wandering at LFO 3's pace, pressure on the
     * filter */
    {"SQUARE LEAD", {JW_PLS, 0, 60, 0, 1, 66, 20, 0}, {2, 80, 116, 44}, 8, 1, PON_FX0,
     XP(P_GLIDE + 1, 24, P_LRATE + 1, 86, P_LD_PIT + 1, 2, P_LFADE + 1, 84)},
    /* a hollow narrow-pulse lead through the high-pass, the wheel widening the pulse to a square,
     * ENV 2 opening the filter behind the note, pressure brightening it and the aftertouch's vibrato */
    {"HOLLOW LEAD", {JW_PLS, 80, 0, 0, 2, 55, 30, 1}, {6, 80, 116, 50}, 6, 1, PON_FX0,
     XP(P_GLIDE + 1, 16, P_MWDST + 1, MD_OFF, P_ATDST + 1, MD_VIB)},
    /* plucks */
    {"PERC PLUCK", {JW_SP, 40, 30, 0, 1, 42, 30, 3}, {0, 72, 0, 56}, 16, 0, PON_FX0},
    {"DARK PLUCK", {JW_SAW, 0, 20, 0, 1, 26, 50, 3}, {0, 76, 0, 64}, 21, 0, PON_FX0, PON_STEPS},
    /* the DCO poly stab: saw and sub through chorus II, velocity on the filter envelope's depth and
     * speed, the high notes a little brighter */
    {"POLY STAB", {JW_SAW, 0, 40, 0, 1, 50, 24, 2}, {0, 84, 30, 50}, 20, 0, PON_FX0},
};
#undef PON_FX0
#undef PON_STEPS

/* (Stallion) the matrix showcase (core.h preset_patch_t): slot, source, destination, amount */
static const pcable_t PONC_TSTR[] = {
    CB(1, AT, CUT, 24), CB(2, AT, AMP, 14), CB(3, AT, L1RATE, 16),
};
static const pcable_t PONC_VBRASS[] = {
    CB(1, VEL, E1AMT, 40), CB(2, DC, E1AMT, -20),    /* the filter envelope: x 0.7 soft .. x 1.3 hard */
    CB(3, VEL, ERATE, 18),                           /* harder: quicker */
    CB(4, VEL, CUT, 8),
};
static const pcable_t PONC_PWMW[] = {
    CB(1, MW, SHAPE, 24), CB(2, MW, CUT, 18), CB(3, MW, L1RATE, 20),
};
static const pcable_t PONC_MORPH[] = {
    CB(1, MAC2, E1 + 2, -30),                        /* SUB */
    CB(2, MAC2, E1 + 6, 30),                         /* RES */
    CB(3, MAC2, E1AMT, 30),                          /* the filter envelope's depth (TIMBR's own: the PW) */
};
/* LIFE-BEGIN (the life pass: each preset a little movement and touch, by its kind; MATRIX-SHOWCASE.md) */
static const pcable_t LIFE_PON_1[] = {   /* STRINGS (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, VEL, CUT, 6),           /* velocity: brighter */
    CB(3, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
    CB(4, MAC3, L1RATE, 24),      /* MOTN: LFO 1's rate faster */
};
static const pcable_t LIFE_PON_3[] = {   /* DARK STRINGS (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, VEL, CUT, 6),           /* velocity: brighter */
    CB(3, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
    CB(4, MAC3, L1RATE, 24),      /* MOTN: LFO 1's rate faster */
};
static const pcable_t LIFE_PON_4[] = {   /* PWM STRINGS (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, SHAPE, 6),        /* LFO 2 (1 Hz): a slow PWM / wave motion */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_PON_5[] = {   /* WARM PAD (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, SHAPE, 6),        /* LFO 2 (1 Hz): a slow PWM / wave motion */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_PON_6[] = {   /* SYNTH CHOIR (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, LFO2, SHAPE, 6),        /* LFO 2 (1 Hz): a slow PWM / wave motion */
    CB(3, VEL, CUT, 6),           /* velocity: brighter */
    CB(4, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
    CB(5, MAC3, L2RATE, 30),      /* MOTN: the motion faster */
};
static const pcable_t LIFE_PON_7[] = {   /* RESO SWEEP (pad) */
    CB(1, LFO3, CUT, 6),          /* LFO 3 (0.4 Hz): the filter breathes */
    CB(2, VEL, CUT, 6),           /* velocity: brighter */
    CB(3, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_8[] = {   /* BRASS+STRING (brass) */
    CB(1, ENV2, CUT, 10),         /* ENV 2: the bright blat of the attack */
    CB(2, VEL, E1AMT, 16),        /* velocity: the swell deeper */
    CB(3, AT, AMP, 10),           /* pressure: louder */
    CB(4, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
    CB(5, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_9[] = {   /* SYNTH BRASS (brass) */
    CB(1, ENV2, CUT, 10),         /* ENV 2: the bright blat of the attack */
    CB(2, VEL, E1AMT, 16),        /* velocity: the swell deeper */
    CB(3, AT, AMP, 10),           /* pressure: louder */
    CB(4, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
    CB(5, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_11[] = {   /* PHASE BRASS (brass) */
    CB(1, ENV2, CUT, 10),         /* ENV 2: the bright blat of the attack */
    CB(2, VEL, E1AMT, 16),        /* velocity: the swell deeper */
    CB(3, AT, AMP, 10),           /* pressure: louder */
    CB(4, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
    CB(5, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_12[] = {   /* FANFARE (brass) */
    CB(1, ENV2, CUT, 10),         /* ENV 2: the bright blat of the attack */
    CB(2, VEL, E1AMT, 16),        /* velocity: the swell deeper */
    CB(3, AT, AMP, 10),           /* pressure: louder */
    CB(4, LFO3, CUT, 4),          /* LFO 3: the filter breathes */
    CB(5, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_13[] = {   /* SYNTH PIANO (keys) */
    CB(1, VEL, CUT, 14),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 16),        /* velocity: the envelope deeper */
    CB(3, KEY, ERATE, 8),         /* high notes shorter */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_14[] = {   /* TREMOLO EP (keys) */
    CB(1, VEL, CUT, 14),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 16),        /* velocity: the envelope deeper */
    CB(3, KEY, ERATE, 8),         /* high notes shorter */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_15[] = {   /* WHISTLE (lead) */
    CB(1, AT, AMP, 10),           /* pressure: a swell */
    CB(2, VEL, E1 + 3, 12),       /* velocity: breathier (the noise through the singing filter) */
    CB(3, MAC2, E1 + 3, 30),      /* TIMBR: the breath */
    CB(4, LFO3, AMP, 3),          /* LFO 3: a slow breath on the level */
};
static const pcable_t LIFE_PON_16[] = {   /* HARPSICHORD (keys) */
    CB(1, VEL, CUT, 14),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 16),        /* velocity: the envelope deeper */
    CB(3, KEY, ERATE, 8),         /* high notes shorter */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_17[] = {   /* ORGAN (organ) */
    CB(1, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(2, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_18[] = {   /* COMBO ORGAN (organ) */
    CB(1, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(2, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
    CB(3, MAC3, L1RATE, 24),      /* MOTN: LFO 1's rate faster */
};
static const pcable_t LIFE_PON_20[] = {   /* PULSE BASS (bass) */
    CB(1, VEL, CUT, 12),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 12),        /* velocity: the envelope deeper */
    CB(3, ENV2, E1 + 6, 10),      /* ENV 2: a resonant bite on the attack */
    CB(4, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_21[] = {   /* SUB BASS (bass) */
    CB(1, VEL, CUT, 12),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 12),        /* velocity: the envelope deeper */
    CB(3, ENV2, E1 + 6, 10),      /* ENV 2: a resonant bite on the attack */
    CB(4, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_22[] = {   /* FUNK BASS (bass) */
    CB(1, VEL, CUT, 12),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 12),        /* velocity: the envelope deeper */
    CB(3, ENV2, E1 + 6, 10),      /* ENV 2: a resonant bite on the attack */
    CB(4, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_23[] = {   /* HOUSE BASS (bass) */
    CB(1, VEL, CUT, 12),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 12),        /* velocity: the envelope deeper */
    CB(3, ENV2, E1 + 6, 10),      /* ENV 2: a resonant bite on the attack */
    CB(4, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_25[] = {   /* SAW LEAD (lead) */
    CB(1, ENV2, PITCH, 6),        /* ENV 2: a pitch blip on the attack */
    CB(2, VEL, CUT, 10),          /* velocity: brighter */
    CB(3, AT, AMP, 8),            /* pressure: a swell */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_26[] = {   /* PWM LEAD (lead) */
    CB(1, ENV2, PITCH, 6),        /* ENV 2: a pitch blip on the attack */
    CB(2, VEL, CUT, 10),          /* velocity: brighter */
    CB(3, AT, AMP, 8),            /* pressure: a swell */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_27[] = {   /* SQUARE LEAD */
    CB(1, AT, CUT, 16),           /* pressure: the filter opens */
    CB(2, LFO2, SHAPE, 8),        /* LFO 2 (slow): the pulse width wanders */
    CB(3, LFO3, L2RATE, 16),      /* LFO 3: LFO 2's pace drifts */
    CB(4, VEL, E1AMT, 24),        /* velocity: the filter envelope deeper */
    CB(5, ENV2, PITCH, 4),        /* ENV 2: a small scoop into the note */
};
static const pcable_t LIFE_PON_28[] = {   /* HOLLOW LEAD */
    CB(1, MW, SHAPE, -20),        /* the wheel: the narrow pulse widens to a square */
    CB(2, AT, CUT, 12),           /* pressure: brighter */
    CB(3, ENV2, CUT, 14),         /* ENV 2 (slow attack): the filter opens behind the note */
    CB(4, LFO3, CUT, 5),          /* LFO 3: a slow breath on the filter */
    CB(5, VEL, CUT, 8),           /* velocity: brighter */
};
static const pcable_t LIFE_PON_29[] = {   /* PERC PLUCK (keys) */
    CB(1, VEL, CUT, 14),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 16),        /* velocity: the envelope deeper */
    CB(3, KEY, ERATE, 8),         /* high notes shorter */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_30[] = {   /* DARK PLUCK (keys) */
    CB(1, VEL, CUT, 14),          /* velocity: brighter */
    CB(2, VEL, E1AMT, 16),        /* velocity: the envelope deeper */
    CB(3, KEY, ERATE, 8),         /* high notes shorter */
    CB(4, LFO3, CUT, 3),          /* LFO 3: a slow shimmer */
    CB(5, MAC2, E1 + 2, -24),     /* TIMBR: less SUB */
};
static const pcable_t LIFE_PON_31[] = {   /* POLY STAB */
    CB(1, VEL, E1AMT, 30), CB(2, DC, E1AMT, -15),    /* the filter envelope: x 0.77 soft .. x 1.23 hard */
    CB(3, VEL, ERATE, 12),        /* harder: snappier */
    CB(4, KEY, CUT, 6),           /* high notes brighter */
    CB(5, LFO3, CUT, 4),          /* LFO 3: a slow shimmer */
};
/* LIFE-END */
static const preset_patch_t PONY_PATCHES[] = {
    PPATCH(&PONY_PRESETS[1], LIFE_PON_1, P_LFADE + 1, 70),    /* STRINGS */
    PPATCH(&PONY_PRESETS[2], PONC_TSTR),              /* TOUCH STRING */
    PPATCH(&PONY_PRESETS[3], LIFE_PON_3, P_LFADE + 1, 70),    /* DARK STRINGS */
    PPATCH(&PONY_PRESETS[4], LIFE_PON_4),    /* PWM STRINGS */
    PPATCH(&PONY_PRESETS[5], LIFE_PON_5),    /* WARM PAD */
    PPATCH(&PONY_PRESETS[6], LIFE_PON_6, P_LFADE + 1, 70),    /* SYNTH CHOIR */
    PPATCH(&PONY_PRESETS[7], LIFE_PON_7),    /* RESO SWEEP */
    PPATCH(&PONY_PRESETS[8], LIFE_PON_8, P_LRATE + 1, 86, P_LD_PIT + 1, 1, P_LFADE + 1, 84),    /* BRASS+STRING */
    PPATCH(&PONY_PRESETS[9], LIFE_PON_9, P_LRATE + 1, 86, P_LD_PIT + 1, 1, P_LFADE + 1, 84),    /* SYNTH BRASS */
    PPATCH(&PONY_PRESETS[10], PONC_VBRASS),           /* VEL BRASS */
    PPATCH(&PONY_PRESETS[11], LIFE_PON_11),    /* PHASE BRASS */
    PPATCH(&PONY_PRESETS[12], LIFE_PON_12, P_LRATE + 1, 86, P_LD_PIT + 1, 1, P_LFADE + 1, 84),    /* FANFARE */
    PPATCH(&PONY_PRESETS[13], LIFE_PON_13),    /* SYNTH PIANO */
    PPATCH(&PONY_PRESETS[14], LIFE_PON_14),    /* TREMOLO EP */
    PPATCH(&PONY_PRESETS[15], LIFE_PON_15, (int8_t)(P_BVCF + 1), 2),    /* WHISTLE (the bend ~+-1.7 st on the filter) */
    PPATCH(&PONY_PRESETS[16], LIFE_PON_16),    /* HARPSICHORD */
    PPATCH(&PONY_PRESETS[17], LIFE_PON_17),    /* ORGAN */
    PPATCH(&PONY_PRESETS[18], LIFE_PON_18),    /* COMBO ORGAN */
    PPATCH(&PONY_PRESETS[19], PONC_PWMW),             /* PWM WHEEL */
    PPATCH(&PONY_PRESETS[20], LIFE_PON_20),    /* PULSE BASS */
    PPATCH(&PONY_PRESETS[21], LIFE_PON_21),    /* SUB BASS */
    PPATCH(&PONY_PRESETS[22], LIFE_PON_22),    /* FUNK BASS */
    PPATCH(&PONY_PRESETS[23], LIFE_PON_23),    /* HOUSE BASS */
    PPATCH(&PONY_PRESETS[24], PONC_MORPH),            /* MORPH BASS */
    PPATCH(&PONY_PRESETS[25], LIFE_PON_25),    /* SAW LEAD */
    PPATCH(&PONY_PRESETS[26], LIFE_PON_26),    /* PWM LEAD */
    PPATCH(&PONY_PRESETS[27], LIFE_PON_27, P_L2RATE + 1, 30),    /* SQUARE LEAD */
    PPATCH(&PONY_PRESETS[28], LIFE_PON_28, P_E2ATK + 1, 60, P_E2DEC + 1, 90, P_E2SUS + 1, 90, P_LRATE + 1, 88),    /* HOLLOW LEAD */
    PPATCH(&PONY_PRESETS[29], LIFE_PON_29),    /* PERC PLUCK */
    PPATCH(&PONY_PRESETS[30], LIFE_PON_30),    /* DARK PLUCK */
    PPATCH(&PONY_PRESETS[31], LIFE_PON_31),    /* POLY STAB */
};

/* (after Clipper 1.2) UNISON (voice.c unison_spread): the DCO and the sub (an octave down) moved by r (Q32) of a
 * turn of the note, the noise from its own place (the note's seed: every voice the same); (Stallion) a singing filter
 * seeded from silence (pony_note_on) from its own place too: the seed split between stages 1 and 2 by r's angle (the
 * same seed in every voice: their rings in phase, WHISTLE's UNISON +8 dB over POLY) */
static void pony_spread(track_t *t, voice_t *v, uint32_t r)
{
    (void)t;
    v->ph[0] += r;
    v->ph[1] += r >> 1;
    v->ph[2] ^= r;
    if (v->s[0] == JF_SEED && !v->s[1]) {
        v->s[0] = (int32_t)(((int64_t)JF_SEED * osc_sine(r + 0x40000000u)) >> 15);
        v->s[1] = (int32_t)(((int64_t)JF_SEED * osc_sine(r)) >> 15);
    }
}

static const engine_t ENG_PONY = {
    "PONY", {"DCO", "VCF"},
    {
        {"WAVE", F_ENUM, 0, 3, JW_SP, N_JWAVE, 0},
        {"PW", F_PCT, 0, 127, 30, 0, 0},
        {"SUB", F_PCT, 0, 127, 40, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"HPF", F_ENUM, 0, 3, 1, N_JHPF, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"CHOR", F_ENUM, 0, 3, 1, N_JCHOR, 0},
    },
    PONY_PRESETS, sizeof(PONY_PRESETS) / sizeof(PONY_PRESETS[0]), 1, pony_note_on, pony_render,
    0x4D7F, {P_E5, P_E6, P_E1, P_E7}, 0, pony_amp, 0, 0,
};
