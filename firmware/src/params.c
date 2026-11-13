/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef UI_SMALL
#define UI_SMALL                                   /* (gfx.c: minsize on the device; a test alone: nothing) */
#endif
/* Parameter descriptors, formatting and the page table. */
static const char *const N_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H", "CHAO"};   /* (append-only: core.h LW_*) */
static const char *const N_AMODE[] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD",   /* (append-only: seq.c AM_*) */
                                       "REPEAT", "DNUP", "UP+8", "CONV", "DIVG", "PINKY", "THUMB", "WALK", "CHORD",
                                       "TURNG",    /* (0.9: a Turing machine over the held notes) */
                                       "INCL"};    /* (Stallion 1.0: up and down, the ends twice; UPDN leaves them once) */
static const char *const N_MOUT[] = {"KEYS", "SEQ"};
static const char *const N_MIN[] = {"NOTES", "CLOCK"};  /* G_ROUTE: MIDI in, notes and clock, or the clock only (seq.c) */    /* G_MIDI: what goes to MIDI OUT (seq.c seq_out_on) */
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T"};
static const char *const N_SDIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1BAR", "2BAR"};   /* core.h div_units */
static const char *const N_DLY[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/8D", "1/16D",
                                     "35MS", "50MS", "75MS", "100MS", "150MS", "200MS", "300MS", "400MS", "500MS", "600MS",
                                     "700MS", "780MS"};          /* core.h dly_units */
static const char *const N_SCALE[] = {"CHR", "MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM",
                                    "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH"};
static const char *const N_ONOFF[] = {"OFF", "ON"};
static const char *const N_QUANT[] = {"OFF", "SNAP", "WHITE"};   /* seq.c kb_map; 1 = SNAP as the old ON */
#if CLIPPER_SYNTH
static const char *const N_VOICE[] = {"POLY", "MONO", "LEG", "PARA", "PARA+", "UNI", "UNI2", "UNI4"};   /* V_POLY ..
                                                                                * V_PUNI4 (PARA, PARA+: MARE's) */
#else
static const char *const N_VOICE[] = {"POLY", "MONO", "LEG", "UNI"};   /* V_POLY .. V_UNISON */
#endif
static const char *const N_GLMODE[] = {"RATE", "TIME", "EXP"};   /* (EXP: a one-pole approach) */
static const char *const N_PRIO[] = {"LAST", "LOW", "HIGH"};
static const char *const N_ALLOC[] = {"ROT", "REUSE", "POLY2"};   /* (POLY2: a new note cuts the notes let go) */
static const char *const N_ORDER[] = {"NOTE", "PLAY"};
static const char *const N_CLICK[] = {"OFF", "REC", "ON"};   /* G_CLOCK is the metronome (seq.c click_tick) */
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
static const char *const N_SYNC[] = {"INT", "USB", "TRS"};   /* G_SYNC: the tempo and PLAY / STOP from (seq.c mclk) */
static const char *const N_GO[] = {"--", "GO"};
static const char *const N_SLCR[] = {"OFF", "GATE", "STUT"};             /* SL_OFF .. SL_STUT (slicer.c) */
static const char *const N_SLDIV[] = {"1/8", "1/16", "1/32", "8T", "16T", "32T"};   /* SL_DEN */
static const char *const N_CHORD[] = {"OFF", "TRIAD", "7TH", "9TH", "SUS4", "POWER"};   /* seq.c CHORD_DEG */
static const char *const N_RTYPE[] = {"ROOM", "PLATE", "HALL", "SHIMR"};   /* (1.0: HALL, SHIMMER) */
static const char *const N_DMODE[] = {"MONO", "PING", "TAPE"};          /* (1.0) G_DMODE */
static const char *const N_E2LOOP[] = {"ONE", "LOOP"};
static const char *const N_FSLOPE[] = {"12", "24", "48"};               /* P_FSLOPE: FS_* */
/* PONY's MODL (params.c track_desc): its OTA first, the stored default (FM_SVF's number), SVF in OTA's place
 * (eng_pony.c pony_model swaps them back): PONY's sounds and the stores before it play OTA, the others keep theirs */
static const char *const N_JFMODEL[] = {"OTA", "LADDR", "SK", "DIODE", "WARM", "NOTCH", "BP", "HP", "SVF"};
static const char *const N_FMODEL2[] = {"OFF", "SVF", "LADDR", "SK", "DIODE", "WARM", "NOTCH", "BP", "HP", "OTA"};   /* P_F2MODEL */
#define N_FMODEL (N_FMODEL2 + 1)                    /* P_FMODEL: FM_* (FILTER 2's list without its OFF) */
static const char *const N_F2ROUTE[] = {"SER", "PAR", "SPLIT"};         /* P_F2ROUTE: F2_* */
static const char *const N_MODDST[] = {"OFF", "VIB", "CUT", "SHP", "WAH", "TREM"};   /* MD_OFF .. MD_TREM */
#if CLIPPER_SYNTH
/* Clipper 0.6: the INSERT's types (fx.c IT_*; SLOOP 2.6's, same order: a SLOOP 2.6 sound keeps its INSERT). Its three
 * values A B C mean what the type says (ins_desc) */
static const char *const N_ITYPE[] = {"OFF", "SOFT", "HARD", "FOLD", "FUZZ", "CRUSH", "PHASR", "FLANG", "CHOR",
                                     "BBD1", "BBD2", "BBD12",   /* (0.8: the PONY chorus, I II I+II, on any sound) */
                                     "PITCH", "PIT2", "PHAS6", "DELAY", "LOCMP",   /* (1.1: the PALOMINO-08's) */
                                     "STDLY", "PPDLY"};   /* (the stereo analog delay, its ping-pong) */
/* CRUSH: BITS 1..16 and RATE (the held sample's rate) over the range in 16 even parts (0-terminated) */
static const char *const N_IBITS[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
                                      "16", 0};
static const char *const N_IRATE[] = {"689", "919", "1.1k", "1.4k", "1.8k", "2.2k", "2.8k", "3.7k", "4.4k", "5.5k",
                                      "7.4k", "8.8k", "11k", "15k", "22k", "44k", 0};
/* (1.1) PITCH, PIT2: semitones -24..+24 (49 even parts), FINE cents -50..+50 in 5s (21); DELAY: TIME ms (32: fx.c DI_MS) */
static const char *const N_ISEMI[] = {"-24", "-23", "-22", "-21", "-20", "-19", "-18", "-17", "-16", "-15", "-14", "-13",
                                      "-12", "-11", "-10", "-9", "-8", "-7", "-6", "-5", "-4", "-3", "-2", "-1", "0", "+1",
                                      "+2", "+3", "+4", "+5", "+6", "+7", "+8", "+9", "+10", "+11", "+12", "+13", "+14",
                                      "+15", "+16", "+17", "+18", "+19", "+20", "+21", "+22", "+23", "+24", 0};
static const char *const N_ICENT[] = {"-50", "-45", "-40", "-35", "-30", "-25", "-20", "-15", "-10", "-5", "0", "+5",
                                      "+10", "+15", "+20", "+25", "+30", "+35", "+40", "+45", "+50", 0};
static const char *const N_IDMS[] = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "12", "14", "16", "18", "20", "22",
                                     "25", "28", "31", "34", "37", "41", "45", "49", "53", "58", "63", "68", "73", "78",
                                     "83", "88", "92", 0};
/* STDLY / PPDLY: TIME 35 .. 780 ms over 32 even parts (fx.c SD_MS), SPACE line 2 against line 1 in
 * sixteenths of an octave (-8 .. +8: x 0.71 .. x 1.41) */
static const char *const N_ISDMS[] = {"35", "39", "43", "47", "52", "58", "64", "71", "78", "86", "95", "105", "116", "129",
                                      "142", "157", "174", "192", "212", "235", "259", "287", "317", "350", "387", "428",
                                      "473", "523", "578", "638", "706", "780", 0};
static const char *const N_ISPC[] = {"-8", "-7", "-6", "-5", "-4", "-3", "-2", "-1", "0", "+1", "+2", "+3", "+4", "+5",
                                     "+6", "+7", "+8", 0};
