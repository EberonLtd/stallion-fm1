/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (Clipper) */
/* CHAO, the LFOs' sixth wave (CLIPPER_SYNTH 1): a smooth chaotic wander, a logistic map stepped every half cycle and
 * glided (voice.c lfo_chao), on LFO 1, 2 and 3.
 *   BOUNDED  LFO 1 at four RATEs: in the Q15 range, over most of it
 *   WANDER   no period in 65536 half cycles (13 minutes at the fastest RATE)
 *   SMOOTH   no step of a block larger than SIN's largest at the same RATE
 *   RATE     its turns a second follow RATE as SIN's do
 *   KEY      LFO 2 and 3 KEY: each note its own wander, from the note's own state only (no rng()); LFO 3's apart
 *            from LFO 2's
 *   SOUND    LFO 1 (to the cutoff), LFO 2 FREE and LFO 3 KEY (in the matrix): the sound moves, bounded
 *   SAME     every other wave bit for bit as before CHAO: LFO presets, and each wave on LFO 1, 2 and 3, FREE and KEY
 *            (the hashes of 7008d31, printed by this test built with -DCHAO_HASHES there; since the merge of Clipper 1.3
 *            the same renders with the mix rounded: they match 7008d31's with 1.1's floors); voice_t's size unchanged
 *   VALUES   CHAO the last name of all three WAVEs (an enum: a switch for MORPH, in RANDOM's range); projects and
 *            user presets keep it */
#define main hostsim_main
#include <stddef.h>
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static const uint8_t TRK_DEF[NPART][2] = {{ENGI_HYBRID, 0}, {1, 0}, {4, 5}};   /* (ui.c, Clipper) */
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? TRK_DEF[i][0] : 0u; }
#include "../firmware/src/project.c"
#define UP_HOST 1
#include "../firmware/src/upreset.c"

static int fails;
static void ok(int c, const char *what)
{
    printf("chaos: %-100s %s\n", what, c ? "ok" : "FAIL");
    fails += !c;
}

#define NCAP (4096u * 24u)
static int32_t cl[NCAP], cr[NCAP];
static uint32_t ncap;
static uint64_t hash;
static void run(uint32_t nb)
{
    static int32_t o[CTL * 2];
    uint32_t b, i;
    ncap = 0;
    hash = 1469598103934665603ull;
    for (b = 0; b < nb; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            if (ncap < NCAP) {
                cl[ncap] = o[2 * i];
                cr[ncap++] = o[2 * i + 1];
            }
            hash = (hash ^ (uint32_t)o[2 * i]) * 1099511628211ull;
            hash = (hash ^ (uint32_t)o[2 * i + 1]) * 1099511628211ull;
        }
    }
}
static void quiet(void)
{
    uint32_t i, k;
    for (i = 0; i < NPART; i++)
        trk_all_off(&trk[i]);
    run(400);
    for (i = 0; i < NPART; i++)
        for (k = 0; k < NVOICE; k++)
            trk[i].v[k].active = trk[i].v[k].gate = 0, trk[i].v[k].stage = 0, trk[i].v[k].env = trk[i].v[k].env_out = 0;
    run(300);
}
static const preset_t BOOM_OWN;
static void plain(track_t *t)                         /* ANALOG BOOM BASS as it was (trim then: 3), made plain */
{
    uint32_t k;
    host_preset_own(t, 0, &BOOM_OWN, 3);
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    t->p[P_LD_PIT] = t->p[P_LD_FLT] = t->p[P_LD_SHP] = t->p[P_LD_AMP] = t->p[P_ED_PIT] = t->p[P_ED_FLT] = 0;
    t->p[P_ATK] = 0, t->p[P_SUS] = 127, t->p[P_REL] = 10, t->p[P_VOICE] = V_POLY, t->p[P_PAN] = 0, t->p[P_GLIDE] = 0;
    t->p[P_MWDST] = t->p[P_ATDST] = MD_OFF;
    t->p[P_AMODE] = 0, t->p[P_VINT] = 0, t->p[P_FMODEL] = FM_SVF;
    t->p[P_ITYPE] = 0, t->p[P_IMIX] = 127;
    for (k = 0; k < MX_SLOTS; k++)
        t->p[mx_pid(k) + 2u] = 0;
    for (k = P_SPREAD; k < P_E0; k++)
        t->p[k] = TP[k].def;
}
static int bounded(void)
{
    uint32_t i;
    for (i = 0; i < ncap; i++)
        if (cl[i] >= (1 << 23) || cl[i] <= -(1 << 23) || cr[i] >= (1 << 23) || cr[i] <= -(1 << 23))
            return 0;
    return 1;
}
static double bright(uint32_t a, uint32_t n)           /* the first difference's rms over the signal's */
{
    double e = 0, d = 0;
    uint32_t i;
    for (i = a + 1u; i < a + n && i < ncap; i++)
        e += (double)cl[i] * cl[i], d += (double)(cl[i] - cl[i - 1]) * (cl[i] - cl[i - 1]);
    return sqrt(d / (e + 1e-9));
}
static double bright_swing(void)                        /* over the capture past its first 4096 samples, by 1024 */
{
    double lo = 1e9, hi = 0;
    uint32_t a;
    for (a = 4096; a + 1024u <= ncap; a += 1024u) {
        double x = bright(a, 1024);
        lo = x < lo ? x : lo, hi = x > hi ? x : hi;
    }
    return hi / (lo + 1e-9);
}
static void slot(track_t *t, uint32_t k, uint32_t src, uint32_t dst, int32_t amt)
{
    t->p[mx_pid(k)] = (int16_t)src, t->p[mx_pid(k) + 1u] = (int16_t)dst, t->p[mx_pid(k) + 2u] = (int16_t)amt;
}

