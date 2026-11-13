/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM6: classic 6-operator FM (SLOOP 2.4, from Felucca 1.0.1). The synthesis is msfa (Dexed's core), ported to
 * integer C in fm6_core.c (Apache-2.0); this file is the engine around it.
 *
 * The patch is the sound: every synth part has one (fm6_patch, the generic 155-byte single-voice layout: six
 * operators with their 4-rate / 4-level envelopes, keyboard level and rate scaling, velocity, ratio or fixed
 * frequency, detune; 32 algorithms, feedback, LFO, pitch envelope, transpose). It is edited in the web editor
 * (EDITOR_PROTOCOL.md FM6_*) and loaded from the PTCH slots. Unlike Felucca 1.0, a SLOOP project does not hold
 * it (project_t is full): the project keeps PTCH (P_E7) as it keeps every macro, and a load (project, preset,
 * user preset, PTCH turned) puts the slot's patch back on the track (fm6_track_loaded / fm6_poll). A patch the
 * editor sent is the track's own until the track loads another; "Store in bank" (fm6_bank.c) keeps it.
 * On the device the eight EDIT values are macros on top of it:
 *   ALG   PAT = the patch's algorithm, 1..32 another one
 *   FB    added to the patch's feedback (0..7)
 *   MLVL  the output level of every operator that is not a carrier (-36 .. +36 dB): the brightness
 *   MRAT  added to the coarse ratio of the modulators (ratio mode only)
 *   MEG   the modulators' envelope times: + slower (rates down to 40 steps), - faster
 *   VMOD  added to the modulators' velocity sensitivity (0..7)
 *   DTUN  spreads the carriers apart in pitch (up to about +-36 cents between the outer ones)
 *   PTCH  loads a patch: F1..F22 the factory patches, B1..B27 the patch bank (fm6_bank.c, flash)
 * The operator envelopes are the voice's amplitude and end it (engine_t.ownenv / done): the track's ADSR and
 * ENV DEST do nothing here. The track's FLT moves MLVL (LFO -> FLT), SHP the feedback, PIT the pitch (glide,
 * tune: the voice's pitch and fine factor). Six voices per part at most (engine_t.poly).
 *
 * State (RAM, NPART parts: the drum track runs no engine): per voice an fm6_note_t (fm6_note, 244 bytes, a
 * side array in the pool: voice_t.s[] is too small), per part the patch, the patch through the macros
 * (fm6_eff, rebuilt in the audio ISR when either changes) and the LFO (once a block). Adapted from Felucca
 * 1.0.1: no modulation matrix, the velocity is the voice's (UNISON scales it), the fine factor comes in
 * vmod_t.fine (voice.c folds it into inc for the other engines). */
#include "fm6_core.c"
#include "felucca_fm6.h"         /* tools/gen_fm6_patches.py: FM6_INIT, FM6_FACTORY[] */

#define FM6_POLY 6               /* engine_t.poly */
#define FM6_BANK_N 27u           /* patch bank slots (fm6_bank.c) */
#define FM6_NSLOT (FM6_NFACTORY + FM6_BANK_N)   /* PTCH: F1..F22, B1..B27 */
#define FM6_PACKED 128u

static uint8_t fm6_patch[NPART][FP_SIZE + 1u];  /* the parts' patches (main loop writes, then fm6_pgen) */
static volatile uint8_t fm6_pgen[NPART];         /* +1 after each write of fm6_patch[t] */
static uint8_t fm6_slot[NPART];                  /* the PTCH value last loaded (main loop); 0xFF = none */
static struct {                                  /* the patch through the macros: the audio ISR's copy */
    uint8_t p[FP_SIZE + 1u];
    uint8_t gen, alg, fb, ok;
    int16_t e[7];                                /* P_E0..P_E6 it was made with */
    int32_t dt[6];                               /* DTUN: per operator, Q24 log2 */
} fm6_eff[NPART];
static fm6_lfo_t fm6_lfo[NPART];
static int32_t fm6_lfo_v[NPART], fm6_lfo_d[NPART];   /* this block's LFO value and delay (Q24) */
/* the voices' fm6_note_t: the part's engine arena (engines.c eng_arena_of) */

/* ------------------------------------------------------- patch formats --- */
/* the highest value of each byte of the 155-byte voice */
static uint32_t fm6_max(uint32_t i)
{
    static const uint8_t OPMAX[21] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
    static const uint8_t VMAX[19] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};
    if (i < 126u)
        return OPMAX[i % 21u];
    if (i < FP_NAME)
        return VMAX[i - 126u];
    return 126u;
}

