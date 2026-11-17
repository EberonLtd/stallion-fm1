/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (Clipper) */
/* Clipper 0.3: the synth's own pages (scope SC_SYN, params.c): the LAYER, the A/B MORPH and the RANDOMIZER.
 *   LAYER    MODE (OFF, LAYER, SPLIT, MULTI), SOUND (any factory or user sound for track 2), SPLIT (track 1 from that
 *            note up, track 2 below; MULTI: CH, the MIDI channel track 2 answers on its own, every other channel and
 *            the keys playing track 1: bi-timbral, 0.6), LEVEL; LAYER 2: OCT, PAN, FILT (track 2's), SWAP (the two sounds trade places: the
 *            way to edit the layer's sound with every page, then SWAP back). The routing: voice.c trk_note_on.
 *   MORPH    SET A, SET B: the sound now as A, as B; MORPH 0..127 between them (every continuous value on the line
 *            between, the switches at half way; A and B on two engines: the engine changes at half way). MIDI CC 20.
 *   RANDOM   AMT (how far from the sound now), WHAT (EDIT: the engine's 8 values; +ENV: and the envelopes; ALL: and
 *            the LFO, its destinations and the sends), RAND, BACK (the sound before the last RAND).
 * Track 2's settings are its own parameters (core.h P_LYMODE ..); the others live here (not in the project). */
#ifndef UI_SMALL
#define UI_SMALL                                   /* (gfx.c: minsize on the device; a test alone: nothing) */
#endif
enum { X_LYMODE, X_LYSOUND, X_LYSPLIT, X_LYLEVEL, X_LYOCT, X_LYPAN, X_LYFILT, X_LYSWAP,
       X_MORPH, X_SETA, X_SETB, X_RNDAMT, X_RNDWHAT, X_RNDGO, X_RNDBACK,
       X_MSLOT, X_MSRC, X_MDST, X_MAMT,                /* (X_MSRC..X_MAMT: the slot's P_MnSRC..P_MnAMT) */
       /* 0.6: FMX (eng_fmx.c): the selected operator's values, the routing, the patch, STORE (the part's patch) */
       X_FOP, X_FWAVE, X_FMODE, X_FRATIO, X_FFINE, X_FLEVEL, X_FVEL, X_FKSC, X_FATK, X_FDEC, X_FSUS, X_FREL,
       X_FTO, X_FFROM, X_FAMT, X_FOUT, X_FDTN, X_FFIX, X_FPEG, X_FPTIM, X_FSLOT, X_FSTORE,
       X_HPOS, X_HCMP,                                 /* 0.7: UNDO (the place in the sound's history), COMPARE */
       X_QLANE, X_QSTEP, X_QVAL, X_QFILL,              /* 0.8: the MOD SEQ: the lane, the step, its value, a SHAPE */
       X_TSYS, X_TROOT,                                /* 0.9: the TUNING (voice.c tune_cfg; kept with the settings) */
       X_TA4,                                          /* 1.0: its reference A4 in Hz */
       X_MIN, X_MOUT,                                  /* 1.0: GLO > MIDI, the instrument's channels (seq.c midi_in_ch) */
       X_MBEND, X_MLOCAL, X_MPROG, X_MCCO,             /* 1.0: the bend range (voice.c midi_bend), LOCAL, PROG, CC OUT */
       X_MBRTH,                                        /* 1.0: EWI (seq.c midi_breath) */
       /* Stallion: GLO > TOUCH (touch.h touch_cfg; kept with the settings): KEY TOUCH, AT SENS, AT OUT, TOUCH CAL; the AT
        * shaping; the lever, the ribbon's range, MOTION REC; the ribbon */
       X_KTOUCH, X_ATSENS, X_ATOUT, X_TCAL, X_ATCURVE, X_ATOFS, X_ATATK, X_ATREL,
       X_LEVER, X_LTIME, X_RIBRNG, X_MREC, X_RSTEPS, X_RSPRING, X_RDELAY, X_RRET,
       /* GLO > KEY AT, KEY AT 2 (touch.h kat_cfg): the SMK-37's keys' made-up pressure */
       X_KON, X_KHIT, X_KVEL, X_KRISE, X_KFALL, X_KHOLD, X_KSWELL, X_KCEIL,
       /* GLO > PAD AT, PAD AT 2 (the pads' squeeze), KEY AT 3 (the keys' swell): pat_cfg.h */
       X_PDEAD, X_PROOM, X_PTOP, X_PSETL, X_PRISE, X_PFALL, X_PSHAPE, X_KDELAY, X_KSHAPE,
       X_MCCA, X_MCCB, X_MSUS,                         /* GLO > MIDI 3: CC A, CC B, SUSTAIN (voice.c midi_cca) */
       X_MCLR,                                         /* MATRIX 2 > CLEAR: all 24 slots off */
       X_COUNT };
static const char *const N_SHAPE[SH_N] = {"LIN", "EASE", "SLOW", "FAST"};
static void mseq_fill(struct track *t, uint32_t l, uint32_t f);
static const char *const N_QFILL[] = {"--", "RAMP+", "RAMP-", "TRI", "SINE", "SQR", "RND", "FLAT", "INVRT", "SHIFT"};
enum { QF_NONE, QF_UP, QF_DOWN, QF_TRI, QF_SINE, QF_SQR, QF_RND, QF_FLAT, QF_INV, QF_SHIFT, QF_N };
static const char *const N_RNDWHAT[] = {"EDIT", "+ENV", "ALL", "ENV", "MOD", "FX", "OPS"};   /* (0.7: one section, as the
                                                        * Hydrasynth's; OPS: FMX's operators, opsix style) */
static const char *const N_OCTS[] = {"-2", "-1", "0", "+1", "+2"};
static int16_t xv[X_COUNT] = {[X_RNDAMT] = 40, [X_RNDWHAT] = 1, [X_MSLOT] = 1, [X_FOP] = 1, [X_FTO] = 1, [X_FFROM] = 2,
                              [X_FSLOT] = 1, [X_QLANE] = 1, [X_QSTEP] = 1};
static const char *const N_FRATIO[32] = {"0.5", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13",
                                         "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26",
                                         "27", "28", "29", "30", "31"};
static const char *const N_FFIXC[4] = {"1Hz", "10Hz", "100Hz", "1kHz"};
_Static_assert(X_FOP == 19 && X_FSTORE == 40 && X_HPOS == 41 && X_QLANE == 43 && X_TSYS == 47 && QF_N == NELEM(N_QFILL),
               "params.c PAGES: FMX's ids, UNDO's, MOD SEQ's");
static const char *const N_CMP[] = {"EDIT", "ORIG"};
static const char *const N_MIN_CH[17] = {"OMNI", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14",
                                         "15", "16"};
static const char *const N_MBEND[25] = {"AUTO", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13",
                                        "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24"};
static const char *const N_MCCO[] = {"OFF", "CC"};   /* (5 characters a value: "IN+OUT" did not fit) */
static const char *const N_MBRTH[] = {"OFF", "VOL", "V+AT"};
static const char *const N_SUSMODE[] = {"ON", "PATCH"};   /* SUSTAIN: PATCH, the pedal only feeds CC A / B */
_Static_assert(X_MCCA == 90 && X_MCCB == 91 && X_MSUS == 92, "params.c PAGES: MIDI 3's ids");
_Static_assert(X_MCLR == 93, "params.c PAGES: MATRIX 2's id");
/* the job a CC has besides feeding CC A / CC B (seq.c part_ctl_cc, MIDI_CC_MAP), shown beside its number */
static const char *cc_job(uint32_t cc)
{
    return cc == 1u ? "MW" : cc == 2u ? "BRTH" : cc == 7u || cc == 11u ? "VOL" : cc == 64u ? "SUST" :
           cc >= 16u && cc <= 19u ? "MACRO" : cc == 74u ? "CUT" : 0;
}
_Static_assert(X_MIN == 50 && X_MOUT == 51 && X_MBEND == 52 && X_MCCO == 55 && X_MBRTH == 56, "params.c PAGES: MIDI's ids");
static const char *const N_KTOUCH[KT_MODES] = {"OFF", "VEL", "V+AT", "AT"};
static const char *const N_ATOUT[AT_OUT_MODES] = {"OFF", "CHAN", "POLY"};
static const char *const N_ATCURVE[AT_CURVES] = {"LIN", "SOFT", "HARD"};
#if BOARD_SMK37
static const char *const N_PADV[PV_MODES] = {"NORM", "LIGHT", "XLITE", "FIXED", "FLOOR"};
#endif
static const char *const N_RSPRING[] = {"HOLD", "SPRNG", "LATCH"};   /* (HOLD, then RIB SPRING ON's: touch_ext rib_sprng) */
static const char *const N_LEVER[1 + LV_MODES] = {"OFF", "SPRNG", "HOLD", "LATCH"};   /* (OFF, then 1 + touch_ext's mode) */
_Static_assert(X_KTOUCH == 57 && X_TCAL == 60 && X_ATCURVE == 61 && X_LEVER == 65 && X_RSTEPS == 69 && X_RRET == 72,
               "params.c PAGES: TOUCH's ids");
static param_desc_t xd_sound = {"SOUND", F_INT, 0, 0, 0, 0, 0};   /* (its range: the list's length, syn_desc) */
static const param_desc_t XD[X_COUNT] = {
    [X_LYMODE] = PE("MODE", N_LYMODE, LY_OFF),
    [X_LYSPLIT] = {"SPLIT", F_SPLIT, 16, 127, 64, 0, 0},
    [X_LYLEVEL] = PD("LEVEL", F_DB, 0, 127, 104),
    [X_LYOCT] = PE("OCT", N_OCTS, 2),
    [X_LYPAN] = PD("PAN", F_BIPCT, -64, 63, 0),
    [X_LYFILT] = PD("FILT", F_FILT, -64, 63, 0),
    [X_LYSWAP] = PE("SWAP", N_GO, 0),
    [X_MORPH] = PD("MORPH", F_PCT, 0, 127, 0),
    [X_SETA] = PE("SET A", N_GO, 0),
    [X_SETB] = PE("SET B", N_GO, 0),
    [X_RNDAMT] = PD("AMT", F_PCT, 0, 127, 40),
    [X_RNDWHAT] = PE("WHAT", N_RNDWHAT, 1),
    [X_RNDGO] = PE("RAND", N_GO, 0),
    [X_RNDBACK] = PE("BACK", N_GO, 0),
    [X_MSLOT] = PD("SLOT", F_INT, 1, MX_SLOTS, 1),   /* (preset format 2: 24) */
    [X_FOP] = PD("OP", F_INT, 1, 6, 1),
    [X_FWAVE] = PE("WAVE", N_FMX_WAVE, 0),
    [X_FMODE] = PE("MODE", N_FMX_MODE, 0),
    [X_FRATIO] = PE("RATIO", N_FRATIO, 1),
    [X_FFINE] = PD("FINE", F_INT, 0, 99, 0),
    [X_FLEVEL] = PD("LEVEL", F_INT, 0, 127, 127),
    [X_FVEL] = PD("VEL", F_PCT, 0, 127, 0),
    [X_FKSC] = PD("KSC", F_PCT, 0, 127, 0),
    [X_FATK] = PD("ATK", F_TIME, 0, 127, 0),
    [X_FDEC] = PD("DEC", F_TIME, 0, 127, 70),
    [X_FSUS] = PD("SUS", F_PCT, 0, 127, 127),
    [X_FREL] = PD("REL", F_TIME, 0, 127, 40),
    [X_FTO] = PD("TO", F_INT, 1, 6, 1),
    [X_FFROM] = PD("FROM", F_INT, 1, 6, 2),
    [X_FAMT] = PD("AMT", F_INT, 0, 127, 0),
    [X_FOUT] = PD("OUT", F_INT, 0, 127, 0),
    [X_FDTN] = PD("DTN", F_INT, -7, 7, 0),
    [X_FFIX] = PE("FREQ", N_FMX_FIX, 0),
    [X_FPEG] = PD("PEG", F_BIPCT, -64, 63, 0),
    [X_FPTIM] = PD("PTIM", F_TIME, 0, 127, 40),
    [X_FSLOT] = PD("SLOT", F_INT, 1, FM6_BANK_N, 1),
    [X_QLANE] = PD("LANE", F_INT, 1, 4, 1),
    [X_QSTEP] = PD("STEP", F_INT, 1, 16, 1),
    [X_QVAL] = PD("VALUE", F_INT, -8, 7, 0),
    [X_QFILL] = PE("SHAPE", N_QFILL, 0),
    [X_TSYS] = PE("TUNE", N_TUNE, 0),
    [X_TROOT] = PE("ROOT", N_NOTE, 0),
    [X_TA4] = {"A4", F_INT, 400, 480, 440, 0, "Hz"},
    [X_MIN] = PE("IN CH", N_MIN_CH, 0),
    [X_MOUT] = PD("OUT", F_INT, 1, 16, 1),          /* (the channel out: "OUT CH" does not fit the column) */
    [X_MBEND] = PE("BEND", N_MBEND, 0),
    [X_MLOCAL] = PE("LOCAL", N_ONOFF, 1),
    [X_MPROG] = PE("PROG", N_ONOFF, 1),
    [X_MCCO] = PE("SEND", N_MCCO, 0),
    [X_MBRTH] = PE("BRTH", N_MBRTH, 0),      /* (a breath controller, CC 2; Stallion: no trademark in the label) */
    [X_MCCA] = PD("CC A", F_INT, 0, 127, 64),       /* GLO > MIDI 3 */
    [X_MCCB] = PD("CC B", F_INT, 0, 127, 4),
    [X_MSUS] = PE("SUST", N_SUSMODE, 0),
    [X_MCLR] = PE("CLEAR", N_GO, 0),                /* MATRIX 2: two detents within 1.5 s (syn_changed) */
    [X_KTOUCH] = PE("KEYS", N_KTOUCH, KT_OFF),   /* (Stallion) GLO > TOUCH: KEY TOUCH */
#if BOARD_SMK37
    [X_ATSENS] = PE("VEL", N_PADV, PV_FLOOR),
#else
    [X_ATSENS] = {"SENS", F_FIX2, TOUCH_SENS_MIN, TOUCH_SENS_MAX, 40, 0, "OCT"},
#endif
    [X_ATOUT] = PE("ATOUT", N_ATOUT, AT_OUT_OFF),
    [X_TCAL] = PE("CAL", N_GO, 0),
    [X_ATCURVE] = PE("CURVE", N_ATCURVE, AT_LIN),
    [X_ATOFS] = PD("OFFS", F_INT, -4, 4, 0),
    [X_ATATK] = {"ATK", F_MS10, 0, 40, 0, 0, 0},     /* (10 ms steps: 0 .. 400 ms) */
    [X_ATREL] = {"REL", F_MS10, 0, 40, 0, 0, 0},
    [X_LEVER] = PE("LEVER", N_LEVER, 1 + LV_LATCH),
    [X_LTIME] = {"LTIME", F_MS10, 1, 100, 12, 0, 0},
    [X_RIBRNG] = {"RIB", F_SEMI, 0, 24, 12, 0, 0},  /* RIB RANGE */
    [X_MREC] = PE("MREC", N_ONOFF, 1),              /* MOTION REC */
    [X_RSTEPS] = PD("STEPS", F_INT, 6, 96, 24),
    [X_RSPRING] = PE("SPRNG", N_RSPRING, 2),
    [X_RDELAY] = {"DELAY", F_MS10, 0, 300, 15, 0, 0},
    [X_RRET] = {"RETRN", F_MS10, 5, 500, 40, 0, 0},
    [X_KON] = PE("KEYAT", N_ONOFF, 1),               /* KEY AT (touch.h kat_cfg, smk_press.h smk_kat) */
    [X_KHIT] = PD("HIT", F_INT, 0, 127, 50),
    [X_KVEL] = PD("VEL%", F_INT, 0, 100, 70),
    [X_KRISE] = {"RISE", F_MS10, 0, 100, 12, 0, 0},
    [X_KFALL] = {"FALL", F_MS10, 1, 300, 40, 0, 0},
    [X_KHOLD] = PD("HOLD%", F_INT, 0, 100, 40),
    [X_KSWELL] = {"SWELL", F_MS10, 0, 1000, 300, 0, 0},
    [X_KCEIL] = PD("CEIL", F_INT, 0, 127, 64),
#if BOARD_SMK37
    [X_PDEAD] = PD("DEAD", F_INT, 0, 150, 60),       /* PAD AT (pat_cfg.h) */
    [X_PRISE] = {"RISE", F_MS10, 1, 40, 12, 0, 0},   /* PAD AT 2 */
    [X_PFALL] = {"FALL", F_MS10, 1, 40, 12, 0, 0},
    [X_PSHAPE] = PE("SHAPE", N_SHAPE, SH_SLOW),
#else                                                /* (the FM-1: GLO > TOUCH 5 / 6, its keys' pressure) */
    [X_PDEAD] = PD("DEAD", F_INT, 0, 150, 10),
    [X_PRISE] = {"RISE", F_MS10, 1, 40, 3, 0, 0},
    [X_PFALL] = {"FALL", F_MS10, 1, 40, 3, 0, 0},
    [X_PSHAPE] = PE("SHAPE", N_SHAPE, SH_LIN),
#endif
    [X_PROOM] = PD("ROOM", F_INT, 20, 250, 80),
    [X_PTOP] = PD("TOP", F_INT, 600, 1000, 800),
    [X_PSETL] = {"SETTL", F_MS10, 5, 50, 25, 0, 0},
    [X_KDELAY] = {"DELAY", F_MS10, 0, 300, 0, 0, 0}, /* KEY AT 3 */
    [X_KSHAPE] = PE("SHAPE", N_SHAPE, SH_EASE),
    [X_FSTORE] = PE("STORE", N_GO, 0),
    [X_HPOS] = PD("UNDO", F_INT, -16, 0, 0),
    [X_HCMP] = PE("CMP", N_CMP, 0),
};