/* ---- SAME: renders of LFO presets and of every wave before CHAO on the three LFOs, FREE and KEY */
static const char *const LFO_PRESETS[] = {"WOBBLE", "DRIFT BASS", "GLASS PAD", "BP PAD", "WHOOSH", "TAPE STR"};
/* (Stallion 1.0) those factory sounds as they were when the hashes were taken (the 1.0 banks changed or dropped them):
 * ANALOG, ANALOG, DIGITAL, TRIO, NOISE, STRINGS, as Clipper 1.3 had them */
static const uint8_t LFO_ENG[] = {0, 0, 1, 6, ENGI_NOISE, ENGI_STRINGS};
static const preset_t LFO_OWN[] = {
    {"WOBBLE", {0, 9, 64, 0, 38, 72, 62, 30}, {0, 64, 127, 22}, 0, 1, FX(10, 0, 0, 4),
     XP(P_LRATE + 1, 74, P_LD_FLT + 1, 40, P_TRANS + 1, -24)},
    {"DRIFT BASS", {0, 22, 64, 0, 56, 22, 36, 40}, {0, 70, 118, 28}, 0, 1, FX(0, 22, 0, 6),
     XP(P_LRATE + 1, 22, P_LD_FLT + 1, 10, P_TRANS + 1, -24)},
    {"GLASS PAD", {4, 2, 1, 5, 34, 100, 0, 0}, {62, 92, 112, 92}, 0, 0, FX(0, 50, 26, 62), XP(P_LD_PIT + 1, 1, P_LRATE + 1, 40)},
    {"BP PAD", {0, 0, 12, 24, 1, 70, 60, 64}, {60, 90, 115, 85}, 0, 0, FX(0, 50, 20, 60), XP(P_LRATE + 1, 30, P_LD_FLT + 1, 20)},
    {"WHOOSH", {0, 60, 60, 90, 127, 0, 60, 0}, {60, 90, 110, 90}, 0, 0, FX(0, 20, 20, 60), XP(P_LRATE + 1, 20, P_LD_FLT + 1, 30)},
    {"TAPE STR", {60, 127, 50, 127, 80, 20, 0, 10}, {60, 100, 120, 80}, 0, 0, FX(0, 0, 10, 55), XP(P_LRATE + 1, 30, P_LD_PIT + 1, 2)},
};
_Static_assert(NELEM(LFO_OWN) == NELEM(LFO_PRESETS) && NELEM(LFO_ENG) == NELEM(LFO_PRESETS), "a sound for each");
/* Clipper 1.3's power-on sounds: HYBRID STALLION (its patch, now 23), DIGITAL TINE KEYS, SAMPLE GLASS HARP; their trims */
static const preset_t PART_OWN[NPART] = {
    {"STALLION", {0, 0, 0, 23, 70, 50, 0, 0}, {2, 90, 110, 70}, 0, 0, FX(0, 20, 18, 30)},
    {"TINE KEYS", {4, 1, 1, 14, 26, 35, 0, 0}, {0, 85, 35, 55}, 0, 0, FX(0, 30, 12, 26), XP(P_LD_AMP + 1, 26, P_LRATE + 1, 84)},
    {"GLASS HARP", {5, 0, 0, 1, 118, 0, 0, 0}, {18, 100, 116, 80}, 0, 0, FX(0, 22, 30, 66)},
};
static const int8_t PART_TRIM[NPART] = {-1, 3, 0};
/* ANALOG BOOM BASS as it was then (the plain sound of the wave renders, t_sound) */
static const preset_t BOOM_OWN = {"BOOM BASS", {3, 0, 0, 0, 127, 0, 48, 0}, {0, 112, 0, 52}, 0, 1, FX(0, 0, 0, 0),
                                  XP(P_GLIDE + 1, 59, P_GLMODE + 1, 1, P_ED_PIT + 1, 18, P_TRANS + 1, -24)};
