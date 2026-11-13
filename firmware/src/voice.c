/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Changes: Copyright (C) 2026 Eberon Limited (John Gorman) */
/* Voice allocation, envelopes, LFO and per-track rendering. Runs in the
 * audio ISR.
 *
 * Each synth part has its own NVOICE voices (so MONO / LEGATO / UNISON keep using
 * v[0..]), but only NVOICE of all the parts' voices sound at once: a part that starts
 * a voice while the budget is full takes one from any part (voice_victim): the quietest
 * released voice, else the oldest extra UNISON voice, else the oldest held voice of a
 * POLY part that is not its lowest note (of equally loud ones, as FM6's pinned envelopes,
 * the oldest). A voice given up fades out over STEAL_BLOCKS (stage 4: the envelope goes
 * to 0, the block's amplitude ramp smooths it) and the note starts at once on a free voice;
 * a part with none free (its cap) fades its own over OWN_BLOCKS and the note starts on it from
 * silence (a tail below -42 dB: restarted in place, unheard; a sample's KILL_BLOCKS). That
 * note keeps its voice's place in the budget while it waits (voices_busy). Extra UNISON
 * voices only start when there is room. The drum track has its own voices (drums.c). */
static uint32_t vage;                                   /* voice ages: one clock for every part */
/* engines that play recorded material (a position, not a phase): no phases kept or spread */
#if FELUCCA_SLICE
static int eng_sampled(const engine_t *e) { return e == &ENG_SAMPLE || e == &ENG_SLICE; }
#else
static int eng_sampled(const engine_t *e) { return e == &ENG_SAMPLE; }
#endif
/* (Stallion) the note-ons' own generators, at file scope (as the function statics they were: the same values, the same
 * order): tests/unison_level_test.c seeds them per engine and preset, its renders the same whatever ran before */
static uint32_t uni_ph_seed = 0x13579BDu, note_seed = 0x5EED1234u, uni_seed = 0x1234567u, puni_seed = 0x2468ACEu,
                pend_seed = 0x13579BDu;
#if CLIPPER_SYNTH
/* (test-all, after unison-clicks; since the merge of Clipper 1.3 only for the engines its unison_spread leaves to ph[0]
 * alone: VECTOR, MARE) an extra unison voice from silence: its start phase ph[0] + d (the callers' random d), its
 * noise (ph[2]) a random start of its own: left at its note-on value it started alike on all the voices and stacked
 * in phase (+10 dB). (Up to Stallion's palomino_spread PALOMINO's VCO-2 here too, a random phase of its own: a UNISON
 * note -15 .. +7 dB over POLY at random) */
static void uni_phases(const engine_t *e, voice_t *v, uint32_t d)
{
    v->ph[0] += d;
    uni_ph_seed = uni_ph_seed * 1664525u + 1013904223u;
    v->ph[2] += uni_ph_seed;                        /* VECTOR: RND's generator; MARE: the noise */
    if (e == &ENG_MARE) {                          /* (ph[1] VCO 2, in step with VCO 1 from its note-on: moved with
                                                     * it; the noise never 0: its note-on reads 0 as "unseeded") */
        v->ph[1] += d;
        if (!v->ph[2])
            v->ph[2] = 1u;
    }                                               /* (VECTOR's ph[1]: MOVE's phase from the note's start, the stack
                                                     * moves as one) */
}
#endif
/* CHAO: the logistic map x' = 3.96 x (1 - x), x on 16 bits (0..65535: 0..1), a new value every half cycle, glided
 * to along a half cosine (never steeper than SIN at the same RATE); nudged by cx, a 16-bit counter (its 65536 steps:
 * no wander repeats sooner, 13 minutes at the fastest RATE). The S&H value holds x - 1/3 (a state of 0 starts on no
 * special point); its range 2566..64884 is spread over the Q15 range */
#define CHAO_LO 2566                                    /* x's lowest: f(the highest, 64881) */
static int32_t chao_x(int32_t y) { return clamp((int32_t)(uint16_t)(y + 21845), CHAO_LO, 65535); }
static int32_t chao_out(int32_t y) { return clamp(((chao_x(y) - CHAO_LO) * 17230 >> 14) - 32768, -32768, 32767); }
static int32_t chao_next(int32_t y, uint32_t cx)
{
    int32_t x = chao_x(y);
    return (int16_t)(((x * (65536 - x) >> 16) * 64881 >> 14) + (int32_t)((cx & 0xFFFFu) >> 13) - 4 - 21845);
}
/* a CHAO LFO's half cycle passed (old: its phase before): the next value, the counter on */
static void lfo_chao(int16_t *y, uint16_t *cx, uint32_t ph, uint32_t old)
{
    if ((ph ^ old) >> 31) {
        *y = (int16_t)chao_next(*y, *cx);
        *cx = (uint16_t)(*cx * 25173u + 13849u);
    }
}

/* an LFO's wave (N_LWAVE) at phase ph; rnd: its S&H value, Q15; cx: its CHAO counter */
static int32_t lfo_wave_n(int32_t wave, uint32_t ph, int32_t rnd, uint32_t cx)
{
    switch (wave) {
    case LW_CHAO: {
        int32_t a = chao_out(rnd), b = chao_out(chao_next(rnd, cx));
        return a + ((b - a) * ((32767 - osc_sine((ph & 0x7FFFFFFFu) + 0x40000000u)) >> 1) >> 15);
    }
    case 1:
        return osc_tri(ph);
    case 2:
        return (int32_t)(ph >> 16) - 32768;
    case 3:
        return ph < 0x80000000u ? 32767 : -32767;
    case 4:
        return rnd;
    default:
        return osc_sine(ph);
    }
}
static int32_t lfo_wave(track_t *t, uint32_t ph)
{
    return HOT_FAR(lfo_wave_n)(t->p[P_LWAVE], ph,   /* (Stallion: in RAM, hal/fm1_hot.h) */
                               (int32_t)(t->lfo_rnd >> 16) - 32768, t->lfo_rnd & 0xFFFFu);
}

static uint8_t midi_breath;                             /* (1.0) GLO > MIDI 2 > EWI (seq.c): 0 OFF, 1 VOL, 2 V+AT */
/* GLO > MIDI 3 (kept with the settings, project.c midi_side_t): the CCs the matrix reads as CC A and CC B
 * (any CC: it keeps its own job too), SUSTAIN: 0 ON, 1 PATCH (CC 64 and the SMK-37 pedal hold no note, they only
 * feed CC A / CC B; seq.c part_ctl_cc) */
static uint8_t midi_cca = 64, midi_ccb = 4, midi_sus_patch = 0;
/* (1.1, the EWI player again: "a soft note adds a second one, at full level") an EWI is a monophonic instrument and
 * its breath is the level: with EWI on, the instrument (and its LAYER) plays one note at a time, legato, whatever
 * the sound's VOICE (a note fingered over the last one moves it, no second voice); the notes' velocity is fixed
 * (seq.c), the breath alone shapes the level */
static inline uint32_t voice_mode(const track_t *t)
{
    return CLIPPER_SYNTH && midi_breath && (uint32_t)(t - trk) < NPART && ctl_part(t) == 0u ? (uint32_t)V_LEGATO
                                                                                   : (uint32_t)t->p[P_VOICE];
}
static uint8_t midi_bend;                               /* (1.0) GLO > MIDI > BEND, or RPN 0 (seq.c): the pitch bend's
                                                         * range in semitones, 0 AUTO (the HYBRID patch's, else 2) */
#if CLIPPER_SYNTH
/* Clipper 0.4: MPE (menu SYSTEM > MPE; seq.c MIDI in). Channel 1 is the master channel (the part's bend, pressure,
 * CCs, as ever); a note on a member channel (2..16) is that channel's own: its pitch bend (over the range chosen
 * here, 48 semitones by the MPE standard), its pressure (as a polyphonic aftertouch: AT) and its CC 74 (SLIDE, a
 * matrix source; with no slot taking it, the cutoff) follow it alone (voice.c track_render, v->ch) */
#define MPE_MODES 6u
static uint8_t mpe_mode;                                /* 0 off, else the member channels' bend range (MPE_RANGE) */
static const char *const MPE_NAME[MPE_MODES] = {"OFF", "48 ST", "2 ST", "12 ST", "24 ST", "96 ST"};
static const uint8_t MPE_RANGE[MPE_MODES] = {0, 48, 2, 12, 24, 96};
static int16_t mpe_bend[16];                            /* per channel: Q15, as part_ctl's */
static uint8_t mpe_press[16];                           /* 0..127 */
static int8_t mpe_slide[16];                            /* CC 74 - 64: 0 at rest */
static uint8_t mpe_cur_ch;                              /* the channel of the note being started (seq.c), 0 = none */
static void mpe_reset(void)
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        mpe_bend[i] = 0;
        mpe_press[i] = 0;
        mpe_slide[i] = 0;
    }
    mpe_cur_ch = 0;
}
#endif

/* (Stallion) PONY, as the instrument it follows (eng_pony.c): ENV DEST FLT +-64 the whole cutoff range, the envelope in
 * dB (pony_fenv); LFO DEST SHP one-sided, the LFO 0 .. 1 (its PWM from the knob's width narrower and back) */
#if CLIPPER_SYNTH
#define PONY_FENV(e, x) ((e) == &ENG_PONY ? pony_fenv(x) << 1 : (x))
#define PONY_LFO1(e, l, f) ((e) == &ENG_PONY ? ((l) + (f)) >> 1 : (l))
#else
#define PONY_FENV(e, x) (x)
#define PONY_LFO1(e, l, f) (l)
#endif
static void track_lfo_tick(track_t *t)
{
    uint32_t old = t->lfo_ph;
#if CLIPPER_SYNTH
    t->lfo_ph += LFO_INC[t->l1rmod ? clamp(t->p[P_LRATE] + t->l1rmod, 0, 127) : t->p[P_LRATE] & 127];   /* (+ LFO1) */
#else
    t->lfo_ph += LFO_INC[t->p[P_LRATE] & 127];
#endif
    if (t->p[P_LWAVE] == LW_CHAO) {                     /* (CHAO: its value in the high half, its counter low) */
        int16_t y = (int16_t)((t->lfo_rnd >> 16) - 32768u);
        uint16_t cx = (uint16_t)t->lfo_rnd;
        lfo_chao(&y, &cx, t->lfo_ph, old);
        t->lfo_rnd = (uint32_t)(y + 32768) << 16 | cx;
    } else if (t->lfo_ph < old) {
        t->lfo_rnd = rng();
    }
    t->lfo_val = lfo_wave(t, t->lfo_ph);
    if (t->lfo_fade < 32767) {
        int32_t step = (int32_t)(ENV_LIN[t->p[P_LFADE] & 127] >> 9);
#if CLIPPER_SYNTH
        if (ENGINES[t->engine] == &ENG_PONY && step && !(t->p[P_JCHR] & JQ_LFO)) {   /* (Stallion) PONY: the delay
                                                         * an RC's rise, as the instrument's (63 % at a third of the
                                                         * straight ramp's time, 95 % at it); CHAR's LFO: straight
                                                         * (eng_pony.c pony_lfo: after a hold-off) */
            int32_t d = ((32767 - t->lfo_fade) * (step < 10922 ? step * 3 : 32767)) >> 15;
            step = d ? d : 1;
        }
#endif
        t->lfo_fade = t->p[P_LFADE] ? clamp(t->lfo_fade + (step ? step : 1), 0, 32767) : 32767;
    }
}

/* voices the engine may use (POLY and UNISON): its cap, else all of them */
static uint32_t trk_nvoice(const track_t *t)
{
    uint32_t c = ENGINES[t->engine] == &ENG_PHYS ? phys_poly(t) : ENGINES[t->engine]->poly;   /* (PHYS: by MODEL) */
    return c && c < NPOLY ? c : NPOLY;
}
/* UNISON's stack: the engine's cap, at most eight voices, as designed (the 12 / 16 voice builds stacked them all:
 * every note on one key the CPU of a 16-note chord, ~3 dB louder than POLY, the detune twice as wide) */
#define UNI_MAX 8u
static uint32_t uni_nvoice(const track_t *t)
{
    uint32_t c = trk_nvoice(t);
    return c < UNI_MAX ? c : UNI_MAX;
}

/* (SLOOP 2.8.3) a voice's pending note (v->pend, voice_start): VP_ON a note waits for the voice's fade */
#define VP_ON 1u
#define VP_OFF 2u
#define VP_GLIDE 4u
#define VP_UNI 8u   /* (after Clipper 1.2) an extra UNISON voice: its start spread (unison_spread) */

/* ------------------------------------------------- the shared voice budget --- */
/* sounding voices of all parts (not the fading ones), and the notes waiting for a voice's fade: (steal-quiet-tails)
 * the note keeps its voice's place, so its start at the fade's end takes no second voice (another part's note
 * meanwhile took one at its own note-on) */
static uint32_t voices_busy(void)
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            n += (trk[p].v[i].active && trk[p].v[i].stage != 4u) || (trk[p].v[i].pend & VP_ON);
    return n;
}

static uint32_t lowest_held(const track_t *t)           /* index of the lowest held note (the bass), NVOICE = none */
{
    uint32_t i, low = NVOICE;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && (low == NVOICE || t->v[i].note < t->v[low].note))
            low = i;
    return low;
}

/* the voice to give up for a new one (see the top); soft: only released voices and
 * extra UNISON voices of other parts. Returns its index, *pp its part; NVOICE = none. (steal-quiet-tails) Of the
 * released ones the quietest, as the overload's shed_voice: the oldest was often still loud (a long release after a
 * short note) while a quieter tail rang on. Equally loud: the oldest (FM6, FMX pin a released voice's envelope at
 * full: the lowest index went, whatever its age) */
static int quieter(const voice_t *v, const voice_t *b)
{
    return v->env < b->env || (v->env == b->env && v->age < b->age);
}
static uint32_t voice_victim(const track_t *self, int soft, track_t **pp)
{
    uint32_t p, i, best = NVOICE, cat = 4;
    for (p = 0; p < NPART; p++) {
        track_t *t = &trk[p];
        uint32_t mode = voice_mode(t), nu = mode == V_UNISON ? uni_nvoice(t) : 1u;
        uint32_t low = voice_poly((int)mode) ? lowest_held(t) : NVOICE;
        if (soft && t == self)
            continue;
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &t->v[i];
            uint32_t c;
            if (!v->active || v->stage == 4u)
                continue;
            if (!v->gate)
                c = 1;                                  /* released */
            else if (mode == V_UNISON && i > 0 && i < nu)
                c = 2;                                  /* an extra UNISON voice */
            else if (!soft && voice_poly((int)mode) && i != low)
                c = 3;                                  /* held, not the bass */
            else
                continue;
            if (c < cat || (c == cat && (c == 1u ? quieter(v, &(*pp)->v[best]) : v->age < (*pp)->v[best].age))) {
                cat = c;
                best = i;
                *pp = t;
            }
        }
    }
    return best;
}

static uint32_t voice_kills;                            /* voices given up (budget, overload): console, hostsim */
/* the fades of a voice given up (env_tick stage 4: v->kill blocks of 0.73 ms):
 *   KILL_BLOCKS   1.5 ms: a sample (SAMPLE, SLICE) played again on its voice fades out first (voice_start), the
 *                 sampled engines' own steal too; the overload's shed after a late half (shed_voice: main's)
 *   STEAL_BLOCKS  (steal-quiet-tails) 5.8 ms, one audio half: a voice given up for the budget, the overload, POLY2
 *                 (voice_kill); a voice the shedder gives up at a half's start is gone by its end. 1.5 ms took a
 *                 pad's tail at -10 dB to nothing in a fraction of a cycle of its lower notes, a tick at every chord
 *                 change on the SMK-37 (PONY PAD, chords played live)
 *   OWN_BLOCKS    5.1 ms: the part's cap is full, or the load's admission takes one of the part's own voices for the
 *                 note (vc_admit): its tail fades out and the new note waits for it (voice_alloc, voice_start), late
 *                 by as much. (voice-pressure) 2.2 ms before: a loud pad tail cut in 2 ms still ticked; 5 ms is the
 *                 shortest fade those chords take without one. A tail below STEAL_QUIET (-42 dB) is restarted in
 *                 place (unheard; tail_level_test's bound) */
#define KILL_BLOCKS 2u
#define STEAL_BLOCKS 8u
#define OWN_BLOCKS 7u
#define STEAL_QUIET (1 << 17)
static uint8_t steal_fade;                              /* voice_alloc -> voice_start: fade the voice out, then the note */
static void voice_kill_n(voice_t *v, uint32_t blocks)  /* fade out over the next blocks (env_tick stage 4) */
{
    v->gate = 0;
    if (v->stage != 4u)
        v->kill = (uint8_t)blocks;
    v->stage = 4;
    voice_kills++;
}
static void voice_kill(voice_t *v) { voice_kill_n(v, STEAL_BLOCKS); }

/* overload (after Felucca 1.0; audio.c asks for it when two halves in a row took > 85 % of their time):
 * fade one voice out over the next half (voice_kill: no click), one more while it lasts (shed_judge). A
 * single late half (a USB burst, a flash write) sheds nothing. First the quietest releasing voice (of equally
 * loud ones the oldest); else the oldest held one that is not a part's lowest POLY note nor its MONO / LEGATO /
 * UNISON lead, as the voice budget keeps them: a dense chord on a heavy engine thins out from the top, bass and
 * lead stay. fast (the last half was late, over 100 %: the sound breaks up already): over KILL_BLOCKS, as before
 * steal-quiet-tails, and that half judged as any. Returns SHED_NONE, SHED_SLOW or SHED_FAST */
static uint32_t shed_count;
enum { SHED_NONE, SHED_SLOW, SHED_FAST };