/* ---- a sound (engine + every parameter) as a snapshot */
typedef struct {
    uint8_t ok, eng, preset, user;
    int16_t p[P_COUNT];
    int8_t ms[MSEQ_LANES][MSEQ_STEPS];               /* (0.8) the MOD SEQ's steps */
} syn_snap_t;
static syn_snap_t syn_a, syn_b, syn_back;

UI_SMALL static void snap_take(syn_snap_t *s, const track_t *t)
{
    s->ok = 1;
    s->eng = t->eng_req;
    s->preset = t->preset;
    s->user = t->user;
    memcpy(s->p, t->p, sizeof s->p);
    memcpy(s->ms, t->ms, sizeof s->ms);
}
/* a snapshot's sound into t (its mix and pattern parameters stay: param_kept) */
UI_SMALL static void snap_put(track_t *t, const syn_snap_t *s)
{
    uint32_t i;
    if (!s->ok)
        return;
    sound_fade_begin(t);
    fm1_irq_off();
    t->eng_req = s->eng;
    for (i = 0; i < P_COUNT; i++)
        if (i >= P_E0 || !param_kept(i))
            t->p[i] = s->p[i];
    t->preset = s->preset;
    t->user = s->user;
    memcpy(t->ms, s->ms, sizeof t->ms);
    mx_prune(t);                                      /* (no cable the engine does not use) */
    fm1_irq_on();
    fm6_track_loaded(t);
    ui.force = 1;
}

/* ---- 0.7: the sound's history (UNDO page) and COMPARE. What researchers of synth users hear most: "no undo, no
 * compare, my edits are lost". Every change of the sound (a knob, the editor, RANDOM, MORPH, a sound loaded) becomes a
 * step once it rests HIST_REST ms (a sweep of a knob is one step), or at once before a sound is loaded; up to HIST_N
 * steps back (UNDO turned left), and forward again (right) until something new is changed. The FMX patch goes with
 * the steps. COMPARE: the sound as it was loaded (ORIG) against the edits (EDIT); a change made while comparing ends
 * it (the edits stay one step back). HOME marks an edited sound with a star; PRESETS asks before it leaves one */
#define HIST_N 16u
#define HIST_REST 900u
typedef struct {
    syn_snap_t s;
    fmx_patch_t fx;                                   /* (the part's FMX patch, eng_fmx.c fmx_cur) */
} hist_t;
static hist_t hist[HIST_N] __attribute__((section(".pool")));   /* a ring: hist_n steps back, then hist_r steps
                                                                  * forward (1.0: in the pool, RAM is near full) */
static hist_t hist_cur, hist_orig, hist_cmp;          /* the step now (committed), the sound as loaded, the edits */
static uint32_t hist_head, hist_n, hist_r, hist_t0;   /* hist_head: the next free place; hist_t0: the last change */
static uint8_t hist_pend, hist_ready, hist_cmp_on, hist_edited, hist_load_req;
static int16_t hist_pos;                              /* the UNDO knob: 0 the newest, -k k steps back */
UI_SMALL static void hist_take(hist_t *h)
{
    snap_take(&h->s, &trk[0]);
    h->fx = fmx_cur[0];
}
UI_SMALL static int hist_same(const hist_t *a, const hist_t *b)  /* the same sound (its mix, pattern and key aside) */
{
    uint32_t i;
    if (a->s.eng != b->s.eng)
        return 0;
    for (i = 0; i < P_COUNT; i++)
        if ((i >= P_E0 || !param_kept(i)) && i != P_ED_FX && a->s.p[i] != b->s.p[i])
            return 0;
    if (memcmp(a->s.ms, b->s.ms, sizeof a->s.ms))
        return 0;                                     /* (0.8: a MOD SEQ step) */
    return a->s.eng != ENGI_FMX || !memcmp(&a->fx, &b->fx, sizeof a->fx);
}
UI_SMALL static void hist_put(const hist_t *h)
{
    snap_put(&trk[0], &h->s);
    if (h->s.eng == ENGI_FMX) {                       /* its patch as it was (not PTCH's again: fmx_block) */
        fm1_irq_off();
        fmx_cur[0] = h->fx;
        fmx_dirty[0] = 1;
        fmx_slot_seen[0] = (int16_t)(clamp(h->s.p[P_E0], 0, (int32_t)FMX_NSLOT - 1) + 1);
        fmx_reload &= (uint8_t)~1u;
        fm1_irq_on();
    }
}
UI_SMALL static void hist_push(void)                           /* hist_cur one step back; nothing forward any more */
{
    hist[hist_head % HIST_N] = hist_cur;
    hist_head = (hist_head + 1u) % HIST_N;
    if (hist_n < HIST_N)
        hist_n++;
    hist_r = 0;
}
UI_SMALL static void hist_commit(void)                         /* what changed since the last step: a step */
{
    hist_t now;
    hist_take(&now);
    if (hist_ready && !hist_same(&now, &hist_cur)) {
        hist_push();
        hist_cur = now;
    }
    hist_pend = 0;
}
/* before a sound is loaded (ui.c apply_preset_to, upreset.c up_load_to): the edits a step of their own, and the
 * sound loaded the new ORIG (hist_poll, the next frame) */
UI_SMALL static void hist_before_load(void)
{
    if (!CLIPPER_SYNTH || !hist_ready || hist_load_req)
        return;                                       /* (a load under way this frame: one step for all of it) */
    if (hist_cmp_on)                                  /* (comparing: the edits back first, as a step) */
        hist_cur = hist_cmp, hist_cmp_on = 0;
    hist_commit();
    hist_load_req = 1;
}
UI_SMALL static void hist_poll(void)                           /* once a UI frame */
{
    hist_t now;
    if (!hist_ready) {
        hist_take(&hist_cur);
        hist_orig = hist_cur;
        hist_ready = 1;
        return;
    }
    hist_take(&now);
    if (hist_load_req) {                              /* a sound loaded: a step, and the new ORIG */
        hist_load_req = 0;
        if (!hist_same(&now, &hist_cur)) {
            hist_push();
            hist_cur = now;
        }
        hist_orig = now;
        hist_pend = 0;
        hist_edited = 0;
        hist_pos = 0;
        return;
    }
    if (hist_cmp_on) {                                /* comparing: a change ends it (the edits a step back) */
        hist_edited = 0;
        if (!hist_same(&now, &hist_orig)) {
            hist_cmp_on = 0;
            xv[X_HCMP] = 0;
            hist_cur = hist_cmp;
            hist_push();
            hist_cur = now;
            ui_message("COMPARE OFF");
        }
        return;
    }
    {
        static hist_t seen;                           /* the sound last frame: still moving, the rest from now */
        if (!hist_same(&now, &seen))
            hist_t0 = fm1_ms;
        seen = now;
    }
    if (!hist_same(&now, &hist_cur)) {
        hist_pend = 1;
        if (fm1_ms - hist_t0 >= HIST_REST) {
            hist_push();
            hist_cur = now;
            hist_pend = 0;
            hist_pos = 0;
        }
    } else {
        hist_pend = 0;
    }
    hist_edited = (uint8_t)!hist_same(&now, &hist_orig);
}
UI_SMALL static void hist_step(int32_t d)                      /* d < 0: undo -d steps; > 0: redo */
{
    if (hist_cmp_on)
        return;
    hist_commit();
    while (d < 0 && hist_n) {                         /* back: hist_cur forward (redo), the newest back step now */
        hist_head = (hist_head + HIST_N - 1u) % HIST_N;
        hist_t prev = hist[hist_head];
        hist[hist_head] = hist_cur;                   /* (the ring keeps it: hist_r counts it as forward) */
        hist_cur = prev;
        hist_n--, hist_r++;
        d++;
    }
    while (d > 0 && hist_r) {
        hist_t next = hist[hist_head];
        hist[hist_head] = hist_cur;
        hist_head = (hist_head + 1u) % HIST_N;
        hist_cur = next;
        hist_n++, hist_r--;
        d--;
    }
    hist_put(&hist_cur);
    hist_pos = (int16_t)-(int32_t)hist_r;
}
UI_SMALL static void hist_compare(int on)
{
    if (on == hist_cmp_on)
        return;
    if (on) {
        hist_commit();
        hist_cmp = hist_cur;
        hist_cmp_on = 1;
        hist_put(&hist_orig);
        ui_message("COMPARE: AS LOADED");
    } else {
        hist_cmp_on = 0;
        hist_put(&hist_cmp);
        ui_message("COMPARE: YOUR EDITS");
    }
}
/* what changed from step a to step b, in a few letters: the first two values that differ (their labels) */
UI_SMALL static void hist_label(const hist_t *a, const hist_t *b, char *out, uint32_t cap)
{
    uint32_t i, n = 0;
    out[0] = 0;
    if (a->s.eng != b->s.eng || a->s.preset != b->s.preset || a->s.user != b->s.user) {
        const engine_t *e = ENGINES[b->s.eng % NENGINES];
        if (b->s.user && b->s.user <= UP_SLOTS && up_used(b->s.user - 1u))
            up_name(b->s.user - 1u, out);
        else
            str_cpy(out, e->npresets ? e->presets[b->s.preset % e->npresets].name : e->name, cap);
        return;
    }
    for (i = 0; i < P_COUNT; i++)
        if ((i >= P_E0 || !param_kept(i)) && i != P_ED_FX && a->s.p[i] != b->s.p[i]) {
            const param_desc_t *d = i >= P_E0 ? &ENGINES[b->s.eng % NENGINES]->edit[i - P_E0] : track_desc(&trk[0], i);
            if (n < 2u && d && d->label) {
                if (n)
                    str_cpy(out + str_len(out), " ", cap - str_len(out));
                str_cpy(out + str_len(out), d->label, cap - str_len(out));
            }
            n++;
        }
    if (!n && memcmp(a->s.ms, b->s.ms, sizeof a->s.ms))
        str_cpy(out, "MOD SEQ", cap);
    else if (!n && b->s.eng == ENGI_FMX)
        str_cpy(out, "FMX PATCH", cap);
    else if (n > 2u) {
        char k[8] = " +";
        fmt_int(k + 2, (int32_t)n - 2);
        str_cpy(out + str_len(out), k, cap - str_len(out));
    }
}