static const int8_t LFO_TRIM[] = {7, 3, -17, -8, 10, -3};   /* (Stallion) their level trims when the hashes were taken: the
                                                           * re-levelling (tools/level_presets.py) moves the trims, not the waves */
#define NSAME (NELEM(LFO_PRESETS) + 10u)
static const uint64_t BEFORE[NSAME] = {   /* (7008d31's renders, as the merge of Clipper 1.3 gives them: its mix rounded to
                                         * the nearest; with 1.1's floors this tree gave 7008d31's hashes, checked. Since
                                         * 8cbf6e7 the amplitude envelope's shortest attack and release (voice.c ENV_ATK_MIN,
                                         * ENV_REL_MIN): these are the same renders through it; with both at 0 the tree gives
                                         * 7008d31's hashes again) */
    0x674ab42d85ba19e1ull, 0xb7d58747b357242dull, 0x5c730a1967802cd5ull, 0xed4bb7cf87b49986ull, 0xefd26ed0dd32a210ull,
    0x4902173a78caf995ull,   /* (spread-centre: GLASS PAD, BP PAD, WHOOSH, TAPE STR no longer get the automatic SPREAD 48 of
                              * a slow preset; the waves' renders below unchanged) */
    0x170bb5cd84c9555bull,   /* (.. the first after TAPE STR: its spread no longer ramping down in its first block) */ 0xfa7978ab56b54d9eull, 0x1684c0bc156dc2e3ull, 0x0e7fa64951134f74ull, 0x6f49fdf9c6465014ull,
    0xb276d5b93fcfca26ull, 0xe6c5e0b84253e190ull, 0x877eb3946689fbd6ull, 0xbca265e7d93e9290ull, 0xb405aa433d6a8e82ull,
};
#define VOICE_SIZE 168u   /* (sizeof(voice_t) before CHAO: 7008d31's 116, filter2's fs[8] + 32, key-follow's erate + 4, PONY's CHAR jq[4] + 16) */
static uint32_t same_renders(uint64_t *h)              /* the presets found */
{
    static const uint8_t chord[3] = {48, 55, 63};
    track_t *t = &trk[0];
    uint32_t r, k, found = 0;
    for (r = 0; r < NSAME; r++) {
        quiet();
        if (r < NELEM(LFO_PRESETS)) {                  /* a factory sound, as it came then */
            host_preset_own(t, LFO_ENG[r], &LFO_OWN[r], LFO_TRIM[r]);
            if (LFO_OWN[r].mono)                       /* (a mono sound loaded as LEGATO then; Stallion loads it MONO) */
                t->p[P_VOICE] = V_LEGATO;
            found++;
        } else {                                       /* wave w on LFO 1 (cutoff), 2 (cutoff) and 3 (pitch) */
            uint32_t w = (r - NELEM(LFO_PRESETS)) / 2u, key = (r - NELEM(LFO_PRESETS)) % 2u;
            plain(t);
            t->p[P_ED_FX] = -9;                        /* (BOOM BASS's trim then, as LFO_TRIM) */
            t->p[P_E4] = 50;
            t->p[P_LRATE] = 90, t->p[P_LWAVE] = (int16_t)w, t->p[P_LD_FLT] = 30;
            t->p[P_L2RATE] = 100, t->p[P_L2WAVE] = (int16_t)w, t->p[P_L2MODE] = (int16_t)(key ? L2_KEY : L2_FREE);
            t->p[P_L3RATE] = 70, t->p[P_L3WAVE] = (int16_t)w, t->p[P_L3MODE] = (int16_t)(key ? L2_KEY : L2_FREE);
            slot(t, 0, MS_LFO2, MT_CUT, 40);
            slot(t, 6, MS_LFO3, MT_PITCH, 20);
        }
        for (k = 0; k < 3u; k++)
            trk_note_on(t, chord[k], 90u + k * 10u);
        run(700);
        h[r] = hash;
        for (k = 0; k < 3u; k++)
            trk_note_off(t, chord[k]);
        run(200);
        h[r] ^= hash * 31u;
    }
    quiet();
    plain(t);
    return found;
}