/* every value inside its range (a name byte outside 32..126 becomes a space) */
static void fm6_sanitize(uint8_t *v)
{
    uint32_t i;
    for (i = 0; i < FP_SIZE; i++) {
        if (i >= FP_NAME)
            v[i] = v[i] < 32u || v[i] > 126u ? ' ' : v[i];
        else if (v[i] > fm6_max(i))
            v[i] = (uint8_t)fm6_max(i);
    }
    v[FP_SIZE] = 0;
}

/* 128-byte packed record (the 32-voice bank's) -> 155-byte voice; bits a record does not use are ignored */
static void fm6_unpack(const uint8_t *b, uint8_t *v)
{
    uint32_t k, i;
    for (k = 0; k < 6u; k++) {
        const uint8_t *o = b + k * 17u;
        uint8_t *d = v + k * FP_OP;
        for (i = 0; i < 11u; i++)
            d[i] = o[i] & 0x7Fu;
        d[FP_LC] = o[11] & 3u;
        d[FP_RC] = (o[11] >> 2) & 3u;
        d[FP_RS] = o[12] & 7u;
        d[FP_DET] = (o[12] >> 3) & 15u;
        d[FP_AMS] = o[13] & 3u;
        d[FP_KVS] = (o[13] >> 2) & 7u;
        d[FP_OL] = o[14] & 0x7Fu;
        d[FP_MODE] = o[15] & 1u;
        d[FP_FC] = (o[15] >> 1) & 31u;
        d[FP_FF] = o[16] & 0x7Fu;
    }
    for (i = 0; i < 9u; i++)
        v[FP_PR1 + i] = b[102 + i] & 0x7Fu;              /* pitch EG, algorithm */
    v[FP_ALG] &= 31u;
    v[FP_FB] = b[111] & 7u;
    v[FP_OKS] = (b[111] >> 3) & 1u;
    for (i = 0; i < 4u; i++)
        v[FP_LFS + i] = b[112 + i] & 0x7Fu;
    v[FP_LKS] = b[116] & 1u;
    v[FP_LFW] = (b[116] >> 1) & 7u;
    v[FP_LPMS] = (b[116] >> 4) & 7u;
    v[FP_TRNSP] = b[117] & 0x7Fu;
    for (i = 0; i < 10u; i++)
        v[FP_NAME + i] = b[118 + i] & 0x7Fu;
    fm6_sanitize(v);
}

/* 155-byte voice -> 128-byte packed record (7-bit bytes: it travels in SysEx as it is) */
static void fm6_pack(const uint8_t *v, uint8_t *b)
{
    uint32_t k, i;
    for (k = 0; k < 6u; k++) {
        const uint8_t *o = v + k * FP_OP;
        uint8_t *d = b + k * 17u;
        for (i = 0; i < 11u; i++)
            d[i] = o[i] & 0x7Fu;
        d[11] = (uint8_t)((o[FP_LC] & 3u) | (o[FP_RC] & 3u) << 2);
        d[12] = (uint8_t)((o[FP_RS] & 7u) | (o[FP_DET] & 15u) << 3);
        d[13] = (uint8_t)((o[FP_AMS] & 3u) | (o[FP_KVS] & 7u) << 2);
        d[14] = o[FP_OL] & 0x7Fu;
        d[15] = (uint8_t)((o[FP_MODE] & 1u) | (o[FP_FC] & 31u) << 1);
        d[16] = o[FP_FF] & 0x7Fu;
    }
    for (i = 0; i < 9u; i++)
        b[102 + i] = v[FP_PR1 + i] & 0x7Fu;
    b[110] &= 31u;
    b[111] = (uint8_t)((v[FP_FB] & 7u) | (v[FP_OKS] & 1u) << 3);
    for (i = 0; i < 4u; i++)
        b[112 + i] = v[FP_LFS + i] & 0x7Fu;
    b[116] = (uint8_t)((v[FP_LKS] & 1u) | (v[FP_LFW] & 7u) << 1 | (v[FP_LPMS] & 7u) << 4);
    b[117] = v[FP_TRNSP] & 0x7Fu;
    for (i = 0; i < 10u; i++)
        b[118 + i] = v[FP_NAME + i] & 0x7Fu;
}

/* ---------------------------------------------------- the track's patch --- */
/* the patch bank (fm6_bank.c sets it with FELUCCA_FLASH): slot k's packed record -> pk, 0 = got it */
static int (*fm6_bank_read)(uint32_t k, uint8_t *pk);
static uint32_t fm6_bank_fmx;                   /* Clipper 0.6 (fm6_bank.c): bit k, slot k holds an FMX patch (eng_fmx.c),
                                                 * an empty slot to FM6 */