/* a value's kind for MORPH and RANDOM: 1 a switch (the names), 0 a continuous one */
UI_SMALL static const param_desc_t *syn_pdesc(uint32_t eng, uint32_t i)
{
    return i >= P_E0 ? &ENGINES[eng % NENGINES]->edit[i - P_E0] : &TP[i];
}
UI_SMALL static int syn_switch(const param_desc_t *d) { return d->fmt == F_ENUM || d->fmt == F_ONOFF || d->names; }

/* the values MORPH and RANDOM move: the sound, not its mix, pattern, key or voice mode */
UI_SMALL static int syn_sound_param(uint32_t i)
{
    if (i >= P_E0)
        return i <= P_E7;
    return !param_kept(i) && i != P_ED_FX && i != P_VOICE && i != P_PRIO && i != P_ALLOC && i != P_GLMODE &&
           !(i >= P_AMODE && i <= P_AORDER);
}

UI_SMALL static void syn_morph_apply(int32_t m)
{
    track_t *t = &trk[0];
    uint32_t i;
    if (!syn_a.ok || !syn_b.ok)
        return;
    if (syn_a.eng != syn_b.eng) {                     /* two engines: the one of the nearer end */
        const syn_snap_t *s = m < 64 ? &syn_a : &syn_b;
        if (t->eng_req != s->eng)
            snap_put(t, s);
        return;
    }
    if (t->eng_req != syn_a.eng)
        snap_put(t, &syn_a);
    for (i = 0; i < P_COUNT; i++) {
        const param_desc_t *d = syn_pdesc(syn_a.eng, i);
        int32_t a = syn_a.p[i], b = syn_b.p[i];
        if (!syn_sound_param(i) || a == b)
            continue;
        t->p[i] = (int16_t)(syn_switch(d) ? (m < 64 ? a : b) : a + ((b - a) * m + (b > a ? 63 : -63)) / 127);
    }
    ui.force = 1;
}

/* ---- the randomizer */
static uint32_t syn_seed = 0x9E3779B9u;
UI_SMALL static uint32_t syn_rnd(void)
{
    syn_seed ^= syn_seed << 13;
    syn_seed ^= syn_seed >> 17;
    syn_seed ^= syn_seed << 5;
    return syn_seed;
}
/* the range a random value may take: kept musical (no endless attack, no siren of pitch modulation) */
UI_SMALL static void syn_rnd_range(uint32_t i, const param_desc_t *d, int32_t *lo, int32_t *hi)
{
    *lo = d->min;
    *hi = d->max;
    switch (i) {
    case P_ATK: *hi = 90; break;
    case P_REL: *hi = 105; break;
    case P_ED_PIT: *lo = -8, *hi = 8; break;
    case P_LD_PIT: *lo = -6, *hi = 6; break;
    case P_LD_AMP: *hi = 60; break;
    case P_DIST: *hi = 70; break;
    default: break;
    }
    uint32_t mf;
    if (mx_slot_of(i, &mf) >= 0) {                    /* a matrix slot's amount: moderate, the pitch finer */
        int32_t r = trk[0].p[i - 1u] == MT_PITCH ? 6 : 40;
        *lo = -r;
        *hi = r;
    }
    if (i >= P_E0 && d->names) {                      /* no empty user slot (WAVE's USR1..4 and the like) */
        while (*hi > *lo && d->names[*hi - d->min] && d->names[*hi - d->min][0] == 'U' && d->names[*hi - d->min][1] == 'S')
            (*hi)--;
    }
}
UI_SMALL static int syn_rnd_in(uint32_t i, uint32_t what)
{
    int edit = i >= P_E0 && i <= P_E7;
    int env = (i >= P_ATK && i <= P_REL) || (i >= P_ED_FLT && i <= P_ED_SHP) || (i >= P_E2ATK && i <= P_E2REL);
    uint32_t mf;
    int mod = (i >= P_LRATE && i <= P_LD_AMP) || i == P_VELF || (i >= P_L2RATE && i <= P_L2MODE) ||
              (i >= P_L3RATE && i <= P_L3MODE) ||       /* (1.0: LFO 3, slots 7..12) */
              (mx_slot_of(i, &mf) >= 0 && mf == 2u && trk[0].p[i - 2u] != MS_OFF && trk[0].p[i - 1u] != MT_OFF);
    int fx = (i >= P_DIST && i <= P_REV) || (trk[0].p[P_ITYPE] && i >= P_IA && i <= P_IMIX) ||   /* (0.7: the INSERT's
                                                         * values, its TYPE kept; */
             (trk[0].p[P_F2MODEL] && (i == P_F2CUT || i == P_F2RES));   /* FILTER 2's CUT and RES, its MODL kept) */
    switch (what) {
    case 0: return edit;
    case 1: return edit || ((i >= P_ATK && i <= P_REL) || (i >= P_ED_FLT && i <= P_ED_SHP));
    case 3: return env;                                  /* 0.7: one section */
    case 4: return mod;
    case 5: return fx;
    case 6: return trk[0].eng_req == ENGI_FMX ? 0 : edit;   /* (OPS: the FMX patch, syn_rnd_fmx; another engine: EDIT) */
    default: return edit || env || mod || fx;            /* ALL (0.4: LFO 2, ENV 2, the amounts of the slots in use) */
    }
}
/* 0.7, RANDOM > WHAT OPS on FMX: the operators of the part's patch, as the opsix's randomizer: ratios from the musical
 * ones, the modulators' levels, the envelopes, now and then a wave; the carriers' levels and the routing's shape kept,
 * its amounts moved. AMT how far */
UI_SMALL static void syn_rnd_fmx(int32_t amt)
{
    static const uint8_t RAT[12] = {1, 1, 1, 2, 2, 3, 4, 5, 6, 7, 8, 14};
    static const uint8_t WV[6] = {FXW_SINE, FXW_SINE, FXW_SINE, FXW_TRI, FXW_HALF, FXW_SAW};
    fmx_patch_t p = fmx_cur[0];
    uint32_t k, j;
    for (k = 0; k < FMX_NOP; k++) {
        fmx_op_t *o = &p.op[k];
        int carrier = p.out[k] != 0;
        if (!(o->mode & 4u) && (int32_t)(syn_rnd() % 128u) < amt)
            o->coarse = RAT[syn_rnd() % 12u];
        if ((int32_t)(syn_rnd() % 128u) < amt / 2)
            o->fine = (uint8_t)((int32_t)(syn_rnd() % 128u) < amt / 3 ? syn_rnd() % 100u : 0u);
        if (!carrier && o->level)
            o->level = (uint8_t)clamp(o->level + ((40 + (int32_t)(syn_rnd() % 76u) - o->level) * amt) / 127, 1, 127);
        for (j = 0; j < 4u; j++) {
            int32_t lo = j == 0 ? 0 : 10, hi = j == 0 ? (carrier ? 40 : 70) : j == 2 ? 127 : 100;
            int32_t u = lo + (int32_t)(syn_rnd() % (uint32_t)(hi - lo + 1));
            o->adsr[j] = (uint8_t)clamp(o->adsr[j] + ((u - o->adsr[j]) * amt) / 127, 0, 127);
        }
        if ((int32_t)(syn_rnd() % 384u) < amt)
            o->wave = WV[syn_rnd() % 6u];
        for (j = 0; j < FMX_NOP; j++)                    /* the routing: where there is a connection */
            if (p.mod[k][j])
                p.mod[k][j] = (uint8_t)clamp(p.mod[k][j] + ((20 + (int32_t)(syn_rnd() % 108u) - p.mod[k][j]) * amt) / 127, 1, 127);
    }
    p.alg = 0;                                           /* (no template any more: the patch as it is) */
    fmx_sanitize(&p);
    fm1_irq_off();
    fmx_cur[0] = p;
    fmx_dirty[0] = 1;
    fm1_irq_on();
}
UI_SMALL static void syn_randomize(void)
{
    track_t *t = &trk[0];
    int32_t amt = xv[X_RNDAMT];
    uint32_t i, what = (uint32_t)xv[X_RNDWHAT];
    snap_take(&syn_back, t);
    sound_fade_begin(t);
    syn_seed ^= fm1_ms * 2654435761u;
    for (i = 0; i < P_COUNT; i++) {
        const param_desc_t *d = syn_pdesc(t->eng_req, i);
        int32_t lo, hi, v = t->p[i], u;
        if (!syn_rnd_in(i, what))
            continue;
        syn_rnd_range(i, d, &lo, &hi);
        if (hi <= lo)
            continue;
        u = lo + (int32_t)(syn_rnd() % (uint32_t)(hi - lo + 1));
        if (syn_switch(d)) {
            if ((int32_t)(syn_rnd() % 128u) < amt)
                v = u;
        } else {
            v += (u - v) * amt / 127;
        }
        t->p[i] = (int16_t)clamp(v, d->min, d->max);
    }
    if (what == 6u && t->eng_req == ENGI_FMX)
        syn_rnd_fmx(amt);                               /* (0.7) the operators */
    t->user = 0;
    ui.force = 1;
}

/* ---- the layer: its sound by the place in the preset list (as PRESETS: factory by kind, then the user's) */
UI_SMALL static uint32_t layer_pos(uint32_t *total)
{
    const track_t *t = &trk[1];
    uint32_t i, cur = 0;
    if (!bank_ready)
        bank_resolve();
    for (i = 0; i < NBANK; i++)
        if (BANK[i].e == t->eng_req && bank_pi[i] == t->preset)
            cur = i;
    if (t->user && t->user <= UP_SLOTS && up_used(t->user - 1u))
        cur = NBANK + up_rank(t->user - 1u);
    *total = NBANK + up_count();
    return cur;
}
UI_SMALL static void layer_load(uint32_t n)
{
    uint32_t k, e = base_at(n, &k);                     /* (0.7: the list without the favourites) */
    track_t *t = &trk[1];
    if (e == NENGINES) {
        up_load_to(t, k);
        return;
    }
    if (e != t->eng_req)
        set_engine_of(t, e);
    apply_preset_to(t, k);
    ui.force = 1;
}
/* track 1 and track 2 trade their sounds (each keeps its mix, its pattern and the layer's settings) */
UI_SMALL static void layer_swap(void)
{
    track_t *a = &trk[0], *b = &trk[1];
    syn_snap_t sa, sb;
    snap_take(&sa, a);
    snap_take(&sb, b);
    sound_fade_begin(a);
    sound_fade_begin(b);
    trk_all_off(a);
    fm1_irq_off();
    a->eng_req = sb.eng, b->eng_req = sa.eng;
    {
        uint32_t i;
        for (i = 0; i < P_COUNT; i++)
            if (i >= P_E0 || !param_kept(i)) {
                a->p[i] = sb.p[i];
                b->p[i] = sa.p[i];
            }
    }
    a->preset = sb.preset, b->preset = sa.preset;
    a->user = sb.user, b->user = sa.user;
    memcpy(a->ms, sb.ms, sizeof a->ms);               /* (0.8) the MOD SEQs trade places too */
    memcpy(b->ms, sa.ms, sizeof b->ms);
    mx_prune(a);                                      /* (each as its engine uses it) */
    mx_prune(b);
    hy_slot_seen[0] = hy_slot_seen[1] = 0;            /* HYBRID: each part its patch again */
    fm1_irq_on();
    fm6_track_loaded(a);
    fm6_track_loaded(b);
    {   /* FMX (0.6): the two parts' patches trade places too, edits and all */
        fmx_patch_t x = fmx_cur[0];
        int16_t s = fmx_slot_seen[0];
        fm1_irq_off();
        fmx_cur[0] = fmx_cur[1], fmx_cur[1] = x;
        fmx_slot_seen[0] = fmx_slot_seen[1], fmx_slot_seen[1] = s;
        fmx_reload &= (uint8_t)~3u;
        fmx_dirty[0] = fmx_dirty[1] = 1;
        fm1_irq_on();
    }
    sync_reload = 1;
    ui.force = 1;
}