static int shed_voice(int fast)
{
    uint32_t p, i, n = fast ? KILL_BLOCKS : STEAL_BLOCKS;
    voice_t *best = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && !v->gate && v->stage != 4u && (!best || quieter(v, best)))
                best = v;
        }
    if (best) {
        voice_kill_n(best, n);
        shed_count++;
        return fast ? SHED_FAST : SHED_SLOW;
    }
    for (p = 0; p < NPART; p++) {
        const track_t *t = &trk[p];
        uint32_t mode = voice_mode(t), low = voice_poly((int)mode) ? lowest_held(t) : 0u;
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && v->gate && v->stage != 4u &&
                ((voice_poly((int)mode) && i != low) || (!voice_poly((int)mode) && i > 0u)) && (!best || v->age < best->age))
                best = v;
        }
    }
    if (best) {
        voice_kill_n(best, n);
        shed_count++;
        return fast ? SHED_FAST : SHED_SLOW;
    }
    return SHED_NONE;
}
/* audio.c, a half rendered: hist (bit k: the half k halves ago over 85 %), shed: what its start gave up (shed_voice),
 * over: this half over 85 %. 1: shed at the next half's start (two in a row). (steal-quiet-tails) A half whose start
 * gave a voice up over STEAL_BLOCKS is left out: the voice fades over all of it, so it costs about what it did;
 * judged, the next half gave up one more voice than the load needed (a tail lost for nothing). The half after it,
 * without the voice, is judged with the ones before */
static int shed_judge(uint8_t *hist, int shed, int over)
{
    if (shed == SHED_SLOW)
        return 0;
    *hist = (uint8_t)(*hist << 1 | (over != 0));
    return (*hist & 3u) == 3u;
}

/* the budget is full: free a voice for part t. 0 = nothing to take (soft) */
static int voice_room(track_t *t, int soft)
{
    track_t *vp = 0;
    uint32_t k;
    if (voices_busy() < NVOICE)
        return 1;
    k = voice_victim(t, soft, &vp);
    if (k == NVOICE)
        return !soft;                                   /* hard: over the budget (cannot happen with 3 parts) */
    voice_kill(&vp->v[k]);
    return 1;
}

#if CLIPPER_SYNTH
/* ------------------------------------------------------------------------ CPU-pressure admission --- *
 * NVOICE is a ceiling: a note-on that would take the predicted render past VC_BUDGET of the half buffer first gives up
 * voices (vc_admit): released tails, the quietest first; then the quietest held voice of the same part (POLY / UNI2 /
 * UNI4, not its lowest note); then the oldest held voice of another such part. Never the note being started, nor a
 * MONO / LEGATO / UNISON part's note; a UNI2 / UNI4 note goes with all its voices. A voice given up fades out over
 * STEAL_BLOCKS (voice_kill) and the note starts at once on a free voice; one of the same part taken for the note
 * fades out over OWN_BLOCKS and the note then starts on it from silence (voice_start, as the cap's own steal).
 * A voice fading out is rendered until its fade ends: two loads are judged. After the fades, the voices
 * sounding and the notes waiting for a voice: VC_BUDGET. The half with the fades: VC_PEAK, the shedder's 85 %; over
 * it, only a voice of the same part is taken, for the note (it fades, the note waits on it: no voice more rendered).
 * Prediction: each part's voices x their cost on one core (vcost_tables.h, measured on fm1-emu: tools/gen_vcost.py),
 * half of them when the part's voices split over both cores (vs_ok), plus the part's own work; the render with no
 * voice (vc_base) and a run-time correction (vc_ratio, Q8: measured / predicted, audio.c vc_half) on top: the table
 * adapts to the real chip without a new bench. From power-on the correction is the build's: VC_R0_SMK (the SMK-37's
 * render ~2 x fm1-emu's: a held PONY PAD triad 69 % where fm1-emu shows 34 %), learnt down if the chip is faster;
 * VC_R0_FM1 1 x. The overload shedder (shed_voice) stays as the last resort. */
#include "vcost_tables.h"
_Static_assert(VC_NENG == NENGINES, "vcost_tables.h: one cost per engine (tools/gen_vcost.py)");
#define VC_BUDGET (VC_Q * 80u / 100u)                   /* 80 % of the half: under the shedder's 85 % */
#define VC_MT_DIST 40u                                  /* its DIST: the mixer's tanh and the sides' after the
                                                         * ladders (eng_mare.c mar_post): ~+13 % a voice on the host
                                                         * (mare_test's cost, 352 -> 399), scaled to fm1-emu (303 q) */
#define VC_IT_SDLY 17                                   /* (fx.c IT_SDLY, IT_PDLY: 17, 18; asserted there) */
#define VC_SDLY 300u                                    /* the INSERT's STDLY / PPDLY, a part's: ~320 .. 420
                                                         * instructions a sample (tests/target_budget.txt ins_sdly), ~1.33
                                                         * a q at fm1-emu's 240 MHz, beyond VC_PFX's share */
#define VC_MT_AC 12u                                    /* its VCOs' analog path (eng_mare.c mar_ac): a
                                                         * high-pass and an all-pass a sample, ~16 instructions */
#define VC_MT_VCO 39u                                   /* a MARE voice's VCO 3 or VCO 4, q: fm1-emu, its
                                                         * heaviest preset in POLY, 4 and 8 voices on both cores, VCO 3 / 4
                                                         * on (every VCO the SAW > SQR blend, two saws): 381 q a voice
                                                         * against two VCOs' 303 (tools/vcost, bench_edit 2) */
#define VC_PEAK (VC_Q * 85u / 100u)                     /* (voice-pressure) the half with the fades: the shedder's */
#define VC_HALF_US (HALF_FRAMES * 1000000u / FS)        /* 5804 us */
#define VC_R0_FM1 256u                                  /* (voice-pressure) the correction at power-on, Q8 */
#define VC_R0_SMK 512u
#ifndef VC_RATIO0
#if BOARD_SMK37
#define VC_RATIO0 VC_R0_SMK
#else
#define VC_RATIO0 VC_R0_FM1
#endif
#endif
static uint16_t vc_ratio = VC_RATIO0;                   /* measured / predicted voice load, Q8 (0.25 .. 4) */
static uint16_t vc_base = VC_BASE;                      /* the render with no voice, 1/VC_Q of the half */
static uint32_t vc_acc;                                 /* this half's predicted voice load, summed over its blocks */
static uint32_t vc_steals;                              /* voices given up for the load (console, tests) */
static uint16_t vc_c[NPART], vc_k[NPART];              /* (vc_cache) a voice's cost, the part's own, each part */
static uint8_t vc_ser[NPART];                           /* .. its voices on core 0 alone */
/* (Stallion) inlined everywhere: mix_part1 runs from RAM (tools/build.py HOT) and vc_voice from the flash, and a call
 * between the two is beyond the 23-bit call range */
static inline __attribute__((always_inline)) int vs_ok(const engine_t *e)
                                                    /* its render may run on two cores at once, for two voices (and
                                                     * reads its EDIT values through vm_ev alone) */
{
    return e == &ENG_HYBRID || e == &ENG_FMX || e == &ENG_WAVE || e == &ENG_ANALOG || e == &ENG_FM6 || e == &ENG_VPS ||
           e == &ENG_TERRAIN || e == &ENG_WSEQ || e == &ENG_MACRO || e == &ENG_STRINGS || e == &ENG_TRIO ||
           e == &ENG_PHASE || e == &ENG_DIGITAL || e == &ENG_ACID || e == &ENG_PONY || e == &ENG_VECTOR ||
           e == &ENG_PALOMINO || e == &ENG_STAMPEDE || e == &ENG_RODEO || e == &ENG_MARE;   /* (RODEO, MARE: their voice's
                                                     * state and the sound's values only; MARE's side: the core's f2_sd) */
}

/* a voice of part t on one core (q = 1/VC_Q of the half); *ser: the part's voices all on this core (the engine not in
 * vs_ok, or core 1 off; routes to EDIT values split as the others since split-edit-routes); *own: the part's own work */
static uint32_t vc_voice(const track_t *t, uint8_t *ser, uint16_t *own)
{
    const engine_t *e = ENGINES[t->engine % NENGINES];
    const int16_t *p = t->p;
    uint32_t c = VC_ENG[t->engine % NENGINES], k, ns = 0;
    for (k = 0; k < MX_SLOTS; k++) {                    /* (as mx_load counts them) */
        uint32_t id = mx_pid(k);
        int32_t sr = p[id], ds = p[id + 1u];
        if (sr <= MS_OFF || sr >= MS_NSRC || ds <= MT_OFF || ds >= MT_COUNT || !p[id + 2u] ||
            (ds == MT_L2RATE && sr == MS_LFO2))
            continue;
        ns++;
    }
    c += ns * VC_SLOT16 / 16u;
    if (e == &ENG_MARE)                                /* VCO 3 and VCO 4, each its own cost */
        c += VC_MT_AC + (p[P_MFFM] > MAR_VOFF ? VC_MT_VCO : 0u) + (p[P_MRING] > MAR_VOFF ? VC_MT_VCO : 0u) +
             (p[P_DIST] ? VC_MT_DIST : 0u);
    if (fm_model2(t))
        c += VC_F2;
    if (p[P_SPREAD] || p[P_MAC4] > 0 || (fm_model2(t) && fm_route(t) == F2_SPLIT))
        c += VC_SIDE;
    *ser = (uint8_t)(!vs_ok(e) || !cpu1_units || !cpu1_on());
    *own = (uint16_t)(VC_PART + (p[P_DIST] || p[P_ITYPE] || p[P_CHOR] || p[P_DLY] || p[P_REV] ? VC_PFX : 0u) +
                      ((uint32_t)(p[P_ITYPE] - VC_IT_SDLY) <= 1u ? VC_SDLY : 0u));
    return c;
}
static inline __attribute__((always_inline)) uint32_t vc_pl(uint32_t c, uint32_t ser, uint32_t k, uint32_t n)   /* a part's n voices (core 0's time) */
{
    if (!n)
        return 0;
    return ser || n < 2u ? n * c + k : (n + 1u) / 2u * c + VC_SPLIT + k;
}
/* the parts' costs; cnt: the voices sounding and the notes waiting for a voice's fade (they sound next: voice_start);
 * fad (or 0): the voices fading out (rendered until the fade ends; voice-pressure) */
static void vc_cache(uint32_t *cnt, uint32_t *fad)
{
    uint32_t p, i;
    for (p = 0; p < NPART; p++) {
        uint32_t f = 0;
        vc_c[p] = (uint16_t)vc_voice(&trk[p], &vc_ser[p], &vc_k[p]);
        cnt[p] = 0;
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &trk[p].v[i];
            if ((v->active && v->stage != 4u) || (v->pend & VP_ON))
                cnt[p]++;
            else
                f += v->active;
        }
        if (fad)
            fad[p] = f;
    }
}
static uint32_t vc_total(const uint32_t *cnt)           /* the predicted render, q */
{
    uint32_t p, s = 0;
    for (p = 0; p < NPART; p++)
        s += vc_pl(vc_c[p], vc_ser[p], vc_k[p], cnt[p]);
    return vc_base + (s * vc_ratio >> 8);
}
static uint32_t vc_load_now(void)
{
    uint32_t cnt[NPART];
    vc_cache(cnt, 0);
    return vc_total(cnt);
}
/* the voices part t may sound now, the other parts as they are (GLO > SYSTEM VOICES) */
static uint32_t vc_fit(const track_t *t)
{
    uint32_t cnt[NPART], self = (uint32_t)(t - trk) % NPART, n;
    vc_cache(cnt, 0);
    for (n = NVOICE; n > 1u; n--) {
        cnt[self] = n;
        if (vc_total(cnt) <= VC_BUDGET)
            break;
    }
    return n;
}

/* the voice to give up for part self's note `note` (vc_admit): cat 1 a tail (the quietest; equal: the oldest), 2 a held
 * voice of self (the quietest), 3 a held voice of another part (the oldest); never a held MONO / LEGATO / UNISON note
 * nor a POLY part's bass, nor self's voices in `own` or on `note`. ours: only self's voices under its cap (np), to take
 * for the note. Returns its part (NPART: none), *bi its voice, *pc its cat */
static uint32_t vc_victim(uint32_t self, uint32_t note, uint32_t own, uint32_t np, int ours, uint32_t *bi, uint32_t *pc)
{
    uint32_t p, i, bp = NPART, cat = 4;
    int pself = voice_poly((int)voice_mode(&trk[self]));
    for (p = 0; p < NPART; p++) {
        const track_t *u = &trk[p];
        uint32_t mode = voice_mode(u), pm = (uint32_t)voice_poly((int)mode), low = pm ? lowest_held(u) : NVOICE;
        if ((p == self && !pself) || (ours && p != self))
            continue;                                   /* a MONO / LEGATO / UNISON part's own note: never */
        for (i = 0; i < (ours ? np : NVOICE); i++) {
            const voice_t *v = &u->v[i], *b = bp < NPART ? &trk[bp].v[*bi] : 0;
            uint32_t c;
            if (!v->active || v->stage == 4u || (p == self && (((own >> i) & 1u) || v->note == note)))
                continue;
            if (!v->gate)
                c = 1;
            else if (!pm || (low < NVOICE && v->note == u->v[low].note))
                continue;                               /* a held MONO / LEGATO / UNISON note, a POLY part's bass */
            else
                c = p == self ? 2u : 3u;
            if (c < cat || (c == cat && (c == 3u ? v->age < b->age :
                                         v->env < b->env || (v->env == b->env && v->age < b->age))))
                cat = c, bp = p, *bi = i;
        }
    }
    *pc = cat;
    return bp;
}

/* room in the load for `need` more voices of part t for its note `note`; gives voices up until the prediction fits.
 * Returns t's own voices taken for the note (bit per voice: voice_start fades each out, then the note on it from
 * silence); within the budget, 0, nothing changed */
static uint32_t vc_admit(track_t *t, uint32_t need, uint32_t note)
{
    uint32_t cnt[NPART], fad[NPART], self = (uint32_t)(t - trk), own = 0, nown = 0, np = trk_nvoice(t), nfree = 0, p, i;
    if (self >= NPART || !need)
        return 0;
    vc_cache(cnt, fad);
    for (i = 0; i < np; i++)
        nfree += !t->v[i].active && !(t->v[i].pend & VP_ON);
    for (;;) {
        uint32_t add = need > nown ? need - nown : 0u, bp, bi = 0, cat = 4, nn, g, fit, peak;
        cnt[self] += add;
        fit = vc_total(cnt) <= VC_BUDGET;               /* after the fades */
        for (p = 0; p < NPART; p++)
            cnt[p] += fad[p];
        peak = vc_total(cnt) <= VC_PEAK;                /* the half with the fades */
        for (p = 0; p < NPART; p++)
            cnt[p] -= fad[p];
        cnt[self] -= add;
        if (fit && (peak || !add))
            break;                                      /* (the fades' half over without this note: nothing to do) */
        bp = peak || !add ? NPART : vc_victim(self, note, own, np, 1, &bi, &cat);   /* over the peak: ours, for it */
        if (bp < NPART && cat > 1u && fit)
            bp = NPART;                                 /* (a held one: not for the fades' half alone) */
        if (bp == NPART) {
            if (fit)
                break;                                  /* (nothing of ours to take: the fades' half as it is) */
            bp = vc_victim(self, note, own, np, 0, &bi, &cat);
        }
        if (bp == NPART)
            break;                                      /* nothing more to give: the note sounds (shed_voice is left) */
        nn = trk[bp].v[bi].note, g = trk[bp].v[bi].gate;
        for (i = 0; i < NVOICE; i++) {                  /* with its stack: the same note, held or released alike */
            voice_t *v = &trk[bp].v[i];
            if (!v->active || v->stage == 4u || v->note != nn || v->gate != g || (bp == self && ((own >> i) & 1u)))
                continue;
            vc_steals++;
            if (bp == self && nown < need && i < np && (!peak || nfree + nown < need)) {
                own |= 1u << i;                         /* ours, for the note: it stays rendered (fading, then the
                                                         * note), one voice fewer to add */
                nown++;
            } else {
                voice_kill(v);                          /* faded out: rendered until its fade ends */
                cnt[bp]--;
                fad[bp]++;
            }
        }
    }
    return own;
}

/* once a half (audio.c, the render's time in us): the correction. No voice: the render's floor (vc_base); voices
 * worth a sixteenth of the half or more: measured / predicted, up fast (1/8 a half: a slower chip crackles), down
 * slowly (1/32), a half's figure held within 1/2 .. 2 x of the last (a flash write, a USB burst) */
static void vc_half(uint32_t us)
{
    uint32_t meas = us * VC_Q / VC_HALF_US, pred = vc_acc * CTL / HALF_FRAMES, r;
    vc_acc = 0;
    if (!pred) {
        int32_t b = (int32_t)vc_base + ((int32_t)meas - (int32_t)vc_base) / 16;
        vc_base = (uint16_t)clamp(b, (int32_t)VC_BASE / 2, (int32_t)VC_Q / 2);
        return;
    }
    if (pred < VC_Q / 16u)
        return;
    r = (meas > vc_base ? meas - vc_base : 0u) * 256u / pred;
    r = clamp((int32_t)r, vc_ratio / 2, vc_ratio * 2);
    if (r > vc_ratio)
        r = vc_ratio + (r - vc_ratio + 7u) / 8u;
    else
        r = vc_ratio - (vc_ratio - r + 31u) / 32u;
    vc_ratio = (uint16_t)clamp((int32_t)r, 64, 1024);
}
#endif

/* POLY allocation (within the engine's voice cap). Same note: reuse its voice. Else a free voice: ROTATE takes
 * the next one round-robin (release tails ring out), REUSE (or any GLIDE) takes
 * the free voice whose last pitch is closest, so poly portamento moves each
 * voice the shortest way. Steal: the quietest released voice, else the oldest
 * held one, never the lowest held note (the bass). A free voice needs room in the
 * shared budget: the voice given up (voice_victim, this part's own too) fades out over
 * STEAL_BLOCKS and the note starts at once on the free voice.
 * (steal-quiet-tails) No free voice (the engine's cap): a voice of the part's own louder than -42 dB
 * fades out over OWN_BLOCKS and the note starts on it from silence (steal_fade: voice_start's fade
 * first, as a sample's), OWN_BLOCKS (5.1 ms) late at most: restarted in place, the tail's pitch
 * stopped dead and the new note began at the tail's level, past its attack. The oldest released
 * voice was taken, often louder than a later one. */