/* part tr's patch = v (155 bytes, sanitized). Main loop: the ISR takes it at its next block */
static void fm6_set_patch(uint32_t tr, const uint8_t *v)
{
    uint8_t s[FP_SIZE + 1u];
    if (tr >= NPART)
        return;
    memcpy(s, v, FP_SIZE);
    fm6_sanitize(s);                                /* not in place: fm6_sync must never copy an unsanitized byte */
    memcpy(fm6_patch[tr], s, FP_SIZE + 1u);
    RING_PUBLISH();
    fm6_pgen[tr]++;
}

/* PTCH value s -> its packed record (F1..F22; the bank, an empty slot: the init voice) */
static void fm6_slot_get(uint32_t s, uint8_t *pk)
{
    if (s < FM6_NFACTORY)
        memcpy(pk, FM6_FACTORY[s], FM6_PACKED);
    else if (s >= FM6_NSLOT || !fm6_bank_read || ((fm6_bank_fmx >> (s - FM6_NFACTORY)) & 1u) ||
             fm6_bank_read(s - FM6_NFACTORY, pk))
        memcpy(pk, FM6_INIT, FM6_PACKED);           /* (Clipper 0.6: an FMX patch there: empty to FM6) */
}

static void fm6_load_slot(uint32_t tr, uint32_t s)
{
    uint8_t pk[FM6_PACKED], v[FP_SIZE + 1u];
    if (tr >= NPART)
        return;
    fm6_slot_get(s, pk);
    fm6_unpack(pk, v);
    fm6_set_patch(tr, v);
    fm6_slot[tr] = (uint8_t)s;
}

/* a sound load put a PTCH value in (a preset, a user preset, a project, an engine change): its patch */
static uint8_t fm6_pend;                            /* (SLOOP 2.6) bit per part: a sound's patch waiting for the fade */
#if CLIPPER_SYNTH
static volatile uint8_t fmx_reload;             /* Clipper 0.6: bit p, part p's FMX patch from its PTCH again (a sound
                                                 * loaded: the edits not stored go; eng_fmx.c fmx_block) */
#endif
static void fm6_track_loaded(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk);
#if CLIPPER_SYNTH
    if (tr < NPART && t->eng_req == ENGI_FMX)
        fmx_reload |= (uint8_t)(1u << tr);
#endif
    if (tr < NPART && t->eng_req == ENGI_FM6) {
        uint32_t i, any = 0;
        for (i = 0; i < NVOICE; i++)
            any |= t->v[i].active;
        if (any && t->xf_on) {                          /* the old sound still fading out (voice.c sound_fade_begin, an
                                                         * engine switch): its patch stays until then (fm6_poll;
                                                         * engine_block holds the new notes for it). Before, the old
                                                         * notes took the new patch at once: a click */
            fm6_pend |= (uint8_t)(1u << tr);
            return;
        }
        fm6_load_slot(tr, (uint32_t)clamp(t->p[P_E7], 0, FM6_NSLOT - 1));
    }
}

/* power-on: every part the init voice */
static void fm6_init(void)
{
    uint8_t v[FP_SIZE + 1u];
    uint32_t tr;
    fm6_unpack(FM6_INIT, v);
    for (tr = 0; tr < NPART; tr++) {
        fm6_set_patch(tr, v);
        fm6_slot[tr] = 0xFFu;
    }
}

/* main loop: PTCH turned (a knob, the editor, MIDI, a lock) -> that patch */
static void fm6_poll(void)
{
    uint32_t tr;
    for (tr = 0; tr < NPART; tr++) {
        if (trk[tr].xf_on && trk[tr].xf)
            continue;                                   /* (a fade under way: the patch after it; faded out, the
                                                         * notes wait for it: voice.c engine_block) */
        if (trk[tr].eng_req == ENGI_FM6 && (trk[tr].p[P_E7] != fm6_slot[tr] || ((fm6_pend >> tr) & 1u)))
            fm6_load_slot(tr, (uint32_t)clamp(trk[tr].p[P_E7], 0, FM6_NSLOT - 1));
        fm6_pend &= (uint8_t)~(1u << tr);
    }
}