#ifndef CHAO_HASHES
/* ---- the wave itself, LFO 1 on a track of its own (track_lfo_tick: one block) */
static track_t lt;
static void lfo_at(int32_t wave, int32_t rate)
{
    memset(&lt, 0, sizeof lt);
    lt.p[P_LWAVE] = (int16_t)wave, lt.p[P_LRATE] = (int16_t)rate;
}
static uint32_t half_cycles(uint32_t n, int32_t rate)    /* blocks for n half cycles */
{
    return (uint32_t)(((uint64_t)n << 31) / LFO_INC[rate]) + 1u;
}

static void t_wave(void)
{
    static const int32_t RATES[4] = {20, 60, 100, 127};
    static int16_t seq[65536u + 4096u];
    char w[200];
    uint32_t i, k, p;
    int32_t lo = 0, hi = 0, inr = 1;
    for (k = 0; k < 4u; k++) {                         /* BOUNDED */
        uint32_t n = half_cycles(3000, RATES[k]);
        int32_t l = 32767, h = -32768;
        lfo_at(LW_CHAO, RATES[k]);
        for (i = 0; i < n; i++) {
            track_lfo_tick(&lt);
            inr &= lt.lfo_val >= -32768 && lt.lfo_val <= 32767;
            l = lt.lfo_val < l ? lt.lfo_val : l, h = lt.lfo_val > h ? lt.lfo_val : h;
        }
        lo = k ? (l > lo ? l : lo) : l, hi = k ? (h < hi ? h : hi) : h;   /* (the narrowest of the four) */
    }
    snprintf(w, sizeof w, "BOUNDED: in the Q15 range at RATE 20 .. 127, over most of it (%d .. %d)", lo, hi);
    ok(inr && lo < -30000 && hi > 30000, w);
    {   /* WANDER: the value of every half cycle at the fastest RATE, no period up to 65535 of them */
        uint32_t n = 0, per = 0;
        lfo_at(LW_CHAO, 127);
        while (n < NELEM(seq)) {
            uint32_t old = lt.lfo_ph;
            track_lfo_tick(&lt);
            if ((lt.lfo_ph ^ old) >> 31)
                seq[n++] = (int16_t)((lt.lfo_rnd >> 16) - 32768u);
        }
        for (p = 1; p < 65536u && !per; p++) {
            for (i = 0; i < 4096u && seq[i] == seq[i + p]; i++)
                ;
            per = i == 4096u ? p : 0u;
        }
        snprintf(w, sizeof w, "WANDER: no period in 65536 half cycles (%.1f minutes at RATE 127; found: %u)",
                 65536.0 * 50.0 / LFO_HZ_X100[127] / 60.0, per);
        ok(!per, w);
    }
    {   /* SMOOTH: the largest step from block to block, CHAO's and SIN's */
        int good = 1;
        char d[120] = "";
        for (k = 0; k < 4u; k++) {
            int32_t m[2] = {0, 0}, prev = 0, wave;
            for (wave = 0; wave < 2; wave++) {
                uint32_t n = half_cycles(600, RATES[k]);
                lfo_at(wave ? LW_CHAO : LW_SIN, RATES[k]);
                track_lfo_tick(&lt);
                prev = lt.lfo_val;
                for (i = 0; i < n; i++) {
                    track_lfo_tick(&lt);
                    m[wave] = abs(lt.lfo_val - prev) > m[wave] ? abs(lt.lfo_val - prev) : m[wave];
                    prev = lt.lfo_val;
                }
            }
            good &= m[1] <= m[0] + m[0] / 20 + 8;
            snprintf(d + strlen(d), sizeof d - strlen(d), "%s%d/%d", k ? " " : "", m[1], m[0]);
        }
        snprintf(w, sizeof w, "SMOOTH: no step of a block above SIN's largest (CHAO/SIN at RATE 20 60 100 127: %s)", d);
        ok(good, w);
    }
    {   /* RATE: turns (the slope changing sign) a second, CHAO's and SIN's, at three RATEs */
        static const int32_t R3[3] = {50, 80, 110};
        double tc[3], ts[3];
        int good = 1;
        for (k = 0; k < 3u; k++) {
            int32_t wave;
            for (wave = 0; wave < 2; wave++) {
                uint32_t n = half_cycles(2000, R3[k]), turns = 0;
                int32_t prev, dir = 0;
                lfo_at(wave ? LW_CHAO : LW_SIN, R3[k]);
                track_lfo_tick(&lt);
                prev = lt.lfo_val;
                for (i = 0; i < n; i++) {
                    int32_t d;
                    track_lfo_tick(&lt);
                    d = lt.lfo_val > prev ? 1 : lt.lfo_val < prev ? -1 : 0;
                    if (d && dir && d != dir)
                        turns++;
                    dir = d ? d : dir;
                    prev = lt.lfo_val;
                }
                (wave ? tc : ts)[k] = turns * ((double)FS / CTL) / n;
            }
        }
        for (k = 1; k < 3u; k++) {
            double want = (double)LFO_HZ_X100[R3[k]] / LFO_HZ_X100[R3[0]], got = tc[k] / tc[0];
            good &= got > want * 0.85 && got < want * 1.15;
        }
        for (k = 0; k < 3u; k++)
            good &= tc[k] > ts[k] * 0.4 && tc[k] < ts[k] * 1.1;
        snprintf(w, sizeof w, "RATE: turns a second follow RATE (CHAO %.2f %.2f %.2f, SIN %.2f %.2f %.2f at 50 80 110)",
                 tc[0], tc[1], tc[2], ts[0], ts[1], ts[2]);
        ok(good, w);
    }
}