static voice_t *voice_alloc(track_t *t, uint32_t note)
{
    uint32_t i, np = trk_nvoice(t), best = np, low = np, nfree = 0;
    int32_t bd = 0x7FFFFFFF;
    steal_fade = 0;
    if (t->p[P_ALLOC] == 2)                             /* POLY2: the 8-voice poly's, a new note cuts the tails */
        for (i = 0; i < np; i++)
            if (t->v[i].active && !t->v[i].gate && t->v[i].note != note)
                voice_kill(&t->v[i]);
    for (i = 0; i < np; i++) {
        if (t->v[i].active && t->v[i].note == note) {
            if (t->v[i].stage == 4u && !(t->v[i].pend & VP_ON) && voices_busy() >= NVOICE)
                voice_room(t, 0);                       /* (SLOOP 2.8.3) a voice fading out (given up) sounds again: within the
                                                         * budget of NVOICE, as a new one (after Melodee's voice_reuse) */
            return &t->v[i];
        }
        nfree += !t->v[i].active;
    }
#if CLIPPER_SYNTH
    {   /* (load-steal) over the CPU budget: voices given up first; one of ours taken for this note: (voice-pressure) it
         * fades out over OWN_BLOCKS, the note then on it from silence (voice_start), as the cap's own steal */
        uint32_t m = vc_admit(t, 1u, note);
        for (i = 0; m && i < np; i++)
            if ((m >> i) & 1u) {
                steal_fade = t->v[i].env >= STEAL_QUIET;
                return &t->v[i];
            }
    }
#endif
    if (nfree && voices_busy() >= NVOICE) {
        track_t *vp = 0;
        uint32_t k = voice_victim(t, 0, &vp);
        if (k < NVOICE)                                 /* (our own too: it fades out, the note starts at once on a
                                                         * free voice; steal-quiet-tails had it wait for the fade) */
            voice_kill(&vp->v[k]);
    }
    if (t->p[P_ALLOC] || t->p[P_GLIDE]) {
        for (i = 0; i < np; i++) {
            int32_t d = t->v[i].pitch_cur - (int32_t)note * 16;
            if (t->v[i].active)
                continue;
            d = d < 0 ? -d : d;
            if (!t->v[i].pitch_cur)
                d = 0x7FFFFFF0;                          /* never played: last choice */
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
    } else {
        for (i = 0; i < np && best == np; i++) {
            uint32_t k = (t->rr + i) % np;
            if (!t->v[k].active)
                best = k;
        }
        t->rr = (uint8_t)((best == np ? t->rr : best) + 1u) % np;
    }
    if (best < np)
        return &t->v[best];
    for (i = 0; i < np; i++)                             /* the lowest held note is protected */
        if (t->v[i].gate && (low == np || t->v[i].note < t->v[low].note))
            low = i;
    for (i = 0; i < np; i++)                             /* quietest released (not one fading out already) */
        if (!t->v[i].gate && t->v[i].stage != 4u && (best == np || quieter(&t->v[i], &t->v[best])))
            best = i;
    if (best == np)
        for (i = 0; i < np; i++)                         /* oldest held, not the bass */
            if (i != low && (best == np || t->v[i].age < t->v[best].age))
                best = i;
    best = best == np ? 0 : best;
    if (t->v[best].stage == 4u && !(t->v[best].pend & VP_ON) && voices_busy() >= NVOICE)
        voice_room(t, 0);                               /* (a voice fading out sounds again: within the budget, as a
                                                         * new one; its fade lasts STEAL_BLOCKS now, often met here) */
    steal_fade = t->v[best].active && t->v[best].stage != 4u && t->v[best].env >= STEAL_QUIET;
    return &t->v[best];
}

/* glide, in 1/4096 semitone a control tick (pitch_cur . pitch_frac): RATE = GLIDE is the time an octave
 * takes (any interval at that speed), TIME = GLIDE is the time of any glide. A sequencer SLIDE with
 * GLIDE 0 glides in SLIDE_T (TIME), as an acid slide */
#define SLIDE_T 54                                       /* TIME_MS index: ~50 ms */
static int32_t glide_ticks(int32_t g)                    /* control ticks of TIME_MS index g */
{
    int32_t k = (int32_t)ENV_LIN[g & 127];
    return k ? (int32_t)((1u << 24) / (uint32_t)k) : 1;
}
static void glide_set(track_t *t, voice_t *v, int glide)
{
    int32_t d, ticks;
    if (!glide || !v->pitch_cur) {
        v->pitch_cur = v->pitch16;
        v->pitch_frac = 0;
        v->gstep = 0;
        return;
    }
    if (t->p[P_GLMODE] || !t->p[P_GLIDE]) {
        d = (v->pitch16 << 8) - ((v->pitch_cur << 8) + v->pitch_frac);
        d = d < 0 ? -d : d;
        ticks = glide_ticks(t->p[P_GLIDE] ? t->p[P_GLIDE] : SLIDE_T);
        v->gstep = d / ticks > 0 ? d / ticks : 1;
    } else {
        v->gstep = 0;
    }
}

/* (SLOOP 2.8.3) a sample (SAMPLE, SLICE) played again on a voice still sounding: it starts over at its first frame, so
 * its attack must start from silence too. The envelope went on from the level it had: the sample jumped from where it
 * was to its start at that level (a click), and ATK did nothing. That was every note in MONO / LEGATO / UNISON (one
 * voice) and the same note played again in POLY (its voice reused); different notes in POLY took fresh voices and
 * faded in (a SLOOP user's chopped breaks, ATK set to soften the chops' starts). Now the voice fades out over
 * KILL_BLOCKS (~1.5 ms: env_tick stage 4), then the note starts on it from silence with its ATK (track_render:
 * VP_ON). A note-off within the fade is kept for it (VP_OFF: released as it starts, or held by the pedal) */
#define SPOS_NEW (-32768)                       /* (spread-centre) voice_t spos: a new voice (spread_place) */
#define SPOS_STEP 1024                          /* .. a voice's glide to its place, a block (Q15) */
static void voice_start(track_t *t, voice_t *v, uint32_t note, uint32_t vel, int glide)
{
    const engine_t *e = ENGINES[t->engine];
    int sounding = v->active && v->stage != 0, fade = steal_fade;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t s0 = v->s[0], s1 = v->s[1], s3 = v->s[3], s4 = v->s[4], s5 = v->s[5], s6 = v->s[6], s7 = v->s[7];
    steal_fade = 0;
    if (sounding && (eng_sampled(e) || fade)) {         /* (SLOOP 2.8.3) fade it out first, then this note from silence
                                                         * (steal-quiet-tails: the part's own voice taken for it, its
                                                         * cap full: OWN_BLOCKS) */
        v->note = (uint8_t)note;                        /* (the same note again finds it: voice_alloc) */
        v->pend_note = (uint8_t)note;
        v->pend_vel = (uint8_t)vel;
        v->pend = (uint8_t)(VP_ON | (glide ? VP_GLIDE : 0u));
        v->gate = 0;
        v->sus = 0;
        if (v->stage != 4u)
            v->kill = fade && !eng_sampled(e) ? OWN_BLOCKS : KILL_BLOCKS;
        if (fade)
            voice_kills++;
        v->stage = 4;
        v->age = ++vage;
        return;
    }
    v->pend = 0;
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->sus = 0;
    v->pat = 0;
#if BOARD_SMK37
    v->squish = smk_note_pad;
#endif
    v->active = 1;
    v->stage = 1;
    v->age = ++vage;
    v->pitch16 = (int32_t)note * 16;
    glide_set(t, v, glide);
    v->penv = 32767;                                    /* the pitch envelope starts over (not on legato) */
    if (!sounding) {
        v->env = 0;
        v->env_out = 0;
        memset(v->fs, 0, sizeof v->fs);                 /* (SLOPE 48's poles, FILTER 2's; sounding: kept, no click) */
#if CLIPPER_SYNTH
        v->spos = SPOS_NEW;                             /* (spread-centre) SPREAD: at its place from the start */
#endif
    }                                                   /* sounding: the attack starts from the current level */
#if CLIPPER_SYNTH
    {   /* Clipper 0.4: ENV 2 starts over (from its level when sounding), LFO 2 (KEY) from 0, a new random value */
        if (!sounding)
            v->env2 = 0;
        v->e2st = 1;
        v->l2ph = 0;
        v->l3ph = 0;                                    /* (1.0) LFO 3 KEY: from the note's start */
        v->erate = 0;                                   /* (after 1.0) ERATE: from the matrix's first tick on */
        v->trg = 0;                                     /* a TRIG / SYNC input high now: an edge */
        note_seed = note_seed * 1664525u + 1013904223u;
        v->rnd = (int16_t)(note_seed >> 16);
        note_seed = note_seed * 1664525u + 1013904223u;
        v->l2rnd = (int16_t)(note_seed >> 16);
        v->l3rnd = v->l2rnd;
        v->l2cx = (uint16_t)note_seed;                  /* CHAO: the note's own wander (LFO 3's apart from LFO 2's) */
        v->l3cx = (uint16_t)~note_seed;
        if (t->p[P_L3WAVE] == LW_CHAO)
            v->l3rnd = (int16_t)(note_seed >> 8);
        v->ch = mpe_cur_ch;
    }
    const int tail = sounding && v->env < (1 << 17);    /* (Stallion) a tail below -42 dB: as from silence (RODEO's,
                                                         * eng_rodeo.c; ANALOG's here) */
#else
    const int tail = 0;
#endif
    e->note_on(t, v);
    if (sounding && !eng_sampled(e)) {                 /* retrigger / steal: keep phases and filter states */
        v->ph[0] = ph0;                                 /* (resetting them clicks) */
#if CLIPPER_SYNTH
        if (e != &ENG_RODEO && e != &ENG_STAMPEDE && e != &ENG_PALOMINO && e != &ENG_MARE && !(e == &ENG_ANALOG && tail))
                                                        /* (Stallion) RODEO, STAMPEDE, PALOMINO, MARE: their note_on's (a
                                                         * tail below -42 dB: OSC 2 a quarter on again, or in step; else
                                                         * as it was); ANALOG: its note_on's on such a tail (osc 2 a
                                                         * quarter on, the filter clear: the drifted phases kept, near
                                                         * half a turn they cancelled, a note -4 .. +11 dB at random;
                                                         * tests/tail_level_test.c) */
#endif
        v->ph[1] = ph1;
        v->ph[2] = ph2;
        if (e == &ENG_ANALOG && !tail) {
            v->s[0] = s0;
            v->s[1] = s1;
        } else if (e == &ENG_ANALOG) {
            v->ph[1] = ph0 + 0x40000000u;               /* (Stallion) the tail: osc 2 a quarter on from osc 1 as it
                                                         * runs on (analog_note_on's from the 0 it gives osc 1 at env 0:
                                                         * a voice started in this block and taken again) */
        } else if (e == &ENG_DIGITAL) {
            v->s[5] = s5;
            v->s[6] = s6;
            v->s[7] = s7;                               /* op 4 phase; the modulator envelope restarts */
            v->s[3] = s3;                               /* (0.9) the index glides there from where it was */
        } else if (e == &ENG_TRIO) {
            v->s[0] = s0;                               /* filter */
            v->s[1] = s1;
            v->s[4] = s4;                               /* the sample waiting for its step corrections */
        } else if (e == &ENG_PHASE) {
            v->s[0] = s0;                               /* the WAVE / WAVE2 toggles go with the phases kept */
            v->s[1] = s1;
        } else if (e == &ENG_NOISE) {
            v->s[0] = s0;                               /* the filter and the colour poles (2.5) */
            v->s[1] = s1;
            v->s[4] = s4;
            v->s[5] = s5;
            v->s[6] = s6;
            v->s[7] = s7;
        }
    }
}

/* a UNISON voice's start phases moved by r (Q32: a turn of its note), as if it had started that much of a period
 * sooner: in phase the voices stack, spread they sum as noise. Each engine moves its own oscillators (after Clipper
 * 1.2: FMX and MACRO took nothing here, every voice in phase: FMX +12 dB over POLY, peaks of 4 full scales; MACRO's
 * chords +7 dB peaks) */
static void unison_spread(track_t *t, voice_t *v, uint32_t r, const voice_t *lead, uint32_t j, uint32_t n)
{
    const engine_t *e = ENGINES[t->engine];
    /* (after Clipper 1.2) each engine moves all its oscillators (and reseeds its noise): ph[0] alone left the others in
     * phase (ANALOG's osc 2, TRIO's 2 and 3, DIGITAL's and FM6's operators, the subs, SAMPLE's start...) */
    if (e == &ENG_ANALOG)
        analog_spread(t, v, r);
    else if (e == &ENG_DIGITAL)
        digital_spread(t, v, r);
    else if (e == &ENG_PHASE)
        phase_spread(t, v, r);
    else if (e == &ENG_TRIO)
        trio_spread(t, v, r);
    else if (e == &ENG_NOISE)
        noise_spread(t, v, r);
    else if (e == &ENG_FM6)
        fm6_spread(t, v, r);
    else if (e == &ENG_SAMPLE)
        sample_spread(t, v, r);
    else if (e == &ENG_HYBRID)
        hybrid_spread(t, v, r);
#if CLIPPER_SYNTH
    else if (e == &ENG_FMX)
        fmx_spread(t, v, r);
    else if (e == &ENG_MACRO)
        macro_spread(t, v, r);
    else if (e == &ENG_STRINGS)
        strings_spread(t, v, r);
    else if (e == &ENG_WAVE)
        wave_spread(t, v, r);
    else if (e == &ENG_VPS)
        vps_spread(t, v, r);
    else if (e == &ENG_PONY)
        pony_spread(t, v, r);
    else if (e == &ENG_WSEQ)
        wseq_spread(t, v, r);
    else if (e == &ENG_STAMPEDE)
        stampede_spread(t, v, r);
    else if (e == &ENG_RODEO)
        rodeo_spread(t, v, r);
    else if (e == &ENG_PALOMINO)
        palomino_spread(t, v, r, lead, j, n);         /* (Stallion: voice j of n at a fixed offset from the stack's lead;
                                                      * lead 0: none) */
    else if (e == &ENG_VECTOR || e == &ENG_MARE)
        uni_phases(e, v, r);                         /* (test-all's: their noise too) */
#endif
#if FELUCCA_SLICE
    else if (e == &ENG_SLICE)
        ;                                            /* (slices: as played) */
#endif
    else
        v->ph[0] += r;
}

/* an extra UNISON voice to start spread (unison_spread): from silence; PALOMINO also over a tail below -42 dB, as its
 * note-on counts it (Stallion: the stack kept where the tail's DETUNE had drifted it, the same note's level at random
 * again) */
static int uni_sounding(const track_t *t, const voice_t *v)
{
#if CLIPPER_SYNTH
    if (ENGINES[t->engine] == &ENG_PALOMINO && v->env < (1 << 17))
        return 0;
#endif
    return v->active && v->stage != 0;
}

/* MONO / LEGATO / UNISON: one note on one voice (eight for UNISON, or the
 * engine's voice cap, spread by DETUNE over the same width) */
/* (Stallion) mono_play and trk_note_off1 compiled for size: a note's event, not a sample (the FM-1 image is full) */
__attribute__((minsize)) static void mono_play(track_t *t, uint32_t note, uint32_t vel, int retrig, int glide)
{
    uint32_t mode = voice_mode(t), nv = mode == V_UNISON ? uni_nvoice(t) : 1u, i;
#if CLIPPER_SYNTH
    if (retrig) {                                       /* (load-steal) the stack's voices to start: room in the CPU
                                                         * budget from the other parts (this part's note is kept) */
        uint32_t need = 0;
        for (i = 0; i < nv; i++)
            need += !t->v[i].active || t->v[i].stage == 4u;
        vc_admit(t, need, note);
    }
#endif
    for (i = 0; i < nv; i++) {
        voice_t *v = &t->v[i];
        int32_t k = 2 * (int32_t)i - (int32_t)(nv - 1u);   /* -7 .. 7 */
        if (nv > 1u && nv < UNI_MAX)
            k = k * (int32_t)(UNI_MAX - 1u) / (int32_t)(nv - 1u);   /* fewer voices: the outer ones as wide */
        v->fine = nv > 1u ? k * t->p[P_DETUNE] * 56 / 889 : 0;   /* up to ~±40 cents */
        if (!retrig && i && (!v->active || v->stage == 4u))
            continue;                                       /* legato: an extra voice given up (the overload shed it,
                                                             * another part took it) waits for the next note: started
                                                             * here it came back with a fresh attack in the line, and
                                                             * brought the load back for the shedder to cut again */
        if (retrig || !v->active || !v->gate) {
            int was_sounding = uni_sounding(t, v);
            if ((!v->active || v->stage == 4u) && !voice_room(t, i > 0))
                continue;                                   /* no room for this extra UNISON voice */
            /* level: about the same sum for 8 or 4 voices at random phases */
            voice_start(t, v, note, nv >= UNI_MAX ? vel * 36u / 100u : nv > 1u ? vel / 2u : vel, glide);
            if (nv > 1u && i && !was_sounding) {   /* random start phases (SAMPLE: its start; not SLICE): */
                uni_seed = uni_seed * 1664525u + 1013904223u;
                unison_spread(t, v, uni_seed, &t->v[0], i, nv);
            } else if (nv > 1u && i && (v->pend & VP_ON))
                v->pend |= VP_UNI;                          /* (a sample's, after its fade: spread as it starts) */
        } else {                                            /* legato: new pitch, same envelope */
            v->note = (uint8_t)note;
#if CLIPPER_SYNTH
            v->ch = mpe_cur_ch;                             /* (MPE: the new note's channel) */
#endif
            v->pitch16 = (int32_t)note * 16;
            if (vel > v->vel && nv == 1u)
                v->vel = (uint8_t)vel;
            glide_set(t, v, glide);
        }
    }
    t->mono_note = (uint8_t)note;
}

/* the note the mono stack asks for (PRIO: LAST / LOW / HIGH), 0 = none */
static uint32_t mono_pick(const track_t *t)
{
    uint32_t i, n;
    if (!t->nmono)
        return 0;
    n = t->mono_stack[t->nmono - 1u];
    for (i = 0; i < t->nmono; i++) {
        if (t->p[P_PRIO] == 1 && t->mono_stack[i] < n)
            n = t->mono_stack[i];
        if (t->p[P_PRIO] == 2 && t->mono_stack[i] > n)
            n = t->mono_stack[i];
    }
    return n;
}

static void mono_remove(track_t *t, uint32_t note)
{
    uint32_t j, k = 0;
    for (j = 0; j < t->nmono; j++)
        if (t->mono_stack[j] != note)
            t->mono_stack[k++] = t->mono_stack[j];
    t->nmono = (uint8_t)k;
}

#if CLIPPER_SYNTH
/* Clipper 0.3: poly unison (VOICE UNI2 / UNI4): each note on 2 or 4 voices spread by DETUNE as UNISON's (the
 * outer ones as wide), random start phases. The voice for one more: a free one while the budget has room, else
 * this part's oldest released, else a released or extra voice of another part, else this part's oldest held that is
 * not its lowest note (the bass). The same note again restarts its own voices in place */
static int puni_taken(const uint32_t *idx, uint32_t n, uint32_t i)
{
    uint32_t j;
    for (j = 0; j < n; j++)
        if (idx[j] == i)
            return 1;
    return 0;
}
static uint32_t puni_pick(track_t *t, const uint32_t *idx, uint32_t n, uint32_t np)
{
    uint32_t i, best = np, low = lowest_held(t);
    if (voices_busy() < NVOICE)
        for (i = 0; i < np; i++)
            if (!t->v[i].active && !puni_taken(idx, n, i))
                return i;
    for (i = 0; i < np; i++)
        if (t->v[i].active && !t->v[i].gate && t->v[i].stage != 4u && !puni_taken(idx, n, i) &&
            (best == np || t->v[i].age < t->v[best].age))
            best = i;
    if (best < np)
        return best;
    if (voice_room(t, 1))                                /* another part gives one up (soft) */
        for (i = 0; i < np; i++)
            if (!t->v[i].active && !puni_taken(idx, n, i))
                return i;
    for (i = 0; i < np; i++)
        if (t->v[i].gate && !puni_taken(idx, n, i) && (low >= NVOICE || t->v[i].note != t->v[low].note) &&
            (best == np || t->v[i].age < t->v[best].age))
            best = i;
    return best;
}
static void puni_on(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t np = trk_nvoice(t), k = t->p[P_VOICE] == V_PUNI2 ? 2u : 4u, idx[4], n = 0, i, j, m;
    if (k > np)
        k = np;
    for (i = 0; i < np && n < k; i++)                  /* the same note: its voices again */
        if (t->v[i].active && t->v[i].note == note && t->v[i].stage != 4u)
            idx[n++] = i;
    m = vc_admit(t, k - n, note);                       /* (load-steal) the note's voices within the CPU budget, as a
                                                         * whole: ours given up are its voices first */
    for (i = 0; m && i < np && n < k; i++)
        if ((m >> i) & 1u)
            idx[n++] = i;
    while (n < k) {
        i = puni_pick(t, idx, n, np);
        if (i >= np)
            break;
        idx[n++] = i;
    }
    for (j = 0; j < n; j++) {
        voice_t *v = &t->v[idx[j]];
        int was = uni_sounding(t, v);
        int32_t s = n > 1u ? (2 * (int32_t)j - (int32_t)(n - 1u)) * 7 : 0;   /* -7 .. 7 (x (n - 1)) */
        v->fine = n > 1u ? s * t->p[P_DETUNE] * 56 / (889 * (int32_t)(n - 1u)) : 0;
        steal_fade = ((m >> idx[j]) & 1u) && v->env >= STEAL_QUIET;   /* (voice-pressure) ours taken: fades first */
        voice_start(t, v, note, n >= 4u ? vel / 2u : n > 1u ? vel * 70u / 100u : vel, t->p[P_GLIDE] != 0);
        if (j && !was) {                                /* random start phases (in phase they stack) */
            puni_seed = puni_seed * 1664525u + 1013904223u;
            unison_spread(t, v, puni_seed, &t->v[idx[0]], j, n);
        } else if (j && (v->pend & VP_ON))
            v->pend |= VP_UNI;                          /* (a sample's, after its fade: spread as it starts) */
    }
}
#endif

/* the notes each synth part started since the UI last looked (menu NOTES: a short note lights its key
 * for a few frames, as a drum hit does; ui_studio.c pads_tick). Set here (mostly the audio ISR), taken
 * by the UI with the IRQ off */
static volatile uint32_t note_hits[NPART][4];
/* the visualiser (ui_vis.c): the tracks a note started on (bit per track), each one's last note, a kick */
static volatile uint8_t vis_hit, vis_kick_hit, vis_note[NTRK];

static void trk_note_on1(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t any = 0, i, mode = voice_mode(t);
    if (trk_silent(t))
        return;                                         /* MUTE, or another track soloed */
    vis_hit |= (uint8_t)(1u << ((uint32_t)(t - trk) & 3u));
    vis_note[(uint32_t)(t - trk) & 3u] = (uint8_t)note;
    if (is_drum(t)) {                                   /* the drum track: GM drums (drums.c) */
        if (note == 35u || note == 36u)
            vis_kick_hit = 1;
#if !CLIPPER_SYNTH || BOARD_SMK37                                      /* (Clipper 1.2: no drum machine, its sounds out of the image) */
        drum_on(note, vel);
#endif
        return;
    }
    if ((uint32_t)(t - trk) < NPART && note < 128u)
        note_hits[t - trk][note >> 5] |= 1u << (note & 31u);
    if (t->xf_on || t->eng_req != t->engine) {          /* engine switch under way: after the fade */
        for (i = 0; i < t->xp_n && t->xp_note[i] != note; i++)
            ;
        if (i == t->xp_n && t->xp_n < 4u)
            t->xp_n++;
        i = i < t->xp_n ? i : t->xp_n - 1u;             /* (full: the last one is replaced) */
        t->xp_note[i] = (uint8_t)note;
        t->xp_vel[i] = (uint8_t)vel;
        return;
    }
    for (i = 0; i < NVOICE; i++)
        any |= t->v[i].gate;
    if (!any) {                                        /* fresh phrase: LFO retrigger and fade */
#if CLIPPER_SYNTH
        if (!(ENGINES[t->engine] == &ENG_PONY && (t->p[P_JCHR] & JQ_LFO)))   /* (PONY's CHAR LFO: free-running) */
#endif
        t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
        t->lfo_fade = 0;
    }
    if (voice_poly((int)mode)) {
        voice_t *v;
        t->nmono = 0;                                   /* no stale mono stack after a mode change */
        t->mono_note = 0;
#if CLIPPER_SYNTH
        if (mode >= V_PUNI2) {                          /* Clipper 0.3: poly unison */
            puni_on(t, note, vel);
            return;
        }
#endif
        v = voice_alloc(t, note);
        v->fine = 0;
        voice_start(t, v, note, vel, t->p[P_GLIDE] != 0);
        return;
    }
    {
        uint32_t sounding = t->mono_note && t->v[0].active && t->v[0].gate, want;
        mono_remove(t, note);
        if (t->nmono >= 8u)
            mono_remove(t, t->mono_stack[0]);
        t->mono_stack[t->nmono++] = (uint8_t)note;
        want = mono_pick(t);
        if (sounding && want == t->mono_note)
            return;                                     /* LOW / HIGH priority: this key does not win */
        if (mode == V_MONO || mode == V_PARAT)          /* MONO (and PARA+, its MULTI TRIG): always retrigger,
                                                         * glide when GLIDE is set */
            mono_play(t, want, vel, 1, t->p[P_GLIDE] != 0 || t->slide_glide);
        else                                            /* LEGATO / UNISON: glide only between held notes */
            mono_play(t, want, vel, !sounding, sounding && (t->p[P_GLIDE] != 0 || t->slide_glide));
    }
}

/* STRUM (2.4, P_STRUM): a chord's notes start one after the other, |P_STRUM| ms apart (> 0 from the lowest up, as
 * a guitar's down stroke; < 0 from the highest down). The later ones wait here, in samples; a note-off before its
 * start cancels it, a track's all-off (STOP) clears them */
#define STQ 16u
static struct { uint8_t on, trk, note, vel; uint32_t left; } stq[STQ];
static void strum_cancel(const track_t *t, uint32_t note, int all)
{
    uint32_t i;
    for (i = 0; i < STQ; i++)
        if (stq[i].on && stq[i].trk == (uint8_t)(t - trk) && (all || stq[i].note == note))
            stq[i].on = 0;
}
__attribute__((minsize)) static void trk_note_off1(track_t *t, uint32_t note)   /* (for size: mono_play's note) */
{
    uint32_t i, k = 0, mode = voice_mode(t);
    if (is_drum(t))
        return;                                         /* one-shots */
    strum_cancel(t, note, 0);
    for (i = 0; i < t->xp_n; i++)                       /* not sounding yet (engine switch): forget it */
        if (t->xp_note[i] != note) {
            t->xp_note[k] = t->xp_note[i];
            t->xp_vel[k++] = t->xp_vel[i];
        }
    t->xp_n = (uint8_t)k;
    mono_remove(t, note);                               /* in any mode: a key let go after a VOICE change must
                                                         * not stay in the MONO stack (after Felucca 1.0) */
    if (!voice_poly((int)mode)) {
        uint32_t nv = mode == V_UNISON ? uni_nvoice(t) : 1u;
        if (t->mono_note == note) {
            uint32_t next = mono_pick(t);
            if (next) {                                 /* fall back to a held note, legato */
                mono_play(t, next, t->v[0].vel, 0, t->p[P_GLIDE] != 0);
            } else {
                for (i = 0; i < nv; i++)
                    if (t->v[i].gate) {
                        t->v[i].gate = 0;
                        t->v[i].stage = 3;
                    }
                t->mono_note = 0;
            }
        }
    }
    for (i = 0; i < NVOICE; i++)                        /* POLY voices (also ones left from another mode: */
        if (t->v[i].note == note && t->v[i].gate &&     /* VOICE changed while they were held) */
            (voice_poly((int)mode) || i >= (mode == V_UNISON ? uni_nvoice(t) : 1u) || t->mono_note != note)) {
            if (voice_poly((int)mode) && part_ctl[CLIPPER_SYNTH ? ctl_part(t) : (uint32_t)(t - trk) % NPART].sus) {
                t->v[i].sus = 1;                        /* Clipper: the sustain pedal holds it */
                continue;
            }
            t->v[i].gate = 0;
            t->v[i].stage = 3;
        }
    for (i = 0; i < NVOICE; i++)                        /* (SLOOP 2.8.3) waiting for its fade: it starts, then releases */
        if ((t->v[i].pend & VP_ON) && t->v[i].pend_note == note && t->mono_note != note)
            t->v[i].pend |= VP_OFF;
}

/* Clipper: the sustain pedal comes up: the notes it held are released (a key still down keeps its note:
 * pressed again, its voice started over and lost the mark) */
static void trk_sustain_up1(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].sus) {
            t->v[i].sus = 0;
            if (t->v[i].gate) {
                t->v[i].gate = 0;
                t->v[i].stage = 3;
            }
        }
}