#endif
static const char *const N_LYMODE[] = {"OFF", "LAYER", "SPLIT", "MULTI"};   /* LY_OFF .. LY_MULTI */
static const char *const N_L2MODE[] = {"FREE", "KEY"};                  /* L2_FREE, L2_KEY */
static const char *const N_PTGT[] = {"BOTH", "VCO1", "VCO2"};          /* PT_BOTH .. PT_VCO2 */
static const char *const N_BTGT[] = {"ALL", "LEAD"};                    /* (Stallion) P_BTGT: BT_* */
static const char *const N_MSRC[] = {"OFF", "LFO1", "LFO2", "ENV1", "ENV2", "VEL", "KEY", "MW", "AT", "RAND", "SLIDE",
                                     "SEQ1", "SEQ2", "SEQ3", "SEQ4",   /* (0.8: the MOD SEQ's lanes) */
                                     "LFO3", "MAC1", "MAC2", "MAC3", "MAC4",   /* (1.0) */
                                     "BEND", "POLAT", "CHNAT", "RIBBN",   /* (Stallion: MS_BEND .. MS_RIB) */
                                     "S&H", "NOISE", "+V",   /* (MS_SH .. MS_DC) */
                                     "GATE", "E2END", "CC A", "CC B"};   /* (MS_GATE .. MS_CCB) */
static const char *const N_MSRATE[] = {"1/32", "16T", "1/16", "8T", "1/8", "1/4", "1/2", "BAR", "2 BAR"};
static const char *const N_MSKEY[] = {"SYNC", "KEY"};
static const char *const N_MDST[] = {"OFF", "PITCH", "CUT", "SHAPE", "AMP", "PAN", "LFO2",
                                     "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8",   /* (track_desc: the engine's names) */
                                     "ERATE", "CUT2", "RES2",   /* (after 1.0; FILTER 2's) */
                                     "PIT1", "PIT2", "XMOD", "HPF",   /* (Stallion: MT_PIT1 .. MT_HPF) */
                                     "CUTB",   /* (MARE's CUTOFF 2) */
                                     "TRIG1", "TRIG2", "SYNC1", "SYNC2", "ARP", "DLY", "REV", "CHO",   /* (MT_TRIG1 .. */
                                     "LFO1", "E1AMT", "FLTFM", "RING",   /* .. MT_RING) */
                                     "SEMI", "SEMI2"};   /* (MARE's semitone steps) */   /* G_RTYPE: RT_ROOM, RT_PLATE */
static const char *const N_ROLL[] = {"1/8", "1/16", "1/32", "32T", "1/64"};   /* seq.c ROLL_DEN */
static const char *const N_ENGNAME[] = {"ANALOG", "DIGITAL", "PHASE", "", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN",
                                        "FM6", "PHYS", "NOISE", "HYBRID",
#if FELUCCA_SLICE
                                             "SLICE",
#endif
#if CLIPPER_SYNTH
                                        "WAVE", "ACID", "STRINGS", "MACRO", "VPS", "TERRAIN", "FMX", "WSEQ",
                                        "PONY", "VECTOR", "PALOMINO", "STAMPEDE", "RODEO", "MARE",
#endif
};
#if CLIPPER_SYNTH
_Static_assert(NELEM(N_MSRC) == MS_NSRC && NELEM(N_MDST) == MT_COUNT, "a name for every matrix source and destination");
#endif
_Static_assert(NELEM(N_ENGNAME) == NENGINES, "N_ENGNAME: a name for every engine (G_ENGSEL's range, the editor's ENG)");

#define PD(l, f, mn, mx, df) {l, f, mn, mx, df, 0, 0}
#define PE(l, n, df) {l, F_ENUM, 0, (int16_t)(sizeof(n) / sizeof(n[0]) - 1), df, n, 0}

