/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef UI_SMALL
#define UI_SMALL                                   /* (gfx.c: minsize on the device; a test alone: nothing) */
#endif
/* Changes: Copyright (C) 2026 Eberon Limited (John Gorman) */
/* Engine table: order = PRESETS browsing order (and the engine numbers of the editor protocol). */
#include "dsp.c"
/* SLOOP 2.5: the big per-part state of GRAIN, FM6 and PHYS shares one arena per synth part (a part plays one
 * engine at a time: on a switch the old engine fades out, its voices end, then the new one starts, voice.c
 * engine_block). The first engine to ask for it after another one gets it cleared, as at power-on. */
static void *eng_arena_of(const track_t *t, uint32_t eng);
static inline int32_t voice_amp(int32_t s, const vmod_t *m, uint32_t i) { return mulq15(mulq15(s, amp_at(m, i)), VOICE_FS); }
static inline int32_t soft_knee(int32_t y, int32_t k)   /* linear up to k, then only the peaks saturate */
{
    int32_t a = y < 0 ? -y : y;
    if (a <= k)
        return y;
    a = k + (softclip((a - k) * 2) >> 1);
    return y < 0 ? -a : a;
}
/* 0.7: the second core renders some of a part's voices (voice.c track_render) into vs_buf while this core renders the
 * others: an engine's scratch that is not on the stack is one per core, picked by C1_IX(out) (1: the second core's) */
static int32_t vs_buf[CTL] __attribute__((aligned(8)));
#if CLIPPER_SYNTH                       /* (1.0) SPREAD: a voice alone into a scratch first (voice.c vrender), one a core */
static int32_t vs_tmp[2][CTL] __attribute__((aligned(8)));
#define C1_IX(out) ((out) == vs_buf || (out) == vs_tmp[1] ? 1u : 0u)
#else
#define C1_IX(out) ((out) == vs_buf ? 1u : 0u)
#endif
/* (0.9) the filter MODEL the engines with a low-pass of their own play (dsp.c fmod_*): P_FMODEL after the short duck
 * a change of it brings (fx.c mix_part1: no click), SVF in the groovebox */
static inline int32_t fm_model(const track_t *t)
{
#if CLIPPER_SYNTH
    return t->fm_eff ? t->fm_eff : clamp(t->p[P_FMODEL], FM_SVF, FM_LAST);
#else
    (void)t;
    return FM_SVF;
#endif
}
#if CLIPPER_SYNTH
/* (P_FSLOPE) what else of the filter waits for the duck (a change of it would crack: the states mean something else):
 * 1 + the SLOPE + 3 x FILTER 2's ROUTE + 9 x its MODL (its ROUTE only while it is on; 0: none yet) */
static inline int32_t fx_want(const track_t *t)
{
    int32_t m2 = clamp(t->p[P_F2MODEL], 0, FM_LAST);
    return 1 + clamp(t->p[P_FSLOPE], FS_12, FS_48) + (m2 ? 3 * clamp(t->p[P_F2ROUTE], F2_SER, F2_SPLIT) + 9 * m2 : 0);
}
static inline int32_t fx_now(const track_t *t) { return (t->fx_eff ? t->fx_eff : fx_want(t)) - 1; }
static inline int32_t fm_model2(const track_t *t) { return fx_now(t) / 9; }    /* FILTER 2's MODL they play, 0 OFF */
static inline int32_t fm_route(const track_t *t) { return fx_now(t) / 3 % 3; }  /* .. its ROUTE */
#endif
static inline int32_t fm_slope(const track_t *t)          /* the SLOPE they play (dsp.c fmod_slope) */
{
#if CLIPPER_SYNTH
    return fx_now(t) % 3;
#else
    (void)t;
    return FS_24;
#endif
}
/* FILTER 2 (FX > FILTER 2): set once a block (f2_setup, 0: off). Its cutoff: with LINK (FX > FILTER) the MODEL
 * filter's (its CUT, the keyboard, every modulation) + CUT - 64 steps, one knob sweeping both; without, CUT, the
 * keyboard half and the voice's modulation (ENV, LFO, VEL>F, MW / AT, BRITE, the matrix's CUT: the two move together);
 * then the matrix's CUT2, RES + RES2. Its own slope (SLOPE is the first's). State: fs[4..7] */