/* ---- Stallion: GLO > TOUCH (touch.h touch_cfg): a knob's value (the times in 10 ms steps) and back */
UI_SMALL static int16_t touch_get(uint32_t id)
{
    const touch_cfg_t *c = &touch_cfg;
    switch (id) {
    case X_KTOUCH: return c->key_touch;
    case X_ATSENS: return c->at_sens;
    case X_ATOUT: return c->at_out;
    case X_ATCURVE: return c->at_curve;
    case X_ATOFS: return c->at_offset;
    case X_ATATK: return (int16_t)((c->at_attack + 5u) / 10u);
    case X_ATREL: return (int16_t)((c->at_release + 5u) / 10u);
    case X_LEVER: return (int16_t)(c->lever ? 1 + touch_ext.lever_mode : 0);
    case X_LTIME: return (int16_t)((c->lever_time + 5u) / 10u);
    case X_RIBRNG: return c->rib_range;
    case X_MREC: return c->motion_rec;
    case X_RSTEPS: return c->rib_steps;
    case X_RSPRING: return (int16_t)(c->rib_spring ? (touch_ext.rib_sprng ? 1 : 2) : 0);
    case X_RDELAY: return (int16_t)((c->rib_delay + 5u) / 10u);
    case X_KON: return kat_cfg.on;
    case X_KHIT: return kat_cfg.hit;
    case X_KVEL: return kat_cfg.vel;
    case X_KRISE: return (int16_t)((kat_cfg.rise + 5u) / 10u);
    case X_KFALL: return (int16_t)((kat_cfg.fall + 5u) / 10u);
    case X_KHOLD: return kat_cfg.hold;
    case X_KSWELL: return (int16_t)((kat_cfg.swell + 5u) / 10u);
    case X_KCEIL: return kat_cfg.ceil;
    case X_PDEAD: return pat_cfg.dead;
    case X_PROOM: return (int16_t)pat_cfg.room;
    case X_PTOP: return (int16_t)pat_cfg.top;
    case X_PSETL: return (int16_t)((pat_cfg.settle + 5u) / 10u);
    case X_PRISE: return (int16_t)((pat_cfg.rise + 5u) / 10u);
    case X_PFALL: return (int16_t)((pat_cfg.fall + 5u) / 10u);
    case X_PSHAPE: return pat_cfg.shape;
    case X_KDELAY: return (int16_t)((pat_cfg.kdelay + 5u) / 10u);
    case X_KSHAPE: return pat_cfg.kshape;
    default: return (int16_t)((c->rib_return + 5u) / 10u);   /* X_RRET */
    }
}
UI_SMALL static void touch_set(uint32_t id, int32_t v)
{
    touch_cfg_t *c = &touch_cfg;
    const param_desc_t *d = &XD[id];
    v = clamp(v, d->min, d->max);
    switch (id) {
    case X_KTOUCH: c->key_touch = (uint8_t)v; break;
    case X_ATSENS: c->at_sens = (uint8_t)v; break;
    case X_ATOUT: c->at_out = (uint8_t)v; break;
    case X_ATCURVE: c->at_curve = (uint8_t)v; break;
    case X_ATOFS: c->at_offset = (int8_t)v; break;
    case X_ATATK: c->at_attack = (uint16_t)(v * 10); break;
    case X_ATREL: c->at_release = (uint16_t)(v * 10); break;
    case X_LEVER:                                     /* (OFF keeps the mode: touch_ext, saved with it) */
        c->lever = v != 0;
        if (v)
            touch_ext.lever_mode = (uint8_t)(v - 1);
        break;
    case X_LTIME: c->lever_time = (uint16_t)(v * 10); break;
    case X_RIBRNG: c->rib_range = (uint8_t)v; break;
    case X_MREC: c->motion_rec = (uint8_t)v; break;
    case X_RSTEPS: c->rib_steps = (uint8_t)v; break;
    case X_RSPRING:                                   /* (HOLD keeps SPRNG / LATCH: touch_ext, saved with it) */
        c->rib_spring = v != 0;
        if (v)
            touch_ext.rib_sprng = v == 1;
        break;
    case X_RDELAY: c->rib_delay = (uint16_t)(v * 10); break;
    case X_RRET: c->rib_return = (uint16_t)(v * 10); break;
    case X_KON: kat_cfg.on = (uint8_t)v; break;
    case X_KHIT: kat_cfg.hit = (uint8_t)v; break;
    case X_KVEL: kat_cfg.vel = (uint8_t)v; break;
    case X_KRISE: kat_cfg.rise = (uint16_t)(v * 10); break;
    case X_KFALL: kat_cfg.fall = (uint16_t)(v * 10); break;
    case X_KHOLD: kat_cfg.hold = (uint8_t)v; break;
    case X_KSWELL: kat_cfg.swell = (uint16_t)(v * 10); break;
    case X_KCEIL: kat_cfg.ceil = (uint8_t)v; break;
    case X_PDEAD: pat_cfg.dead = (uint8_t)v; break;
    case X_PROOM: pat_cfg.room = (uint16_t)v; break;
    case X_PTOP: pat_cfg.top = (uint16_t)v; break;
    case X_PSETL: pat_cfg.settle = (uint16_t)(v * 10); break;
    case X_PRISE: pat_cfg.rise = (uint16_t)(v * 10); break;
    case X_PFALL: pat_cfg.fall = (uint16_t)(v * 10); break;
    case X_PSHAPE: pat_cfg.shape = (uint8_t)v; break;
    case X_KDELAY: pat_cfg.kdelay = (uint16_t)(v * 10); break;
    case X_KSHAPE: pat_cfg.kshape = (uint8_t)v; break;
    default: break;
    }
    touch_cfg_fix(c);
    kat_cfg_fix(&kat_cfg);
    pat_cfg_fix(&pat_cfg);
    if (id == X_ATCURVE || id == X_ATOFS || id == X_ATATK || id == X_ATREL) {
        fm1_irq_off();                                /* (the audio's table: whole between two blocks) */
        at_build();
        fm1_irq_on();
    }
}

/* ---- Stallion: GLO > TOUCH > CAL (specs/touch R9; key_touch.c kt_cal_*, after ESTAMPIE's calibration). The measured
 * keys (F3..C5) light up; each is played KT_CAL_N times gently (the lightest press that still sounds: its ceiling),
 * then KT_CAL_N times firmly (its floor), its light going out when it has enough; the medians are its ends, kept with
 * the settings. OCT+ moves on with the keys that are done (the others keep theirs), OCT- cancels; TC_IDLE_MS without
 * a press cancels too. The scan measures the keys meanwhile even with KEY TOUCH OFF (main.c); a calibration with KEY
 * TOUCH OFF turns it on (VEL). The keys sound as they are played (at the old ends). A screen of its own over the UI,
 * not a loop: the audio, MIDI and the rest go on */
#define TC_IDLE_MS 60000u
static uint8_t tc_on;
static uint32_t tc_t0, tc_drawn;
UI_SMALL static int touch_cal_on(void) { return tc_on; }
UI_SMALL static void touch_cal_open(void)
{
    kt_cal_begin();
    tc_on = 1;
    tc_t0 = fm1_ms;
    tc_drawn = ~0u;
    ui.force = 1;
}
UI_SMALL static uint32_t touch_cal_done(void)      /* the keys with enough presses in this phase */
{
    uint32_t k, n = 0, ph = kt_cal_phase ? kt_cal_phase : 1u;
    for (k = 0; k < KT_NKEY; k++)
        n += !kt_cal_left(ph, k);
    return n;
}
UI_SMALL static uint32_t touch_cal_keys(void)      /* the keys lit: those still wanted */
{
    uint32_t k, m = 0, ph = kt_cal_phase ? kt_cal_phase : 1u;
    for (k = 0; k < KT_NKEY; k++)
        if (kt_cal_left(ph, k))
            m |= 1u << k;
    return m;
}
UI_SMALL static void touch_cal_close(const char *msg)
{
    tc_on = 0;
    kt_cal_phase = 0;
    ui.force = 1;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui_message(msg);
}
UI_SMALL static void touch_cal_input(uint32_t pressed)
{
    uint32_t k, done = touch_cal_done();
    for (k = 0; k < NE; k++)
        (void)panel_enc(k);                           /* (the knobs do nothing here) */
    if (done != (tc_drawn & 0xFFu))
        tc_t0 = fm1_ms;                               /* (a key done: not idle) */
    if ((pressed >> panel.btn[B_OCTDN]) & 1u || fm1_ms - tc_t0 > TC_IDLE_MS) {
        touch_cal_close("TOUCH NOT CHANGED");
        return;
    }
    if (!((pressed >> panel.btn[B_OCTUP]) & 1u) && done < KT_NKEY)
        return;
    if (kt_cal_phase == 1u) {                         /* gently: done, now firmly */
        kt_cal_phase = 2;
        tc_t0 = fm1_ms;
        ui.force = 1;
        return;
    }
    kt_cal_finish();
    if (touch_cfg.key_touch == KT_OFF)
        touch_cfg.key_touch = KT_VEL;
    settings_later = 1;
    if (kt_cal_close) {                               /* (keys whose gentle and firm presses read alike: redo them) */
        char b[24];
        uint32_t n = 0;
        for (k = 0; k < KT_NKEY; k++)
            n += (kt_cal_close >> k) & 1u;
        fmt_int(b, (int32_t)n);
        str_cpy(b + str_len(b), n == 1u ? " KEY SOFT=HARD" : " KEYS SOFT=HARD", sizeof b - str_len(b));
        touch_cal_close(b);
        return;
    }
    touch_cal_close("TOUCH CALIBRATED");
}
/* (Stallion) the meter: the held key's press as a bar, live, and the last landing (its key, reading, counted) */
static uint32_t tc_meter_drawn = ~0u;
UI_SMALL static void touch_cal_meter(void)
{
    uint32_t d = 0, m = 0, w, sig;
    int32_t k = kt_held(&d);
    char b[24], nn[6];
    if (k >= 0)
        m = kt_cal_meter(d);
    sig = (k >= 0 ? m >> 2 : 0x40u) | (uint32_t)kt_cal_lastk << 8 | (uint32_t)(kt_cal_lastv >> 2) << 16 |
          (uint32_t)kt_cal_lastok << 31;
    if (!ui.force && sig == tc_meter_drawn)
        return;
    tc_meter_drawn = sig;
    lcd_fill(20, 156, 200, 12, C_DIM);                /* the bar: soft at the left, hard at the right */
    w = m * 196u / 127u;
    if (w)
        lcd_fill(22, 158, w, 8, kt_cal_phase == 2u ? C_WHITE : C_HI);
    lcd_fill(0, 172, 240, 18, C_BLACK);
    if (kt_cal_lastk < KT_NKEY) {
        note_name(nn, 53u + kt_cal_lastk);            /* key 0 = F3 */
        str_cpy(b, nn, sizeof b);
        str_cpy(b + str_len(b), "  ", sizeof b - str_len(b));
        fmt_int(b + str_len(b), (int32_t)kt_cal_meter(kt_cal_lastv));
        str_cpy(b + str_len(b), kt_cal_lastok ? "  COUNTED" : "  HAS 3", sizeof b - str_len(b));
        draw_text_box(0, 172, 240, &FONT_S, b, kt_cal_lastok ? C_HI : C_AMB, 1);
    }
}
UI_SMALL static void touch_cal_draw(void)
{
    uint32_t ph = kt_cal_phase == 2u ? 2u : 1u, done = touch_cal_done(), sig = ph << 8 | done;
    char b[24];
    if (ui.force || sig != tc_drawn) {
        tc_drawn = sig;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        draw_text_box(0, 16, 240, &FONT_S, "TOUCH CAL", C_HI, 1);
        draw_text_box(0, 44, 240, &FONT_S, "PLAY EACH LIT KEY", C_AMB, 1);
        draw_text_box(0, 70, 240, &FONT_L, ph == 1u ? "GENTLY" : "FIRMLY", C_WHITE, 1);
        draw_text_box(0, 108, 240, &FONT_S, ph == 1u ? "THE LIGHTEST THAT SOUNDS" : "AS HARD AS YOU PLAY", C_AMB, 1);
        draw_text_box(0, 126, 240, &FONT_S, "3 TIMES EACH", C_AMB, 1);
        fmt_int(b, (int32_t)done);
        str_cpy(b + str_len(b), " OF 20 KEYS", sizeof b - str_len(b));
        draw_text_box(0, 192, 240, &FONT_S, b, C_HI, 1);
        draw_text_box(0, 214, 240, &FONT_S, "OCT+ NEXT   OCT- CANCEL", C_DIM, 1);
        ui.force = 1;                                 /* (the meter under it again) */
    }
    touch_cal_meter();
}

/* the MATRIX page. SOURCE and DESTINATION step through every code once, in the order the
 * web editor's patch bay reads left to right (web/editor.html PB_MAIN, then PB_STRIP): X_MSRC / X_MDST are a place in
 * these tables, syn_changed stores the code (the codes themselves do not change) */
static const uint8_t MX_SRC_ORD[MS_NSRC] = {
    MS_OFF, MS_LFO1, MS_SH, MS_NOISE, MS_LFO2, MS_DC, MS_ENV1, MS_ENV2, MS_E2END,   /* MODULATION UTILITIES ENVELOPES */
    MS_KEY, MS_VEL, MS_GATE, MS_AT, MS_PAT, MS_CAT, MS_MW, MS_BEND, MS_RIB, MS_CCA, MS_CCB,   /* KEYBOARD, PERFORMANCE */
    MS_LFO3, MS_RAND, MS_SLIDE, MS_SEQ1, MS_SEQ2, MS_SEQ3, MS_SEQ4, MS_MAC1, MS_MAC2, MS_MAC3, MS_MAC4};   /* MORE */
static const uint8_t MX_DST_ORD[MT_COUNT] = {
    MT_OFF, MT_ARP, MT_L1RATE, MT_SYNC1, MT_L2RATE, MT_SYNC2,                    /* CONTROL MODULATION UTILITIES */
    MT_PITCH, MT_PIT1, MT_SHAPE, MT_XMOD, MT_PIT2, MT_RING, MT_SEMI, MT_SEM34,   /* OSCILLATORS (SEMI) */
    MT_CUT, MT_FFM, MT_CUT2, MT_CUTB, MT_HPF, MT_RES2,                           /* FILTERS */
    MT_TRIG1, MT_E1AMT, MT_TRIG2, MT_ERATE, MT_DLY, MT_REV, MT_CHO, MT_AMP, MT_PAN,   /* ENVELOPES SENDS OUTPUT */
    MT_E1, MT_E1 + 1, MT_E1 + 2, MT_E1 + 3, MT_E1 + 4, MT_E1 + 5, MT_E1 + 6, MT_E1 + 7};   /* the engine's EDIT */