static void trk_all_off1(track_t *t)
{
    uint32_t i;
    strum_cancel(t, 0, 1);
    for (i = 0; i < NVOICE; i++) {
        t->v[i].gate = 0;
        t->v[i].sus = 0;
        t->v[i].stage = t->v[i].active ? 3 : 0;
        t->v[i].pend = 0;                               /* (SLOOP 2.8.3: a note waiting for its voice's fade: gone too) */
    }
    t->nmono = 0;
    t->mono_note = 0;
    t->xp_n = 0;
}

/* Clipper 0.3: the layer. Track 2's sound with track 1's (LAYER: every note on both; SPLIT: below SPLIT on track
 * 2, from it up on track 1), its octave OCT; what a note-on did is kept per note, so its note-off goes where it
 * went whatever changed meanwhile (the mode, the split, the octave). Track 2 has no other way in: its sequencer and
 * arpeggiator never run, the keys, MIDI and track 1's sequencer and arpeggiator all come through here */
#if CLIPPER_SYNTH
static volatile int16_t syn_morph_req = -1;      /* MIDI CC 20 (seq.c): MORPH, applied by the UI (ui_synth.c) */
static uint8_t ly_route[128], ly_note[128];           /* per note: bit 0 track 1, bit 1 track 2; track 2's note */
static uint32_t layer_mode(void)
{
    int32_t m = trk[1].p[P_LYMODE];
    return m == LY_LAYER || m == LY_SPLIT || m == LY_MULTI ? (uint32_t)m : LY_OFF;
}
static void trk_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t m, r = 1;
    if (t != &trk[0] || note > 127u) {
        trk_note_on1(t, note, vel);
        return;
    }
    m = layer_mode();                                   /* (MULTI: the layer comes by its own channel only) */
    if (m == LY_LAYER)
        r = 3;
    else if (m == LY_SPLIT)
        r = (int32_t)note >= trk[1].p[P_LYSPLIT] - 4 ? 1u : 2u;
    if (ly_route[note] & 2u)                            /* (the same note again: its layer note first let go) */
        trk_note_off1(&trk[1], ly_note[note]);
    ly_route[note] = (uint8_t)r;
    if (r & 2u) {
        ly_note[note] = (uint8_t)clamp((int32_t)note + (trk[1].p[P_LYOCT] - 2) * 12, 0, 127);
        trk_note_on1(&trk[1], ly_note[note], vel);
    }
    if (r & 1u)
        trk_note_on1(t, note, vel);
}
static void trk_note_off(track_t *t, uint32_t note)
{
    uint32_t r;
    if (t != &trk[0] || note > 127u) {
        trk_note_off1(t, note);
        return;
    }
    r = ly_route[note] ? ly_route[note] : 1u;
    ly_route[note] = 0;
    if (r & 2u)
        trk_note_off1(&trk[1], ly_note[note]);
    if (r & 1u)
        trk_note_off1(t, note);
}
static void trk_sustain_up(track_t *t)
{
    trk_sustain_up1(t);
    if (t == &trk[0] && !layer_multi())                /* (MULTI: the layer has its own pedal) */
        trk_sustain_up1(&trk[1]);
}
static void trk_all_off(track_t *t)
{
    trk_all_off1(t);
    if (t == &trk[0]) {
        trk_all_off1(&trk[1]);
        memset(ly_route, 0, sizeof ly_route);
    }
}
#else
#define trk_note_on trk_note_on1
#define trk_note_off trk_note_off1
#define trk_sustain_up trk_sustain_up1
#define trk_all_off trk_all_off1
#endif
/* MIDI all sound off (CC 120): every note let go as trk_all_off does, then faded out over KILL_BLOCKS (1.5 ms, no
 * click) instead of through its release: the sound stops whatever REL is set (all notes off, CC 123, releases) */
static void trk_all_cut1(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        if (v->active && v->stage != 4u) {
            v->kill = KILL_BLOCKS;
            v->stage = 4;
        }
    }
}
static void trk_all_cut(track_t *t)
{
    trk_all_off(t);
    trk_all_cut1(t);
    if (CLIPPER_SYNTH && t == &trk[0])
        trk_all_cut1(&trk[1]);                          /* (the layer, as trk_all_off) */
}

/* note i of a chord of n (notes[]): at once, or strummed (rank: its place from the lowest, or the highest) */
static void trk_note_chord(track_t *t, const uint8_t *notes, uint32_t n, uint32_t i, uint32_t vel)
{
    int32_t s = t->p[P_STRUM];
    uint32_t rank = 0, j, q;
    if (!s || n < 2u || is_drum(t)) {
        trk_note_on(t, notes[i], vel);
        return;
    }
    for (j = 0; j < n; j++)                             /* its place: lowest first (down), highest first (up) */
        if (j != i && (s > 0 ? notes[j] < notes[i] || (notes[j] == notes[i] && j < i)
                             : notes[j] > notes[i] || (notes[j] == notes[i] && j < i)))
            rank++;
    if (!rank) {
        trk_note_on(t, notes[i], vel);
        return;
    }
    strum_cancel(t, notes[i], 0);
    for (q = 0; q < STQ && stq[q].on; q++)
        ;
    if (q == STQ) {                                     /* (full: at once) */
        trk_note_on(t, notes[i], vel);
        return;
    }
    stq[q].trk = (uint8_t)(t - trk);
    stq[q].note = (uint8_t)notes[i];
    stq[q].vel = (uint8_t)vel;
    stq[q].left = rank * (uint32_t)(s < 0 ? -s : s) * (uint32_t)FS / 1000u;
    stq[q].on = 1;
}
/* each audio block: the strummed notes due */
static void strum_block(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < STQ; i++)
        if (stq[i].on) {
            if (stq[i].left <= n) {
                stq[i].on = 0;
                trk_note_on(&trk[stq[i].trk % NTRK], stq[i].note, stq[i].vel);
            } else {
                stq[i].left -= n;
            }
        }
}

/* engine switch, at each block start (events_block), before any note of the block. The UI writes
 * eng_req and the new engine's P_E0..P_E7 together (IRQ off). The part's sounding voices (released by
 * the preset change) then fade out over XF_BLOCKS blocks on the old engine, rendered with its own
 * parameters (pe_old: an engine never reads another engine's values, which index its tables); only
 * then the engine switches and the notes that came during the fade start on it. A part with nothing
 * sounding switches at once. */
#if CLIPPER_SYNTH
#define XF_BLOCKS 8u                                    /* Clipper: 8 x 32 samples, 5.8 ms (a bass fades without a thump) */
#else
#define XF_BLOCKS 4u                                    /* 4 x 32 samples: 2.9 ms */
#endif
static void engine_block(track_t *t)
{
    uint32_t i, any = 0;
    if (t->xf_on || t->eng_req != t->engine) {
        if (!t->xf_on) {
            for (i = 0; i < NVOICE; i++)
                any |= t->v[i].active;
            if (any) {
                t->xf_on = 1;
                t->xf = XF_BLOCKS;
                return;
            }
        } else if (t->xf) {
            return;                                     /* still fading (track_render counts down) */
        }
        for (i = 0; i < NVOICE; i++) {                  /* faded to 0: gone */
            voice_t *v = &t->v[i];
            v->active = v->gate = 0;
            v->pend = 0;                                /* (a note waiting for its voice's fade: the old sound's) */
            v->stage = 0;
            v->env = v->env_out = 0;
        }
        if (t->eng_req == ENGI_FM6 && ((fm6_pend >> ((uint32_t)(t - trk) % NPART)) & 1u))
            return;                                     /* (SLOOP 2.6) FM6: the new notes wait for the new sound's patch
                                                         * (eng_fm6.c fm6_poll, main loop): not a note of the old one */
        t->engine = t->eng_req % NENGINES;
        t->xf_on = 0;
#if CLIPPER_SYNTH
        t->xf_snd = 0;
#endif
        t->nmono = 0;
        t->mono_note = 0;
        {
            uint32_t n = t->xp_n;
            t->xp_n = 0;
            for (i = 0; i < n; i++)
                trk_note_on1(t, t->xp_note[i], t->xp_vel[i]);   /* (the layer had it then) */
        }
    }
    for (i = 0; i < 8u; i++)                            /* the engine's own values, for a later fade */
        t->pe_old[i] = t->p[P_E0 + i];
}