/* -------------------------------------------------------------- macros --- */
/* the patch through E0..E6 (audio ISR; cheap when nothing changed) */
static void fm6_sync(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk), k, j, car, n = 0;
    const int16_t *e = &t->p[P_E0];
    int32_t meg, step;
    if (tr >= NPART)
        return;
    if (fm6_eff[tr].ok && fm6_eff[tr].gen == fm6_pgen[tr]) {
        for (k = 0; k < 7u && fm6_eff[tr].e[k] == e[k]; k++)
            ;
        if (k == 7u)
            return;
    }
    fm6_eff[tr].gen = fm6_pgen[tr];
    memcpy(fm6_eff[tr].p, fm6_patch[tr], FP_SIZE);
    for (k = 0; k < 7u; k++)
        fm6_eff[tr].e[k] = e[k];
    {
        uint8_t *p = fm6_eff[tr].p;
        uint32_t alg = e[0] >= 1 && e[0] <= 32 ? (uint32_t)e[0] - 1u : p[FP_ALG] & 31u;
        fm6_eff[tr].alg = (uint8_t)alg;
        fm6_eff[tr].fb = (uint8_t)clamp(p[FP_FB] + e[1], 0, 7);
        car = fm6_carriers(alg);
        meg = clamp(e[4], -64, 63) * 40 / 64;               /* MEG: rate steps (+ slower) */
        step = clamp(e[6], 0, 127) * (12 * 13981) / 127;    /* DTUN: up to 12 cents a step (1 cent = 13981) */
        for (k = 0; k < 6u; k++) {
            uint8_t *op = p + (5u - k) * FP_OP;              /* OP1 first: carriers in their order */
            uint32_t ki = 5u - k;
            fm6_eff[tr].dt[ki] = 0;
            if ((car >> ki) & 1u) {                          /* carriers 0, +1, -1, +2, -2, +3 steps apart */
                static const int8_t SPREAD[6] = {0, 1, -1, 2, -2, 3};
                fm6_eff[tr].dt[ki] = SPREAD[n++] * step;
                continue;
            }
            if (!op[FP_MODE])
                op[FP_FC] = (uint8_t)clamp(op[FP_FC] + e[3], 0, 31);
            for (j = 0; j < 4u; j++)
                op[FP_R1 + j] = (uint8_t)clamp(op[FP_R1 + j] - meg, 0, 99);
            op[FP_KVS] = (uint8_t)clamp(op[FP_KVS] + e[5], 0, 7);
        }
        fm6_lfo_reset(&fm6_lfo[tr], p);
    }
    fm6_eff[tr].ok = 1;
}

/* --------------------------------------------------------------- voice --- */
static fm6_note_t *fm6_note_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < FM6_POLY ? &((fm6_note_t *)eng_arena_of(t, ENGI_FM6))[i] : 0;
}

static void fm6_note_on(track_t *t, voice_t *v)
{
    fm6_note_t *n = fm6_note_of(t, v);
    uint32_t tr = (uint32_t)(t - trk);
    const uint8_t *p;
    if (!n)
        return;
    fm6_sync(t);
    p = fm6_eff[tr].p;
    fm6_note_init(n, p, (int32_t)v->note + p[FP_TRNSP] - 24, v->vel,
                  (!v->env && !v->env_out) || !n->live);    /* from silence: phases and gains from 0 */
    fm6_lfo_key(&fm6_lfo[tr]);
}

static void fm6_block(track_t *t)       /* once a block and part: the macros, the LFO */
{
    uint32_t tr = (uint32_t)(t - trk);
    if (tr >= NPART)
        return;
    fm6_sync(t);
    fm6_lfo_v[tr] = fm6_lfo_sample(&fm6_lfo[tr]);
    fm6_lfo_d[tr] = fm6_lfo_delay(&fm6_lfo[tr]);
}

/* every carrier has gone silent for good: the voice ends (voice.c, engine_t.done) */
static int fm6_done(track_t *t, voice_t *v)
{
    fm6_note_t *n = fm6_note_of(t, v);
    uint32_t tr = (uint32_t)(t - trk);
    if (!n || !n->live)
        return 1;
    if (!fm6_note_done(n, fm6_eff[tr].p, fm6_eff[tr].alg))
        return 0;
    n->live = 0;
    return 1;
}