_Static_assert(MS_NSRC == 31 && MT_COUNT == 37, "MATRIX: every source and destination in MX_SRC_ORD, MX_DST_ORD once");
/* the destinations t's engine uses (engines.c mx_dst_live), in MX_DST_ORD's order, OFF first: what the
 * DESTINATION knob steps through (a dead one is never offered, never kept: mx_prune); returns how many */
static uint8_t mx_dst_live_ord[MT_COUNT];
UI_SMALL static uint32_t mx_dst_list(const track_t *t)
{
    uint64_t live = mx_dst_live(t->eng_req);
    uint32_t i, n = 0;
    for (i = 0; i < MT_COUNT; i++)
        if (live >> MX_DST_ORD[i] & 1u)
            mx_dst_live_ord[n++] = MX_DST_ORD[i];
    return n;
}
UI_SMALL static uint32_t mx_pos(const uint8_t *ord, uint32_t n, int32_t code)   /* a code's place in its table */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (ord[i] == code)
            return i;
    return 0;
}
/* the signal colours (shared with the web editor's SIG_COL, pbSrcType): pitch blue, gate yellow, envelope green,
 * LFO / modulation orange, performance purple, +V white (red is audio's, never patched). The purple is the web's --pu,
 * 6.9:1 on the black the page is drawn on, whatever the palette */
#define MX_PURPLE RGB(196, 107, 255)
#define MX_ARM 0xD0u                                  /* (ui.arm: CLEAR armed; G_* ids are below, USER's 0xE1..) */
enum { MXC_MOD, MXC_PITCH, MXC_GATE, MXC_ENV, MXC_PERF, MXC_DC };
static const uint8_t MX_SRC_COL[MS_NSRC] = {
    [MS_KEY] = MXC_PITCH, [MS_GATE] = MXC_GATE, [MS_E2END] = MXC_GATE, [MS_ENV1] = MXC_ENV, [MS_ENV2] = MXC_ENV,
    [MS_VEL] = MXC_PERF, [MS_MW] = MXC_PERF, [MS_AT] = MXC_PERF, [MS_PAT] = MXC_PERF, [MS_CAT] = MXC_PERF,
    [MS_BEND] = MXC_PERF, [MS_RIB] = MXC_PERF, [MS_MAC1] = MXC_PERF, [MS_MAC2] = MXC_PERF, [MS_MAC3] = MXC_PERF,
    [MS_MAC4] = MXC_PERF, [MS_CCA] = MXC_PERF, [MS_CCB] = MXC_PERF, [MS_DC] = MXC_DC};   /* (the rest: modulation) */
UI_SMALL static uint16_t mx_col(uint32_t src)
{
    uint32_t g = src < MS_NSRC ? MX_SRC_COL[src] : MXC_MOD;
    return !src || src >= MS_NSRC ? C_GRAY : g == MXC_PITCH ? TE_COL[0] : g == MXC_GATE ? TE_COL[2] : g == MXC_ENV ? TE_COL[1]
           : g == MXC_PERF ? MX_PURPLE : g == MXC_DC ? C_WHITE : TE_COL[3];
}
/* the slot k of t plays: a source, a destination and an amount (voice.c mx_load) */
UI_SMALL static int mx_live(const track_t *t, uint32_t k)
{
    uint32_t id = mx_pid(k);
    return t->p[id] > MS_OFF && t->p[id] < MS_NSRC && t->p[id + 1u] > MT_OFF && t->p[id + 1u] < MT_COUNT && t->p[id + 2u];
}
/* the destinations t's playing slots go into (bits: 1 ARP, 2 CUT2, 4 VCO1, 8 VCO2) */
UI_SMALL static uint32_t mx_into(const track_t *t)
{
    uint32_t k, into = 0;
    for (k = 0; k < MX_SLOTS; k++)
        if (mx_live(t, k)) {
            int32_t d = t->p[mx_pid(k) + 1u];
            into |= d == MT_ARP ? 1u : d == MT_CUTB ? 2u : d == MT_PIT1 ? 4u : d == MT_PIT2 ? 8u : 0u;
        }
    return into;
}
/* what the page says under the cables: bit 0 LAYER ARPEGGIO: UNUSED (one arpeggiator, part 1's, seq.c: an ARP cable
 * in the layer's sound while the layer is on, or on the layer's part if an editor ever selects it, reaches nothing),
 * bit 1 CUT2 FOLLOWS CUT1 (MARE, nothing into CUT2), bit 2 VCO2 FOLLOWS VCO1 (MARE, a cable into VCO1, none into
 * VCO2). Each fits the 240 px line in FONT_S (tests/matrix_ui_test.c) */
static const char *const MX_NOTE[3] = {"LAYER ARPEGGIO: UNUSED", "CUT2 FOLLOWS CUT1", "VCO2 FOLLOWS VCO1"};
UI_SMALL static uint32_t mx_notes(const track_t *t)
{
    uint32_t into = mx_into(t);
    int arp = t != &trk[0] ? (into & 1u) != 0u : trk[1].p[P_LYMODE] != LY_OFF && (mx_into(&trk[1]) & 1u);
    return (arp ? 1u : 0u) | (t->eng_req == ENGI_MARE ? (!(into & 2u) ? 2u : 0u) | ((into & 12u) == 4u ? 4u : 0u) : 0u);
}
UI_SMALL static int mx_note_shown(const track_t *t)   /* the note on the screen now, -1 none (more: 3 s each) */
{
    uint32_t b = mx_notes(t), n = (b & 1u) + (b >> 1 & 1u) + (b >> 2 & 1u), i, w;
    if (!n)
        return -1;
    w = fm1_ms / 3000u % n;
    for (i = 0; i < 3u; i++)
        if ((b >> i & 1u) && !w--)
            break;
    return (int)i;
}

/* params.c page_desc: a value of the synth's pages */
UI_SMALL static const param_desc_t *syn_desc(uint32_t id, int16_t **valp)
{
    track_t *l = &trk[1];
    switch (id) {
    case X_LYMODE: *valp = &l->p[P_LYMODE]; break;
    case X_HPOS: {                                    /* 0.7: the place in the history, 0 the newest */
        static param_desc_t hd = {"UNDO", F_INT, 0, 0, 0, 0, 0};
        hd.min = (int16_t)-(int32_t)(hist_n + hist_r);
        hd.max = 0;
        xv[X_HPOS] = (int16_t)-(int32_t)hist_r;
        *valp = &xv[X_HPOS];
        return &hd;
    }
    case X_HCMP: xv[X_HCMP] = hist_cmp_on; *valp = &xv[X_HCMP]; break;
    case X_TSYS: xv[X_TSYS] = (int16_t)(tune_cfg.sys % TU_N); *valp = &xv[X_TSYS]; break;
    case X_TROOT: xv[X_TROOT] = (int16_t)(tune_cfg.root % 12u); *valp = &xv[X_TROOT]; break;
    case X_TA4: xv[X_TA4] = (int16_t)(440 + clamp(tune_cfg.a4, -40, 40)); *valp = &xv[X_TA4]; break;
    case X_MIN: xv[X_MIN] = (int16_t)(midi_in_ch % 17u); *valp = &xv[X_MIN]; break;
    case X_MOUT: xv[X_MOUT] = (int16_t)(1 + (midi_out_ch & 15u)); *valp = &xv[X_MOUT]; break;
    case X_MBEND: xv[X_MBEND] = (int16_t)(midi_bend % 25u); *valp = &xv[X_MBEND]; break;
    case X_MLOCAL: xv[X_MLOCAL] = !midi_local_off; *valp = &xv[X_MLOCAL]; break;
    case X_MPROG: xv[X_MPROG] = !midi_pc_off; *valp = &xv[X_MPROG]; break;
    case X_MCCO: xv[X_MCCO] = midi_cc_out != 0u; *valp = &xv[X_MCCO]; break;
    case X_MBRTH: xv[X_MBRTH] = (int16_t)(midi_breath % 3u); *valp = &xv[X_MBRTH]; break;
    case X_MCCA:
    case X_MCCB: {                                    /* the CC's number, its other job as the unit */
        static param_desc_t cd[2];                    /* (one each: a page holds both at once) */
        param_desc_t *d = &cd[id - X_MCCA];
        xv[id] = (int16_t)((id == X_MCCA ? midi_cca : midi_ccb) & 127u);
        *valp = &xv[id];
        *d = XD[id];
        d->unit = cc_job((uint32_t)xv[id]);
        return d;
    }
    case X_MSUS: xv[X_MSUS] = midi_sus_patch != 0u; *valp = &xv[X_MSUS]; break;
    case X_KTOUCH: case X_ATSENS: case X_ATOUT: case X_ATCURVE: case X_ATOFS: case X_ATATK: case X_ATREL: case X_LEVER:
    case X_LTIME: case X_RIBRNG: case X_MREC: case X_RSTEPS: case X_RSPRING: case X_RDELAY: case X_RRET:
    case X_KON: case X_KHIT: case X_KVEL: case X_KRISE: case X_KFALL: case X_KHOLD: case X_KSWELL: case X_KCEIL:
    case X_PDEAD: case X_PROOM: case X_PTOP: case X_PSETL: case X_PRISE: case X_PFALL: case X_PSHAPE: case X_KDELAY:
    case X_KSHAPE:
        xv[id] = touch_get(id);                       /* (Stallion) GLO > TOUCH */
        *valp = &xv[id];
        break;
    case X_QVAL:                                      /* (0.8) the selected step of the selected lane */
        xv[X_QVAL] = TSEL->ms[clamp(xv[X_QLANE], 1, 4) - 1][clamp(xv[X_QSTEP], 1, 16) - 1];
        *valp = &xv[X_QVAL];
        break;
    case X_LYSPLIT:
        if (layer_multi()) {                          /* 0.6, MULTI: no split; the knob is the layer's channel */
            static const param_desc_t ch = {"CH", F_INT, 1, 16, 2, 0, 0};
            *valp = &l->p[P_LYCH];
            return &ch;
        }
        *valp = &l->p[P_LYSPLIT];
        break;
    case X_LYLEVEL: *valp = &l->p[P_LEVEL]; break;
    case X_LYOCT: *valp = &l->p[P_LYOCT]; break;
    case X_LYPAN: *valp = &l->p[P_PAN]; break;
    case X_LYFILT: *valp = &l->p[P_TFLT]; break;
    case X_MSRC:
    case X_MDST: {                                    /* a place in MX_SRC_ORD / MX_DST_ORD, the names so */
        static param_desc_t od[2];                    /* (one each: the page shows both at once) */
        static const char *ns[MS_NSRC], *nd[MT_COUNT];
        uint32_t in = id == X_MDST, n = in ? mx_dst_list(TSEL) : MS_NSRC, i;   /* (DESTINATION: the engine's) */
        uint32_t pid = mx_pid((uint32_t)(clamp(xv[X_MSLOT], 1, (int32_t)MX_SLOTS) - 1)) + in;
        const uint8_t *ord = in ? mx_dst_live_ord : MX_SRC_ORD;
        const char *const *src = track_desc(TSEL, in ? (uint32_t)P_M1DST : pid)->names;   /* (E1..E8: the engine's) */
        const char **dn = in ? nd : ns;
        od[in] = *track_desc(TSEL, pid);
        for (i = 0; i < n; i++)
            dn[i] = src[ord[i]];
        od[in].min = 0, od[in].max = (int16_t)(n - 1u), od[in].def = 0, od[in].names = dn;
        xv[id] = (int16_t)mx_pos(ord, n, TSEL->p[pid]);
        *valp = &xv[id];
        return &od[in];
    }
    case X_MAMT: {                                    /* the slot's own values, the selected sound's */
        uint32_t pid = mx_pid((uint32_t)(clamp(xv[X_MSLOT], 1, (int32_t)MX_SLOTS) - 1)) + 2u;
        *valp = &TSEL->p[pid];
        return track_desc(TSEL, pid);
    }
    case X_FWAVE: case X_FMODE: case X_FRATIO: case X_FFINE: case X_FLEVEL: case X_FVEL: case X_FKSC: case X_FATK:
    case X_FDEC: case X_FSUS: case X_FREL: case X_FAMT: case X_FOUT: case X_FDTN: case X_FFIX: case X_FPEG:
    case X_FPTIM: {                                   /* FMX: the part's patch, the selected operator / connection */
        const fmx_patch_t *p = &fmx_cur[song.sel % NPART];
        const fmx_op_t *o = &p->op[clamp(xv[X_FOP], 1, 6) - 1];
        uint32_t to = (uint32_t)clamp(xv[X_FTO], 1, 6) - 1u, fr = (uint32_t)clamp(xv[X_FFROM], 1, 6) - 1u;
        static param_desc_t rd;
        int16_t v;
        switch (id) {
        case X_FWAVE: v = o->wave; break;
        case X_FMODE: v = (int16_t)(o->mode & 3u); break;
        case X_FRATIO: v = o->coarse; break;
        case X_FFINE: v = o->fine; break;
        case X_FLEVEL: v = o->level; break;
        case X_FVEL: v = o->vel; break;
        case X_FKSC: v = o->ksc; break;
        case X_FATK: case X_FDEC: case X_FSUS: case X_FREL: v = o->adsr[id - X_FATK]; break;
        case X_FAMT: v = p->mod[to][fr]; break;
        case X_FOUT: v = p->out[to]; break;
        case X_FDTN: v = (int16_t)((int32_t)o->dtn - 7); break;
        case X_FFIX: v = (int16_t)((o->mode & FXM_FIX) != 0u); break;
        case X_FPEG: v = (int16_t)((int32_t)p->peg - 64); break;
        default: v = p->ptime; break;
        }
        xv[id] = v;
        *valp = &xv[id];
        if (id == X_FRATIO && (o->mode & FXM_FIX)) {  /* FIX: the decade */
            rd = XD[X_FRATIO];
            rd.max = 3, rd.names = N_FFIXC;
            xv[id] = (int16_t)(o->coarse & 3u);
            return &rd;
        }
        if (id == X_FAMT && to == fr) {               /* an operator into itself: feedback */
            rd = XD[X_FAMT];
            rd.label = "FB";
            return &rd;
        }
        return &XD[id];
    }
    case X_LYSOUND: {
        uint32_t total;
        xv[X_LYSOUND] = (int16_t)layer_pos(&total);
        xd_sound.max = (int16_t)(total ? total - 1u : 0u);
        *valp = &xv[X_LYSOUND];
        return &xd_sound;
    }
    default:
        if (id >= X_COUNT) {
            *valp = 0;
            return 0;
        }
        *valp = &xv[id];
        break;
    }
    return &XD[id];
}