#if CLIPPER_SYNTH
/* Clipper 0.3: before a sound changes under sounding voices (a preset, an engine, a user preset, SWAP, BACK...):
 * they fade out over XF_BLOCKS with all their own values (p_old: fx.c mix_part renders the part with them; pe_old the
 * engine's), then go; notes played meanwhile wait for the new sound (engine_block). Without it the voices ringing
 * on took the new values at once (a new trim, filter, envelope, table) and clicked. The main loop / UI calls it */
static void sound_fade_begin(track_t *t)
{
    uint32_t i, any = 0;
    if ((uint32_t)(t - trk) >= NPART || t->xf_on)
        return;
    for (i = 0; i < NVOICE; i++)
        any |= t->v[i].active && t->v[i].stage != 0;
    if (!any)
        return;
    memcpy(t->p_old, t->p, sizeof t->p_old);
    t->xf_snd = 1;
    t->xf = XF_BLOCKS;
    RING_PUBLISH();
    t->xf_on = 1;                                       /* (last: the ISR uses p_old from then on) */
}
#else
#define sound_fade_begin(t) ((void)0)
#endif

/* (after 1.0) the matrix's ERATE: a time index (ATK DEC REL of ENV 1 and 2) the voice's ERATE away, up faster, down
 * slower: 52 steps of TIME (7.5 % each) at full scale. KEY to ERATE is the key follow (the 8-voice poly's KEY FOLLOW): AMT
 * +63 about half the times an octave above C4, twice an octave below; VEL to ERATE a harder note's faster attack. None
 * (0): the index as it is */
static inline uint32_t env_rate(const voice_t *v, int32_t i)
{
#if CLIPPER_SYNTH
    if (v->erate)
        return (uint32_t)clamp(i - ((v->erate * 52 + 16384) >> 15), 0, 127);
#else
    (void)v;
#endif
    return (uint32_t)i & 127u;
}
/* Stallion: the amplitude envelope's shortest attack and release (indexes of TIME: 1 ms .. 10 s). ATK 0 (1 ms) and REL
 * up to 30 (5 ms to -40 dB) took a bass from silence to full, and back, in a fraction of its cycle: a tick at each
 * end of the note, audible in headphones (MARE BASS: up in 1 ms, down 20 dB in 2 ms; 231 of the 517
 * factory sounds fell 20 dB within 10 ms of the key let go). At least 3 ms up (15) and 37 ms down to -40 dB (50,
 * 18 ms to -20 dB: a 65 Hz cycle is 15 ms); a longer time is as set. ENV 2 (the matrix's) is left as it is */
#define ENV_ATK_MIN 15u
#define ENV_REL_MIN 50u
static inline uint32_t env_floor(uint32_t i, uint32_t lo) { return i < lo ? lo : i; }
/* one control tick (CTL samples) of the amplitude envelope; returns Q15 */
static int32_t env_tick(track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    int32_t sus = (int32_t)p[P_SUS] << 17;              /* Q24 */
    switch (v->stage) {
    case 1:
        {
            int32_t inc = (int32_t)ENV_LIN[env_floor(env_rate(v, p[P_ATK]), ENV_ATK_MIN)];
#if CLIPPER_SYNTH
            if (t->engine >= ENGI_PALOMINO && t->engine <= ENGI_MARE)   /* an analog attack: an RC charge toward 1.3, */
                inc = (int32_t)(((((int64_t)inc * (21810381 - v->env)) >> 24) * 6005) >> 12);   /* the same length */
#endif
            v->env += inc > 0 ? inc : 1;
        }
        if (v->env >= (1 << 24)) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2:
        v->env += mulq16(sus - v->env, ENV_EXP[env_rate(v, p[P_DEC])]);
        break;
    case 3:
        v->env -= mulq16(v->env, ENV_EXP[env_floor(env_rate(v, p[P_REL]), ENV_REL_MIN)]);
        if (v->env < (1 << 12)) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
        break;
    case 4:                                             /* given up: faded out over v->kill blocks (STEAL_BLOCKS;
                                                         * a sample played again KILL_BLOCKS) */
        if (v->kill > 1u) {
            v->env -= v->env / v->kill;
            v->kill--;
        } else {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
            v->kill = 0;
        }
        break;
    default:
        v->env = 0;
        break;
    }
    return v->env >> 9;
}

#if CLIPPER_SYNTH
/* Clipper 0.4: ENV 2, a second ADSR (P_E2ATK..P_E2REL) for the matrix; it releases with the voice. Q15.
 * tg: MX_E2_PATCHED, a cable into the matrix's TRIG2: at the end of the decay ENV 2 holds while MX_E2_HIGH (the
 * input high last block; LOOP: loops), else it releases. 0: the plain ADSR */
#define MX_E2_PATCHED 1u
#define MX_E2_HIGH 2u
static int32_t env2_tick(const int16_t *p, voice_t *v, uint32_t tg)
{
    if (v->stage >= 3u && v->e2st && v->e2st < 3u)
        v->e2st = 3;
    switch (v->e2st) {
    case 1:
        v->env2 += (int32_t)ENV_LIN[env_rate(v, p[P_E2ATK])];
        if (v->env2 >= (1 << 24)) {
            v->env2 = 1 << 24;
            v->e2st = 2;
        }
        break;
    case 2:
        v->env2 += mulq16(((int32_t)p[P_E2SUS] << 17) - v->env2, ENV_EXP[env_rate(v, p[P_E2DEC])]);
        if (tg && !(tg & MX_E2_HIGH) && v->env2 - ((int32_t)p[P_E2SUS] << 17) < (1 << 18) &&
            ((int32_t)p[P_E2SUS] << 17) - v->env2 < (1 << 18))
            v->e2st = 3;                                /* TRIG2 patched and low: the decay done, released */
        else if (p[P_E2LOOP] && v->env2 - ((int32_t)p[P_E2SUS] << 17) < (1 << 18))
            v->e2st = 1;                                /* (1.0) LOOP: close to the sustain, the attack again (from
                                                         * there: no jump), until the note is let go */
        break;
    case 3:
        v->env2 -= mulq16(v->env2, ENV_EXP[env_rate(v, p[P_E2REL])]);
        if (v->env2 < (1 << 12)) {
            v->env2 = 0;
            v->e2st = 0;
        }
        break;
    default:
        v->env2 = 0;
        break;
    }
    return v->env2 >> 9;
}

/* Clipper 0.4: the modulation matrix, on every engine. Six slots (P_M1SRC..P_M6AMT) of a source (MS_*: LFO 1 / 2,
 * ENV 1 / 2, velocity, key, mod wheel, aftertouch, a random value per note, MPE slide), all Q15, times AMT
 * (-64..63), to a destination (MT_*): the pitch (+-24 semitones, finer near 0), the cutoff (+-4 octaves), the shape,
 * the level, the pan (the part's, from its newest note), LFO 2's rate, or any of the engine's eight EDIT values
 * (over their whole range, per note). The slots in use, read once a block */
typedef struct {
    uint8_t src[MX_SLOTS], dst[MX_SLOTS];
    int8_t amt[MX_SLOTS];
    uint32_t n;                                         /* slots in use */
    uint64_t srcs, dsts;                                /* bit per source, per destination (destinations: up to 64) */
} mx_t;
/* the TRIG / SYNC inputs: a bit each in voice_t trg, an edge when the cables' sum rises past a third of +V */
#define MX_TB_T1 1u
#define MX_TB_T2 2u
#define MX_TB_S1 4u
#define MX_TB_S2 8u
#define MX_TRIG_LEVEL 10922
#define MX_TRIG_DSTS (1ull << MT_TRIG1 | 1ull << MT_TRIG2 | 1ull << MT_SYNC1 | 1ull << MT_SYNC2)
/* live: the destinations the engine uses (engines.c mx_dst_live_e); a slot into another is skipped as an
 * empty one, so it changes nothing whether mx_prune has cleared it yet or not: not srcs (MPE's SLIDE-to-cutoff when no
 * slot takes SLIDE; NOISE's random numbers, drawn only while a slot takes NOISE), not n */
static void mx_load(const int16_t *p, mx_t *x, uint64_t live)
{
    uint32_t k;
    x->n = x->srcs = x->dsts = 0;
    for (k = 0; k < MX_SLOTS; k++) {
        uint32_t id = mx_pid(k);                        /* (24 slots: core.h mx_pid) */
        int32_t sr = p[id], ds = p[id + 1u], a = p[id + 2u];
        if (sr <= MS_OFF || sr >= MS_NSRC || ds <= MT_OFF || ds >= MT_COUNT || !a || !(live >> ds & 1u))
            continue;
        if (ds == MT_L2RATE && sr == MS_LFO2)
            continue;                                   /* (LFO 2 on its own rate: none) */
        x->src[x->n] = (uint8_t)sr;
        x->dst[x->n] = (uint8_t)ds;
        x->amt[x->n] = (int8_t)clamp(a, -64, 63);
        x->srcs |= 1ull << sr;
        x->dsts |= 1ull << ds;
        x->n++;
    }
}
/* Clipper 0.8: the MOD SEQ. Four lanes of 16 steps (-8..7) per sound, stepping at RATE (the song's grid while it
 * plays and START is SYNC; from each first key with KEY, and while stopped), LEN steps, each lane's value gliding
 * (SMTH) to its step's: the matrix's sources SEQ1..SEQ4 (seq.c mseq_tick moves them, once a block). A sound loaded
 * (a factory one) brings four shapes to start from: a ramp, a triangle, a rhythm, a random line */
static const int8_t MSEQ_INIT[MSEQ_LANES][MSEQ_STEPS] = {
    {-8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7},
    {0, 2, 4, 6, 7, 5, 3, 1, -1, -3, -5, -7, -8, -6, -4, -2},
    {7, 7, -8, -8, 7, -8, 7, 7, -8, 7, -8, -8, 7, -8, 7, -8},
    {3, -5, 6, -2, 0, 7, -7, 2, -4, 5, -1, -8, 4, 1, -6, 6},
};
static void mseq_init(track_t *t)
{
    memcpy(t->ms, MSEQ_INIT, sizeof t->ms);
}
static int32_t mseq_q15(int32_t v) { return clamp(v * 4369, -32767, 32767); }   /* a step: -8 full scale, 7 just under */
/* Clipper 0.9, the TUNING (GLO > TUNING; the editor sends a Scala scale): every note's pitch off equal temperament by
 * tun_tab (1/4096 semitone), between notes interpolated (a glide goes through the scale). Two kinds of systems:
 *   12-note tables, each pitch class's deviation from equal temperament around ROOT (JUST, PYTHAG, MEANTONE, WERCK3,
 *   KIRNB3, the maqams RAST and BAYATI with their neutral thirds);
 *   scales of n degrees in a period (SLENDRO, PELOG, 19 / 24 / 31-EDO, the USER scale from a .scl file): every key
 *   the next degree, C4 + ROOT the scale's 1/1 at its equal-tempered pitch.
 * G_TUNE (A4 in cents) still moves it all. EQUAL: tun_on 0, nothing touched, bit for bit.
 * (1.0) A4: the reference pitch in Hz (400 .. 480: 432, 415 baroque, 442 / 443 orchestras), every system on top of it */
enum { TU_EQUAL, TU_JUST, TU_PYTH, TU_MEAN, TU_WERCK, TU_KIRN, TU_RAST, TU_BAYATI, TU_SLENDRO, TU_PELOG, TU_19, TU_24,
       TU_31, TU_USER, TU_N };
static const char *const N_TUNE[TU_N] = {"EQUAL", "JUST", "PYTHA", "MEANT", "WERCK", "KIRNB", "RAST", "BAYAT", "SLEND",
                                         "PELOG", "19EDO", "24EDO", "31EDO", "USER"};
#define TU_MAXDEG 48u
typedef struct {
    uint8_t sys, root, n;                               /* system, ROOT (0..11), USER: degrees (1..TU_MAXDEG) */
    int8_t a4;                                          /* (1.0) A4 - 440 Hz (-40..40: 400..480 Hz); 0.9 stored 0 */
    uint16_t period;                                    /* USER: the period, 1/16 cent (1200 cents: 19200) */
    uint16_t deg[TU_MAXDEG];                            /* USER: degrees 1..n - 1 above 1/1 (deg[0] = 0), 1/16 cent */
} tune_t;
static tune_t tune_cfg = {TU_EQUAL, 0, 12, 0, 19200,      /* (USER until a scale comes: equal temperament) */
                          {0, 1600, 3200, 4800, 6400, 8000, 9600, 11200, 12800, 14400, 16000, 17600}};
static int32_t tun_tab[129];
/* (1.0) the reference A4, 400 .. 480 Hz: 12 x log2(A4 / 440) in 1/4096 semitone (432: -1301, 415: -4148) */
static const int16_t TU_A4[81] = {-6759, -6582, -6405, -6229, -6053, -5878, -5703, -5528, -5354, -5181, -5008, -4835, -4663,
    -4491, -4319, -4148, -3977, -3807, -3637, -3468, -3299, -3130, -2962, -2794, -2627, -2460, -2293, -2127, -1961, -1795,
    -1630, -1465, -1301, -1137, -974, -810, -648, -485, -323, -161, 0, 161, 322, 482, 642, 801, 960, 1119, 1278, 1436, 1594,
    1751, 1908, 2065, 2221, 2377, 2533, 2688, 2843, 2998, 3152, 3306, 3460, 3613, 3766, 3919, 4071, 4223, 4375, 4526, 4677,
    4828, 4978, 5128, 5278, 5428, 5577, 5726, 5874, 6022, 6170};
static volatile uint8_t tun_on;
static const int16_t TU_TAB12[6][12] = {               /* 1/10 cent off equal temperament, from ROOT */
    {0, 117, 39, 156, -137, -20, -98, 20, 137, -156, 176, -117},       /* JUST (5-limit) */
    {0, -98, 39, -59, 78, -20, 117, 20, -78, 59, -39, 98},             /* PYTHAG */
    {0, -240, -68, 103, -137, 34, -205, -34, -274, -103, 68, -171},    /* MEANTONE (1/4 comma) */
    {0, -98, -78, -59, -98, -20, -117, -39, -78, -117, -39, -78},      /* WERCKMEISTER III */
    {0, -98, -68, -59, -137, -20, -98, -34, -78, -103, -39, -117},     /* KIRNBERGER III */
    {0, 0, 0, 0, -500, 0, 0, 0, 0, 0, 0, -500},                        /* RAST: E and B half flat */
};
static int32_t tun_deg_cents16(uint32_t sys, uint32_t k, uint32_t *n, uint32_t *period)   /* degree k, 1/16 cent */
{
    static const uint16_t SL[5] = {0, 231, 474, 717, 955}, PE[7] = {0, 120, 258, 539, 675, 785, 943};
    switch (sys) {
    case TU_SLENDRO: *n = 5, *period = 19200; return SL[k % 5u] * 16;
    case TU_PELOG: *n = 7, *period = 19200; return PE[k % 7u] * 16;
    case TU_19: *n = 19, *period = 19200; return (int32_t)(k % 19u) * 19200 / 19;
    case TU_24: *n = 24, *period = 19200; return (int32_t)(k % 24u) * 800;
    case TU_31: *n = 31, *period = 19200; return (int32_t)(k % 31u) * 19200 / 31;
    default:
        *n = clamp(tune_cfg.n, 1, TU_MAXDEG), *period = clamp(tune_cfg.period, 16, 65535);
        return k % *n ? tune_cfg.deg[k % *n] : 0;
    }
}
/* the table from tune_cfg (the main loop: a change on GLO > TUNING, the editor, power-on) */
static void tune_build(void)
{
    static int32_t tab[129];
    uint32_t sys = tune_cfg.sys < TU_N ? tune_cfg.sys : TU_EQUAL, key, root = tune_cfg.root % 12u;
    int32_t a4 = TU_A4[clamp(tune_cfg.a4, -40, 40) + 40];   /* (1.0) the reference pitch, on every key */
    for (key = 0; key < 128u; key++) {
        int32_t off = 0;
        if (sys >= TU_JUST && sys <= TU_RAST) {
            off = TU_TAB12[sys - TU_JUST][(key + 12u - root) % 12u] * 4096 / 1000;
        } else if (sys == TU_BAYATI) {                  /* (on ROOT: its 2nd degree half flat) */
            off = (key + 12u - root) % 12u == 2u ? -50 * 4096 / 100 : 0;
        } else if (sys != TU_EQUAL) {
            int32_t d = (int32_t)key - (60 + (int32_t)root), oct, k;
            uint32_t n, per;
            tun_deg_cents16(sys, 0, &n, &per);
            oct = d >= 0 ? d / (int32_t)n : -((-d + (int32_t)n - 1) / (int32_t)n);
            k = d - oct * (int32_t)n;
            off = (oct * (int32_t)per + tun_deg_cents16(sys, (uint32_t)k, &n, &per)) * 64 / 25 -
                  d * 4096;                             /* (1/16 cent x 4096 / 1600; fits 32 bits: +-8 octaves) */
            off = clamp(off, -100 * 4096, 100 * 4096);
        }
        tab[key] = off + a4;
    }
    tab[128] = tab[127];
    fm1_irq_off();
    memcpy(tun_tab, tab, sizeof tun_tab);
    tun_on = (uint8_t)(sys != TU_EQUAL || a4 != 0);
    fm1_irq_on();
}
static inline int32_t tun_at(int32_t p16)               /* the offset at a pitch in 1/16 semitone */
{
    int32_t n = clamp(p16, 0, 127 * 16) >> 4, f = clamp(p16, 0, 127 * 16) & 15;
    return tun_tab[n] + (((tun_tab[n + 1] - tun_tab[n]) * f) >> 4);
}