static int32_t fm6_mbus[2][2][FM6_N];               /* (0.7) the operators' modulator buses, one pair per core */
static void fm6_render(track_t *t, voice_t *v, int32_t *out, uint32_t len, const vmod_t *m)
{
    const int16_t *ev = vm_ev(t, m);              /* the EDIT values: the voice's own or the part's */
    fm6_note_t *n = fm6_note_of(t, v);
    uint32_t tr = (uint32_t)(t - trk), i;
    const uint8_t *p;
    int32_t bus[FM6_N], lf, fb, lvl, k, a, da;
    if (!n || !n->live || len != FM6_N)
        return;
    p = fm6_eff[tr].p;
    if (!v->gate && n->down)
        fm6_note_key(n, p, 0);
    lf = fm6_note_logfreq(m->pitch16 + (p[FP_TRNSP] - 24) * 16, m->fine);
    fb = clamp(fm6_eff[tr].fb + ((m->shape - (64 << 8)) >> 11), 0, 7);
    lvl = clamp(ev[2], -64, 63) * 24 + clamp(m->cutoff >> 8, -150, 150) * 16;   /* microsteps */
    if (!fm6_note_compute(n, p, bus, fm6_mbus[C1_IX(out)], fm6_lfo_v[tr], fm6_lfo_d[tr], lf, fm6_eff[tr].alg, fb,
                          fm6_eff[tr].dt, lvl << 16))
        return;                                         /* every carrier below the threshold */
    /* the voice's amplitude ramp x VOICE_FS (one carrier at full: VOICE_FS / 2), six voices in unison ~ one */
    k = t->p[P_VOICE] == V_UNISON ? VOICE_FS * 2 / 5 : VOICE_FS;
#if CLIPPER_SYNTH   /* (after Clipper 1.2) UNI4 / UNI2 as well: 4 (2) voices at random phases ~ one (they had 2 (1.4)) */
    if (t->p[P_VOICE] == V_PUNI4 || t->p[P_VOICE] == V_PUNI2)
        k = t->p[P_VOICE] == V_PUNI4 ? VOICE_FS / 2 : VOICE_FS * 7 / 10;
#endif
    a = mulq15(m->amp0, k);
    da = (mulq15(m->amp1, k) - a) >> FM6_LG_N;
    for (i = 0; i < FM6_N; i++) {
        int32_t x = bus[i] - n->dc;
        n->dc += x >> 10;                                 /* a one-pole high-pass, ~7 Hz: FM puts sidebands at 0 Hz */
        a += da;
        out[i] += (int32_t)(((int64_t)x * a) >> 26);
    }
}

/* --------------------------------------------------------- the engine --- */
static const char *const N_FM6_ALG[] = {"PAT", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13",
                                        "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25",
                                        "26", "27", "28", "29", "30", "31", "32", 0};
static const char *const N_FM6_PATCH[] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
                                          "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "B1", "B2", "B3", "B4",
                                          "B5", "B6", "B7", "B8", "B9", "B10", "B11", "B12", "B13", "B14", "B15",
                                          "B16", "B17", "B18", "B19", "B20", "B21", "B22", "B23", "B24", "B25",
                                          "B26", "B27", 0};
_Static_assert(NELEM(N_FM6_PATCH) == FM6_NSLOT + 1u, "a PTCH name per slot");

/* {ALG, FB, MLVL, MRAT, MEG, VMOD, DTUN, PTCH}: a factory patch (PTCH 0 = F1), as it is or through the macros. The ADSR
 * does nothing on this engine (ownenv): kept at its defaults. Dry (Stallion 1.0: no sends in a factory sound) */
#define FM6P(name, alg, fb, mlvl, mrat, meg, vmod, dtun, ptch, mono, xp) \
    {name, {alg, fb, mlvl, mrat, meg, vmod, dtun, ptch}, {0, 0, 127, 0}, 0, mono, FX(0, 0, 0, 0), xp}