typedef struct {
    fmod_t f;
    uint8_t on, route;
    int32_t *sd;                                         /* SPLIT: this core's side scratch, else 0 */
} f2_t;
static int32_t f2_sd[2][CTL] __attribute__((aligned(8)));   /* the voice's side (SPLIT), one a core: C1_IX (voice.c
                                                         * vrender clears it, adds it to the part's) */
#if BOARD_SMK37                                         /* (Stallion: on the SMK-37 Pro f2_setup runs from the flash, */
#define F2_SETUP HOT_FAR(f2_setup)                      /* a call a block, its RAM code area full: tools/build.py */
#define F2_COEF HOT_FAR(fmod_coef)                      /* HOT_NOT_SMK37) */
#else
#define F2_SETUP f2_setup
#define F2_COEF fmod_coef
#endif
static int f2_setup(f2_t *z, const track_t *t, const voice_t *v, const vmod_t *m, int32_t cut1, int link, int32_t *out)
{
#if CLIPPER_SYNTH
    int32_t m2 = fm_model2(t), cut;
    z->on = (uint8_t)(m2 != 0);
    z->sd = 0;
    if (!m2)
        return 0;
    cut = link && t->p[P_F2LINK] ? cut1 + ((t->p[P_F2CUT] - 64) << 8) :
          (t->p[P_F2CUT] << 8) + m->cutoff + ((v->pitch16 - 60 * 16) << 3);
    F2_COEF(&z->f, m2, cut + m->cut2, t->p[P_F2RES] + m->res2);
    z->route = (uint8_t)fm_route(t);
    if (z->route == F2_SPLIT)
        z->sd = f2_sd[C1_IX(out)];
    return 1;
#else
    (void)t, (void)v, (void)m, (void)cut1, (void)link, (void)out;
    z->on = 0;
    z->sd = 0;
    return 0;
#endif
}
/* in the engines with the MODEL filter (f2_own: ANALOG, WAVE, VECTOR, PONY's OTA), beside it: f the engine's filter
 * (fmod_coef, fmod_slope done) becomes the pair (dsp.c fmod_pair, its room p); 1: the engine's loop now goes through
 * fmod_tick (its model FM_PAIR), and f2_pair_end keeps FILTER 2's state after it */
typedef struct { fmod_t a, b; f2st_t st; } f2pair_t;
_Static_assert(sizeof(fmod_t) % 4u == 0, "copy32 (dsp.c) copies words");
static int f2_pair(fmod_t *f, f2pair_t *p, const track_t *t, const voice_t *v, const vmod_t *m, int32_t cut1, int32_t *out)
{
    f2_t z;
    if (!F2_SETUP(&z, t, v, m, cut1, 1, out))
        return 0;
    copy32(&p->a, f, sizeof *f);                       /* (= *f, = z.f: Stallion, dsp.c copy32) */
    copy32(&p->b, &z.f, sizeof z.f);
    p->st.s[0] = v->fs[4], p->st.s[1] = v->fs[5], p->st.s[2] = v->fs[6], p->st.s[3] = v->fs[7];
    p->st.sd = z.sd;
    f->model = FM_PAIR;
    f->route = z.route;
    f->p1 = &p->a, f->p2 = &p->b;
    f->st = &p->st;
    return 1;
}
static void f2_pair_end(const f2pair_t *p, voice_t *v)
{
    v->fs[4] = p->st.s[0], v->fs[5] = p->st.s[1], v->fs[6] = p->st.s[2], v->fs[7] = p->st.s[3];
}
/* (pitch_inc: felucca_tables.h, SLOOP 2.5: from the top octave) */
/* (after Clipper 1.2) UNISON's start-phase spread (voice.c unison_spread): r (Q32: a turn of the note at pitch p16,
 * 1/16 semitone) in turns of an oscillator d16 (1/16 semitone) above it, the share of the same time; per note */
static uint32_t uni_turns(uint32_t r, int32_t p16, int32_t d16)
{
    uint32_t b = pitch_inc((uint32_t)clamp(p16, 0, 2047));
    return b ? (uint32_t)(((uint64_t)r * pitch_inc((uint32_t)clamp(p16 + d16, 0, 2047))) / b) : r;
}

#include "eng_analog.c"
#include "eng_digital.c"
#include "eng_phase.c"
#include "eng_sample.c"
#include "eng_formant.c"
#include "eng_trio.c"
/* (Stallion 1.0) ENGINES[ENGI_GONE] (core.h): the empty place LOFI left. Nothing selects it (eng_live); were a voice
 * ever to start there, it would sound nothing */