/* Clipper 0.9, VINTAGE (VOICE 2 > VINTG): the voices of an analog polysynth are never quite alike (the Prophet-5's
 * "slop"): each voice here has its own small tuning error and filter offset (its "card", fixed), a slow drift of its
 * pitch (a random walk, a new target every second or so) and each note a little of its own. At 127: the card +-6 cents,
 * the drift +-5, the note +-2, the filter +-1.5 steps; 0: nothing, bit for bit */
static int32_t vcard(uint32_t k, uint32_t salt)         /* the voice card's value, Q15 */
{
    uint32_t h = (k + 1u) * 0x9E3779B1u ^ (salt + 7u) * 0x85EBCA77u;
    h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12;
    return (int32_t)(h >> 16) - 32768;
}
static int32_t vintage_pitch(voice_t *v, uint32_t k, int32_t vint)   /* 1/4096 semitone; once a block */
{
    if ((rng() & 1023u) < 2u)                           /* (~1.4 s between targets) */
        v->vdtgt = (int16_t)((int32_t)(rng() >> 16) - 32768);
    v->vdrift = (int16_t)(v->vdrift + ((v->vdtgt - v->vdrift) >> 9));
    return ((((vcard(k, 0u) * 246) >> 15) + ((v->vdrift * 205) >> 15) + ((v->rnd * 82) >> 15)) * vint) >> 7;
}
/* LFO 2's rate index, + the slots to MT_L2RATE (sv: the sources) */
static uint32_t mx_l2rate(const int16_t *p, const mx_t *x, const int32_t *sv)
{
    int32_t r = 0;
    uint32_t k;
    if (x->dsts & (1ull << MT_L2RATE))
        for (k = 0; k < x->n; k++)
            if (x->dst[k] == MT_L2RATE)
                r += sv[x->src[k]] * x->amt[k] >> 6;
    return (uint32_t)clamp(p[P_L2RATE] + (r >> 9), 0, 127);   /* (+-64 steps) */
}
/* the steps the slots to MT_L1RATE add to LFO 1's RATE, from the part's sources (as
 * L2RATE's in FREE: LFO 1 on its own rate none) */
static int32_t mx_l1rate(const int16_t *p, const mx_t *x, const int32_t *sv)
{
    int32_t r = 0;
    uint32_t k;
    (void)p;
    if (x->dsts & (1ull << MT_L1RATE))
        for (k = 0; k < x->n; k++)
            if (x->dst[k] == MT_L1RATE && x->src[k] != MS_LFO1)
                r += sv[x->src[k]] * x->amt[k] >> 6;
    return clamp(r >> 9, -64, 64);
}
#endif
/* an ENV DEST depth times the matrix's E1AMT (Q15: 32768 as it is, 0 none, 65536 x 2) */
static inline int32_t e1_scale(int32_t x, int32_t a)
{
    return a == 32768 ? x : (x * (a >> 1)) >> 14;
}

/* (Stallion) measured at boot on the device (main.c clk_boot, hal/fm1_clk.h): the core's clock (adds a microsecond
 * from RAM, x10) and one flash line's fill (ns x10); 0 on the host (GLO's CPU column then shows neither) */
static uint32_t clk_mhz10, clk_fill10;

/* render one block of a part into out (cleared here); returns the voices rendered */
#if CLIPPER_SYNTH
/* 0.7, the voices on both cores. The second core of the FM-1 (cpu1.c, after SLOOP 2.7 and X0X) takes some of a part's
 * voices: track_render works out every voice's modulation here as before, keeps the render calls of an engine that
 * may split (vs_ok: its render touches its voice, its output and what it only reads), hands the second half to core
 * 1 (into vs_buf), renders the first half here, waits, adds. A voice whose EDIT values the matrix moves takes its own
 * (vs_e, vmod_t ev: the render reads them through vm_ev, the part's p unchanged) and goes to either core as the
 * others. Integer sums: the same output, bit for bit, whoever renders what (tests: CORES=1 / 2, the golden renders).
 * Core 1 late (8 ms): it is held and its voices render here, once */
static voice_t *vs_v[NVOICE];
static vmod_t vs_m[NVOICE];
static int16_t vs_e[NVOICE][8];                      /* the voices' own EDIT values (a matrix slot on them) */
static uint32_t vs_n;
_Static_assert(sizeof(vmod_t) % 4u == 0, "copy32 (dsp.c) copies words");
/* (1.0) SPREAD: each voice's place in the stereo field. The part renders its voices' sum (the mid, as before: bit for
 * bit without SPREAD) and, when SPREAD is up, part_side: each voice times its gain (its place x SPREAD, gliding
 * through the block), which fx.c mix_part1 adds to the left and takes from the right. A voice alone into a scratch
 * first (vs_tmp, one per core: engines.c C1_IX). FILTER 2 (t->f2v): on an engine without the MODEL filter, after the
 * voice alone (engines.c f2_post); SPLIT: the voice's side too (engines.c f2_sd, the core's: the engine's filter pair
 * writes it before the voice's level, dsp.c fmod_pair; f2_post adds it at the voice's), into the part's side */
static int32_t part_side[CTL], vs_side1[CTL];
static uint8_t part_side_on, vs_side1_on;
static int32_t vs_g0[NVOICE], vs_g1[NVOICE];
/* the places, Q15: neighbours (the voices one note after the other take) left and right, the outer ones first */
static const int16_t VS_POS[16] = {-32767, 32767, -16384, 16384, -24576, 24576, -8192, 8192,
                                   -29491, 29491, -19661, 19661, -11469, 11469, -3277, 3277};
/* (spread-centre) SPREAD: a sounding voice's place is its slot's (VS_POS) less the part's centre, its sounding voices'
 * places weighed by their level (squared: as they add up; a voice in its attack at the level it heads for, its velocity),
 * scaled so the outermost of the voices within 18 dB of the loudest sits at the side (quieter ones at most there): a
 * lone note in the middle (a release tail beside it barely counts), any set of voices balanced (two either side, three
 * or more and unison stacks symmetric), a chord as wide as any other. A voice glides to its place (SPOS_STEP a block,
 * ~23 ms side to middle: no jump when a voice starts or stops beside it); a new one (voice_start, from silence:
 * SPOS_NEW) starts at it. g0, g1: each voice's gain at the block's start and end (vrender). In the flash (track_render
 * calls it through HOT_FAR): core 0, before the voices are handed to core 1 */
static void spread_place(track_t *t, int32_t spr0, int32_t spr, int sided, int32_t *g0, int32_t *g1)
{
    uint32_t i;
    int32_t w[NVOICE], sw = 0, sp = 0, wmax = 0, mean, dev = 0, k;
    for (i = 0; i < NVOICE; i++) {
        const voice_t *v = &t->v[i];
        int32_t a = !v->active ? v->pend_vel * 258 : v->stage == 1u || v->spos == SPOS_NEW ? v->vel * 258 : v->env_out;
        a = (a > 32767 ? 32767 : a < 0 ? 0 : a) >> 7;   /* (8 bits, squared 16: the sums within 32 bits) */
        w[i] = v->active || (v->pend & VP_ON) ? a * a + 1 : 0;   /* (+1: a silent one still counts, a little) */
        sw += w[i], sp += (VS_POS[i % 16u] >> 5) * w[i];
        wmax = w[i] > wmax ? w[i] : wmax;
    }
    mean = sw ? (sp / sw) * 32 : 0;
    for (i = 0; i < NVOICE; i++)
        if (w[i] && w[i] >= wmax >> 6) {
            int32_t d = VS_POS[i % 16u] - mean;
            d = d < 0 ? -d : d;
            dev = d > dev ? d : dev;
        }
    k = dev >= 1024 ? (32767 << 15) / dev : 0;          /* (|place - mean| held to dev: the product within 32 bits;
                                                         * under 1024, one voice that counts, the centre's rounding: 0) */
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        int32_t to = (clamp(VS_POS[i % 16u] - mean, -dev, dev) * k) >> 15, at = v->spos;
        g0[i] = g1[i] = 0;
        if (!w[i])
            continue;
        if (at == SPOS_NEW || !v->active)               /* (new, or its note waiting: from silence) */
            at = to;
        v->spos = (int16_t)(at + clamp(to - at, -SPOS_STEP, SPOS_STEP));
        if (sided)
            g0[i] = (at * spr0 * 23) >> 12, g1[i] = (v->spos * spr * 23) >> 12;
    }
}
static void vrender(track_t *t, const engine_t *e, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m, int32_t g0,
                    int32_t g1, int32_t *side, int32_t *tmp)
{
    uint32_t i, f2 = t->f2v;
    int32_t *sd = f2 & (F2V_SIDE | F2V_ESIDE) ? f2_sd[C1_IX(tmp)] : 0;   /* (ESIDE: the engine adds its own) */
    if (!g0 && !g1 && !(f2 & (F2V_POST | F2V_SIDE | F2V_ESIDE))) {   /* (FILTER 2 SER / PAR in it: as without) */
        e->render(t, v, out, n, m);
        return;
    }
    if (sd)
        for (i = 0; i < n; i++)
            sd[i] = 0;
    if (!g0 && !g1 && !(f2 & F2V_POST)) {
        e->render(t, v, out, n, m);
    } else {
        for (i = 0; i < n; i++)
            tmp[i] = 0;
        e->render(t, v, tmp, n, m);
        if (f2 & F2V_POST)
            f2_post(t, v, m, tmp, n);
        if (!g0 && !g1)
            for (i = 0; i < n; i++)
                out[i] += tmp[i];
        else
            for (i = 0; i < n; i++) {
                int32_t g = g0 + (((g1 - g0) * (int32_t)i) >> CTL_LOG2);
                out[i] += tmp[i];
                side[i] += ((tmp[i] >> 4) * g) >> 11;  /* (>> 4 first: 32 bits) */
            }
    }
    if (sd && (f2 & F2V_OWN))                           /* (the engine's: before its level, as its sound) */
        for (i = 0; i < n; i++)
            side[i] += mulq15(mulq15(sd[i] << 1, amp_at(m, i)), VOICE_FS) << 1;
    else if (sd)
        for (i = 0; i < n; i++)
            side[i] += sd[i];
}
static struct { track_t *t; const engine_t *e; uint32_t n, a, b; } vsj;
static uint32_t vs_jobs, vs_split;                  /* (console, tests: blocks handed over, voices rendered there) */
/* (Stallion) the same per engine (console clk): blocks with two voices or more of an engine that may split (vs_ok), and
 * of those the blocks core 1 took a share of, and the voices it rendered */
static uint32_t vs_eblk[NENGINES], vs_esplit[NENGINES], vs_evoices[NENGINES];
static void vs_job(uint32_t arg)                    /* on core 1 */
{
    uint32_t i;
    (void)arg;
    for (i = 0; i < vsj.n; i++)
        vs_buf[i] = vs_side1[i] = 0;
    vs_side1_on = 0;
    for (i = vsj.a; i < vsj.b; i++) {
        vs_side1_on |= vs_g0[i] || vs_g1[i] || (vsj.t->f2v & (F2V_SIDE | F2V_ESIDE));
        vrender(vsj.t, vsj.e, vs_v[i], vs_buf, vsj.n, &vs_m[i], vs_g0[i], vs_g1[i], vs_side1, vs_tmp[1]);
    }
}
#ifndef VS_CHECK
#define VS_CHECK 0                                  /* 1: the bench build below (VS_CHECK=1 ./build.sh) */
#endif
#if VS_CHECK
/* (bench) core 1's voices of PONY and PALOMINO (their whole state is in the voice) rendered again on this core after
 * the wait, from the state they were handed over with, and compared: the output and the voice after it. Console:
 * vs_checks (blocks compared), vs_diffs (core 1's output not this core's), vs_sdiffs (its voices left otherwise). The
 * voices keep core 1's result: the sound is the split's */
static voice_t vsc_in[NVOICE], vsc_out[NVOICE];
static int32_t vsc_buf[CTL], vsc_side[CTL], vsc_tmp[CTL];
static uint32_t vs_checks, vs_diffs, vs_sdiffs;
static int vsc_on(const engine_t *e) { return e == &ENG_PONY || e == &ENG_PALOMINO; }
static void vs_check(track_t *t, const engine_t *e, uint32_t n, uint32_t h)
{
    uint32_t k, i, d = 0, sd = 0;
    for (i = 0; i < n; i++)
        vsc_buf[i] = vsc_side[i] = 0;
    for (k = h; k < vs_n; k++) {
        memcpy(&vsc_out[k], vs_v[k], sizeof(voice_t));
        memcpy(vs_v[k], &vsc_in[k], sizeof(voice_t));
        vrender(t, e, vs_v[k], vsc_buf, n, &vs_m[k], vs_g0[k], vs_g1[k], vsc_side, vsc_tmp);
        sd |= memcmp(vs_v[k], &vsc_out[k], sizeof(voice_t)) != 0;
        memcpy(vs_v[k], &vsc_out[k], sizeof(voice_t));
    }
    for (i = 0; i < n; i++)
        d |= vsc_buf[i] != vs_buf[i] || (vs_side1_on && vsc_side[i] != vs_side1[i]);
    vs_checks++;
    vs_diffs += d;
    vs_sdiffs += sd;
}
#endif
static void vs_finish(track_t *t, const engine_t *e, int32_t *out, uint32_t n)
{
    uint32_t k, i, h = vs_n, handed = 0;
    if (vs_n >= 2u)
        vs_eblk[t->engine % NENGINES]++;
    if (vs_n >= 2u && n <= CTL && cpu1_units != 0) {
        h = vs_n / 2u;                              /* core 1 the second half (the bigger one when odd: this core
                                                     * also works out the modulation, the mix and the effects) */
        vsj.t = t, vsj.e = e, vsj.n = n, vsj.a = h, vsj.b = vs_n;
#if VS_CHECK
        if (vsc_on(e))
            for (k = h; k < vs_n; k++)
                memcpy(&vsc_in[k], vs_v[k], sizeof(voice_t));
#endif
        handed = (uint32_t)cpu1_run(vs_job, 0);
        if (!handed)
            h = vs_n;
    }
    for (k = 0; k < h; k++)
        vrender(t, e, vs_v[k], out, n, &vs_m[k], vs_g0[k], vs_g1[k], part_side, vs_tmp[0]);
    if (handed) {
        if (cpu1_wait() < 0) {                      /* late: held from now on (cpu1.c); its voices here */
            for (k = h; k < vs_n; k++)
                vrender(t, e, vs_v[k], out, n, &vs_m[k], vs_g0[k], vs_g1[k], part_side, vs_tmp[0]);
        } else {
            for (i = 0; i < n; i++)
                out[i] += vs_buf[i];
            if (vs_side1_on)
                for (i = 0; i < n; i++)
                    part_side[i] += vs_side1[i];
            vs_jobs++;
            vs_split += vs_n - h;
            vs_esplit[t->engine % NENGINES]++;
            vs_evoices[t->engine % NENGINES] += vs_n - h;
#if VS_CHECK
            if (vsc_on(e))
                vs_check(t, e, n, h);
#endif
        }
    }
    vs_n = 0;
}
#endif

#if CLIPPER_SYNTH
/* (after Clipper 1.2) the level of a voice of UNISON (UNI2, UNI4) against POLY's, Q15, as mono_play and puni_on set it
 * through the velocity (about the same sum at random phases): for FMX, whose envelopes are the voice's level (no
 * velocity there: its unison came out +6 dB over POLY with the phases spread, +12 before) */