/* ui_input.c: a value of the synth's pages was turned to *vp */
UI_SMALL static void syn_changed(uint32_t id, int16_t *vp)
{
    switch (id) {
    case X_LYSOUND:
        layer_load((uint32_t)*vp);
        break;
    case X_LYMODE:
        trk_all_off1(&trk[1]);                        /* (its notes came by another way: let them go) */
        ui_say("MODE ", N_LYMODE[clamp(*vp, 0, LY_MULTI)]);
        break;
    case X_LYSWAP:
        if (*vp) {
            *vp = 0;
            layer_swap();
            ui_message("SOUNDS SWAPPED");
        }
        break;
    case X_MORPH:
        if (!syn_a.ok || !syn_b.ok)
            ui_message("SET A AND B FIRST");
        else if (syn_a.eng != syn_b.eng && (*vp < 64) != (trk[0].eng_req == syn_a.eng))
            ui_message("A / B: TWO ENGINES");
        syn_morph_apply(*vp);
        break;
    case X_SETA:
    case X_SETB:
        if (*vp) {
            *vp = 0;
            snap_take(id == X_SETA ? &syn_a : &syn_b, &trk[0]);
            xv[X_MORPH] = id == X_SETA ? 0 : 127;
            ui_message(id == X_SETA ? "A = THIS SOUND" : "B = THIS SOUND");
        }
        break;
    case X_MSRC:                                      /* the place in the panel's order: the code stored */
    case X_MDST:                                      /* (DESTINATION, among the engine's: mx_dst_list) */
        TSEL->p[mx_pid((uint32_t)(clamp(xv[X_MSLOT], 1, (int32_t)MX_SLOTS) - 1)) + (id == X_MDST)] =
            (int16_t)(id == X_MDST ? mx_dst_live_ord[clamp(*vp, 0, (int32_t)mx_dst_list(TSEL) - 1)]
                                   : MX_SRC_ORD[clamp(*vp, 0, MS_NSRC - 1)]);
        ui.force = 1;
        break;
    case X_MCLR:                                      /* MATRIX 2 > CLEAR: one detent arms, a second one
                                                       * within ~1.5 s clears (as LOAD / SAVE); UNDO brings it back */
        if (*vp) {
            *vp = 0;
            if (ui.arm != MX_ARM) {
                ui.arm = MX_ARM;
                ui.arm_t = 90;
                ui_say("AGAIN: ", "CLEAR");
                break;
            }
            ui.arm = 0;
            hist_commit();
            {
                uint32_t k;
                for (k = 0; k < MX_SLOTS; k++)
                    TSEL->p[mx_pid(k)] = TSEL->p[mx_pid(k) + 1u] = TSEL->p[mx_pid(k) + 2u] = 0;
            }
            ui_message("MATRIX CLEARED");
            ui.force = 1;
        }
        break;
    case X_MSLOT:
    case X_FOP:
    case X_FTO:
    case X_FFROM:
    case X_QLANE:
    case X_QSTEP:
        ui.force = 1;
        break;
    case X_QVAL:
        TSEL->ms[clamp(xv[X_QLANE], 1, 4) - 1][clamp(xv[X_QSTEP], 1, 16) - 1] = (int8_t)clamp(*vp, -8, 7);
        ui.force = 1;
        break;
    case X_TSYS:
    case X_TROOT:
    case X_TA4:                                       /* (0.9) the TUNING: the table again, saved with the settings */
        if (id == X_TSYS)
            tune_cfg.sys = (uint8_t)clamp(*vp, 0, TU_N - 1);
        else if (id == X_TROOT)
            tune_cfg.root = (uint8_t)clamp(*vp, 0, 11);
        else
            tune_cfg.a4 = (int8_t)clamp(*vp - 440, -40, 40);
        tune_build();
        settings_later = 1;
        break;
    case X_MIN:                                       /* (1.0) GLO > MIDI: the instrument's channels, kept with the settings */
    case X_MOUT:
        if (id == X_MIN) {
            midi_in_ch = (uint8_t)clamp(*vp, 0, 16);
            panic_req |= 1u;                          /* (notes held on the old channel(s): none stuck) */
        } else {
            midi_out_ch = (uint8_t)(clamp(*vp, 1, 16) - 1);
        }
        settings_later = 1;
        break;
    case X_MBEND:                                     /* (1.0) GLO > MIDI, MIDI 2: kept with the settings */
        midi_bend = (uint8_t)clamp(*vp, 0, 24);
        settings_later = 1;
        break;
    case X_MLOCAL:
        midi_local_off = !*vp;
        panic_req |= 1u;                              /* (a key held: none stuck) */
        settings_later = 1;
        break;
    case X_MPROG:
        midi_pc_off = !*vp;
        settings_later = 1;
        break;
    case X_MCCO:
        midi_cc_out = *vp != 0;
        settings_later = 1;
        break;
    case X_MCCA:                                      /* GLO > MIDI 3: kept with the settings */
    case X_MCCB: {
        uint8_t *cc = id == X_MCCA ? &midi_cca : &midi_ccb, n = (uint8_t)clamp(*vp, 0, 127);
        uint32_t p;
        if (n != *cc)                                 /* another CC: the last value of the old one goes (to 0, smoothed) */
            for (p = 0; p < NPART; p++) {
                if (id == X_MCCA)
                    part_ctl[p].cca = 0;
                else
                    part_ctl[p].ccb = 0;
            }
        *cc = n;
        settings_later = 1;
        break;
    }
    case X_MSUS:                                      /* (a pedal down then: its coming up still lets go, seq.c) */
        midi_sus_patch = *vp != 0;
        settings_later = 1;
        break;
    case X_MBRTH:
        midi_breath = (uint8_t)clamp(*vp, 0, 2);
        part_ctl[0].exoff = 0;                        /* (full until the first breath; OFF: no cut left behind) */
        settings_later = 1;
        break;
    case X_KTOUCH: case X_ATSENS: case X_ATOUT: case X_ATCURVE: case X_ATOFS: case X_ATATK: case X_ATREL: case X_LEVER:
    case X_LTIME: case X_RIBRNG: case X_MREC: case X_RSTEPS: case X_RSPRING: case X_RDELAY: case X_RRET:
    case X_KON: case X_KHIT: case X_KVEL: case X_KRISE: case X_KFALL: case X_KHOLD: case X_KSWELL: case X_KCEIL:
    case X_PDEAD: case X_PROOM: case X_PTOP: case X_PSETL: case X_PRISE: case X_PFALL: case X_PSHAPE: case X_KDELAY:
    case X_KSHAPE:
        touch_set(id, *vp);                           /* (Stallion) GLO > TOUCH: kept with the settings */
        settings_later = 1;
        break;
    case X_TCAL:
        if (*vp) {
            *vp = 0;
            touch_cal_open();
        }
        break;
    case X_QFILL:
        mseq_fill(TSEL, (uint32_t)clamp(xv[X_QLANE], 1, 4) - 1u, (uint32_t)clamp(*vp, 0, QF_N - 1));
        ui.force = 1;
        break;
    case X_FWAVE: case X_FMODE: case X_FRATIO: case X_FFINE: case X_FLEVEL: case X_FVEL: case X_FKSC: case X_FATK:
    case X_FDEC: case X_FSUS: case X_FREL: case X_FAMT: case X_FOUT: case X_FDTN: case X_FFIX: case X_FPEG:
    case X_FPTIM: {                                   /* FMX: into the part's patch (fmx_block rebuilds its own) */
        uint32_t tr = song.sel % NPART;
        fmx_patch_t *p = &fmx_cur[tr];
        fmx_op_t *o = &p->op[clamp(xv[X_FOP], 1, 6) - 1];
        uint32_t to = (uint32_t)clamp(xv[X_FTO], 1, 6) - 1u, fr = (uint32_t)clamp(xv[X_FFROM], 1, 6) - 1u;
        uint8_t u = (uint8_t)clamp(*vp, 0, 127);
        switch (id) {
        case X_FWAVE: o->wave = u; break;
        case X_FMODE: o->mode = (uint8_t)((o->mode & FXM_FIX) | (u % FXM_N)); break;
        case X_FRATIO: o->coarse = u; break;
        case X_FFINE: o->fine = u; break;
        case X_FLEVEL: o->level = u; break;
        case X_FVEL: o->vel = u; break;
        case X_FKSC: o->ksc = u; break;
        case X_FATK: case X_FDEC: case X_FSUS: case X_FREL: o->adsr[id - X_FATK] = u; break;
        case X_FAMT: p->mod[to][fr] = u; p->alg = 0; break;
        case X_FOUT: p->out[to] = u; p->alg = 0; break;
        case X_FDTN: o->dtn = (uint8_t)clamp(*vp + 7, 0, 14); break;
        case X_FFIX: o->mode = (uint8_t)((o->mode & 3u) | (*vp ? FXM_FIX : 0u)); o->coarse &= *vp ? 3u : 31u; break;
        case X_FPEG: p->peg = (uint8_t)clamp(*vp + 64, 0, 127); break;
        default: p->ptime = u; break;
        }
        if ((id == X_FAMT || id == X_FOUT) && TSEL->p[P_E1]) {   /* the routing changed: the template's off (ALG
                                                               * PAT), what it filled kept in the patch */
            uint32_t a = (uint32_t)TSEL->p[P_E1] - 1u, d, s;
            for (d = 0; d < FMX_NOP; d++) {
                for (s = 0; s < FMX_NOP; s++)
                    if (!((FMX_ALG[a][d] >> s) & 1u))
                        p->mod[d][s] = d == to && s == fr ? p->mod[d][s] : 0u;
                    else if (!p->mod[d][s] && !(d == to && s == fr))
                        p->mod[d][s] = s == d ? 60u : 100u;
                if (!(d == to && id == X_FOUT))
                    p->out[d] = (FMX_ALG[a][6] >> d) & 1u ? (p->out[d] ? p->out[d] : 100u) : 0u;
            }
            TSEL->p[P_E1] = 0;
        }
        fmx_dirty[tr] = 1;
        break;
    }
    case X_FSTORE:
        if (*vp) {
            *vp = 0;
            if (song.playing)
                ui_message("STOP THE SONG FIRST");
            else if (!fmx_bank_store)
                ui_message("NO FLASH");
            else if (fmx_bank_store((uint32_t)clamp(xv[X_FSLOT], 1, FM6_BANK_N) - 1u,
                                    (const uint8_t *)&fmx_cur[song.sel % NPART]))
                ui_message("STORE ERROR");
            else {
                char m[16];
                TSEL->p[P_E0] = (int16_t)(FMX_NFACTORY + (uint32_t)clamp(xv[X_FSLOT], 1, FM6_BANK_N) - 1u);
                fmx_slot_seen[song.sel % NPART] = (int16_t)(TSEL->p[P_E0] + 1);   /* (its patch: this one) */
                str_cpy(m, "STORED B", sizeof m);
                fmx_reload &= (uint8_t)~(1u << (song.sel % NPART));
                fmt_int(m + str_len(m), xv[X_FSLOT]);
                ui_message(m);
            }
        }
        break;
    case X_RNDGO:
        if (*vp) {
            *vp = 0;
            hist_commit();                            /* (0.7: the sound before it, a step: UNDO goes back) */
            syn_randomize();
            ui_message("RANDOM");
        }
        break;
    case X_HPOS: {                                    /* 0.7: left back, right forward */
        int32_t d = *vp - (int32_t)-(int32_t)hist_r;
        if (d)
            hist_step(d);
        ui_message(hist_r ? "UNDO" : "LATEST");
        break;
    }
    case X_HCMP:
        hist_compare(*vp != 0);
        break;
    case X_RNDBACK:
        if (*vp) {
            *vp = 0;
            if (syn_back.ok) {
                snap_put(&trk[0], &syn_back);
                ui_message("BACK");
            }
        }
        break;
    default:
        break;
    }
}