static void gone_note_on(track_t *t, voice_t *v) { (void)t, (void)v; }
static void gone_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t, (void)v, (void)m;
    memset(out, 0, n * sizeof *out);
}
static const engine_t ENG_GONE = {"", {"", ""}, {{0}}, 0, 0, -1, gone_note_on, gone_render, 0, {P_E0, P_E1, P_E2, P_E3}, 0};
#include "eng_drawbar.c"
#include "eng_grain.c"
#include "eng_fm6.c"            /* FM6: 6-operator FM, msfa ported (fm6_core.c, Apache-2.0); SLOOP 2.4 */
#include "eng_phys.c"           /* PHYS: physical models, from Felucca 1.0 (DaisySP, Rings: MIT); SLOOP 2.5 */
#include "eng_noise.c"          /* NOISE: from Felucca 1.0; SLOOP 2.5 */
#include "eng_hybrid.c"         /* HYBRID: the Clipper voice (3 oscillators, 2 filters, the matrix) */
#if CLIPPER_SYNTH
#include "eng_wave.c"           /* WAVE: wavetables (Clipper 0.2) */
#include "eng_acid.c"           /* ACID: a 303-style bass line */
#include "eng_strings.c"        /* STRINGS: a string machine */
#include "eng_macro.c"          /* MACRO: chords, swarm, additive, speech (after Plaits) */
#include "eng_vps.c"            /* VPS: vector phaseshaping (Clipper 0.5) */
#include "eng_terrain.c"        /* TERRAIN: wave terrain, lite (Clipper 0.5) */
#include "eng_fmx.c"            /* FMX: six operators, waves, PM / FM / RING, any routing (Clipper 0.6) */
#include "eng_wseq.c"           /* WSEQ: wave sequencing (Clipper 0.6) */
#include "eng_pony.c"           /* PONY: the 1980s DCO poly, its chorus in fx.c (Clipper 0.8) */
#include "eng_vector.c"         /* VECTOR: vector synthesis (Clipper 0.8) */
#include "eng_palomino.c"        /* PALOMINO: the flagship poly voice (two VCOs, XMOD, SYNC; dsp.c ota_tick) */
#include "eng_stampede.c"           /* STAMPEDE: the dual-channel poly voice (two channels, HPF -> LPF, the IL / AL envelope, RING) */
#include "eng_rodeo.c"           /* RODEO: OberVave's voice (zvenson, GPL-3.0-only): two oscillators, sync, X-MOD, SVF */
#include "eng_mare.c"          /* MARE: a semi-modular paraphonic voice (two VCOs, two ladders SPACE apart, in stereo) */
#endif

typedef union {
    gr_part_t grain;
    fm6_note_t fm6[FM6_POLY];
    uint32_t phys[PHYS_ARENA / 4u];      /* eng_phys.c: two SYMP slots or three small ones */
    hy_part_t hybrid;                    /* eng_hybrid.c: the voices */
#if CLIPPER_SYNTH
    fmx_part_t fmx;                      /* eng_fmx.c: the voices */
    mar_part_t mare;                     /* eng_mare.c: VCO 3 / 4, PARA, the stereo delay's lines */
#endif
} eng_arena_t;
static eng_arena_t eng_arena[NPART] __attribute__((section(".pool")));
#if CLIPPER_SYNTH
_Static_assert(sizeof(mar_part_t) <= PHYS_ARENA, "MARE's state within the arena PHYS and GRAIN size: no more pool");
#endif
static uint8_t eng_arena_own[NPART];            /* the engine + 1 whose state is in it, 0 = none */
#ifdef ARENA_STATS
static uint32_t eng_arena_claims, eng_arena_bad;   /* host tests: claims, accesses by an engine the part does not play */
#endif
static void *eng_arena_of(const track_t *t, uint32_t eng)
{
    uint32_t p = (uint32_t)(t - trk);
    if (p >= NPART)
        return 0;
#ifdef ARENA_STATS
    if (t->engine != eng)
        eng_arena_bad++;
#endif
    if (eng_arena_own[p] != eng + 1u) {
        memset(&eng_arena[p], 0, sizeof eng_arena[p]);
        eng_arena_own[p] = (uint8_t)(eng + 1u);
#ifdef ARENA_STATS
        eng_arena_claims++;
#endif
    }
    return &eng_arena[p];
}
#if FELUCCA_SLICE
#include "eng_slice.c"
#endif
#if CLIPPER_SYNTH
#define FM_OWN_ENG (1u << 0 | 1u << ENGI_WAVE | 1u << ENGI_VECTOR | 1u << ENGI_PONY)   /* (f2_own's, as a mask, ANALOG engine 0:
                                                          * FX > FILTER > MODL shown on them alone, params.c page_desc) */