static const preset_t FM6_PRESETS[] = {
    FM6P("INIT", 0, 0, 0, 0, 0, 0, 0, 0, 0, XP(0)),
    FM6P("TINE EP", 0, 0, 0, 0, 0, 0, 0, 1, 0, XP(0)),
    FM6P("BRIGHT EP", 0, 0, 14, 0, 0, 1, 0, 1, 0, XP(0)),
    FM6P("MELLOW EP", 0, 0, -14, 0, 0, 0, 0, 1, 0, XP(0)),
    FM6P("REED EP", 0, 0, 0, 0, 0, 0, 0, 9, 0, XP(0)),
    FM6P("FUNK CLAV", 0, 0, 0, 0, 0, 0, 0, 10, 0, XP(0)),
    FM6P("SYNTH CLAV", 0, 0, 10, 1, 0, 0, 0, 10, 0, XP(0)),
    FM6P("HARPSICHORD", 0, 0, 0, 0, 0, 0, 0, 11, 0, XP(0)),
    FM6P("FM GRAND", 0, 0, 0, 0, 0, 0, 0, 12, 0, XP(0)),
#if CLIPPER_SYNTH
    /* (Stallion 1.0, no bells, no imitations: the replacements play the synth patches through the macros, the shared
     * patch bank as it is; FM6_PATCHES moves them) */
    FM6P("WARM PAD", 0, 0, 8, 0, 10, 0, 30, 5, 0, XP(0)),                  /* F6 PAD, brighter, slower, spread */
    FM6P("SWEEP PAD", 0, 0, -4, 0, 0, 0, 24, 18, 0, XP(0)),                /* F19 STRINGS, ENV 2 opening it */
    FM6P("FM CHOIR", 0, 0, -12, 1, 0, 0, 16, 5, 0, XP(P_LD_PIT + 1, 1, P_LRATE + 1, 70)),   /* F6, modulators x2 */
    FM6P("BRASS PAD", 0, 0, -6, 0, 30, 0, 24, 4, 0, XP(0)),                /* F5 BRASS, its brightness swelling */
    FM6P("DIGI KEYS", 0, 0, -6, 1, 0, 0, 0, 1, 0, XP(P_MWDST + 1, MD_TREM)),   /* F2 TINE EP, modulators up */
    FM6P("FM PLUCK", 0, 0, 6, 0, -20, 0, 10, 3, 0, XP(0)),                 /* F4 ROUND BASS, faster modulators */
    FM6P("BRASS LEAD", 0, 1, 0, 0, 0, 0, 0, 4, 1, XP(P_GLIDE + 1, 20)),    /* F5 BRASS, mono */
#else
    FM6P("MARIMBA", 0, 0, 0, 0, 0, 0, 0, 6, 0, XP(0)),
    FM6P("XYLOPHONE", 0, 0, 12, -1, 20, 0, 0, 6, 0, XP(0)),
    FM6P("VIBES", 0, 0, 0, 0, 0, 0, 0, 13, 0, XP(0)),
    FM6P("TUBULAR BELL", 0, 0, 0, 0, 0, 0, 0, 2, 0, XP(0)),
    FM6P("STEEL DRUM", 0, 0, 0, 0, 0, 0, 0, 14, 0, XP(0)),
    FM6P("KOTO", 0, 0, 0, 0, 0, 0, 0, 15, 0, XP(0)),
    FM6P("NYLON GUITAR", 0, 0, 0, 0, 0, 0, 0, 8, 0, XP(0)),
#endif
    FM6P("FM BASS", 0, 0, 0, 0, 0, 0, 0, 16, 1, XP(0)),
#if CLIPPER_SYNTH
    FM6P("PUNCH BASS", 0, 0, 6, 0, 0, 2, 0, 16, 1, XP(0)),                /* F17 BASS 1, brighter, velocity */
#else
    FM6P("SLAP BASS", 0, 0, 10, 0, 0, 2, 0, 16, 1, XP(0)),
#endif
    FM6P("ROUND BASS", 0, 0, 0, 0, 0, 0, 0, 3, 1, XP(0)),
    FM6P("SYNTH BASS", 0, 0, 8, 1, 0, 0, 0, 3, 1, XP(0)),
    FM6P("BRASS SECT", 0, 0, 0, 0, 0, 0, 0, 4, 0, XP(0)),
    FM6P("SYNTH BRASS", 0, 2, 10, 0, 10, 0, 0, 4, 0, XP(0)),
#if CLIPPER_SYNTH
    FM6P("SOFT BRASS", 0, 0, -14, 0, 0, 0, 0, 4, 0, XP(0)),               /* F5 BRASS, mellow */
#else
    FM6P("SOFT HORNS", 0, 0, -14, 0, 0, 0, 0, 4, 0, XP(0)),
#endif
    FM6P("DRAWBARS", 0, 0, 0, 0, 0, 0, 0, 7, 0, XP(0)),
    FM6P("ROCK ORGAN", 31, 5, 0, 0, 0, 0, 15, 7, 0, XP(0)),
    FM6P("PIPE ORGAN", 0, 0, 0, 0, 0, 0, 0, 17, 0, XP(0)),
    FM6P("FM STRINGS", 0, 0, 0, 0, 0, 0, 0, 18, 0, XP(0)),
    FM6P("ENSEMBLE", 0, 0, 0, 0, 0, 0, 40, 18, 0, XP(0)),
    FM6P("SOFT PAD", 0, 0, 0, 0, 0, 0, 30, 5, 0, XP(0)),
#if CLIPPER_SYNTH
    FM6P("SOFT LEAD", 0, -3, -20, 0, 0, 0, 0, 21, 1, XP(P_GLIDE + 1, 16)), /* F22 SYNTH LEAD, darker */
    FM6P("SQUARE LEAD", 0, -4, 0, 1, 0, 0, 0, 21, 1, XP(P_GLIDE + 1, 12)), /* F22, modulators x2: odd harmonics */
#else
    FM6P("FLUTE", 0, 0, 0, 0, 0, 0, 0, 19, 1, XP(P_GLIDE + 1, 10)),
    FM6P("HARMONICA", 0, 0, 0, 0, 0, 0, 0, 20, 1, XP(P_GLIDE + 1, 10)),
#endif
    FM6P("SYNTH LEAD", 0, 0, 0, 0, 0, 0, 0, 21, 1, XP(P_GLIDE + 1, 24)),
};
#undef FM6P

#if CLIPPER_SYNTH
/* (Stallion 1.0, no bells, no imitations) the replacements' patching (core.h preset_patch_t; params.c PATCH_TABLES).
 * What FM6 answers to: PITCH, CUT and E3 (MLVL: the modulators' level, the brightness, fm6_render), SHAPE (the
 * feedback, in steps), AMP, and the LFOs' rates; the operator envelopes are the patch's (no ENV 1, TRIG1), and the
 * other EDIT values are read once a block from the part (fm6_sync): a cable to them would do nothing, so none goes
 * there. The default AT> is CUT (MLVL), MW> the vibrato */