static void t_key(void)
{
    track_t *t = &trk[0];
    char w[200];
    uint32_t i, b, follow = 1, moved = 0, apart = 0, notes = 0;
    int16_t y2[NVOICE], y3[NVOICE];
    uint16_t c2[NVOICE], c3[NVOICE];
    quiet();
    plain(t);
    t->p[P_L2RATE] = 110, t->p[P_L2WAVE] = LW_CHAO, t->p[P_L2MODE] = L2_KEY;
    t->p[P_L3RATE] = 110, t->p[P_L3WAVE] = LW_CHAO, t->p[P_L3MODE] = L2_KEY;
    slot(t, 0, MS_LFO2, MT_CUT, 30);
    slot(t, 1, MS_LFO3, MT_SHAPE, 30);
    trk_note_on(t, 48, 100), trk_note_on(t, 55, 100);
    run(1);
    for (i = 0; i < NVOICE; i++) {                     /* each note's state from its start, followed on the side */
        voice_t *v = &t->v[i];
        y2[i] = v->l2rnd, c2[i] = v->l2cx, y3[i] = v->l3rnd, c3[i] = v->l3cx;
        notes += v->active;
    }
    for (b = 0; b < 3000u; b++) {
        uint32_t inc = LFO_INC[110];
        run(1);
        rng();                                         /* (rng() moved on: nothing of it in the wander) */
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &t->v[i];
            if (!v->active)
                continue;
            lfo_chao(&y2[i], &c2[i], v->l2ph, v->l2ph - inc);
            lfo_chao(&y3[i], &c3[i], v->l3ph, v->l3ph - inc);
            follow &= v->l2rnd == y2[i] && v->l2cx == c2[i] && v->l3rnd == y3[i] && v->l3cx == c3[i];
            apart += v->l2rnd != v->l3rnd;
        }
    }
    for (i = 0; i < NVOICE; i++)
        for (b = i + 1u; b < NVOICE; b++)
            moved |= t->v[i].active && t->v[b].active && t->v[i].l2rnd != t->v[b].l2rnd;
    snprintf(w, sizeof w, "KEY: each note its own wander (%u notes), from its own state only, LFO 3's apart from LFO 2's",
             notes);
    ok(notes == 2u && follow && moved && apart > 2u * 2900u, w);
    trk_all_off(t);
}