static inline __attribute__((always_inline)) int f2_own(const engine_t *e)                     /* FILTER 2 beside its own MODEL filter (f2_pair) */
{
    return e == &ENG_ANALOG || e == &ENG_WAVE || e == &ENG_VECTOR || e == &ENG_PONY;
}
/* FILTER 2 on the other engines: after the voice (x: the voice alone, at its level, the envelope in it), at its own
 * cutoff (no LINK: there is no MODEL filter to follow). SER the voice through it, PAR the voice and it, SPLIT the
 * voice left and it right (the side into f2_sd, voice.c vrender) */
static void f2_post(const track_t *t, voice_t *v, const vmod_t *m, int32_t *x, uint32_t n)
{
    f2_t z;
    int32_t st[4] = {v->fs[4], v->fs[5], v->fs[6], v->fs[7]};
    uint32_t i;
    if (!F2_SETUP(&z, t, v, m, 0, 0, x))
        return;
    for (i = 0; i < n; i++) {
        int32_t y = soft_knee(fmod_tick(&z.f, x[i], st, st), 24000) << 1;   /* (its peaks past a full voice: soft) */
        if (z.route == F2_SER) {
            x[i] = y;
        } else if (z.route == F2_PAR) {
            x[i] = (x[i] + y) * 23 >> 5;
        } else {
            z.sd[i] += (x[i] - y) >> 1;
            x[i] = (x[i] + y) >> 1;
        }
    }
    v->fs[4] = st[0], v->fs[5] = st[1], v->fs[6] = st[2], v->fs[7] = st[3];
}
#endif

static const engine_t *const ENGINES[NENGINES] = {&ENG_ANALOG, &ENG_DIGITAL, &ENG_PHASE, &ENG_GONE, &ENG_SAMPLE,
                                                    &ENG_FORMANT, &ENG_TRIO, &ENG_DRAWBAR, &ENG_GRAIN,
                                                    &ENG_FM6,    /* 9 (ENGI_FM6): always; SLICE after it (core.h) */
                                                    &ENG_PHYS, &ENG_NOISE, &ENG_HYBRID,
#if FELUCCA_SLICE
                                                    &ENG_SLICE,
#endif
#if CLIPPER_SYNTH
                                                    &ENG_WAVE,   /* 13 (ENGI_WAVE) */
                                                    &ENG_ACID, &ENG_STRINGS, &ENG_MACRO,   /* 14, 15, 16 */
                                                    &ENG_VPS, &ENG_TERRAIN,                /* 17, 18 */
                                                    &ENG_FMX, &ENG_WSEQ,                   /* 19, 20 */
                                                    &ENG_PONY, &ENG_VECTOR, &ENG_PALOMINO,  /* 21, 22, 23 */
                                                    &ENG_STAMPEDE, &ENG_RODEO, &ENG_MARE,      /* 24, 25, 26 */
#endif
};
#if CLIPPER_SYNTH
_Static_assert(ENGI_WAVE == 13u && ENGI_ACID == 14u && ENGI_STRINGS == 15u && ENGI_MACRO == 16u && ENGI_VPS == 17u &&
               ENGI_TERRAIN == 18u && ENGI_FMX == 19u && ENGI_WSEQ == 20u && ENGI_PONY == 21u && ENGI_VECTOR == 22u &&
               ENGI_PALOMINO == 23u && ENGI_STAMPEDE == 24u && ENGI_RODEO == 25u && ENGI_MARE == 26u,
               "ENGINES[] order (Clipper)");
/* (eng_wave.c) the table of the instrument: track 1's, else its layer's (one table in RAM: two WAVE sounds share
 * track 1's); while a sound fades out after a change (voice.c sound_fade_begin) its table stays */