/* once a UI frame: MIDI CC 20 moves MORPH */
UI_SMALL static void syn_tick(void)
{
    uint32_t k;
    for (k = 0; k < NPART; k++)                       /* no part keeps a cable its engine does not use,
                                                       * whatever brought it (before the history sees the sound) */
        mx_prune(&trk[k]);
    hist_poll();                                      /* (0.7) the sound's history */
    int16_t r = syn_morph_req;
    uint32_t pc = midi_pc_req, total, b[FAV_N];
    if (pc) {                                         /* (1.0) a MIDI program change (bank: CC 0): the sound at that
                                                       * place in the PRESETS list (no favourites: the factory sounds
                                                       * in order, then the user presets) */
        midi_pc_req = 0;
        base_pos(&total);
        if (pc - 1u < total)
            preset_go(fav_list(b) + pc - 1u);
    }
    if (r >= 0) {
        syn_morph_req = -1;
        xv[X_MORPH] = r;
        syn_morph_apply(r);
    }
}

/* the graph of the LAYER pages: the layer's sound and the keyboard (the split) */
UI_SMALL static void graph_layer(void)
{
    const track_t *l = &trk[1];
    const engine_t *e = ENGINES[l->eng_req % NENGINES];
    char nm[16];
    uint32_t m = layer_mode(), i;
    int32_t sp = l->p[P_LYSPLIT] - 4, x;
    if (l->user && l->user <= UP_SLOTS && up_used(l->user - 1u))
        up_name(l->user - 1u, nm);
    else
        str_cpy(nm, e->npresets ? e->presets[l->preset % e->npresets].name : e->name, sizeof nm);
    if (m == LY_MULTI) {                              /* 0.6: two parts, the layer on its channel */
        char c[12];
        str_cpy(c, "MULTI  CH ", sizeof c);
        fmt_int(c + str_len(c), (int32_t)layer_ch());
        cv_text(8, 4, &FONT_S, c, C_GRAY);
    } else {
        cv_text(8, 4, &FONT_S, m ? "LAYER" : "LAYER OFF", m ? C_GRAY : C_DIM);
    }
    cv_text(8, 22, &FONT_S, nm, m ? C_WHITE : C_GRAY);
    cv_text(8, 40, &FONT_S, e->name, C_DIM);
    for (i = 0; i < 61u; i++) {                       /* the keys C2..C7 (36..96): where each sound plays */
        uint32_t note = 36u + i;
        int on2 = m == LY_LAYER || m == LY_MULTI || (m == LY_SPLIT && (int32_t)note < sp);
        int on1 = m != LY_SPLIT || (int32_t)note >= sp;
        x = 40 + (int32_t)i * 3;
        cv_rect(x, 62, 2, 7, on1 ? C_HI : C_LINE);
        cv_rect(x, 73, 2, 7, on2 ? TE_COL[1] : C_LINE);
        if (note % 12u == 0u)
            cv_rect(x, 82, 1, 3, C_DIM);
    }
    cv_text(8, 58, &FONT_S, "1", C_HI);
    cv_text(24, 69, &FONT_S, "2", m ? TE_COL[1] : C_DIM);
}
UI_SMALL static void graph_morph(void)
{
    static const char *const AB[2] = {"A", "B"};
    uint32_t k;
    int32_t w = xv[X_MORPH] * 200 / 127;
    for (k = 0; k < 2u; k++) {
        const syn_snap_t *s = k ? &syn_b : &syn_a;
        char nm[16];
        const engine_t *e = ENGINES[s->eng % NENGINES];
        int32_t y = 6 + (int32_t)k * 22;
        cv_text(8, y, &FONT_S, AB[k], s->ok ? C_WHITE : C_DIM);
        if (s->ok)
            str_cpy(nm, e->npresets ? e->presets[s->preset % e->npresets].name : e->name, sizeof nm);
        else
            str_cpy(nm, "--", sizeof nm);
        cv_text(28, y, &FONT_S, nm, s->ok ? C_GRAY : C_DIM);
    }
    cv_rect(20, 62, 200, 1, C_LINE);
    cv_rect(20 + w - 1, 56, 3, 13, C_WHITE);
    cv_text(8, 56, &FONT_S, "A", C_DIM);
    cv_text(226, 56, &FONT_S, "B", C_DIM);
}
/* 0.7, the graph of the UNDO page: the sound's steps, the newest on top, the one playing lit */
/* the AT shaping's pages (the SMK-37's AFTERTOUCH, the FM-1's TOUCH 2, 5, 6): a pressure in (across) as
 * the sound gets it (up: AT CURVE, OFFS), and a dot on it at the most a key or pad presses now */
UI_SMALL static uint32_t atc_live(void)
{
    uint32_t k, m = 0;
    for (k = 0; k < KP_NKEY; k++)
        if (kt_p[k] > m)
            m = kt_p[k];
#if BOARD_SMK37
    if (smk_ext_at_mx > m)
        m = smk_ext_at_mx;
#endif
    return m > 127u ? 127u : m;
}
UI_SMALL static uint32_t atc_sig(void)
{
    return touch_cfg.at_curve * 7u + (uint32_t)(touch_cfg.at_offset + 8) * 131u + atc_live() * 7919u + pat_cfg.shape * 104729u +
           pat_cfg.dead * 1299709u;
}
UI_SMALL static void graph_atc(uint16_t c)
{
    uint32_t i, m = atc_live();
    int32_t px = 6, py = 90, y;
    cv_line(6, 90, 232, 90, TE_G1);
    cv_line(6, 8, 6, 90, TE_G1);
    for (i = 0; i <= 127u; i++) {
        int32_t x = 6 + (int32_t)(i * 226u / 127u);
        y = 90 - (int32_t)(at_lut[i] * 82u / 127u);
        cv_line(px, py, x, y, c);
        px = x;
        py = y;
    }
    if (m) {
        int32_t x = 6 + (int32_t)(m * 226u / 127u);
        y = 90 - (int32_t)(at_lut[m] * 82u / 127u);
        cv_line(x, 8, x, 90, TE_G2);
        cv_rect(x - 2, y - 2, 5, 5, C_WHITE);
    }
    cv_text(232 - text_w(&FONT_S, "PRESSURE >"), 72, &FONT_S, "PRESSURE >", C_DIM);
}
#if BOARD_SMK37
/* GLO > KEY AT: the made-up pressure of a held key over time, as the sound gets it (AT CURVE, OFFS too),
 * for a soft (dim), a medium (grey) and a full hit (the track's colour); the time across, bottom right */
UI_SMALL static uint32_t kat_sig(void)
{
    const kat_cfg_t *k = &kat_cfg;
    const pat_cfg_t *q = &pat_cfg;
    return k->on + k->hit * 3u + k->vel * 131u + k->hold * 1031u + k->ceil * 7919u + k->rise * 104729u +
           k->fall * 1299709u + k->swell * 15485863u + touch_cfg.at_curve * 7u + (uint32_t)(touch_cfg.at_offset + 8) * 11u +
           (q->dead + q->shape * 251u + q->kshape * 509u + q->room * 1021u + q->top * 2039u + q->settle * 4093u +
            q->rise * 8191u + q->fall * 16381u + q->kdelay * 32749u) * 2654435761u +
           (smk_live_id ? (smk_live_id * 977u + smk_live_vel * 131u + (fm1_ms - smk_live_t0) / 30u) * 40503u : 0u);   /* (the dot) */
}
UI_SMALL static void graph_kat(uint16_t c)
{
    static const uint8_t VEL[3] = {40, 80, 127};
    const kat_cfg_t *k = &kat_cfg;
    uint32_t span = k->rise + k->fall, j, x;
    char t[12];
    if (pat_cfg.kdelay + k->swell * 2u > span)
        span = pat_cfg.kdelay + k->swell * 2u;      /* (a soft hit swells over up to twice SWELL, after DELAY) */
    span = span + span / 8u;
    span = span < 1000u ? 1000u : span > 20000u ? 20000u : span;
    cv_line(6, 90, 232, 90, TE_G1);
    cv_line(6, 8, 6, 90, TE_G1);
    if (!k->on) {
        cv_text((240 - text_w(&FONT_S, "KEY AT OFF")) / 2, 40, &FONT_S, "KEY AT OFF", C_DIM);
        return;
    }
    for (j = 0; j < 3u; j++) {
        int32_t px = 6, py = 90;
        uint16_t col = j == 2u ? c : j ? C_GRAY : C_DIM;
        for (x = 0; x <= 226u; x += 2u) {
            uint32_t ms = span * x / 226u;
            uint32_t a = at_lut[smk_kat(k->hit, k->vel, k->hold, k->ceil, k->rise, k->fall, k->swell, pat_cfg.kdelay,
                                        pat_cfg.kshape, VEL[j], ms) & 127u];
            int32_t y = 90 - (int32_t)(a * 82u / 127u);
            cv_line(px, py, 6 + (int32_t)x, y, col);
            px = 6 + (int32_t)x;
            py = y;
        }
    }
    if (smk_live_id) {                              /* a key held: its own curve white, a dot where it is now */
        uint32_t now = fm1_ms - smk_live_t0, lx = now >= span ? 226u : now * 226u / span, a;
        int32_t px = 6, py = 90, y;
        for (x = 0; x <= lx; x += 2u) {
            uint32_t ms = span * x / 226u;
            a = at_lut[smk_kat(k->hit, k->vel, k->hold, k->ceil, k->rise, k->fall, k->swell, pat_cfg.kdelay,
                               pat_cfg.kshape, smk_live_vel, ms) & 127u];
            y = 90 - (int32_t)(a * 82u / 127u);
            cv_line(px, py, 6 + (int32_t)x, y, C_WHITE);
            px = 6 + (int32_t)x;
            py = y;
        }
        a = at_lut[smk_kat(k->hit, k->vel, k->hold, k->ceil, k->rise, k->fall, k->swell, pat_cfg.kdelay, pat_cfg.kshape,
                           smk_live_vel, now) & 127u];
        y = 90 - (int32_t)(a * 82u / 127u);
        cv_line(6 + (int32_t)lx, 8, 6 + (int32_t)lx, 90, TE_G2);
        cv_rect(6 + (int32_t)lx - 2, y - 2, 5, 5, C_WHITE);
    }
    fmt_int(t, (int32_t)(span / 1000u));            /* "2.5 S" */
    j = str_len(t);
    t[j] = '.';
    t[j + 1u] = (char)('0' + span % 1000u / 100u);
    str_cpy(t + j + 2u, " S", sizeof t - j - 2u);
    cv_text(232 - text_w(&FONT_S, t), 72, &FONT_S, t, C_DIM);
}
#endif
#if BOARD_SMK37
/* GLO > PAD AT, PAD AT 2: what a squeeze gives, as the sound gets it (AT CURVE, OFFS too), from a press
 * settled at 650 on a pad that has not learned a top (TOP): the dead zone grey, then the squeeze to the top */
UI_SMALL static void graph_pat(uint16_t c)
{
    const pat_cfg_t *q = &pat_cfg;
    uint32_t top = q->top, base = 650u + q->room > top ? (top > q->room ? top - q->room : 0u) : 650u, dead, x, d0;
    int32_t px = 6, py = 90;
    char t[16];
    dead = (top - base) / 3u;
    d0 = q->dead * 5u / 12u;
    dead = dead > q->dead ? q->dead : dead < d0 ? d0 : dead;
    cv_line(6, 90, 232, 90, TE_G1);
    cv_line(6, 8, 6, 90, TE_G1);
    for (x = 0; x <= 226u; x += 2u) {               /* from the zero to the top, across */
        uint32_t h = base + (top - base) * x / 226u, a = 0, s;
        if (h > base + dead) {
            uint32_t span = top > base + dead ? top - base - dead : 1u, dd = h - base - dead;
            s = dd >= span ? 1024u : dd * 1024u / span;
            a = at_lut[((smk_shape(s, q->shape) * 127u + 512u) >> 10) & 127u];
        }
        {
            int32_t y = 90 - (int32_t)(a * 82u / 127u);
            cv_line(px, py, 6 + (int32_t)x, y, h <= base + dead ? TE_G2 : c);
            px = 6 + (int32_t)x;
            py = y;
        }
    }
    str_cpy(t, "SQUEEZE >", sizeof t);
    cv_text(232 - text_w(&FONT_S, t), 72, &FONT_S, t, C_DIM);
}
#endif
UI_SMALL static void graph_hist(void)
{
    uint32_t rows = 6u, k, total = hist_n + hist_r + 1u, now = hist_n;   /* index 0 the oldest; now: hist_cur */
    uint32_t top = total - 1u;
    if (hist_cmp_on) {
        cv_text(8, 6, &FONT_S, "COMPARING:", C_GRAY);
        cv_text(8, 24, &FONT_S, "THE SOUND AS LOADED", C_WHITE);
        cv_text(8, 50, &FONT_S, "CMP EDIT: YOUR EDITS BACK", C_DIM);
        return;
    }
    if (top >= rows && now + rows / 2u < top)
        top = now + rows / 2u < rows - 1u ? rows - 1u : now + rows / 2u;
    for (k = 0; k < rows && k <= top; k++) {
        uint32_t i = top - k, y = 2u + k * 15u;
        const hist_t *h, *older;
        char lab[15], tag[6];                         /* (14 letters: EDITED stays clear on the right) */
        if (i == now) {
            h = &hist_cur;
        } else if (i < now) {
            h = &hist[(hist_head + HIST_N - (now - i)) % HIST_N];
        } else {
            h = &hist[(hist_head + (i - now - 1u)) % HIST_N];
        }
        if (!i) {
            str_cpy(lab, "(OLDEST)", sizeof lab);
        } else {
            uint32_t j = i - 1u;
            older = j == now ? &hist_cur : j < now ? &hist[(hist_head + HIST_N - (now - j)) % HIST_N]
                                                   : &hist[(hist_head + (j - now - 1u)) % HIST_N];
            hist_label(older, h, lab, sizeof lab);
        }
        if (i == now)
            str_cpy(tag, "NOW", sizeof tag);
        else {
            tag[0] = i < now ? '-' : '+';
            fmt_int(tag + 1, (int32_t)(i < now ? now - i : i - now));
        }
        if (i == now)
            cv_rect(4, (int32_t)y + 6, 3, 3, C_WHITE);
        cv_text(12, (int32_t)y, &FONT_S, tag, i == now ? C_WHITE : C_DIM);
        cv_text(52, (int32_t)y, &FONT_S, lab, i == now ? C_WHITE : i < now ? C_GRAY : C_DIM);
    }
    if (hist_edited)
        cv_text(184, 2, &FONT_S, "EDITED", C_AMB);
}