static void t_sound(void)
{
    track_t *t = &trk[0];
    char w[200];
    double s[4];
    uint32_t c;
    int good = 1;
    for (c = 0; c < 4u; c++) {                         /* none, LFO 1 > cutoff, LFO 2 FREE > CUT, LFO 3 KEY > CUT */
        quiet();
        plain(t);
        t->p[P_E4] = 40;
        t->p[P_LWAVE] = t->p[P_L2WAVE] = t->p[P_L3WAVE] = LW_CHAO;
        t->p[P_LRATE] = t->p[P_L2RATE] = t->p[P_L3RATE] = 100;
        t->p[P_L3MODE] = L2_KEY;
        if (c == 1u)
            t->p[P_LD_FLT] = 40;
        if (c == 2u)
            slot(t, 0, MS_LFO2, MT_CUT, 50);
        if (c == 3u)
            slot(t, 6, MS_LFO3, MT_CUT, 50);
        trk_note_on(t, 48, 100);
        run(2500);
        s[c] = bright_swing();
        good &= bounded();
        trk_note_off(t, 48);
    }
    snprintf(w, sizeof w, "SOUND: LFO 1 / 2 FREE / 3 KEY CHAO move the brightness (swing %.2f %.2f %.2f; none %.2f), bounded",
             s[1], s[2], s[3], s[0]);
    ok(good && s[0] < 1.1 && s[1] > 1.3 && s[2] > 1.3 && s[3] > 1.3, w);
}

static project_t pj;
static void t_values(void)
{
    static const uint32_t ID[3] = {P_LWAVE, P_L2WAVE, P_L3WAVE};
    track_t *a = &trk[0];
    uint32_t k, i, names = 1, kept = 1;
    for (k = 0; k < 3u; k++) {
        const param_desc_t *d = &TP[ID[k]];
        names &= d->fmt == F_ENUM && d->max == LW_CHAO && !strcmp(d->names[LW_CHAO], "CHAO") &&   /* (F_ENUM: */
                 !strcmp(d->names[LW_SH], "S&H");                                                 /* syn_switch) */
    }
    ok(names, "VALUES: CHAO the last name of LFO 1, 2 and 3's WAVE (a switch for MORPH, in RANDOM's range)");
    quiet();
    for (k = 0; k < 3u; k++)
        a->p[ID[k]] = LW_CHAO;
    proj_capture(&pj);
    for (k = 0; k < 3u; k++)
        a->p[ID[k]] = 0;
    proj_apply(&pj, 1);
    for (k = 0; k < 3u; k++)
        kept &= a->p[ID[k]] == LW_CHAO;
    {
        up_rec_t r;
        int16_t v[P_COUNT], out[P_COUNT], def[P_COUNT];
        for (i = 0; i < P_COUNT; i++)
            v[i] = def[i] = i >= P_E0 ? 0 : TP[i].def;
        for (k = 0; k < 3u; k++)
            v[ID[k]] = LW_CHAO;
        memset(&r, 0, sizeof r);
        r.used = UP_USED, r.name[0] = 'T';
        up_rec_set(&r, v);
        up_params(&r, out, def);
        for (k = 0; k < 3u; k++)
            kept &= out[ID[k]] == LW_CHAO;
    }
    ok(kept, "VALUES: projects and user presets keep CHAO on the three LFOs");
}
#endif

int main(void)
{
    uint64_t h[NSAME];
    uint32_t i, same, found;
    host_tracks_init();
    for (i = 0; i < NPART; i++)                        /* the power-on sounds as they were when the hashes were taken
                                                        * (the silent parts' LFOs draw on the random generator too) */
        host_preset_own(&trk[i], i == 0 ? ENGI_HYBRID : i == 1 ? 1u : 4u, &PART_OWN[i], PART_TRIM[i]);
    clipper_proj_neutral();
    trk[1].p[P_LYMODE] = LY_OFF;
    found = same_renders(h);
#ifdef CHAO_HASHES
    for (i = 0; i < NSAME; i++)
        printf("    0x%016llxull,\n", (unsigned long long)h[i]);
    printf("sizeof(voice_t) %u, %u presets\n", (unsigned)sizeof(voice_t), found);
    return 0;
#else
    for (i = 0, same = found == NELEM(LFO_PRESETS); i < NSAME; i++)
        same &= h[i] == BEFORE[i];
    ok(same && sizeof(voice_t) == VOICE_SIZE,
       "SAME: LFO presets, each older wave on LFO 1, 2, 3 (FREE, KEY): bit for bit as before CHAO; voice_t's size");
    t_wave();
    t_key();
    t_sound();
    t_values();
    printf(fails ? "chaos: %d FAILED\n" : "chaos: all passed\n", fails);
    return fails;
#endif
}