static int32_t wt_wanted(void)
{
    uint32_t k;
    for (k = 0; k < 2u; k++) {
        const track_t *t = &trk[k];
        if (k && (t->p[P_LYMODE] != LY_LAYER && t->p[P_LYMODE] != LY_SPLIT && t->p[P_LYMODE] != LY_MULTI))
            break;
        if (t->xf_on && ENGINES[t->engine % NENGINES] == &ENG_WAVE)
            return wt_have;
        if (ENGINES[t->eng_req % NENGINES] == &ENG_WAVE)
            return wt_tab_eff[k] ? wt_tab_eff[k] - 1 : (int32_t)((uint32_t)t->p[P_E0] % WT_NALL);   /* (0.9: after the duck) */
    }
    return -1;
}
#endif
_Static_assert(ENGI_FM6 == 9u, "ENGINES[ENGI_FM6] is FM6");
_Static_assert(ENGI_GONE == 3u && ENGI_TRIO == 6u, "ENGINES[]: 3 the empty place (was LOFI), 6 TRIO");
_Static_assert(ENGI_ANALOG == 0u && ENGI_DIGITAL == 1u && ENGI_PHASE == 2u && ENGI_SAMPLE == 4u && ENGI_FORMANT == 5u &&
               ENGI_DRAWBAR == 7u, "ENGINES[] order (the IDs are permanent, core.h)");
/* (Stallion 1.0) the DISPLAY ORDER: every live engine once, as the player meets them (ALGORITHM's next engine, the
 * browser's engine filter, the editor's engine list: INFO sends it). Its own list, apart from the IDs (core.h): a
 * reorder, an addition or a removal here moves no ID. For 1.0 the order of the IDs, without LOFI's retired 3 */
static const uint8_t ENG_ORDER[] = {
    ENGI_ANALOG, ENGI_DIGITAL, ENGI_PHASE, ENGI_SAMPLE, ENGI_FORMANT, ENGI_TRIO, ENGI_DRAWBAR, ENGI_GRAIN, ENGI_FM6,
    ENGI_PHYS, ENGI_NOISE, ENGI_HYBRID,
#if FELUCCA_SLICE
    ENGI_SLICE,
#endif
#if CLIPPER_SYNTH
    ENGI_WAVE, ENGI_ACID, ENGI_STRINGS, ENGI_MACRO, ENGI_VPS, ENGI_TERRAIN, ENGI_FMX, ENGI_WSEQ, ENGI_PONY, ENGI_VECTOR,
    ENGI_PALOMINO, ENGI_STAMPEDE, ENGI_RODEO, ENGI_MARE,
#endif
};
#define NENG_ORDER ((uint32_t)NELEM(ENG_ORDER))
_Static_assert(NELEM(ENG_ORDER) == NENGINES - 1u, "ENG_ORDER: every engine but the retired 3");
static uint32_t eng_rank(uint32_t e)                 /* engine e's place in ENG_ORDER (a retired ID: TRIO's) */
{
    uint32_t i;
    e = eng_live(e % NENGINES);
    for (i = 0; i < NENG_ORDER; i++)
        if (ENG_ORDER[i] == e)
            return i;
    return 0;
}
_Static_assert(ENGI_GRAIN == 8u && ENGI_PHYS == 10u && ENGI_NOISE == 11u && ENGI_HYBRID == 12u && ENGI_SLICE == 13u, "ENGINES[] order");

#if CLIPPER_SYNTH
/* the matrix destinations each engine uses: a cable into any other would do nothing, so the FM-1 never
 * keeps one (mx_prune), its MATRIX page and the web editor never offer one (INFO sends this mask per engine). From the
 * engines' own facts, next to their table (tests/engine_dests_test.c checks it against engine_t and eng_*.c):
 *   FLTFM, RING   MARE's FILTER FM and RING (voice.c: only eng_mare.c reads m->ffm, m->ring)
 *   TRIG1         restarts ENV 1, which an engine with its own envelopes never plays (engine_t ownenv: FM6, FMX)
 *   XMOD, HPF     move the EDIT value engine_t xmod / hpf names; none elsewhere
 *   SHAPE         the engines whose render never reads m->shape (mx_shape_dead)
 *   E1..E8        an EDIT slot the engine leaves unused (its label "-" or none)
 * Kept, though they do nothing for a while: CUT and CUTB on FMX and WHEEL (no filter of their own: they move FILTER 2,
 * f2_post, which the player can turn on at any time), CUT2 and RES2 while FILTER 2 is OFF (the same), ARP on the
 * layer's part (the MATRIX page says LAYER ARPEGGIO: UNUSED). Those depend on a value, not on the engine: a cable
 * there waits for it */