/* the graph of the MATRIX page: the six slots, source > destination and amount, the one turned lit */
/* 0.8: a SHAPE into lane l of the MOD SEQ, over its LEN steps (SHIFT: one step to the left, INVRT: upside down) */
UI_SMALL static void mseq_fill(track_t *t, uint32_t l, uint32_t f)
{
    uint32_t i, len = (uint32_t)clamp(t->p[P_MSLEN], 1, 16);
    int8_t *q = t->ms[l % MSEQ_LANES], first = q[0];
    for (i = 0; i < len; i++) {
        int32_t v = q[i];
        switch (f) {
        case QF_UP: v = len > 1u ? -8 + (int32_t)(i * 15u / (len - 1u)) : 0; break;
        case QF_DOWN: v = len > 1u ? 7 - (int32_t)(i * 15u / (len - 1u)) : 0; break;
        case QF_TRI: {
            uint32_t h = (i * 2u * 15u + len / 2u) / len;   /* 0 .. 30 and back */
            v = (int32_t)(h <= 15u ? h : 30u - h) - 8;
            break;
        }
        case QF_SINE: v = (osc_sine(i * (0xFFFFFFFFu / len)) * 7 + 16383) / 32767; break;
        case QF_SQR: v = i < len / 2u ? 7 : -8; break;
        case QF_RND: v = (int32_t)(rng() >> 28) - 8; break;
        case QF_FLAT: v = 0; break;
        case QF_INV: v = clamp(-v, -8, 7); break;
        case QF_SHIFT: v = i + 1u < len ? q[i + 1u] : first; break;
        default: break;
        }
        q[i] = (int8_t)clamp(v, -8, 7);
    }
}
/* the MOD SEQ's pages: the selected lane large (the step being edited white, the one playing lit, past LEN dim), the
 * four lanes small below it */
UI_SMALL static void graph_mseq(void)
{
    const track_t *t = TSEL;
    uint32_t i, l, lane = (uint32_t)clamp(xv[X_QLANE], 1, 4) - 1u, st = (uint32_t)clamp(xv[X_QSTEP], 1, 16) - 1u;
    uint32_t len = (uint32_t)clamp(t->p[P_MSLEN], 1, 16), play = t->ms_step;
    int edit = cur_page()->id[0] == X_QLANE;
    cv_rect(6, 30, 228, 1, C_LINE);
    for (i = 0; i < MSEQ_STEPS; i++) {
        int32_t v = t->ms[lane][i], x = 8 + (int32_t)i * 14, h = v * 3;
        uint16_t c = i >= len ? C_DIM : i == play ? C_HI : C_GRAY;
        if (edit && i == st)
            c = C_WHITE;
        if (h >= 0)
            cv_rect(x, 30 - h, 11, h ? h : 1, c);
        else
            cv_rect(x, 31, 11, -h, c);
        if (edit && i == st)
            cv_rect(x, 56, 11, 2, C_WHITE);
    }
    for (l = 0; l < MSEQ_LANES; l++) {
        int32_t x0 = 6 + (int32_t)l * 58;
        char b[3] = {'S', (char)('1' + l), 0};
        cv_text(x0, 62, &FONT_S, b, l == lane ? C_HI : C_DIM);
        for (i = 0; i < MSEQ_STEPS; i++) {
            int32_t v = t->ms[l][i], x = x0 + 16 + (int32_t)i * 2;
            cv_rect(x, v >= 0 ? 70 - v : 71, 1, v >= 0 ? (v ? v : 1) : -v, i >= len ? C_DIM : l == lane ? C_HI : C_GRAY);
        }
    }
}
/* the MATRIX page's header line into line: "SRC > DST AMT" in FONT_L within 236 px, drawn at most a pixel closer a
 * letter; too wide still: "SRC>DST AMT", then the destination's name a letter shorter at a time (one letter at least).
 * Returns the pixels each letter is drawn closer (cv_tight): 0 or 1 */
UI_SMALL static uint32_t mx_head(char *line, uint32_t size, const char *src, const char *dst, const char *val)
{
    char d[12];
    uint32_t step, n = 0;
    int32_t w = 0;
    str_cpy(d, dst, sizeof d);
    for (step = 0; step < 12u; step++) {
        if (step >= 2u && d[1])
            d[str_len(d) - 1u] = 0;
        str_cpy(line, src, size);
        n = (uint32_t)str_len(line);
        str_cpy(line + n, step ? ">" : " > ", size - n);
        n = (uint32_t)str_len(line);
        str_cpy(line + n, d, size - n);
        n = (uint32_t)str_len(line);
        str_cpy(line + n, " ", size - n);
        str_cpy(line + n + 1u, val, size - n - 1u);
        n = (uint32_t)str_len(line), w = text_w(&FONT_L, line);
        if (w <= 236 + (int32_t)n)                    /* (a pixel closer each at most) */
            break;
    }
    return w > 236 ? 1u : 0u;
}
/* the MATRIX (and MATRIX 2) page, drawn from the strip's top: the selected cable on one line in FONT_L,
 * "LFO1 > CUT +62", in its source's signal colour (mx_col); the six slots of it (1..6 .. 19..24) below in FONT_S, the
 * number and source in the signal colour, the selected one marked and white, slots that do not play dim; the amount as
 * a bar from the middle; the last line what the page should say (mx_notes: MARE's normals, the layer's ARP) */
UI_SMALL static void graph_matrix(void)
{
    const track_t *t = TSEL;
    const char *const *dn = track_desc(t, P_M1DST)->names;   /* (the engine's names for E1..E8, every slot) */
    uint32_t k, sel = (uint32_t)clamp(xv[X_MSLOT], 1, (int32_t)MX_SLOTS) - 1u, k0 = sel / 6u * 6u;   /* (the six of the
                                                         * selected one: 1..6, 7..12, 13..18 or 19..24) */
    char line[28], val[12];
    const char *unit = 0;
    int note = mx_note_shown(t);
    for (k = k0; k <= k0 + 6u; k++) {                 /* (k0 + 6: the selected one again, as the header) */
        uint32_t s = k < k0 + 6u ? k : sel, id = mx_pid(s);
        int32_t sr = clamp(t->p[id], 0, MS_NSRC - 1), ds = clamp(t->p[id + 1u], 0, MT_COUNT - 1), a = t->p[id + 2u];
        int on = mx_live(t, s);
        uint16_t sc = on ? mx_col((uint32_t)sr) : C_DIM, c = !on ? C_DIM : s == sel ? C_WHITE : C_GRAY;
        param_format(&TP[id + 2u], a, val, &unit);
        if (k == k0 + 6u) {                           /* the header: "SRC > DST AMT" (narrower when it must) */
            cv_tight = (uint8_t)mx_head(line, sizeof line, N_MSRC[sr], dn[ds], val);
            cv_text(2, -2, &FONT_L, line, sr ? mx_col((uint32_t)sr) : C_GRAY);
            cv_tight = 0;
            break;
        }
        {
            char nb[3] = {s >= 9u ? (char)('0' + (s + 1u) / 10u) : (char)('1' + s), s >= 9u ? (char)('0' + (s + 1u) % 10u) : 0, 0};
            int32_t y = 26 + (int32_t)(k - k0) * 13;
            if (s == sel)
                cv_rect(0, y + 2, 5, 11, C_WHITE);    /* (the selected one: a mark 5 px wide, white words) */
            cv_text(s >= 9u ? 6 : 14, y, &FONT_S, nb, sc);   /* (10..24 right-aligned) */
            cv_text(32, y, &FONT_S, N_MSRC[sr], sc);
            cv_text(84, y, &FONT_S, ">", C_DIM);
            cv_text(98, y, &FONT_S, dn[ds], c);
            cv_text(166, y, &FONT_S, val, c);       /* (-100 clear of the bar) */
            if (on) {                                 /* the amount as a bar from the middle (x 220: -100 .. +98 as
                                                         * 18 .. 17 px, inside the screen, clear of the value "-100") */
                int32_t w = a * 18 / 64;
                cv_rect(w < 0 ? 220 + w : 220, y + 5, w < 0 ? -w : (w ? w : 1), 4, s == sel ? C_WHITE : C_GRAY);
            }
            cv_rect(220, y + 3, 1, 9, C_LINE);
        }
    }
    if (note >= 0)
        cv_text((240 - text_w(&FONT_S, MX_NOTE[note])) / 2, 106, &FONT_S, MX_NOTE[note], C_HI);
}
/* FMX's pages: the routing as a grid (rows: TO, columns: FROM; a square's size its amount; the diagonal feedback),
 * the output column, the operators' waves and ratios; the operator / connection being edited white */
UI_SMALL static void graph_fmx(void)
{
    const fmx_patch_t *p = &fmx_cur[song.sel % NPART];
    const fmx_eff_t *f = &fmx_eff[song.sel % NPART];
    uint32_t d, s, op = (uint32_t)clamp(xv[X_FOP], 1, 6) - 1u;
    uint32_t to = (uint32_t)clamp(xv[X_FTO], 1, 6) - 1u, fr = (uint32_t)clamp(xv[X_FFROM], 1, 6) - 1u;
    int route = cur_page()->id[0] == X_FTO;
    char b[8];
    for (s = 0; s < FMX_NOP; s++) {
        b[0] = (char)('1' + s), b[1] = 0;
        cv_text(26 + (int32_t)s * 12, 0, &FONT_S, b, route && s == fr ? C_WHITE : C_DIM);
    }
    cv_text(102, 0, &FONT_S, "OUT", C_DIM);
    for (d = 0; d < FMX_NOP; d++) {
        int32_t y = 13 + (int32_t)d * 12, a;
        const fmx_op_t *o = &p->op[d];
        uint16_t rc = (route ? d == to : d == op) ? C_WHITE : C_GRAY;
        b[0] = (char)('1' + d), b[1] = 0;
        cv_text(8, y - 1, &FONT_S, b, rc);
        for (s = 0; s < FMX_NOP; s++) {
            int32_t x = 26 + (int32_t)s * 12;
            uint32_t j, used = 0;
            a = 0;
            for (j = 0; j < f->n[d]; j++)             /* (what plays: the template if ALG is on) */
                if (f->src[d][j] == s)
                    used = 1, a = f->amt[d][j];
            cv_rect(x + 4, y + 4, 1, 1, C_LINE);
            if (used) {
                int32_t w = 2 + clamp(a, 0, 127) * 7 / 127;
                cv_rect(x + 5 - w / 2, y + 5 - w / 2, w, w, s == d ? TE_COL[1] : C_HI);
            }
            if (route && d == to && s == fr)
                cv_rect(x, y, 11, 1, C_WHITE), cv_rect(x, y + 10, 11, 1, C_WHITE), cv_rect(x, y, 1, 11, C_WHITE),
                    cv_rect(x + 10, y, 1, 11, C_WHITE);
        }
        a = (f->carriers >> d) & 1u ? f->outg[d] / 258 : 0;
        if (a)
            cv_rect(102, y + 3, 1 + a * 24 / 127, 5, route && d == to ? C_WHITE : C_HI);
        cv_text(136, y - 1, &FONT_S, N_FMX_WAVE[o->wave % FMX_NWAVE], rc);
        str_cpy(b, o->mode & FXM_FIX ? "F" : N_FRATIO[o->coarse & 31u], sizeof b);
        cv_text(186, y - 1, &FONT_S, b, rc);
        cv_text(214, y - 1, &FONT_S, N_FMX_MODE[(o->mode & 3u) % FXM_N], (o->mode & 3u) ? C_HI : C_DIM);
    }
}
UI_SMALL static uint32_t fmx_sig(void)
{
    const uint8_t *b = (const uint8_t *)&fmx_cur[song.sel % NPART];
    uint32_t i, h = 2166136261u;
    for (i = 0; i < sizeof(fmx_patch_t); i++)
        h = (h ^ b[i]) * 16777619u;
    return h ^ (uint32_t)xv[X_FOP] * 31u ^ (uint32_t)xv[X_FTO] * 37u ^ (uint32_t)xv[X_FFROM] * 41u;
}
UI_SMALL static uint32_t syn_sig(void)
{
    const track_t *l = &trk[1];
    return fmx_sig() + (uint32_t)l->eng_req * 131u + l->preset * 7u + l->user * 3u + (uint32_t)l->p[P_LYMODE] * 17u +
           (uint32_t)l->p[P_LYSPLIT] * 257u + (uint32_t)xv[X_MORPH] * 65537u + syn_a.ok * 11u + syn_b.ok * 13u +
           syn_a.eng * 101u + syn_b.eng * 103u + syn_a.preset * 107u + syn_b.preset * 109u + (uint32_t)xv[X_MSLOT] * 6007u +
           (hist_n * 31u + hist_r * 977u + hist_cmp_on * 7919u + hist_edited * 104729u + hist_head) * 2654435761u +
           (cur_page()->graph == GR_MATRIX ? (uint32_t)(mx_note_shown(TSEL) + 2) * 40503u : 0u);   /* (the note turns) */
}