static const pcable_t F6C_WARM[] = {      /* WARM PAD */
    CB(1, LFO2, CUT, 16),                 /* LFO 2: the brightness breathing slowly */
    CB(2, LFO3, L2RATE, 14),              /* LFO 3: that breathing's rate drifting */
    CB(3, MW, CUT, 30),                   /* the wheel: brighter */
    CB(4, RAND, CUT, 10),                 /* each note a little different */
};
static const pcable_t F6C_SWPP[] = {      /* SWEEP PAD */
    CB(1, ENV2, CUT, 40),                 /* ENV 2: the brightness opening slowly */
    CB(2, LFO2, CUT, 16),                 /* LFO 2: a slow sweep */
    CB(3, LFO3, PITCH, 3),                /* LFO 3: a few cents of drift */
    CB(4, KEY, CUT, -10),                 /* high notes softer */
};
static const pcable_t F6C_CHOIR[] = {     /* FM CHOIR */
    CB(1, LFO2, E1 + 2, 6),               /* LFO 2: MLVL moving, the vowel shifting */
    CB(2, LFO3, L2RATE, 10),              /* LFO 3: that movement's rate drifting */
    CB(3, VEL, CUT, 14),                  /* velocity: brighter */
    CB(4, MW, SHAPE, 24),                 /* the wheel: more feedback, breath */
};
static const pcable_t F6C_BPAD[] = {      /* BRASS PAD */
    CB(1, LFO2, CUT, 6),                  /* LFO 2: a slow shimmer */
    CB(2, LFO3, L2RATE, 12),              /* LFO 3: its rate drifting */
    CB(3, VEL, CUT, 20),                  /* velocity: brighter */
    CB(4, MW, CUT, 24),                   /* the wheel: brighter */
};
static const pcable_t F6C_DKEY[] = {      /* DIGI KEYS */
    CB(1, VEL, CUT, 24),                  /* velocity: the bark */
    CB(2, LFO2, CUT, 8),                  /* LFO 2: a gentle shimmer */
    CB(3, KEY, CUT, -12),                 /* high notes mellower */
};
static const pcable_t F6C_PLCK[] = {      /* FM PLUCK */
    CB(1, ENV2, CUT, 30),                 /* ENV 2: the pluck, bright and falling to the body */
    CB(2, VEL, CUT, 20),                  /* velocity: brighter */
    CB(3, ENV2, SHAPE, 12),               /* .. a burst of feedback on the attack */
    CB(4, LFO3, CUT, 6),                  /* LFO 3: a slow shimmer */
    CB(5, KEY, CUT, -8),                  /* high notes rounder */
};
static const pcable_t F6C_BRLD[] = {      /* BRASS LEAD */
    CB(1, ENV2, PITCH, -6),               /* ENV 2: a scoop up into each note */
    CB(2, VEL, CUT, 24),                  /* velocity: brighter */
    CB(3, LFO2, CUT, 8),                  /* LFO 2: a shimmer */
    CB(4, MW, SHAPE, 24),                 /* the wheel: more feedback, a rasp */
};
static const pcable_t F6C_PNCH[] = {      /* PUNCH BASS */
    CB(1, VEL, CUT, 24),                  /* velocity: brighter */
    CB(2, ENV2, CUT, 28),                 /* ENV 2: a bright attack, falling */
    CB(3, MW, SHAPE, 24),                 /* the wheel: more feedback, a growl */
    CB(4, RAND, CUT, 6),                  /* each note a little different */
};
static const pcable_t F6C_SBRS[] = {      /* SOFT BRASS */
    CB(1, VEL, CUT, 12),                  /* velocity: brighter */
    CB(2, LFO2, CUT, 6),                  /* LFO 2: a slow shimmer */
    CB(3, ENV2, CUT, 18),                 /* ENV 2: a bloom after the attack */
    CB(4, LFO3, L2RATE, 10),              /* LFO 3: the shimmer's rate drifting */
};
static const pcable_t F6C_SLEAD[] = {     /* SOFT LEAD */
    CB(1, VEL, CUT, 24),                  /* velocity: brighter */
    CB(2, ENV2, CUT, 24),                 /* ENV 2: a touch of brightness on the attack */
    CB(3, LFO2, CUT, 10),                 /* LFO 2: a shimmer */
    CB(4, LFO3, L2RATE, 12),              /* LFO 3: its rate drifting */
};
static const pcable_t F6C_SQLD[] = {      /* SQUARE LEAD */
    CB(1, LFO2, CUT, 16),                 /* LFO 2: the brightness moving like a PWM */
    CB(2, LFO3, L2RATE, 12),              /* LFO 3: that movement's rate drifting */
    CB(3, VEL, CUT, 16),                  /* velocity: brighter */
    CB(4, KEY, CUT, -8),                  /* high notes rounder */
};
static const preset_patch_t FM6_PATCHES[] = {
    PPATCH(&FM6_PRESETS[9], F6C_WARM, P_L2RATE + 1, 28, P_L3RATE + 1, 16),         /* WARM PAD */
    PPATCH(&FM6_PRESETS[10], F6C_SWPP, P_E2ATK + 1, 100, P_E2SUS + 1, 90, P_L2RATE + 1, 14, P_L3RATE + 1, 22),   /* SWEEP PAD */
    PPATCH(&FM6_PRESETS[11], F6C_CHOIR, P_L2RATE + 1, 34, P_L3RATE + 1, 18),       /* FM CHOIR */
    PPATCH(&FM6_PRESETS[12], F6C_BPAD, P_L2RATE + 1, 40, P_L3RATE + 1, 20),        /* BRASS PAD */
    PPATCH(&FM6_PRESETS[13], F6C_DKEY, P_L2RATE + 1, 60),                          /* DIGI KEYS */
    PPATCH(&FM6_PRESETS[14], F6C_PLCK, P_E2DEC + 1, 24, P_L3RATE + 1, 30),         /* FM PLUCK */
    PPATCH(&FM6_PRESETS[15], F6C_BRLD, P_E2DEC + 1, 24, P_L2RATE + 1, 74),         /* BRASS LEAD */
    PPATCH(&FM6_PRESETS[17], F6C_PNCH, P_E2DEC + 1, 26),                           /* PUNCH BASS */
    PPATCH(&FM6_PRESETS[22], F6C_SBRS, P_L2RATE + 1, 44, P_E2ATK + 1, 40, P_E2DEC + 1, 80, P_E2SUS + 1, 40),   /* SOFT BRASS */
    PPATCH(&FM6_PRESETS[29], F6C_SLEAD, P_E2DEC + 1, 30, P_L2RATE + 1, 66, P_L3RATE + 1, 20),   /* SOFT LEAD */
    PPATCH(&FM6_PRESETS[30], F6C_SQLD, P_L2RATE + 1, 48, P_L2WAVE + 1, LW_TRI, P_L3RATE + 1, 25),   /* SQUARE LEAD */
};
#endif