static int mx_shape_dead(const engine_t *e)
{
    return e == &ENG_ACID || e == &ENG_HYBRID || e == &ENG_MACRO || e == &ENG_PHYS ||
           e == &ENG_SAMPLE || e == &ENG_STRINGS || e == &ENG_DRAWBAR || e == &ENG_WSEQ
#if FELUCCA_SLICE
           || e == &ENG_SLICE
#endif
        ;
}
/* bit d set: destination d does something on engine e (bit MT_OFF set: OFF is always allowed). The voice reads it too
 * (voice.c mx_load): a slot into a dead destination counts for nothing there, pruned or not, so pruning never changes
 * a sound (not the MPE SLIDE fallback, not the random numbers NOISE draws) */
UI_SMALL static uint64_t mx_dst_live_e(const engine_t *e)
{
    uint64_t m = (1ull << MT_COUNT) - 1u;
    uint32_t k;
    if (e != &ENG_MARE)
        m &= ~(1ull << MT_FFM | 1ull << MT_RING | 1ull << MT_SEMI | 1ull << MT_SEM34);
    if (e->ownenv)
        m &= ~(1ull << MT_TRIG1);
    if (!e->xmod)
        m &= ~(1ull << MT_XMOD);
    if (!e->hpf)
        m &= ~(1ull << MT_HPF);
    if (mx_shape_dead(e))
        m &= ~(1ull << MT_SHAPE);
    for (k = 0; k < 8u; k++) {
        const char *l = e->edit[k].label;
        if (!l || !l[0] || l[0] == '-')
            m &= ~(1ull << (MT_E1 + k));
    }
    return m;
}
static uint64_t mx_dst_live(uint32_t ei) { return mx_dst_live_e(ENGINES[ei % NENGINES]); }
/* a destination code allowed on engine ei: OFF, or one it uses (an unknown code: not) */
static int mx_dst_ok(uint32_t ei, int32_t d)
{
    return d >= MT_OFF && d < MT_COUNT && (mx_dst_live(ei) >> d & 1u);
}
/* the one place a sound's dead cables go: every slot of p whose destination does nothing on engine ei cleared (source,
 * destination, amount 0). They did nothing, so the sound does not change. Called wherever a sound comes to a part
 * (a preset, an engine, a user preset, a project, UNDO / MORPH / the layer swap, the editor) before it plays, and once
 * a main-loop pass for anything else (mx_prune_parts); returns the slots cleared */
UI_SMALL static uint32_t mx_prune_p(int16_t *p, uint32_t ei)
{
    uint64_t live = mx_dst_live(ei);
    uint32_t k, n = 0;
    for (k = 0; k < MX_SLOTS; k++) {
        int16_t *s = &p[mx_pid(k)];
        if (s[1] > MT_OFF && s[1] < MT_COUNT && !(live >> s[1] & 1u)) {
            s[0] = s[1] = s[2] = 0;
            n++;
        }
    }
    return n;
}
static uint32_t mx_prune(track_t *t) { return t == TDRUM ? 0u : mx_prune_p(t->p, t->eng_req); }
#endif

/* every factory sound as loud as the others: a level trim per preset, 1/2 dB, measured on a phrase
 * that fits the sound (tools/level_presets.py writes preset_trim.h); a track keeps it in P_ED_FX */
#include "preset_trim.h"
#if CLIPPER_SYNTH
#include "preset_trim_clipper.h"
#endif
static int16_t preset_trim(uint32_t e, uint32_t pi)
{
    if (CLIPPER_SYNTH && (ENGINES[e % NENGINES] == &ENG_SAMPLE || ENGINES[e % NENGINES] == &ENG_GRAIN))
        return 0;                                    /* (Clipper: the user's own samples, no factory level) */
#if CLIPPER_SYNTH
    if (e >= PTC_FIRST)                              /* Clipper's own engines: their table (preset_trim_clipper.h) */
        return e - PTC_FIRST < PTC_ENGINES && pi < PTC_MAX ? PRESET_TRIM_CL[e - PTC_FIRST][pi] : 0;
#endif
    return e < PT_ENGINES && pi < PT_MAX ? PRESET_TRIM[e][pi] : 0;
}