static inline __attribute__((always_inline)) int32_t uni_level(const track_t *t)   /* (Stallion: inlined, the render
                                                                                    * runs from RAM, out of a call's
                                                                                    * reach of the flash) */
{
    uint32_t vm = voice_mode(t);
    return vm == V_UNISON ? (HOT_FAR(trk_nvoice)(t) >= NVOICE ? 11796 : 16384) : vm == V_PUNI4 ? 16384 : vm == V_PUNI2 ? 22938 : 32767;
}
#endif
static uint32_t track_render(track_t *t, int32_t *out, uint32_t n)
{
    const engine_t *e = ENGINES[t->engine];
    const int16_t *p = t->p;
    uint32_t i;
    int32_t lfo = mulq15(t->lfo_val, t->lfo_fade);
    /* Clipper: the pitch bend (MIDI En), in 1/4096 semitone: the HYBRID patch's range, else +-2 semitones.
     * (Stallion) the sound's RANGE UP / DOWN (GLO: that one), the lever added, the sum limited to them; then the
     * ribbon (x RIB RANGE, beyond the limit). Both on the VCOs the BEND page has on, for the voices of BEND TARGET;
     * bvcf: VCF BEND's cutoff (the bend and the lever alone); bsum the matrix's BEND. All at rest: as before */
    uint32_t pc = CLIPPER_SYNTH ? ctl_part(t) : (uint32_t)(t - trk) % NPART;   /* (Clipper: the layer plays part 1's
                                                         * controls; in MULTI its own) */
    const part_ctl_t *pcx = &part_ctl[pc];
    int32_t bend = 0, bvcf = 0, bsum = 0;
    if ((uint32_t)(t - trk) < NPART) {
        int32_t glo = midi_bend ? (int32_t)midi_bend : e == &ENG_HYBRID ? (int32_t)hy_cur[t - trk].bend : 2;
#if CLIPPER_SYNTH
        int32_t up = p[P_BUP] >= 0 ? clamp(p[P_BUP], 0, 24) : glo, dn = p[P_BDN] >= 0 ? clamp(p[P_BDN], 0, 24) : glo;
        if (pcx->ml_on & ML_ON_BEND) {                  /* (a BEND lane playing, seq.c motion.c: the sum it recorded, over
                                                         * the range of its sign) */
            bsum = pcx->ml_bend;
            bend = clamp((bsum * (bsum < 0 ? dn : up)) >> 3, -dn * 4096, up * 4096);
        } else {
            bend = (pcx->bend * (pcx->bend < 0 ? dn : up)) >> 3;
            if (pcx->lever)
                bend = clamp(bend + ((pcx->lever * (pcx->lever < 0 ? dn : up)) >> 3), -dn * 4096, up * 4096);
            bsum = clamp(pcx->bend + pcx->lever, -32768, 32767);
        }
        bvcf = (bsum * clamp(p[P_BVCF], -63, 63)) >> 7;
        bend += (pc_rib(pcx) * (int32_t)touch_cfg.rib_range) >> 3;  /* GLO > TOUCH > RIB RANGE (a RIBBON lane: its) */
#else
        bend = (pcx->bend * glo) >> 3;
#endif
    }
#if CLIPPER_SYNTH
    /* Clipper 0.3: the mod wheel's and the aftertouch's destinations (P_MWDST, P_ATDST), velocity to the cutoff
     * (P_VELF); VIB and TREM ride a vibrato LFO of their own (~5.5 Hz sine), WAH the sound's LFO */
    static uint32_t vib_ph[NPART];
    int32_t vib, mw = part_ctl[pc].mw, cat = (int32_t)pc_at(pcx);   /* (Stallion: a PRESSURE lane playing: its) */
    if (e == &ENG_HYBRID)                               /* (HYBRID: its matrix takes MW and AT) */
        mw = cat = 0;
    vib_ph[(uint32_t)(t - trk) % NPART] += 535642u * n;
    vib = osc_sine(vib_ph[(uint32_t)(t - trk) % NPART]);
    /* Clipper 0.4: the matrix (mx_load); sv: its sources (the part's here, the voice's in the loop) */
    mx_t mx;
    int32_t sv[MS_NSRC], l2free, l3free, pan_new = 0, vint = clamp(t->p[P_VINT], 0, 127);
    uint32_t pan_age = 0, catr = pc_at(pcx), e2run, synced = 0;   /* synced: LFO 1 / LFO 2 FREE restarted (MX_TB_S*) */
    /* ARP, DLY, REV, CHO: the part's, from its newest voice's sums (as PAN) */
    const uint64_t mx_pp = 1ull << MT_ARP | 1ull << MT_DLY | 1ull << MT_REV | 1ull << MT_CHO;
    int32_t pp_new[4] = {0, 0, 0, 0};
    uint32_t pp_age = 0, pp_seen = 0;
    /* (Stallion) BEND TARGET LEAD (a note per key): the bend on the newest held note's voices alone */
    int lead = p[P_BTGT] == BT_LEAD && voice_poly((int)voice_mode(t)) && (bend || bvcf), lnote = -1;
    int b1 = !e->vco2 || p[P_BVCO1], b2 = !e->vco2 || p[P_BVCO2];   /* (the BEND page's VCOs: two-VCO engines) */
    /* (1.0) the macros: BRITE (the cutoff, +-) and TIMBR (the shape, +-), MOTN (a vibrato and LFO 3 on the cutoff),
     * SPACE (fx.c: the reverb and delay sends, and SPREAD); SPREAD: the voices' gains from the last block's to this
     * one's */
    int32_t mac1 = clamp(t->p[P_MAC1], -64, 63), mac2 = clamp(t->p[P_MAC2], -64, 63), mac3 = clamp(t->p[P_MAC3], 0, 127);
    int32_t spr = clamp(t->p[P_SPREAD] + clamp(t->p[P_MAC4], 0, 127) / 2, 0, 127), spr0 = t->spr;
    int sided;
    int32_t vg0[NVOICE], vg1[NVOICE];                   /* (spread-centre) the voices' gains: spread_place */
    {   /* FILTER 2 this block: in the engine (beside its MODEL filter) or after it; SPLIT: the voices' side */
        int32_t m2 = fm_model2(t);
        t->f2v = (uint8_t)(!m2 ? 0u : (f2_own(e) ? F2V_OWN : F2V_POST) | (fm_route(t) == F2_SPLIT ? F2V_SIDE : 0u));
        if (e == &ENG_MARE && (uint32_t)(t - trk) < NPART)   /* (MARE: its stereo filters, a side of its own) */
            t->f2v |= F2V_ESIDE;
    }
    sided = (uint32_t)(t - trk) < NPART && (spr || spr0 || (t->f2v & (F2V_SIDE | F2V_ESIDE)));
#endif
    /* TUNE in cents: whole 1/16 semitones in the pitch, the rest as a fine factor (no dead zone) */
    int32_t tune = song.g[G_TUNE] >= 0 ? song.g[G_TUNE] * 16 / 100 : -((-song.g[G_TUNE] * 16 + 99) / 100);
    int32_t tune_fine = (song.g[G_TUNE] * 16 - tune * 100) * 2367 / 16000;   /* rest, in 1/4096 (1 ct = 2.367) */
    uint32_t nr = 0, fade = t->xf_on && t->xf;
    int16_t pe_new[8];
#if CLIPPER_SYNTH
    int split = vs_ok(e) && cpu1_units != 0 && cpu1_on();   /* (0.7) the voices on both cores */
    vs_n = 0;
    part_side_on = (uint8_t)sided;
    if (sided)
        for (i = 0; i < n; i++)
            part_side[i] = 0;
    t->spr = (int16_t)spr;
    HOT_FAR(spread_place)(t, spr0, spr, sided, vg0, vg1);   /* (in the flash: this runs from RAM, hal/fm1_hot.h) */
#endif
    for (i = 0; i < n; i++)
        out[i] = 0;
    if (fade)                                           /* engine switch: the old engine, its own values */
        for (i = 0; i < 8u; i++) {
            pe_new[i] = t->p[P_E0 + i];
            t->p[P_E0 + i] = t->pe_old[i];
        }
    track_lfo_tick(t);
#if CLIPPER_SYNTH
    mx_load(p, &mx, HOT_FAR(mx_dst_live_e)(e));       /* (Stallion: HOT_FAR, hal/fm1_hot.h: this runs from RAM,
                                                         * mx_dst_live_e in the flash) */
    e2run = (mx.srcs & (1ull << MS_E2END)) || (mx.dsts & (1ull << MT_TRIG2));   /* ENV 2 ticked for its
                                                         * END, its TRIG2 too */
    for (i = 0; i < MS_NSRC; i++)
        sv[i] = 0;
    if (e == &ENG_PONY)                                 /* (Stallion) PONY's CHAR: LFO, TICK, LEVER (eng_pony.c) */
        lfo = HOT_FAR(pony_lfo)(t, lfo, &vib, n);
    sv[MS_LFO1] = lfo;                                  /* (the part's sources; with FADE, as the LFO's routes) */
    sv[MS_MW] = part_ctl[pc].mw * 258;                  /* (on HYBRID too: its own matrix has them as well) */
    sv[MS_AT] = (int32_t)catr * 258;
    sv[MS_BEND] = bsum;                                 /* (Stallion) the bend and the lever, the part's pressure, */
    sv[MS_CAT] = (int32_t)catr * 258;                   /* .. the ribbon */
    sv[MS_RIB] = pc_rib(pcx);
    sv[MS_DC] = 32767;                                  /* +V: an offset */
    if ((uint32_t)(t - trk) == pc) {                    /* CC A, CC B: smoothed once a block, by the part's */
        part_ctl_t *c = &part_ctl[pc];                  /* own track (95 % of a step in 8 blocks, ~6 ms) */
        int32_t da = c->cca * 258 - c->cca_s, db = c->ccb * 258 - c->ccb_s;
        c->cca_s = da > -4 && da < 4 ? c->cca * 258 : c->cca_s + ((da * 5) >> 4);
        c->ccb_s = db > -4 && db < 4 ? c->ccb * 258 : c->ccb_s + ((db * 5) >> 4);
    }
    sv[MS_CCA] = part_ctl[pc].cca_s;
    sv[MS_CCB] = part_ctl[pc].ccb_s;
    if (lead) {
        uint32_t ag = 0;
        for (i = 0; i < NVOICE; i++)
            if (t->v[i].active && t->v[i].gate && t->v[i].age >= ag)
                ag = t->v[i].age, lnote = t->v[i].note;
    }
    for (i = 0; i < MSEQ_LANES; i++)                    /* (0.8) the MOD SEQ's lanes: the part's */
        sv[MS_SEQ1 + i] = t->ms_cur[i];
    {   /* LFO 2 FREE: one for the part, its rate moved by the part's sources only */
        uint32_t old = t->l2ph;
        t->l2ph += LFO_INC[mx_l2rate(p, &mx, sv)];
        if (p[P_L2WAVE] == LW_CHAO)
            lfo_chao(&t->l2rnd, &t->l2cx, t->l2ph, old);
        else if (t->l2ph < old)
            t->l2rnd = (int16_t)(rng() >> 16);
        l2free = lfo_wave_n(p[P_L2WAVE], t->l2ph, t->l2rnd, t->l2cx);
    }
    {   /* (1.0) LFO 3 FREE, and the macros as sources */
        uint32_t old = t->l3ph;
        t->l3ph += LFO_INC[clamp(p[P_L3RATE], 0, 127)];
        if (p[P_L3WAVE] == LW_CHAO)
            lfo_chao(&t->l3rnd, &t->l3cx, t->l3ph, old);
        else if (t->l3ph < old)
            t->l3rnd = (int16_t)(rng() >> 16);
        l3free = lfo_wave_n(p[P_L3WAVE], t->l3ph, t->l3rnd, t->l3cx);
        sv[MS_LFO3] = l3free;
        sv[MS_MAC1] = mac1 * 512, sv[MS_MAC2] = mac2 * 512;
        sv[MS_MAC3] = mac3 * 258, sv[MS_MAC4] = clamp(p[P_MAC4], 0, 127) * 258;
    }
    sv[MS_LFO2] = p[P_L2MODE] == L2_KEY ? 0 : l2free;   /* LFO1: the next block's (the voices set LFO 2) */
    t->l1rmod = (int8_t)mx_l1rate(p, &mx, sv);
#endif
    if (e->block)                                       /* the engine's per-part work (DRAWBAR: bars, rotor) */
        e->block(t);
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        vmod_t m;
        int32_t env, pitch, e1a = 32768;                /* e1a: ENV 1's amount (matrix E1AMT, e1_scale) */
#if CLIPPER_SYNTH
        int32_t mpit = 0, mcut = 0, mshp = 0, mamp = 0, mer = 0, med[8], mcut2 = 0, mres2 = 0, mp1 = 0, mp2 = 0;
        int32_t mcutb = 0, cutb_on = 0;                 /* MARE's CUTOFF 2 */
        int32_t mffm = 0, mring = 0;                    /* MARE's FILTER FM and RING */
        uint32_t medm = 0;                              /* bit per EDIT value the matrix moves */
        int mpe;
#endif
        if ((v->pend & VP_ON) && !v->active && !fade) {   /* (SLOOP 2.8.3) faded out: the sample's note, from silence */
            uint32_t pf = v->pend, md = voice_mode(t);
            v->pend = 0;
            if ((voice_poly((int)md) || i < (md == V_UNISON ? HOT_FAR(uni_nvoice)(t) : 1u)) &&   /* (VOICE changed
                                                         * meanwhile: not on a voice the mode does not play) */
                HOT_FAR(voice_room)(t, 0)) {            /* (counted while it waited, voices_busy: no second voice; Stallion:
                                                         * HOT_FAR, hal/fm1_hot.h: this runs from RAM, these in the flash) */
                HOT_FAR(voice_start)(t, v, v->pend_note, v->pend_vel, (pf & VP_GLIDE) != 0);
                if (pf & VP_UNI) {                      /* (after Clipper 1.2) UNISON: not in phase with the others */
                    pend_seed = pend_seed * 1664525u + 1013904223u;
                    HOT_FAR(unison_spread)(t, v, pend_seed, 0, 0, 0);
                }
                if (pf & VP_OFF) {                      /* let go within the fade (~1.5 ms): released as it starts, */
                    if (voice_poly((int)voice_mode(t)) &&     /* or held by the sustain pedal */
                        part_ctl[CLIPPER_SYNTH ? ctl_part(t) : (uint32_t)(t - trk) % NPART].sus)
                        v->sus = 1;
                    else {
                        v->gate = 0;
                        v->stage = 3;
                    }
                }
            }
        }
        if (!v->active)
            continue;
#if CLIPPER_SYNTH
        mpe = mpe_mode && v->ch;                        /* MPE: the note's own channel */
        if (mpe)
            v->pat = mpe_press[v->ch & 15u];            /* (its pressure: the voice's aftertouch) */
#endif
        {
            env = env_tick(t, v);
            if (e->ownenv && v->active) {               /* the engine's envelopes (FM6): they end the voice */
                if (e->done(t, v)) {
                    v->active = v->gate = 0;
                    v->stage = 0;
                    v->kill = 0;
                    v->env = v->env_out = 0;
                    continue;
                }
                if (v->stage != 4u) {                   /* (the ADSR's release never ends it; a voice given up for
                                                         * another part still fades out: env_tick stage 4) */
                    v->env = 1 << 24;
                    env = 32767;
                }
            }
#if CLIPPER_SYNTH
            if (mx.n) {                                 /* the matrix: the voice's sources, then every slot */
                uint32_t k;
                int32_t pan = 0, mpp[4] = {0, 0, 0, 0}, me1 = 0;   /* ARP, DLY, REV, CHO; E1AMT */
                sv[MS_ENV1] = env;
                int32_t trig[4] = {0, 0, 0, 0};         /* the sums into TRIG1, TRIG2, SYNC1, SYNC2 */
                sv[MS_ENV2] = mx.srcs & (1ull << MS_ENV2) || e2run ?
                              env2_tick(p, v, mx.dsts & (1ull << MT_TRIG2) ?
                                              MX_E2_PATCHED | (v->trg & MX_TB_T2 ? MX_E2_HIGH : 0u) : 0u) : 0;
                sv[MS_GATE] = v->gate ? 32767 : 0;      /* the key held (or the pedal holding it) */
                sv[MS_E2END] = v->e2st == 0u || (v->e2st == 2u && p[P_E2SUS] <= 0 && v->env2 < (1 << 12)) ? 32767 : 0;
                                                        /* .. ENV 2 back at 0: released, or a decay to SUS 0 run out */
                sv[MS_VEL] = v->vel * 258;
                sv[MS_KEY] = clamp(((int32_t)v->note - 60) * 512, -32768, 32767);
                sv[MS_AT] = (int32_t)(v->pat > catr ? v->pat : catr) * 258;
                sv[MS_PAT] = (int32_t)v->pat * 258;     /* (Stallion) the voice's own pressure */
                sv[MS_RAND] = v->rnd;
                sv[MS_SLIDE] = mpe ? mpe_slide[v->ch & 15u] * 512 : 0;
                if (p[P_L2MODE] == L2_KEY) {            /* LFO 2 KEY: the note's own, from its start */
                    uint32_t old = v->l2ph;
                    sv[MS_LFO2] = 0;
                    v->l2ph += LFO_INC[mx_l2rate(p, &mx, sv)];
                    if (p[P_L2WAVE] == LW_CHAO)
                        lfo_chao(&v->l2rnd, &v->l2cx, v->l2ph, old);
                    else if (v->l2ph < old)
                        v->l2rnd = (int16_t)(rng() >> 16);
                    sv[MS_LFO2] = lfo_wave_n(p[P_L2WAVE], v->l2ph, v->l2rnd, v->l2cx);
                } else {
                    sv[MS_LFO2] = l2free;
                }
                sv[MS_SH] = p[P_L2MODE] == L2_KEY ? v->l2rnd : t->l2rnd;   /* S&H: LFO 2's held value */
                sv[MS_NOISE] = mx.srcs & (1ull << MS_NOISE) ? (int16_t)(rng() >> 16) : 0;
                if (p[P_L3MODE] == L2_KEY && (mx.srcs & (1ull << MS_LFO3))) {   /* (1.0) LFO 3 KEY: the note's own */
                    uint32_t old = v->l3ph;
                    v->l3ph += LFO_INC[clamp(p[P_L3RATE], 0, 127)];
                    if (p[P_L3WAVE] == LW_CHAO)
                        lfo_chao(&v->l3rnd, &v->l3cx, v->l3ph, old);
                    else if (v->l3ph < old)
                        v->l3rnd = (int16_t)(rng() >> 16);
                    sv[MS_LFO3] = lfo_wave_n(p[P_L3WAVE], v->l3ph, v->l3rnd, v->l3cx);
                } else {
                    sv[MS_LFO3] = l3free;
                }
                for (k = 0; k < mx.n; k++) {
                    int32_t x = sv[mx.src[k]] * mx.amt[k] >> 6;   /* Q15 x AMT / 64 */
                    uint32_t d = mx.dst[k];
                    if (d == MT_PITCH)                  /* +-24 semitones, AMT squared: fine near 0 */
                        mpit += (x * (mx.amt[k] < 0 ? -mx.amt[k] : mx.amt[k]) * 24) >> 9;
                    else if (d == MT_PIT1 || d == MT_PIT2) {   /* (Stallion) a VCO's own, as PITCH (one VCO: PITCH) */
                        int32_t y = (x * (mx.amt[k] < 0 ? -mx.amt[k] : mx.amt[k]) * 24) >> 9;
                        if (!e->vco2)
                            mpit += y;
                        else if (d == MT_PIT1) {
                            mp1 += y;
                            if (e == &ENG_MARE && !(mx.dsts & (1ull << MT_PIT2)))
                                mp2 += y;               /* MARE: PIT1 normalled into PIT2 until patched */
                        } else
                            mp2 += y;
                    }
                    else if (d == MT_CUT)
                        mcut += x >> 1;
                    else if (d == MT_CUTB)              /* MARE's filter 2 alone (else CUT) */
                        mcutb += x >> 1, cutb_on = 1;
                    else if (d == MT_FFM)               /* MARE's FILTER FM, RING (else nothing) */
                        mffm += x;
                    else if (d == MT_RING)
                        mring += x;
                    else if (d == MT_SHAPE)
                        mshp += x >> 1;
                    else if (d == MT_AMP)
                        mamp += x;
                    else if (d == MT_PAN)
                        pan += x;
                    else if (d == MT_ERATE)
                        mer += x;
                    else if (d == MT_CUT2)              /* FILTER 2's: its cutoff as MT_CUT, its RES (+-127) */
                        mcut2 += x >> 1;
                    else if (d == MT_RES2)
                        mres2 += x;
                    else if (d >= MT_TRIG1 && d <= MT_SYNC2)
                        trig[d - MT_TRIG1] += x;
                    else if (d >= MT_ARP && d <= MT_CHO)   /* the part's ARP, sends: the newest voice's */
                        mpp[d - MT_ARP] += x;
                    else if (d == MT_E1AMT)             /* .. ENV 1's amount: x (1 + sum), 0 .. x2 */
                        me1 += x;
                    else if (d >= MT_E1) {              /* (Stallion: XMOD, HPF the engine's EDIT value, if it has one) */
                        int32_t ek = d < MT_E1 + 8u ? (int32_t)(d - MT_E1) : d == MT_XMOD ? e->xmod - 1 :
                                     d == MT_HPF ? e->hpf - 1 : -1;
                        if (ek < 0 || ek > 7)
                            continue;
                        if (!(medm & (1u << ek)))
                            med[ek] = 0;
                        medm |= 1u << ek;
                        med[ek] += x;
                    }
                }
                if ((mx.dsts & (1ull << MT_PAN)) && v->age >= pan_age) {   /* the pan: the newest note's */
                    pan_age = v->age;
                    pan_new = pan;
                }
                if ((mx.dsts & mx_pp) && (!pp_seen || v->age >= pp_age)) {   /* ARP, sends: the same */
                    pp_age = v->age, pp_seen = 1;
                    for (k = 0; k < 4u; k++)
                        pp_new[k] = mpp[k];
                }
                if (mx.dsts & (1ull << MT_E1AMT))
                    e1a = clamp(32768 + me1, 0, 65536);
                if (mx.dsts & MX_TRIG_DSTS) {           /* TRIG / SYNC: the edges, for the next block */
                    uint32_t hi = 0, edge;
                    for (k = 0; k < 4u; k++)
                        hi |= (uint32_t)(trig[k] > MX_TRIG_LEVEL) << k;
                    edge = hi & ~(uint32_t)v->trg;
                    v->trg = (uint8_t)hi;
                    if ((edge & MX_TB_T1) && (v->stage == 1u || v->stage == 2u) && !e->ownenv)
                        v->stage = 1;                   /* ENV 1: the attack again, from its level (never the release) */
                    if ((edge & MX_TB_T2) && v->stage < 3u)
                        v->e2st = 1;                    /* ENV 2: from its level, not once the note is let go */
                    if ((edge & MX_TB_S1) && !(synced & MX_TB_S1)) {
                        synced |= MX_TB_S1;             /* LFO 1 (the part's): the first voice's edge in the block */
                        t->lfo_ph = (uint32_t)p[P_LPHASE] << 25;
                    }
                    if (edge & MX_TB_S2) {
                        if (p[P_L2MODE] == L2_KEY)
                            v->l2ph = 0;                /* LFO 2 KEY: the voice's own */
                        else if (!(synced & MX_TB_S2)) {
                            synced |= MX_TB_S2;         /* .. FREE: the part's, as LFO 1 */
                            t->l2ph = 0;
                            if (p[P_L2WAVE] != LW_CHAO) /* (release-1.0) a restart samples: the S&H a new value, as a */
                                t->l2rnd = (int16_t)(rng() >> 16);   /* wrap does (GATE -> SYNC2 under S&H: notes
                                                         * shorter than a cycle all held one value, MARE STEREO VAR) */
                        }
                    }
                }
            } else if (mx.srcs & (1ull << MS_ENV2) || e2run) {
                env2_tick(p, v, 0u);
            }
            v->erate = (int16_t)clamp(mer, -32767, 32767);   /* (after 1.0) ERATE: the envelopes' next ticks */
#endif
            if (e->amp)                                 /* the engine's own amplitude curve */
                env = e->amp(t, v, env);
            m.envq15 = e->ownenv ? 0 : env;
            m.amp1 = e->ownenv ? env : mulq15(env, v->vel * 258);
#if CLIPPER_SYNTH
            if (e == &ENG_PONY && (p[P_JVCA] || (p[P_JCHR] & JQ_VCA)))   /* (Stallion) PONY's VCA GATE: the amp
                                                         * follows the key, ENV 1 the filter alone; CHAR's VCA curve
                                                         * (eng_pony.c pony_vamp) */
                m.amp1 = HOT_FAR(pony_vamp)(t, v, env);
#endif
            m.vel = v->vel;
#if CLIPPER_SYNTH
            if (e == &ENG_FMX && t->p[P_VOICE] >= V_UNISON)   /* (after Clipper 1.2) FMX: UNISON's level per voice */
                m.amp1 = mulq15(m.amp1, uni_level(t));
            if (t->p[P_VOICE] >= V_UNISON) {           /* (Stallion) the velocity as played, the level's share undone */
                int32_t ul = uni_level(t);
                m.vel = ul < 32767 ? (int32_t)v->vel * 32767 / ul : v->vel;
                m.vel = m.vel > 127 ? 127 : m.vel;
            }
#endif
            if (p[P_LD_AMP])
                m.amp1 = mulq15(m.amp1, 32767 - mulq15((lfo + 32768) >> 1, p[P_LD_AMP] * 258));
#if CLIPPER_SYNTH
            {   /* TREM: up to 60 % of the level away, at the vibrato's rate */
                int32_t tr = (p[P_MWDST] == MD_TREM ? mw : 0) + (p[P_ATDST] == MD_TREM ? (e == &ENG_HYBRID ? 0 : v->pat > cat ? v->pat : cat) : 0);
                if (tr)
                    m.amp1 = mulq15(m.amp1, 32767 - mulq15((vib + 32768) >> 1, (tr > 127 ? 127 : tr) * 155));
            }
            if (mamp)                                   /* the matrix's AMP: 0 .. x1.5 */
                m.amp1 = m.amp1 * clamp(32768 + mamp, 0, 49151) >> 15;
#endif
            if (fade)                                   /* linear to 0 over the fade */
                m.amp1 = m.amp1 * (int32_t)(t->xf - 1u) / (int32_t)XF_BLOCKS;
            m.amp0 = v->env_out;
            v->env_out = m.amp1;
        }
        if (v->pitch_cur != v->pitch16 || v->pitch_frac) {   /* glide, in 1/4096 semitone */
            int32_t q = (v->pitch_cur << 8) + v->pitch_frac, d = (v->pitch16 << 8) - q;
            int32_t st = v->gstep ? v->gstep : (int32_t)((ENV_LIN[p[P_GLIDE] & 127] * 3u) >> 10);   /* RATE: an octave in GLIDE */
            if (st < 1)
                st = 1;
            q += d > 0 ? (d < st ? d : st) : (-d < st ? d : -st);
            v->pitch_cur = q >> 8;
            v->pitch_frac = (uint8_t)(q & 255);
        }
        if (v->penv)                                    /* the pitch envelope: ~40 ms */
            v->penv = (int16_t)(v->penv - ((v->penv * 590 + 32767) >> 15));
        if (!env && !m.amp0 && v->stage == 2 && !eng_sampled(e))
            continue;                                   /* held at a silent sustain (SUS 0): nothing to render */
        {   /* the pitch in 1/4096 semitone: glide, LFO, the pitch envelope; the fraction goes into the increment */
            int32_t pm = ((lfo * p[P_LD_PIT] * 3) >> 7) + e1_scale((v->penv * p[P_ED_PIT] * 3) >> 7, e1a);
            int32_t vb = bend, q;
            m.vo1 = m.vo2 = 0;
#if CLIPPER_SYNTH
            if (lead && !(v->gate && (int)v->note == lnote))   /* (Stallion) LEAD: another note's voice, unbent */
                vb = 0;
            if (vb && !(b1 && b2)) {                    /* .. a VCO off: VCO 1 rides the pitch, VCO 2 its difference */
                m.vo2 = vb * (b2 - b1);
                vb = b1 ? vb : 0;
            }
            m.vo1 = clamp(mp1, -24 * 4096, 24 * 4096);  /* .. the matrix's PIT1, PIT2 (two VCOs) */
            m.vo2 += clamp(mp2, -24 * 4096, 24 * 4096);
#endif
            q = (v->pitch_cur << 8) + v->pitch_frac + pm + vb;
            m.pmod = pm;                                /* (the LFO's and ENV's part: PALOMINO's P>VCO) */
#if CLIPPER_SYNTH
            {   /* VIB: up to +-half a semitone */
                int32_t vd = (p[P_MWDST] == MD_VIB ? mw : 0) + (p[P_ATDST] == MD_VIB ? (e == &ENG_HYBRID ? 0 : v->pat > cat ? v->pat : cat) : 0);
                if (e == &ENG_PONY && p[P_MWDST] == MD_VIB) {   /* (Stallion) PONY's wheel: the instrument's lever, its
                                                         * square up to +-4 semitones (64: +-1, 32: +-25 cents) */
                    int32_t w = mw > 127 ? 127 : mw;
                    vd -= mw;
                    q += (vib * w * w) >> 15;
                }
                if (vd)
#if BOARD_SMK37
                    q += (vib * (vd > 127 ? 127 : vd)) >> 10;   /* (the SMK-37's wheels: up to +-1 semitone) */
#else
                    q += (vib * (vd > 127 ? 127 : vd)) >> 11;
#endif
                q += clamp(mpit, -24 * 4096, 24 * 4096);   /* the matrix */
                if (mac3)                               /* (1.0) MOTN: a vibrato up to +-35 cents */
                    q += (vib * mac3) >> 12;
                if (mpe)                                /* MPE: the note's own bend */
                    q += (mpe_bend[v->ch & 15u] * (int32_t)MPE_RANGE[mpe_mode % MPE_MODES]) >> 3;
                if (tun_on)                             /* (0.9) the TUNING: the note's own pitch (glides too) */
                    q += tun_at(v->pitch_cur);
                if (vint)                               /* (0.9) VINTAGE: the voice's own tuning and drift */
                    q += vintage_pitch(v, (uint32_t)(t - trk) * NVOICE + i, vint);
            }
#endif
            pitch = (q >> 8) + tune;
            m.pitch16 = clamp(pitch, 0, 2047);
            m.inc = pitch_inc(m.pitch16);
            q = (q & 255) * 3792 >> 16;                 /* the fraction, as fine (1/16 st = 14.8) */
            m.fine = v->fine + tune_fine + q;
            if (m.fine)                                 /* unison detune, fine tune and the fraction */
                m.inc += (uint32_t)((int32_t)(m.inc >> 12) * m.fine);
        }
        m.cutoff = ((lfo * p[P_LD_FLT]) >> 7) + e1_scale((PONY_FENV(e, m.envq15) * p[P_ED_FLT]) >> 7, e1a);
        m.cut2 = m.res2 = 0;
        m.cutb = m.cutb_on = 0;
        m.ffm = m.ring = 0;
        m.ev = 0;                                       /* (the part's EDIT values) */
        if (v->vel > 110)                               /* accent opens the filter with the env */
            m.cutoff += (m.envq15 * 24) >> 7;
        m.shape = (64 << 8) + ((PONY_LFO1(e, lfo, t->lfo_fade) * p[P_LD_SHP]) >> 7) +
                  e1_scale((m.envq15 * p[P_ED_SHP]) >> 7, e1a);
#if CLIPPER_SYNTH
        {   /* VEL>F: around velocity 96, up to ~2 octaves up (127) and ~4.5 down (32); CUT: up to ~4 octaves,
             * WAH: the sound's LFO on the cutoff, up to ~+-2 octaves; SHP: the shape up to its top */
            int32_t a = e == &ENG_HYBRID ? 0 : v->pat > cat ? v->pat : cat;
            if (p[P_VELF])
                m.cutoff += ((int32_t)v->vel - 96) * p[P_VELF] * 4;
            if (p[P_MWDST] == MD_CUT)
                m.cutoff += mw * 120;
            if (p[P_ATDST] == MD_CUT)
#if BOARD_SMK37
                m.cutoff += a * 120 - (v->squish && e != &ENG_HYBRID ? SMK_SQUISH_DARK : 0);   /* (0 since the squeeze) */
#else
                m.cutoff += a * 120;
#endif
            if (p[P_MWDST] == MD_WAH)
                m.cutoff += (lfo * mw) >> 9;
            if (p[P_ATDST] == MD_WAH)
                m.cutoff += (lfo * a) >> 9;
            if (p[P_MWDST] == MD_SHP)
                m.shape += mw * 128;
            if (p[P_ATDST] == MD_SHP)
                m.shape += a * 128;
            if (!cutb_on || e != &ENG_MARE)           /* CUTOFF 2 is CUT (MARE: until it is patched) */
                mcut += mcutb, cutb_on = 0;
            m.cutoff += clamp(mcut, -32767, 32767);    /* the matrix */
            if (bvcf && !(lead && !(v->gate && (int)v->note == lnote)))
                m.cutoff += bvcf;                       /* (Stallion) VCF BEND (LEAD: the newest note's alone) */
            m.cutoff += mac1 * 160;                     /* (1.0) BRITE: +-40 steps (about +-2.5 octaves) */
            if (mac3)                                   /* .. MOTN: LFO 3 on the cutoff, up to +-24 steps */
                m.cutoff += (l3free * mac3) >> 10;
            m.shape = clamp(m.shape + mac2 * 128, 0, 127 << 8);   /* .. TIMBR: +-32 steps of the shape */
            if (vint)                                   /* (0.9) VINTAGE: each voice's filter a little off */
                m.cutoff += ((vcard((uint32_t)(t - trk) * NVOICE + i, 1u) * 400) >> 15) * vint >> 7;
            if (mshp)
                m.shape = clamp(m.shape + clamp(mshp, -32767, 32767), 0, 127 << 8);
            if (mpe && !(mx.srcs & (1ull << MS_SLIDE)))  /* MPE: SLIDE to the cutoff when no live slot takes it */
                m.cutoff += mpe_slide[v->ch & 15u] * 200;   /* (+-64: about +-3 octaves) */
            if (cutb_on)                                /* MARE, CUTOFF 2 patched: filter 2 everything */
                m.cutb = m.cutoff - clamp(mcut, -32767, 32767) + clamp(mcutb, -32767, 32767), m.cutb_on = 1;   /* but CUTOFF 1's cables */
            if (e == &ENG_MARE)                        /* FILTER FM, RING: MARE's alone */
                m.ffm = clamp(mffm, -32767, 32767), m.ring = clamp(mring, -32767, 32767);
            m.cut2 = clamp(mcut2, -32767, 32767);       /* FILTER 2's own (engines.c f2_setup) */
            m.res2 = clamp(mres2, -32767, 32767) >> 8;
        }
        if (medm && split) {                            /* (0.7) the EDIT values the matrix moves: the voice's own, on */
            uint32_t k;                                 /* either core (the engines of vs_ok read them through vm_ev) */
            for (k = 0; k < 8u; k++) {
                const param_desc_t *d = &e->edit[k];
                vs_e[vs_n][k] = t->p[P_E0 + k];
                if ((medm >> k) & 1u && d->fmt != F_ENUM && d->max > d->min)
                    vs_e[vs_n][k] = (int16_t)clamp(t->p[P_E0 + k] + ((clamp(med[k], -32767, 32767) * (d->max - d->min)) >> 15),
                                                   d->min, d->max);
            }
            m.ev = vs_e[vs_n];
        } else if (medm) {                              /* the EDIT values the matrix moves: this voice's own (one core:
                                                         * in the part's for its call, every engine reads them there) */
            int16_t keep[8];
            uint32_t k;
            for (k = 0; k < 8u; k++) {
                const param_desc_t *d = &e->edit[k];
                keep[k] = t->p[P_E0 + k];
                if ((medm >> k) & 1u && d->fmt != F_ENUM && d->max > d->min)
                    t->p[P_E0 + k] = (int16_t)clamp(keep[k] + ((clamp(med[k], -32767, 32767) * (d->max - d->min)) >> 15),
                                                     d->min, d->max);
            }
            vrender(t, e, v, out, n, &m, vg0[i], vg1[i], part_side, vs_tmp[0]);
            for (k = 0; k < 8u; k++)
                t->p[P_E0 + k] = keep[k];
            nr++;
            continue;
        }
        if (split) {                                    /* (0.7) rendered after the loop, on both cores */
            vs_v[vs_n] = v;
            copy32(&vs_m[vs_n], &m, sizeof m);          /* (= m: Stallion, dsp.c copy32) */
            vs_g0[vs_n] = vg0[i];
            vs_g1[vs_n] = vg1[i];
            vs_n++;
            nr++;
            continue;
        }
        vrender(t, e, v, out, n, &m, vg0[i], vg1[i], part_side, vs_tmp[0]);
        nr++;
        continue;
#endif
        e->render(t, v, out, n, &m);
        nr++;
    }