static const param_desc_t TP[P_COUNT] = {
    [P_LEVEL] = PD("LVL", F_DB, 0, 127, 104),
    [P_ATK] = PD("ATK", F_TIME, 0, 127, 10),
    [P_DEC] = PD("DEC", F_TIME, 0, 127, 70),
    [P_SUS] = PD("SUS", F_PCT, 0, 127, 90),
    [P_REL] = PD("REL", F_TIME, 0, 127, 60),
    [P_ED_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_ED_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_ED_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_ED_FX] = PD("TRIM", F_INT, -64, 63, 0),   /* the sound's level trim, 1/2 dB (presets set it) */
    [P_LRATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [P_LWAVE] = PE("WAVE", N_LWAVE, 0),
    [P_LPHASE] = PD("PHS", F_INT, 0, 127, 0),
    [P_LFADE] = PD("FADE", F_TIME, 0, 127, 0),
    [P_LD_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_LD_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_LD_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_LD_AMP] = PD("AMP", F_PCT, 0, 127, 0),
    [P_AMODE] = PE("MODE", N_AMODE, 0),
    [P_ARATE] = PE("RATE", N_DIV, 2),
    [P_AOCT] = PD("OCT", F_INT, 1, 4, 1),
    [P_AGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_ASWING] = PD("SWG", F_SWING, 0, 100, 0),
    [P_APROB] = PD("PROB", F_PCT, 0, 127, 127),
    [P_AHOLD] = PE("HOLD", N_ONOFF, 0),
    [P_AORDER] = PE("ORD", N_ORDER, 0),
    [P_ROOT] = PD("ROOT", F_NOTE, 0, 11, 0),
    [P_SCALE] = PE("SCL", N_SCALE, 0),
    [P_QUANT] = PE("QNT", N_QUANT, 0),
    [P_TRANS] = PD("TRN", F_SEMI, -24, 24, 0),
    [P_SLEN] = PD("LEN", F_STEPS, 1, NSTEP, 16),
    [P_SDIV] = PE("DIV", N_SDIV, 2),
    [P_SSWING] = PD("SWG", F_SWING, 0, 100, 0),
    [P_SGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_DIST] = PD("DST", F_PCT, 0, 127, 0),
    [P_CHOR] = PD("CHO", F_PCT, 0, 127, 0),
    [P_DLY] = PD("DLY", F_PCT, 0, 127, 0),
    [P_REV] = PD("REV", F_PCT, 0, 127, 0),
    [P_VOICE] = PE("VCE", N_VOICE, 0),
    [P_GLIDE] = PD("GLD", F_TIME, 0, 127, 0),
    [P_GLMODE] = PE("GLMOD", N_GLMODE, 0),
    [P_PRIO] = PE("PRIO", N_PRIO, 0),
    [P_ALLOC] = PE("ALLOC", N_ALLOC, 0),
    [P_DETUNE] = PD("DTUNE", F_INT, 0, 127, 40),
    [P_PAN] = PD("PAN", F_BIPCT, -64, 63, 0),
    [P_MUTE] = PE("MUTE", N_ONOFF, 0),
#if CLIPPER_SYNTH                                  /* the sound's modulation (core.h P_VELF ..: voice.c) */
    [P_SLCR] = PD("VEL>F", F_BIPCT, -64, 63, 0),
    [P_SLPAT] = {"MODL", F_ENUM, FM_SVF, FM_LAST, FM_SVF, N_FMODEL, 0},   /* P_FMODEL (core.h; 0.9: + SK DIODE WARM NOTCH BP HP; + OTA) */
    [P_SLRATE] = PE("MW>", N_MODDST, MD_VIB),
    [P_SLDEPTH] = PE("AT>", N_MODDST, MD_CUT),
#else
    [P_SLCR] = PE("SLCR", N_SLCR, 0),
    [P_SLPAT] = PD("PAT", F_INT, 1, 16, 1),        /* SL_PAT[] */
    [P_SLRATE] = PE("RATE", N_SLDIV, 1),
    [P_SLDEPTH] = PD("DEPTH", F_PCT, 0, 127, 127),
#endif
    [P_CHORD] = PE("CHORD", N_CHORD, 0),
    [P_TFLT] = PD("FILT", F_FILT, -64, 63, 0),
    [P_STRUM] = PD("STRUM", F_INT, -60, 60, 0),   /* ms a note: > 0 low to high (down), < 0 high to low */
    [P_VLEAD] = PE("VLEAD", N_ONOFF, 0),
#if CLIPPER_SYNTH                                  /* Clipper 0.4: LFO 2, ENV 2, the matrix (voice.c) */
    [P_L2RATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [P_L2WAVE] = PE("WAVE", N_LWAVE, 0),
    [P_L2MODE] = PE("MODE", N_L2MODE, L2_FREE),
    [P_E2ATK] = PD("ATK", F_TIME, 0, 127, 0),
    [P_E2DEC] = PD("DEC", F_TIME, 0, 127, 70),
    [P_E2SUS] = PD("SUS", F_PCT, 0, 127, 0),
    [P_E2REL] = PD("REL", F_TIME, 0, 127, 60),
#define MXP(n) [P_M##n##SRC] = PE("SRC", N_MSRC, MS_OFF), [P_M##n##DST] = PE("DST", N_MDST, MT_OFF), \
               [P_M##n##AMT] = PD("AMT", F_BIPCT, -64, 63, 0)
    MXP(1), MXP(2), MXP(3), MXP(4), MXP(5), MXP(6),
    MXP(7), MXP(8), MXP(9), MXP(10), MXP(11), MXP(12),   /* 1.0 */
#undef MXP
    /* Clipper 0.6: the sound's INSERT (fx.c track_insert, SLOOP 2.6's): TYPE OFF = as before. A B C as the editor
     * protocol names them; the device shows the type's own labels (ins_desc) */
    [P_ITYPE] = PE("INSRT", N_ITYPE, 0),
    [P_IA] = PD("INS A", F_PCT, 0, 127, 64),
    [P_IB] = PD("INS B", F_PCT, 0, 127, 96),
    [P_IC] = PD("INS C", F_PCT, 0, 127, 96),
    [P_IMIX] = PD("MIX", F_PCT, 0, 127, 127),
    /* Clipper 0.8: the MOD SEQ (seq.c mseq_tick: the steps; the matrix's SEQ1..SEQ4 read them) and SPICE / DICE */
    [P_MSRATE] = PE("RATE", N_MSRATE, 2),
    [P_MSLEN] = PD("LEN", F_INT, 1, 16, 16),
    [P_MSSMTH] = PD("SMTH", F_PCT, 0, 127, 0),
    [P_MSKEY] = PE("START", N_MSKEY, 1),
    [P_SPICE] = PD("SPICE", F_PCT, 0, 127, 0),
    [P_DICE] = PD("DICE", F_INT, 0, 127, 0),
    [P_VINT] = PD("VINTG", F_PCT, 0, 127, 0),       /* 0.9 */
    [P_SLOOP] = PD("LOOP", F_INT, 1, 16, 16),
    /* Clipper 1.0 */
    [P_SPREAD] = PD("SPRED", F_PCT, 0, 127, 0),
    [P_L3RATE] = PD("RATE", F_LFOHZ, 0, 127, 40),
    [P_L3WAVE] = PE("WAVE", N_LWAVE, 0),
    [P_L3MODE] = PE("MODE", N_L2MODE, L2_FREE),
    [P_E2LOOP] = PE("LOOP", N_E2LOOP, 0),
    [P_MAC1] = PD("BRITE", F_BIPCT, -64, 63, 0),
    [P_MAC2] = PD("TIMBR", F_BIPCT, -64, 63, 0),
    [P_MAC3] = PD("MOTN", F_PCT, 0, 127, 0),
    [P_MAC4] = PD("SPACE", F_PCT, 0, 127, 0),
    [P_PTGT] = PE("P>VCO", N_PTGT, PT_BOTH),       /* (PALOMINO's and MARE's) */
    [P_FSLOPE] = {"SLOPE", F_ENUM, FS_12, FS_48, FS_24, N_FSLOPE, "dB"},   /* (the 4-pole MODELs) */
    [P_F2MODEL] = PE("MODL", N_FMODEL2, 0),           /* FILTER 2: OFF, or FM_* */
    [P_F2CUT] = PD("CUT", F_CUTOFF, 0, 127, 64),      /* (with LINK: track_desc OFS, 64 = the first's) */
    [P_F2RES] = PD("RES", F_PCT, 0, 127, 0),
    [P_F2ROUTE] = PE("ROUTE", N_F2ROUTE, F2_SER),
    [P_F2LINK] = PE("LINK", N_ONOFF, 1),
    /* (Stallion) LFO > BEND, BEND 2: the defaults as before (the global range, both VCOs, no VCF bend, ALL) */
    [P_BUP] = PD("UP", F_BRNG, -1, 24, -1),
    [P_BDN] = PD("DOWN", F_BRNG, -1, 24, -1),
    [P_BVCO1] = PE("VCO1", N_ONOFF, 1),
    [P_BVCO2] = PE("VCO2", N_ONOFF, 1),
    [P_BVCF] = PD("VCF", F_INT, -63, 63, 0),
    [P_BTGT] = PE("TARGT", N_BTGT, BT_ALL),
    [P_MFFM] = PD("VCO3", F_SEMIO, -25, 24, -25),     /* MARE's VCO 3 and VCO 4: their intervals, OFF (two
                                                     * VCOs); EDIT 3, MARE only (FILTER FM and RING: the matrix's) */
    [P_MRING] = PD("VCO4", F_SEMIO, -25, 24, -25),
#define MXP(n) [P_M##n##SRC] = PE("SRC", N_MSRC, MS_OFF), [P_M##n##DST] = PE("DST", N_MDST, MT_OFF), \
               [P_M##n##AMT] = PD("AMT", F_BIPCT, -64, 63, 0)
    MXP(13), MXP(14), MXP(15), MXP(16), MXP(17), MXP(18),   /* (preset format 2: slots 13..24) */
    MXP(19), MXP(20), MXP(21), MXP(22), MXP(23), MXP(24),
#undef MXP
    [P_MW34] = {"WAV34", F_INT, 0, 127, MTW_SAW, N_MTWAVE, 0},   /* VCO 3 and 4's WAVE (EDIT 3) */
    [P_JRNG] = PE("RANGE", N_JRNG, 1),                /* PONY's EDIT 3 (eng_pony.c): the DCO's 16' 8' 4' */
    [P_JKYB] = {"KYBD", F_PCT, 0, 100, JK_DEF, 0, 0},  /* .. the filter's key tracking (43 %: as before) */
    [P_JVCA] = PE("VCA", N_JVCA, 0),                  /* .. ENV, GATE */
    [P_JCHR] = {"CHAR", F_CHAR, 0, JQ_ALL, JQ_CLASSIC, N_JQBIT, 0},   /* .. the quirks, a bit each (core.h JQ_*;
                                                     * the device: CLEAN / CLASSIC / FULL / CUSTOM, N_JCHAR) */
    [P_OKYB] = {"KYBD", F_PCT, 0, 100, 0, 0, 0},     /* RODEO's EDIT 3: the filter's key tracking (0: none, as the originals) */
    [P_OPW] = PE("PW0", N_OPW, 1),                    /* .. the pulse's rest width (eng_rodeo.c ROD_PW_REST) */
#endif
};
#if CLIPPER_SYNTH
/* a project that Clipper 0.3 did not write: its drum globals and slicer slots meant something else */
UI_SMALL static void clipper_proj_neutral(void)
{
    uint32_t k;
    song.g[G_EQLOW] = EQ_LOW_C;
    song.g[G_EQMID] = EQ_MID_C;
    song.g[G_EQHIGH] = EQ_HIGH_C;
    song.g[G_COMP] = 0;
    song.g[G_DMODE] = 0;
    for (k = 0; k < NPART; k++) {
        trk[k].p[P_VELF] = TP[P_VELF].def;
        trk[k].p[P_MWDST] = TP[P_MWDST].def;
        trk[k].p[P_ATDST] = TP[P_ATDST].def;
    }
    trk[1].p[P_LYMODE] = LY_OFF;
    trk[1].p[P_LYSPLIT] = 64;                         /* C4 */
    trk[1].p[P_LYOCT] = 2;                            /* 0 */
    trk[1].p[P_LYCH] = 2;                             /* MULTI: the layer on channel 2 */
}
#endif
/* (Stallion) the factory presets' patching (core.h preset_patch_t): each engine's side table, by its file */
#if CLIPPER_SYNTH
static const struct { const preset_patch_t *t; uint8_t n; } PATCH_TABLES[] = {
    {PONY_PATCHES, NELEM(PONY_PATCHES)}, {PALOMINO_PATCHES, NELEM(PALOMINO_PATCHES)},
    {STAMPEDE_PATCHES, NELEM(STAMPEDE_PATCHES)}, {RODEO_PATCHES, NELEM(RODEO_PATCHES)},
    {MARE_PATCHES, NELEM(MARE_PATCHES)},   /* (an engine with a side table: its line here) */
    {PHYS_PATCHES, NELEM(PHYS_PATCHES)},
    {DIGITAL_PATCHES, NELEM(DIGITAL_PATCHES)},   /* (Stallion 1.0: no bells, no imitations) */
    {FM6_PATCHES, NELEM(FM6_PATCHES)},
    {FMX_PATCHES, NELEM(FMX_PATCHES)}, {WAVE_PATCHES, NELEM(WAVE_PATCHES)},   /* (digi group) */
    {VECTOR_PATCHES, NELEM(VECTOR_PATCHES)}, {WSEQ_PATCHES, NELEM(WSEQ_PATCHES)}, {VPS_PATCHES, NELEM(VPS_PATCHES)},
    {NOISE_PATCHES, NELEM(NOISE_PATCHES)},
    {SMP_PATCHES, NELEM(SMP_PATCHES)},
    {ANALOG_PATCHES, NELEM(ANALOG_PATCHES)},     /* (no bells, no imitations: the replacements' tables) */
    {PHASE_PATCHES, NELEM(PHASE_PATCHES)},
    {FORMANT_PATCHES, NELEM(FORMANT_PATCHES)},
    {TRIO_PATCHES, NELEM(TRIO_PATCHES)},
    {STRINGS_PATCHES, NELEM(STRINGS_PATCHES)},
    {MACRO_PATCHES, NELEM(MACRO_PATCHES)},
};
/* the factory MOD SEQ steps (core.h preset_mseq_t): each engine's table, by its file */
static const struct { const preset_mseq_t *t; uint8_t n; } MSEQ_TABLES[] = {
    {MARE_MSEQ, NELEM(MARE_MSEQ)},
};
UI_SMALL static void mseq_unpack(int8_t (*ms)[MSEQ_STEPS], const uint8_t *st)   /* 32 packed bytes into the four lanes */
{
    uint32_t n;
    for (n = 0; n < 32u; n++) {
        uint32_t b = st[n], a = 2u * n, c = a + 1u;
        ms[a / MSEQ_STEPS][a % MSEQ_STEPS] = (int8_t)((int32_t)(b & 15u) - 8);
        ms[c / MSEQ_STEPS][c % MSEQ_STEPS] = (int8_t)((int32_t)(b >> 4) - 8);
    }
}
/* pr's MOD SEQ steps into ms (1: it has some; 0: none, ms as it was: mseq_init's shapes) */
UI_SMALL static int preset_mseq(int8_t (*ms)[MSEQ_STEPS], const preset_t *pr)
{
    uint32_t k, i;
    for (k = 0; k < NELEM(MSEQ_TABLES); k++)
        for (i = 0; i < MSEQ_TABLES[k].n; i++)
            if (MSEQ_TABLES[k].t[i].pr == pr && pr) {
                mseq_unpack(ms, MSEQ_TABLES[k].t[i].st);
                return 1;
            }
    return 0;
}
/* pr's entry in the side tables (its first; preset_patch_from: the one after `after`), 0: none */
UI_SMALL static const preset_patch_t *preset_patch_from(const preset_t *pr, const preset_patch_t *after)
{
    uint32_t k, i;
    for (k = 0; k < NELEM(PATCH_TABLES); k++)
        for (i = 0; i < PATCH_TABLES[k].n; i++)
            if (PATCH_TABLES[k].t[i].pr == pr) {
                if (!after)
                    return &PATCH_TABLES[k].t[i];
                if (&PATCH_TABLES[k].t[i] == after)
                    after = 0;
            }
    return 0;
}
UI_SMALL static const preset_patch_t *preset_patch(const preset_t *pr) { return preset_patch_from(pr, 0); }
#endif
/* (param id + 1, value) pairs into p, each clamped to its range; returns 1 if one set SPREAD */
UI_SMALL static int preset_pairs(int16_t *p, const int8_t *x)
{
    uint32_t i;
    int own = 0;
    for (i = 0; i + 1u < 8u; i += 2u) {
        int32_t id = (uint8_t)x[i] - 1;   /* (unsigned, the ids past 127: eng_mare.c MAR_XV3) */
        if (id >= 0 && id < P_E0) {
            p[id] = (int16_t)clamp(x[i + 1u], TP[id].min, TP[id].max);
#if CLIPPER_SYNTH
            own |= id == P_SPREAD;
#endif
        }
    }
    return own;
}
/* a preset's extra parameters (preset_t.x, then its side-table entry: its extras, its cables) into p */
UI_SMALL static void preset_extras(int16_t *p, const preset_t *pr)
{
    int own = preset_pairs(p, pr->x);
#if CLIPPER_SYNTH
    const preset_patch_t *pp;
    for (pp = preset_patch(pr); pp; pp = preset_patch_from(pr, pp)) {   /* (a preset may have more than
                                                         * one entry, each more extras and cables, applied in order) */
        uint32_t i;
        own |= preset_pairs(p, pp->x);
        for (i = 0; i < pp->n; i++) {
            const pcable_t *c = &pp->c[i];
            int16_t *s;
            if (c->slot < 1u || c->slot > MX_SLOTS)
                continue;
            s = &p[mx_pid(c->slot - 1u)];
            s[0] = (int16_t)(c->src < MS_NSRC ? c->src : MS_OFF);
            s[1] = (int16_t)(c->dst < MT_COUNT ? c->dst : MT_OFF);
            s[2] = (int16_t)clamp(c->amt, -64, 63);
        }
    }
    /* (1.0) the unison sounds spread their voices a little, unless the preset sets SPREAD itself */
    if (!own)
        p[P_SPREAD] = (int16_t)(voice_uni(p[P_VOICE]) ? 48 : TP[P_SPREAD].def);
#else
    (void)own;
#endif
}


static const param_desc_t GP[G_COUNT] = {
    [G_BPM] = PD("BPM", F_BPM, 40, 240, 90),
    [G_SWING] = PD("SWING", F_SWING, 0, 100, 0),
    [G_CLOCK] = PE("CLICK", N_CLICK, 0),            /* (the old CLK slot: projects keep their format) */
    [G_TUNE] = PD("TUNE", F_INT, -50, 50, 0),
    [G_DTIME] = PE("TIME", N_DLY, 1),
    [G_DFDBK] = PD("FDBK", F_PCT, 0, 120, 60),
    [G_DCOLOR] = PD("COLR", F_PCT, 0, 127, 70),
    [G_DMIX] = PD("MIX", F_PCT, 0, 127, 90),
    [G_RSIZE] = PD("SIZE", F_PCT, 0, 127, 90),
    [G_RDAMP] = PD("DAMP", F_PCT, 0, 127, 60),
    [G_CRATE] = PD("CRT", F_LFOHZ, 0, 127, 40),
    [G_CDEPTH] = PD("CDP", F_PCT, 0, 127, 60),
    [G_MIDI] = PE("MIDI", N_MOUT, 0),           /* MIDI OUT: the keys, or the sequencer too (a setting of the FM-1) */
    [G_SYNC] = PE("SYNC", N_SYNC, 0),           /* a setting of the FM-1, not of a project (panel.c lights_sync) */
    [G_ROUTE] = PE("IN", N_MIN, 0),             /* MIDI IN: NOTES (and the clock) or CLOCK only (a setting of the FM-1) */
    [G_INFO] = PD("CPU", F_INT, 0, 0, 0),
    [G_SLOT] = PD("SLOT", F_INT, 1, 4, 1),
    [G_NAME] = PE("NAME", N_DASH, 0),
    [G_LOAD] = PE("LOAD", N_GO, 0),
    [G_SAVE] = PE("SAVE", N_GO, 0),
    [G_ENGSEL] = PE("ENG", N_ENGNAME, 0),
    [G_ENGGO] = PE("SET", N_GO, 0),
    [G_CLRSEQ] = PE("CLRSQ", N_GO, 0),
    [G_INITSND] = PE("INIT", N_GO, 0),
#if CLIPPER_SYNTH                                         /* the master bus (core.h G_EQLOW .., bus.c) */
    [G_DRCH] = PD("MID", F_CDB, EQ_MID_C - 10, EQ_MID_C + 10, EQ_MID_C),
    [G_DRLVL] = PD("LOW", F_CDB, EQ_LOW_C - 12, EQ_LOW_C + 12, EQ_LOW_C),
    [G_DRREV] = PD("HIGH", F_CDB, EQ_HIGH_C - 12, EQ_HIGH_C + 12, EQ_HIGH_C),
#else
    [G_DRCH] = PD("CH", F_INT, 0, 16, 10),            /* GM drum part MIDI channel, 0 = off */
    [G_DRLVL] = PD("LVL", F_INT, 0, 127, 100),
    [G_DRREV] = PD("REV", F_INT, 0, 127, 16),
#endif
    [G_DUST] = PD("DUST", F_PCT, 0, 127, 0),
#if CLIPPER_SYNTH                                         /* G_RTYPE, G_RPRE (core.h): the reverb, fx.c */
    [G_DUCK] = PE("TYPE", N_RTYPE, RT_ROOM),
    [G_FILT] = PD("FILT", F_FILT, -64, 63, 0),
    [G_ROLL] = {"PRE", F_INT, 0, 100, 0, 0, "ms"},
#else
    [G_DUCK] = PD("DUCK", F_PCT, 0, 127, 0),
    [G_FILT] = PD("FILT", F_FILT, -64, 63, 0),
    [G_ROLL] = PE("ROLL", N_ROLL, 1),
#endif
    [G_NEWPRJ] = PE("NEW", N_GO, 0),
#if CLIPPER_SYNTH
    [G_DRDLY] = PD("COMP", F_PCT, 0, 127, 0),         /* G_COMP */
    [G_DMODE] = PE("MODE", N_DMODE, 0),                /* 1.0 */
    [G_RSHIM] = PD("SHIM", F_PCT, 0, 127, 0),          /* (the octave-up line on any reverb TYPE: fx.c) */
#else
    [G_DRDLY] = PD("DLY", F_INT, 0, 127, 0),
#endif
};

static const param_desc_t DRUM_KIT_DESC = PE("KIT", DRUM_KIT_NAMES, 0);
#if CLIPPER_SYNTH
/* the INSERT's A B C as its TYPE means them (TP's ranges and defaults): the drives DRIVE TONE LEVEL, CRUSH BITS RATE
 * LPF, the swept ones RATE DEPTH FDBK; OFF TP's own (as SLOOP 2.6). 1.1: PITCH, PIT2, DELAY, LOCMP (PHAS6 the swept
 * ones') */
#define PU(l, f, df, n, u) {l, f, 0, 127, df, n, u}
static const param_desc_t INS_DESC[9][3] = {
    {PU("DRIVE", F_PCT, 64, 0, 0), PU("TONE", F_CUTOFF, 96, 0, 0), PU("LEVEL", F_DB, 96, 0, 0)},
    {PU("BITS", F_INT, 64, N_IBITS, 0), PU("RATE", F_INT, 96, N_IRATE, "Hz"), PU("LPF", F_CUTOFF, 96, 0, 0)},
    {PU("RATE", F_LFOHZ, 64, 0, 0), PU("DEPTH", F_PCT, 96, 0, 0), PU("FDBK", F_PCT, 96, 0, 0)},
    {PU("WIDTH", F_PCT, 127, 0, 0), PU("TONE", F_CUTOFF, PCH_TONE, 0, 0), PU("NOISE", F_PCT, 0, 0, 0)},   /* PONY1 .. JUN12 (fx.c
                                                     * pony_chorus: TONE each tap's two poles, PONY's at PCH_TONE) */
    {PU("PITCH", F_INT, 95, N_ISEMI, "st"), PU("FINE", F_INT, 64, N_ICENT, "ct"), PU("FDBK", F_PCT, 0, 0, 0)},
    {PU("PTCH1", F_INT, 32, N_ISEMI, "st"), PU("PTCH2", F_INT, 81, N_ISEMI, "st"), PU("BAL", F_PCT, 64, 0, 0)},
    {PU("TIME", F_INT, 84, N_IDMS, "ms"), PU("FDBK", F_PCT, 40, 0, 0), PU("TONE", F_CUTOFF, 90, 0, 0)},
    {PU("COMP", F_PCT, 80, 0, 0), PU("LOFI", F_PCT, 48, 0, 0), PU("TONE", F_CUTOFF, 100, 0, 0)},
    {PU("TIME", F_INT, 92, N_ISDMS, "ms"), PU("FDBK", F_PCT, 64, 0, 0), PU("SPACE", F_INT, 64, N_ISPC, 0)},   /* STDLY */
};
#undef PU
/* each type's row of INS_DESC (OFF: TP's own) */
static const uint8_t INS_ROW[] = {0, 0, 0, 0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 2, 6, 7, 8, 8};
_Static_assert(sizeof INS_ROW == sizeof N_ITYPE / sizeof N_ITYPE[0], "a row of INS_DESC for every INSERT type");
UI_SMALL static const param_desc_t *ins_desc(int32_t type, uint32_t k)
{
    if (type <= 0 || type >= (int32_t)(sizeof N_ITYPE / sizeof N_ITYPE[0]))
        return &TP[P_IA + k];
    return &INS_DESC[INS_ROW[type]][k % 3u];
}
#endif
UI_SMALL static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
#if CLIPPER_SYNTH
    uint32_t mf;
#endif
    if(is_drum(t) && id==P_E0) return &DRUM_KIT_DESC;
#if CLIPPER_SYNTH
    if ((id == P_SPICE || id == P_DICE) && t->p[P_AMODE] == 15) {   /* (0.9) the arp's TURNG (seq.c AM_TURING) */
        static param_desc_t d;
        d = TP[id];
        d.label = id == P_SPICE ? "MUTAT" : "SEED";
        return &d;
    }
    if (id == P_FMODEL && t->eng_req % NENGINES == ENGI_PONY) {   /* PONY's MODL: OTA first (N_JFMODEL) */
        static const param_desc_t jm = {"MODL", F_ENUM, FM_SVF, FM_LAST, FM_SVF, N_JFMODEL, 0};
        return &jm;
    }
    if (id >= P_IA && id <= P_IC)                     /* the INSERT's values as its TYPE names them */
        return ins_desc(t->p[P_ITYPE], id - P_IA);
    if (id == P_F2CUT && t->p[P_F2LINK] && f2_own(ENGINES[t->eng_req % NENGINES])) {   /* FILTER 2 linked: OFS */
        static const param_desc_t ofs = {"OFS", F_COFS, 0, 127, 64, 0, "st"};
        return &ofs;
    }
    if (mx_slot_of(id, &mf) >= 0 && mf == 1u) {      /* a matrix DST (every slot): the engine's own names */
        static const char *names[MT_COUNT];
        static param_desc_t d;
        const engine_t *e = ENGINES[t->eng_req % NENGINES];
        uint32_t k;
        for (k = 0; k < MT_COUNT; k++)
            names[k] = k >= MT_E1 && k < MT_E1 + 8u && e->edit[k - MT_E1].label && e->edit[k - MT_E1].label[0] &&
                       e->edit[k - MT_E1].label[0] != '-' ?
                       e->edit[k - MT_E1].label : N_MDST[k];
        d = TP[id];
        d.names = names;
        return &d;
    }
#endif
    if (id >= P_E0 && id <= P_E7) {                   /* the engine asked for (t->engine follows after a fade) */
        const engine_t *e = ENGINES[t->eng_req % NENGINES];
        const param_desc_t *d = e->desc ? e->desc(t, id - P_E0) : 0;   /* a mode-dependent label / names */
        return d ? d : &e->edit[id - P_E0];
    }
    return &TP[id];
}

/* value string (<= 5 chars) and unit for a parameter value */
UI_SMALL static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
    switch (d->fmt) {
    case F_PCT:                                       /* of the range: 0 .. 100 % */
        fmt_int(val, d->max > 0 ? (v * 100 + d->max / 2) / d->max : v);
        *unit = "%";
        break;
    case F_SWING:                                     /* 0 straight .. 100 the most (2.5: it showed MPC's 50..75 %,
                                                       * the share of a step pair the first one gets: confusing) */
        fmt_int(val, clamp(v, 0, 100));
        break;
    case F_FILT:                                      /* LP 1..100 % closed, HP 1..100 % */
        if (!v) {
            str_cpy(val, "OFF", 6);
        } else {
            str_cpy(val, v < 0 ? "LP" : "HP", 6);
            fmt_int(val + 2, v < 0 ? (-v * 100 + 32) / 64 : (v * 100 + 31) / 63);
            *unit = "%";
        }
        break;
    case F_BIPCT:
        fmt_int(val, v * 100 / 64);
        if (v > 0) {
            char t[8];
            fmt_int(t, v * 100 / 64);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "%";
        break;
    case F_TIME: {
        uint32_t ms10 = TIME_MS_X10[v & 127];
        if (ms10 < 100u) {
            fmt_fix(val, (int32_t)ms10, 1);
            *unit = "ms";
        } else if (ms10 < 10000u) {
            fmt_int(val, (int32_t)((ms10 + 5u) / 10u));
            *unit = "ms";
        } else {
            fmt_fix(val, (int32_t)(ms10 / 100u), 2);
            if (ms10 >= 100000u)
                fmt_fix(val, (int32_t)(ms10 / 1000u), 1);
            *unit = "s";
        }
        break;
    }
    case F_LFOHZ: {
        uint32_t h = LFO_HZ_X100[v & 127];
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else
            fmt_fix(val, (int32_t)(h / 10u), 1);
        *unit = "Hz";
        break;
    }
    case F_CUTOFF: {
        uint32_t h = CUTOFF_HZ[v & 127];
        if (h < 1000u) {
            fmt_int(val, (int32_t)h);
            *unit = "Hz";
        } else if (h < 10000u) {
            fmt_fix(val, (int32_t)(h / 100u), 1);
            *unit = "kHz";
        } else {
            fmt_int(val, (int32_t)((h + 500u) / 1000u));   /* "12 kHz": "12.5" would leave no room for the unit */
            *unit = "kHz";
        }
        break;
    }
    case F_DB:
        if (v <= 0) {
            str_cpy(val, "OFF", 6);
        } else if (LEVEL_DB_X10[v] > -100) {
            fmt_fix(val, LEVEL_DB_X10[v], 1);
            *unit = "dB";
        } else {
            fmt_int(val, (LEVEL_DB_X10[v] - 5) / 10);   /* "-12 dB": five characters leave no room for the unit */
            *unit = "dB";
        }
        break;
    case F_SEMI:
    case F_SEMIL:                                     /* (F_SEMIL: its minimum is LOW) */
    case F_SEMIO:                                     /* (F_SEMIO: its minimum is OFF) */
        if (d->fmt != F_SEMI && v <= d->min) {
            str_cpy(val, d->fmt == F_SEMIL ? "LOW" : "OFF", 6);
            break;
        }
        fmt_int(val, v);
        if (v > 0) {
            char t[8];
            fmt_int(t, v);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "st";
        break;
    case F_ENUM:
        str_cpy(val, d->names[(v < d->min ? d->min : v > d->max ? d->max : v) - d->min], 6);   /* (P_FMODEL: from 1) */
        if (d->unit)
            *unit = d->unit;
        break;
    case F_BPM:
        fmt_int(val, v);
        *unit = "BPM";
        break;
    case F_NOTE:
        str_cpy(val, N_NOTE[v % 12], 6);
        break;
    case F_ONOFF:
        str_cpy(val, N_ONOFF[v ? 1 : 0], 6);
        break;
    case F_STEPS:
        fmt_int(val, v);
        *unit = "STEP";
        break;
#if CLIPPER_SYNTH
    case F_CHAR:                                      /* (Stallion) PONY's CHAR: the set its bits make */
        str_cpy(val, N_JCHAR[jq_char(v)], 6);
        break;
#endif
    case F_CDB: {                                     /* dB around the middle of the range */
        int32_t c = (d->min + d->max) / 2, x = v - c;
        if (x > 0) {
            char t[8];
            fmt_int(t, x);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        } else {
            fmt_int(val, x);
        }
        *unit = "dB";
        break;
    }
    case F_COFS: {                                    /* steps from 64 (30 Hz .. 16 kHz: 127 steps), in semitones */
        int32_t st = (v - 64) * 856;
        st = st < 0 ? -((-st + 500) / 1000) : (st + 500) / 1000;
        fmt_int(val, st);
        if (st > 0) {
            char t[8];
            fmt_int(t, st);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "st";
        break;
    }
    case F_BRNG:                                      /* (Stallion) a bend range: -1 the global one */
        if (v < 0) {
            str_cpy(val, "GLO", 6);
            break;
        }
        fmt_int(val, v);
        *unit = "st";
        break;
    case F_MS10:                                      /* (Stallion) 10 ms steps: ms, from 1 s in s */
        if (v < 100) {
            fmt_int(val, v * 10);
            *unit = "ms";
        } else {
            fmt_fix(val, v, 2);
            *unit = "s";
        }
        break;
    case F_FIX2:                                      /* (Stallion) hundredths */
        fmt_fix(val, v, 2);
        if (d->unit)
            *unit = d->unit;
        break;
    case F_SPLIT: {                                   /* stored + 4: C4 = 60 + 4 */
        uint32_t k;
        v -= 4;
        if (v < 0)
            v = 0;
        str_cpy(val, N_NOTE[(uint32_t)v % 12u], 6);
        k = 0;
        while (val[k])
            k++;
        if (v < 12) {
            val[k++] = '-';
            val[k++] = '1';
        } else {
            val[k++] = (char)('0' + (v / 12 - 1) % 10);
        }
        val[k] = 0;
        break;
    }
    default:
        if (d->names) {                               /* F_INT with a 0-terminated name list: the range */
            uint32_t k = 0;                           /* split evenly over the names (engine desc hooks) */
            while (d->names[k])
                k++;
            str_cpy(val, d->names[(uint32_t)(clamp(v, d->min, d->max) - d->min) * k / (uint32_t)(d->max - d->min + 1)], 6);
        } else {
            fmt_int(val, v);
        }
        if (d->unit)
            *unit = d->unit;
        break;
    }
}

/* ------------------------------------------------------------ pages --- */
enum { FAM_HOME, FAM_ENV, FAM_LFO, FAM_FX, FAM_SCL, FAM_EDIT, FAM_GLO, FAM_SAVE, FAM_ARP, FAM_SEQ, FAM_TRK,
       FAM_COUNT };
enum { SC_TRACK, SC_GLOBAL, SC_ENGINE, SC_STEP, SC_TRK, SC_SONG, SC_DRUM, SC_SYN };   /* SC_SYN: ui_synth.c */
enum { GR_NONE, GR_ADSR, GR_LFO, GR_STEPS, GR_ARP, GR_SCALE, GR_FX, GR_ROLL, GR_BROWSE, GR_SLOTS, GR_USER, GR_TRK,
       GR_SLCR, GR_LAYER, GR_MORPH, GR_MATRIX, GR_FMX, GR_HIST, GR_MSEQ, GR_KAT, GR_PAT, GR_ATC };

typedef struct {
    const char *title;
    uint8_t fam, scope, graph;
    uint8_t id[4];               /* param ids; 0xFF = empty slot */
} page_t;

static const page_t PAGES[] = {
    {"ENV", FAM_ENV, SC_TRACK, GR_ADSR, {P_ATK, P_DEC, P_SUS, P_REL}},
    {"ENV DEST", FAM_ENV, SC_TRACK, GR_NONE, {P_ED_FLT, P_ED_PIT, P_ED_SHP, 0xFF}},   /* (P_ED_FX: the level trim, no page) */
#if CLIPPER_SYNTH
    {"ENV 2", FAM_ENV, SC_TRACK, GR_NONE, {P_E2ATK, P_E2DEC, P_E2SUS, P_E2REL}},   /* (a modulation source: MATRIX) */
    {"ENV 2 LOOP", FAM_ENV, SC_TRACK, GR_NONE, {P_E2LOOP, 0xFF, 0xFF, 0xFF}},       /* (1.0) */
#endif
    {"LFO", FAM_LFO, SC_TRACK, GR_LFO, {P_LRATE, P_LWAVE, P_LPHASE, P_LFADE}},
    {"LFO DEST", FAM_LFO, SC_TRACK, GR_NONE, {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP}},
#if CLIPPER_SYNTH                                                        /* (velocity, mod wheel, aftertouch) */
    {"MOD", FAM_LFO, SC_TRACK, GR_NONE, {P_VELF, P_MWDST, P_ATDST, P_PTGT}},   /* (P>VCO: PALOMINO's, MARE's) */
    {"BEND", FAM_LFO, SC_TRACK, GR_NONE, {P_BUP, P_BDN, P_BVCF, P_BTGT}},     /* (Stallion: the sound's bend) */
    {"BEND 2", FAM_LFO, SC_TRACK, GR_NONE, {P_BVCO1, P_BVCO2, 0xFF, 0xFF}},   /* (.. on VCO 1 / VCO 2) */
    {"LFO 2", FAM_LFO, SC_TRACK, GR_NONE, {P_L2RATE, P_L2WAVE, P_L2MODE, 0xFF}},
    {"LFO 3", FAM_LFO, SC_TRACK, GR_NONE, {P_L3RATE, P_L3WAVE, P_L3MODE, 0xFF}},   /* (1.0) */
    {"MATRIX", FAM_LFO, SC_SYN, GR_MATRIX, {15, 16, 17, 18}},   /* X_MSLOT X_MSRC X_MDST X_MAMT (ui_synth.c) */
    {"MATRIX 2", FAM_LFO, SC_SYN, GR_MATRIX, {93, 0xFF, 0xFF, 0xFF}},   /* X_MCLR: CLEAR all 24 slots */
    {"MOD SEQ", FAM_LFO, SC_SYN, GR_MSEQ, {43, 44, 45, 46}},    /* (0.8) X_QLANE X_QSTEP X_QVAL X_QFILL */
    {"MOD SEQ 2", FAM_LFO, SC_TRACK, GR_MSEQ, {P_MSRATE, P_MSLEN, P_MSSMTH, P_MSKEY}},
#endif
    {"FX", FAM_FX, SC_TRACK, GR_FX, {P_DIST, P_CHOR, P_DLY, P_REV}},
#if CLIPPER_SYNTH                                                        /* (the sound's filter model too) */
    {"FILTER", FAM_FX, SC_TRACK, GR_NONE, {P_TFLT, P_FMODEL, P_FSLOPE, P_F2LINK}},   /* (LINK: FILTER 2 on it) */
    {"FILTER 2", FAM_FX, SC_TRACK, GR_NONE, {P_F2MODEL, P_F2CUT, P_F2RES, P_F2ROUTE}},
    {"INSERT", FAM_FX, SC_TRACK, GR_NONE, {P_ITYPE, P_IA, P_IB, P_IC}},   /* 0.6: the sound's INSERT (fx.c), */
    {"INSERT 2", FAM_FX, SC_TRACK, GR_NONE, {P_IMIX, 0xFF, 0xFF, 0xFF}},  /* .. its dry / wet */
#else
    {"FILTER", FAM_FX, SC_TRACK, GR_NONE, {P_TFLT, 0xFF, 0xFF, 0xFF}},   /* the track's filter (drum track too) */
#endif
#if !CLIPPER_SYNTH                                                       /* (Clipper: no groovebox slicer) */
    {"SLICER", FAM_FX, SC_TRACK, GR_SLCR, {P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH}},   /* drum track too */
#endif
    {"DLY", FAM_FX, SC_GLOBAL, GR_NONE, {G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX}},
#if CLIPPER_SYNTH
    {"DLY 2", FAM_FX, SC_GLOBAL, GR_NONE, {G_DMODE, 0xFF, 0xFF, 0xFF}},          /* (1.0: MONO PING TAPE) */
#endif
#if CLIPPER_SYNTH                                                        /* (the plate: TYPE and PRE-delay) */
    {"REVERB", FAM_FX, SC_GLOBAL, GR_NONE, {G_RTYPE, G_RSIZE, G_RDAMP, G_RPRE}},
    {"REVERB 2", FAM_FX, SC_GLOBAL, GR_NONE, {G_RSHIM, 0xFF, 0xFF, 0xFF}},       /* (the SHIMMER on any TYPE) */
    {"CHORUS", FAM_FX, SC_GLOBAL, GR_NONE, {G_CRATE, G_CDEPTH, 0xFF, 0xFF}},
    {"EQ+COMP", FAM_FX, SC_GLOBAL, GR_NONE, {G_EQLOW, G_EQMID, G_EQHIGH, G_COMP}},   /* (the master bus: bus.c) */
#else
    {"REV/CHO", FAM_FX, SC_GLOBAL, GR_NONE, {G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH}},
#endif
    {"SEL", FAM_SCL, SC_TRACK, GR_SCALE, {P_ROOT, P_SCALE, P_QUANT, P_CHORD}},
    {"SEL 2", FAM_SCL, SC_TRACK, GR_SCALE, {P_TRANS, P_STRUM, P_VLEAD, 0xFF}},   /* (2.4: the chords played) */
    {"EDIT 1", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E0, P_E1, P_E2, P_E3}},
    {"EDIT 2", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E4, P_E5, P_E6, P_E7}},
#if CLIPPER_SYNTH                                          /* MARE's FILTER FM and RING, on MARE only */
    {"EDIT 3", FAM_EDIT, SC_TRACK, GR_NONE, {P_MFFM, P_MRING, P_MW34, 0xFF}},   /* (VCO 3, VCO 4) */
    {"EDIT 3", FAM_EDIT, SC_TRACK, GR_NONE, {P_JRNG, P_JKYB, P_JVCA, P_JCHR}},  /* PONY's (eng_pony.c), on PONY only */
    {"EDIT 3", FAM_EDIT, SC_TRACK, GR_NONE, {P_OKYB, P_OPW, 0xFF, 0xFF}},   /* RODEO's KYBD PW0 (eng_rodeo.c), on RODEO only */
#endif
#if CLIPPER_SYNTH                                          /* 0.6: FMX's own pages (ui_synth.c X_F*), on FMX only */
    {"OP", FAM_EDIT, SC_SYN, GR_FMX, {19, 20, 21, 22}},          /* X_FOP X_FWAVE X_FMODE X_FRATIO */
    {"OP 2", FAM_EDIT, SC_SYN, GR_FMX, {23, 24, 25, 26}},        /* X_FFINE X_FLEVEL X_FVEL X_FKSC */
    {"OP ENV", FAM_EDIT, SC_SYN, GR_FMX, {27, 28, 29, 30}},      /* X_FATK X_FDEC X_FSUS X_FREL */
    {"ROUTE", FAM_EDIT, SC_SYN, GR_FMX, {31, 32, 33, 34}},       /* X_FTO X_FFROM X_FAMT X_FOUT */
    {"FMX", FAM_EDIT, SC_SYN, GR_FMX, {35, 36, 37, 38}},         /* X_FDTN X_FFIX X_FPEG X_FPTIM */
    {"STORE", FAM_EDIT, SC_SYN, GR_FMX, {39, 40, 0xFF, 0xFF}},   /* X_FSLOT X_FSTORE */
#endif
    {"VOICE", FAM_EDIT, SC_TRACK, GR_NONE, {P_VOICE, P_GLIDE, P_GLMODE, P_PRIO}},
#if CLIPPER_SYNTH                                                        /* (one instrument: nothing to mute) */
    {"VOICE 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_ALLOC, P_DETUNE, P_PAN, P_VINT}},   /* (0.9: VINTAGE) */
    {"VOICE 3", FAM_EDIT, SC_TRACK, GR_NONE, {P_SPREAD, 0xFF, 0xFF, 0xFF}},      /* (1.0: SPREAD) */
    {"MACROS", FAM_EDIT, SC_TRACK, GR_NONE, {P_MAC1, P_MAC2, P_MAC3, P_MAC4}},   /* (1.0) */
    {"LAYER", FAM_EDIT, SC_SYN, GR_LAYER, {0, 1, 2, 3}},         /* X_LYMODE X_LYSOUND X_LYSPLIT X_LYLEVEL */
    {"LAYER 2", FAM_EDIT, SC_SYN, GR_LAYER, {4, 5, 6, 7}},       /* X_LYOCT X_LYPAN X_LYFILT X_LYSWAP */
    {"MORPH", FAM_EDIT, SC_SYN, GR_MORPH, {8, 9, 10, 0xFF}},     /* X_MORPH X_SETA X_SETB */
    {"RANDOM", FAM_EDIT, SC_SYN, GR_NONE, {11, 12, 13, 14}},     /* X_RNDAMT X_RNDWHAT X_RNDGO X_RNDBACK */
    {"UNDO", FAM_EDIT, SC_SYN, GR_HIST, {41, 42, 0xFF, 0xFF}},   /* (0.7) X_HPOS X_HCMP: the sound's history, COMPARE */
#else
    {"VOICE 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_ALLOC, P_DETUNE, P_PAN, P_MUTE}},
#endif
    {"GLOBAL", FAM_GLO, SC_GLOBAL, GR_NONE, {G_BPM, G_SWING, G_CLOCK, G_TUNE}},
#if CLIPPER_SYNTH                                                        /* (no kick to duck to, no note repeat) */
    {"MASTER", FAM_GLO, SC_GLOBAL, GR_NONE, {G_DUST, G_FILT, 0xFF, 0xFF}},
    {"TUNING", FAM_GLO, SC_SYN, GR_NONE, {47, 48, 49, 0xFF}},   /* (0.9) X_TSYS X_TROOT (1.0: X_TA4): voice.c tune_cfg */
    {"SYSTEM", FAM_GLO, SC_GLOBAL, GR_NONE, {G_MIDI, G_SYNC, G_ROUTE, G_INFO}},
    {"MIDI", FAM_GLO, SC_SYN, GR_NONE, {50, 51, 52, 53}},   /* (1.0) X_MIN X_MOUT X_MBEND X_MLOCAL (seq.c midi_in_ch) */
    {"MIDI 2", FAM_GLO, SC_SYN, GR_NONE, {54, 55, 56, 0xFF}},    /* (1.0) X_MPROG X_MCCO X_MBRTH */
    {"MIDI 3", FAM_GLO, SC_SYN, GR_NONE, {90, 91, 92, 0xFF}},    /* X_MCCA X_MCCB X_MSUS: the matrix's CCs */
#if BOARD_SMK37                                          /* (the keys have contacts, no converter; real wheels: only the AT shaping) */
    {"AFTERTOUCH", FAM_GLO, SC_SYN, GR_ATC, {61, 62, 63, 64}},   /* X_ATCURVE X_ATOFS X_ATATK X_ATREL */
    {"PAD VEL", FAM_GLO, SC_SYN, GR_NONE, {58, 0xFF, 0xFF, 0xFF}},   /* X_ATSENS's slot: smk_press.h PV_* */
    {"KEY AT", FAM_GLO, SC_SYN, GR_KAT, {73, 74, 75, 76}},      /* X_KON X_KHIT X_KVEL X_KRISE: the keys' */
    {"KEY AT 2", FAM_GLO, SC_SYN, GR_KAT, {77, 78, 79, 80}},    /* X_KFALL X_KHOLD X_KSWELL X_KCEIL: made-up pressure */
    {"KEY AT 3", FAM_GLO, SC_SYN, GR_KAT, {88, 89, 0xFF, 0xFF}},   /* X_KDELAY X_KSHAPE: the swell's start and curve */
    {"PAD AT", FAM_GLO, SC_SYN, GR_PAT, {81, 82, 83, 84}},      /* X_PDEAD X_PROOM X_PTOP X_PSETL: the pads' squeeze */
    {"PAD AT 2", FAM_GLO, SC_SYN, GR_PAT, {85, 86, 87, 0xFF}},  /* X_PRISE X_PFALL X_PSHAPE (pat_cfg.h) */
#else
    {"TOUCH", FAM_GLO, SC_SYN, GR_NONE, {57, 58, 59, 60}},       /* (Stallion) X_KTOUCH X_ATSENS X_ATOUT X_TCAL */
    {"TOUCH 2", FAM_GLO, SC_SYN, GR_ATC, {61, 62, 63, 64}},      /* X_ATCURVE X_ATOFS X_ATATK X_ATREL: the AT shaping */
    {"TOUCH 3", FAM_GLO, SC_SYN, GR_NONE, {65, 66, 67, 68}},     /* X_LEVER X_LTIME X_RIBRNG X_MREC */
    {"TOUCH 4", FAM_GLO, SC_SYN, GR_NONE, {69, 70, 71, 72}},     /* X_RSTEPS X_RSPRING X_RDELAY X_RRET: the ribbon */
    {"TOUCH 5", FAM_GLO, SC_SYN, GR_ATC, {81, 84, 87, 0xFF}},    /* X_PDEAD X_PSETL X_PSHAPE: the keys' squeeze */
    {"TOUCH 6", FAM_GLO, SC_SYN, GR_ATC, {85, 86, 0xFF, 0xFF}},  /* X_PRISE X_PFALL (key_touch.c kt_lean, pat_cfg.h) */
#endif
#else
    {"MASTER", FAM_GLO, SC_GLOBAL, GR_NONE, {G_DUST, G_DUCK, G_FILT, G_ROLL}},
    {"SYSTEM", FAM_GLO, SC_GLOBAL, GR_NONE, {G_MIDI, G_SYNC, G_ROUTE, G_INFO}},
    {"DRUMS", FAM_GLO, SC_GLOBAL, GR_NONE, {G_DRCH, G_DRLVL, G_DRREV, G_DRDLY}},   /* GM kit on MIDI ch 10 */
#endif
    {"PRESETS", FAM_SAVE, SC_GLOBAL, GR_BROWSE, {0xFF, 0xFF, 0xFF, 0xFF}},   /* browser: PRESETS knob / KNOB 1 */
    {"USER", FAM_SAVE, SC_GLOBAL, GR_USER, {0xFF, 0xFF, 0xFF, 0xFF}},       /* user presets: SLOT LOAD ERASE SAVE */
#if !CLIPPER_SYNTH                                   /* (Clipper 1.0: no projects on the FM-1: one instrument, kept by
                                                     * the autosave; the sounds by the user presets) */
    {"PROJECT", FAM_SAVE, SC_GLOBAL, GR_SLOTS, {G_SLOT, 0xFF, G_LOAD, G_SAVE}},
#endif
    {"TOOLS", FAM_SAVE, SC_GLOBAL, GR_NONE, {G_CLRSEQ, G_INITSND, 0xFF, G_NEWPRJ}},
    {"ARP", FAM_ARP, SC_TRACK, GR_ARP, {P_AMODE, P_ARATE, P_AOCT, P_AGATE}},
    {"ARP 2", FAM_ARP, SC_TRACK, GR_NONE, {P_ASWING, P_APROB, P_AHOLD, P_AORDER}},
#if CLIPPER_SYNTH
    {"SPICE", FAM_ARP, SC_TRACK, GR_NONE, {P_SPICE, P_DICE, P_SLOOP, 0xFF}},   /* (0.8) the arp's and the steps' notes
                                                                                * vary; (0.9) TURNG: MUTAT SEED LOOP */
#endif
    {"NOTES", FAM_SEQ, SC_STEP, GR_ROLL, {0, 1, 2, 3}},   /* (1.0, after SLOOP 2.7.2: the NOTES editor; "STEP" before) */
    {"PATTERN", FAM_SEQ, SC_TRACK, GR_STEPS, {P_SLEN, P_SDIV, P_SSWING, P_SGATE}},
    {"SONG", CLIPPER_SYNTH ? FAM_TRK : FAM_SEQ, SC_SONG, GR_NONE, {0xFF, 0xFF, 0xFF, 0xFF}},   /* (Clipper: out of reach) */
    {"TRACKS", FAM_TRK, SC_TRK, GR_TRK, {0, 1, 2, 3}},   /* REC button; TRACK LEVEL LEN PAN */
    {"DRUMS", FAM_TRK, SC_DRUM, GR_NONE, {0xFF,0xFF,0xFF,0xFF}},
};
#define NPAGES (sizeof(PAGES) / sizeof(PAGES[0]))
/* a page not there for the sound selected (0.6: FMX's own pages on another engine): the page walks pass it */
UI_SMALL static int page_hidden(uint32_t i)
{
#if CLIPPER_SYNTH
    const page_t *pg = &PAGES[i < NPAGES ? i : 0];
    uint32_t e = TSEL->eng_req, id = pg->id[0];     /* (the EDIT 3 pages: MARE's, PONY's, RODEO's, on their engine) */
    return i < NPAGES && ((pg->graph == GR_FMX && e != ENGI_FMX) ||
                          (pg->scope == SC_TRACK &&
                           e != (id == P_MFFM ? ENGI_MARE : id == P_JRNG ? ENGI_PONY : id == P_OKYB ? ENGI_RODEO : e)));
#else
    (void)i;
    return 0;
#endif
}

/* the drum track has no sound of its own: it uses the global pages (not the preset
 * pages, nor TOOLS > INIT: page_desc), STEP, PATTERN, SLICER and TRACKS; every other page
 * shows "DRUM TRACK" */
UI_SMALL static int page_for_drum(const page_t *pg)
{
    if (pg->scope == SC_STEP)                       /* (2.4) the synth steps' roll: the drums have their grid. Reached */
        return 0;                                   /* by ALGO from a synth track, it drew the drum steps as notes */
    if (pg->scope == SC_GLOBAL)
        return pg->graph != GR_BROWSE && pg->graph != GR_USER;
    return pg->scope != SC_ENGINE && (pg->scope != SC_TRACK || pg->fam == FAM_SEQ || pg->graph == GR_SLCR);
}

#if CLIPPER_SYNTH
static const param_desc_t *syn_desc(uint32_t id, int16_t **valp);   /* ui_synth.c */
#endif
UI_SMALL static const param_desc_t *page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t id = pg->id[slot];
    if (id == 0xFFu || (is_drum(TSEL) && (!page_for_drum(pg) || (pg->scope == SC_GLOBAL && id == G_INITSND)))) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_STEP || pg->scope == SC_TRK) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_GLOBAL) {
        *valp = &song.g[id];
        return &GP[id];
    }
#if CLIPPER_SYNTH
    if (pg->scope == SC_SYN)
        return syn_desc(id, valp);
    if (id == P_FMODEL && pg->scope == SC_TRACK && !((1u << TSEL->eng_req % 32u) & FM_OWN_ENG)) {
        *valp = 0;                                  /* MODL: the engines with a MODEL filter of their own alone */
        return 0;
    }
#endif
    *valp = &TSEL->p[id];
    return track_desc(TSEL, id);
}