/* (after Clipper 1.2) UNISON (voice.c unison_spread): the six operators moved together by r (Q32) of a turn of
 * the note, each by its own frequency's share (fixed ones too): the relative phases, the timbre, kept (phases 0 from
 * silence: every voice in phase) */
static void fm6_spread(track_t *t, voice_t *v, uint32_t r)
{
    fm6_note_t *nt = fm6_note_of(t, v);
    uint32_t tr = (uint32_t)(t - trk), k;
    int32_t lf, f0;
    if (!nt || tr >= NPART)
        return;
    lf = fm6_note_logfreq(v->pitch16 + (fm6_eff[tr].p[FP_TRNSP] - 24) * 16, v->fine);
    f0 = fm6_freq(lf);
    if (f0 <= 0)
        return;
    for (k = 0; k < 6u; k++) {
        int32_t fk = fm6_freq((nt->fixed >> k) & 1u ? nt->base[k] : lf + nt->base[k]);   /* Q24 cycles a sample */
        nt->op[k].phase = (int32_t)((uint32_t)nt->op[k].phase + (uint32_t)(((uint64_t)(r >> 8) * (uint32_t)fk) / (uint32_t)f0));
    }
}

static const engine_t ENG_FM6 = {
    "FM6", {"OPS", "PATCH"},
    {
        {"ALG", F_INT, 0, 32, 0, N_FM6_ALG, 0},
        {"FB", F_INT, -7, 7, 0, 0, 0},
        {"MLVL", F_BIPCT, -64, 63, 0, 0, 0},
        {"MRAT", F_INT, -16, 16, 0, 0, 0},
        {"MEG", F_BIPCT, -64, 63, 0, 0, 0},
        {"VMOD", F_INT, -7, 7, 0, 0, 0},
        {"DTUN", F_PCT, 0, 127, 0, 0, 0},
        {"PTCH", F_INT, 0, FM6_NSLOT - 1, 0, N_FM6_PATCH, 0},
    },
    FM6_PRESETS, NELEM(FM6_PRESETS), -1, fm6_note_on, fm6_render,
    0x4E99, {P_E2, P_E3, P_E4, P_E7}, FM6_POLY, 0, 0, fm6_block,
    .ownenv = 1, .done = fm6_done,
};