#if CLIPPER_SYNTH
    if (split)
        vs_finish(t, e, out, n);
#endif
#if CLIPPER_SYNTH
    {   /* the matrix's PAN (fx.c mix_part): to the newest note's, 4 steps a block at most (no click) */
        int32_t tgt = (mx.dsts & (1ull << MT_PAN)) ? clamp(pan_new >> 9, -64, 64) : 0;
        if (pan_age || !(mx.dsts & (1ull << MT_PAN)))
            t->panmod = (int16_t)(t->panmod + clamp(tgt - t->panmod, -4, 4));
    }
    if (pp_seen || !(mx.dsts & mx_pp)) {                /* ARP, DLY, REV, CHO (none sounding: held) */
        t->arpmod = (int8_t)clamp(pp_new[0] * 6 / 32768, -5, 5);
        t->dlymod = (int8_t)clamp(pp_new[1] >> 8, -127, 127);
        t->revmod = (int8_t)clamp(pp_new[2] >> 8, -127, 127);
        t->chomod = (int8_t)clamp(pp_new[3] >> 8, -127, 127);
    }
#endif
    if (fade) {
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = pe_new[i];
        t->xf--;
    }
#if CLIPPER_SYNTH
    if (nr && (uint32_t)(t - trk) < NPART) {            /* (load-steal) the block's predicted voice load (vc_half) */
        uint8_t ser;
        uint16_t pk;
        uint32_t c = HOT_FAR(vc_voice)(t, &ser, &pk);   /* (Stallion: HOT_FAR, hal/fm1_hot.h: this runs from RAM,
                                                         * vc_voice in the flash) */
        vc_acc += vc_pl(c, ser, pk, nr);
    }
#endif
    return nr;
}
