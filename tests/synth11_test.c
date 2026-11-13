/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (Clipper) */
/* After Clipper 1.0 (CLIPPER_SYNTH 1): the common values after the matrix's slot 12 (P>VCO), and where they are kept;
 * the matrix's ERATE (the key follow of the envelopes) and KEY > CUT (the filter's).
 *   P>VCO    LFO > MOD's 4th value, BOTH by default; another engine than PALOMINO sounds the same with VCO1 / VCO2
 *   ERATE    the matrix's 16th destination; none: every time index as it is; KEY > ERATE +63: ENV 1's attack, decay,
 *            release and ENV 2's about half an octave above C4, twice an octave below, as at C4 there; VEL > ERATE: a
 *            harder note's attack faster; every engine on the voice's envelope hears it (FM6 aside); slots 1..6 keep
 *            it in the user presets' 4 bits
 *   KEY>CUT  the filter's key follow on top of an engine's own: PALOMINO at C6 darker with AMT -32 (its tracking gone),
 *            brighter with +32 (twice)
 *   STORE    projects: both sounds' values after 1.0's check byte, which stays where 1.0 reads it; a 1.0 project (nothing
 *            there): the defaults, 1.0's values kept. User presets: every value back; the tail's second part after
 *            1.0's (1.0 reads its 472 bytes as before); a save without it (1.0's): the defaults
 *   PAGES    MOD: VEL>F MW> AT> P>VCO */
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
    printf("synth11: %-96s %s\n", what, c ? "ok" : "FAIL");
    fails += !c;
}

static uint64_t hash;
static void run(uint32_t nb)
{
    static int32_t o[CTL * 2];
    uint32_t b, i;
    hash = 1469598103934665603ull;
    for (b = 0; b < nb; b++) {
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            hash = (hash ^ (uint32_t)o[i]) * 1099511628211ull;
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
/* a chord through the whole mix, from the same state each time: its hash */
static uint64_t chord(uint32_t eng, uint32_t pi, void (*set)(track_t *))
{
    static const uint8_t ch[4] = {36, 55, 64, 84};
    track_t *t = &trk[0];
    uint32_t i;
    quiet();
    host_preset(t, eng, pi);
    t->p[P_AMODE] = 0, t->p[P_VOICE] = V_POLY;
    t->p[P_LD_PIT] = 30, t->p[P_ED_PIT] = -20, t->p[P_LRATE] = 90;
    if (set)
        set(t);
    for (i = 0; i < 4u; i++)
        trk_note_on(t, ch[i], 100);
    run(300);
    for (i = 0; i < 4u; i++)
        trk_note_off(t, ch[i]);
    run(200);
    return hash;
}
/* .. in a child process: the same state from power-on for each */
static uint64_t chord_fresh(uint32_t eng, uint32_t pi, void (*set)(track_t *))
{
    int fd[2];
    pid_t c;
    uint64_t h = 0;
    if (pipe(fd))
        return 0;
    c = fork();
    if (!c) {
        h = chord(eng, pi, set);
        _exit(write(fd[1], &h, sizeof h) == sizeof h ? 0 : 1);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != sizeof h)
        h = 0;
    close(fd[0]);
    waitpid(c, 0, 0);
    return h;
}
static void set_vco1(track_t *t) { t->p[P_PTGT] = PT_VCO1; }
static void set_vco2(track_t *t) { t->p[P_PTGT] = PT_VCO2; }
static void set_keyrate(track_t *t) { t->p[P_M6SRC] = MS_KEY, t->p[P_M6DST] = MT_ERATE, t->p[P_M6AMT] = 63; }

/* control ticks (blocks) the voice of one note takes through the mix: its attack (ENV 1, or ENV 2), its decay to within
 * 1/64 of the sustain, its release to the end. The matrix: KEY > ERATE at amt (slot 7), ENV 2 > SHAPE (it ticks) */
static void env_blocks(uint32_t note, int32_t amt, uint32_t src, int env2, uint32_t vel, uint32_t *a, uint32_t *d, uint32_t *r)
{
    static int32_t o[CTL * 2];
    track_t *t = &trk[0];
    voice_t *v = 0;
    uint32_t n, i;
    quiet();
    host_preset(t, 0, host_pi(0, "BOOM BASS"));
    t->p[P_AMODE] = 0, t->p[P_VOICE] = V_POLY;
    t->p[P_ATK] = t->p[P_DEC] = t->p[P_REL] = 70, t->p[P_SUS] = 40;
    t->p[P_E2ATK] = t->p[P_E2DEC] = t->p[P_E2REL] = 70, t->p[P_E2SUS] = 40, t->p[P_E2LOOP] = 0;
    t->p[P_M7SRC] = (int16_t)src, t->p[P_M7DST] = MT_ERATE, t->p[P_M7AMT] = (int16_t)amt;
    t->p[P_M8SRC] = MS_ENV2, t->p[P_M8DST] = MT_SHAPE, t->p[P_M8AMT] = 1;
    trk_note_on(t, note, vel);
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].note == note)
            v = &t->v[i];
    for (n = 0; v && n < 100000u && (env2 ? v->e2st : v->stage) == 1u; n++)
        mix_block(o, CTL);
    *a = n;
    for (n = 0; v && n < 100000u && (env2 ? v->env2 : v->env) - (40 << 17) > (1 << 24) / 64; n++)
        mix_block(o, CTL);
    *d = n;
    trk_note_off(t, note);
    for (n = 0; v && n < 100000u && (env2 ? v->e2st : v->active); n++)
        mix_block(o, CTL);
    *r = n;
    t->p[P_M7SRC] = t->p[P_M8SRC] = MS_OFF;
}
static void t_erate(void)
{
    static const uint8_t nt[3] = {48, 60, 72};
    uint32_t a[3], d[3], r[3], a2[3], d2[3], r2[3], k, i, same = 1, e, moved = 1, c0, c4, x;
    voice_t v;
    char w[220];
    ok(MT_ERATE == 15 && MT_RES2 == 17 && MT_COUNT >= 18 &&   /* (Stallion: more after RES2) */
        TP[P_M1DST].max == MT_COUNT - 1 && TP[P_M7DST].max == MT_COUNT - 1 &&
       !strcmp(N_MDST[MT_ERATE], "ERATE"), "ERATE: the matrix's 16th destination (4 bits: the user presets' slots 1..6)");
    memset(&v, 0, sizeof v);
    for (i = 0; i < 128u; i++)
        same &= env_rate(&v, (int32_t)i) == i;
    ok(same, "ERATE none: every time index as it is (the envelopes bit for bit)");
    for (k = 0; k < 3u; k++) {
        env_blocks(nt[k], 63, MS_KEY, 0, 100, &a[k], &d[k], &r[k]);
        env_blocks(nt[k], 63, MS_KEY, 1, 100, &a2[k], &d2[k], &r2[k]);
    }
#define HALF(x, y) ((double)(x) / (y) > 0.4 && (double)(x) / (y) < 0.62)
    snprintf(w, sizeof w, "KEY > ERATE +63, ENV 1 at C3 / C4 / C5: attack %u / %u / %u ticks, decay %u / %u / %u, release "
             "%u / %u / %u", a[0], a[1], a[2], d[0], d[1], d[2], r[0], r[1], r[2]);
    ok(HALF(a[2], a[1]) && HALF(a[1], a[0]) && HALF(d[2], d[1]) && HALF(d[1], d[0]) && HALF(r[2], r[1]) && HALF(r[1], r[0]), w);
    snprintf(w, sizeof w, "KEY > ERATE +63, ENV 2 too (attack %u / %u / %u, decay %u / %u / %u, release %u / %u / %u)",
             a2[0], a2[1], a2[2], d2[0], d2[1], d2[2], r2[0], r2[1], r2[2]);
    ok(HALF(a2[2], a2[1]) && HALF(a2[1], a2[0]) && HALF(d2[2], d2[1]) && HALF(d2[1], d2[0]) && HALF(r2[2], r2[1]) &&
       HALF(r2[1], r2[0]), w);
#undef HALF
    env_blocks(60, 0, MS_OFF, 0, 100, &c0, &x, &x);
    env_blocks(60, -63, MS_KEY, 0, 100, &c4, &x, &x);
    env_blocks(72, -63, MS_KEY, 0, 100, &x, &k, &i);
    snprintf(w, sizeof w, "KEY > ERATE: C4 as without it (%u, %u ticks); -63 slower up the keys (C5 attack %u)", a[1], c0, x);
    ok(a[1] == c0 && c4 == c0 && x > c0 * 3 / 2, w);
    env_blocks(60, 63, MS_VEL, 0, 127, &c4, &x, &x);
    env_blocks(60, 63, MS_VEL, 0, 20, &c0, &x, &x);
    snprintf(w, sizeof w, "VEL > ERATE: the attack of a hard note faster than a soft one's (%u, %u ticks)", c4, c0);
    ok(c4 * 2 < c0, w);
    for (e = 0; e < NENGINES; e++)                     /* every engine on the voice's envelope hears it */
        if (e != ENGI_GONE && !ENGINES[e]->ownenv && chord_fresh(e, host_pi(e, "INIT"), 0) == chord_fresh(e, host_pi(e, "INIT"), set_keyrate))
            moved = 0, printf("  %s: KEY > ERATE changes nothing\n", ENGINES[e]->name);
    ok(moved, "KEY > ERATE: every engine on the voice's envelope sounds otherwise (FM6 aside: its own envelopes)");
}

/* PALOMINO at C6, CUT 40, KEY > CUT at amt: the energy through the filter */
static double pal_c6(int32_t amt)
{
    static int32_t o[CTL * 2];
    track_t *t = &trk[0];
    double e = 0;
    uint32_t b, i;
    quiet();
    host_preset(t, ENGI_PALOMINO, host_pi(ENGI_PALOMINO, "INIT"));
    t->p[P_AMODE] = 0, t->p[P_VOICE] = V_POLY, t->p[P_LD_PIT] = t->p[P_ED_FLT] = 0, t->p[P_SUS] = 127;
    t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0, t->p[P_SPREAD] = t->p[P_VINT] = 0;
    t->p[P_E5] = 40, t->p[P_E6] = 0;
    t->p[P_M6SRC] = MS_KEY, t->p[P_M6DST] = MT_CUT, t->p[P_M6AMT] = (int16_t)amt;
    trk_note_on(t, 84, 100);
    run(100);
    for (b = 0; b < 200u; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            e += (double)o[2u * i] * o[2u * i];
    }
    trk_note_off(t, 84);
    t->p[P_M6SRC] = MS_OFF;
    return e;
}
static void t_keycut(void)
{
    double lo = pal_c6(-32), mid = pal_c6(0), hi = pal_c6(32);
    char w[200];
    snprintf(w, sizeof w, "KEY > CUT: PALOMINO at C6 darker with AMT -32 (its tracking cancelled, %+.1f dB), brighter with +32 "
             "(%+.1f dB)", 10 * log10(lo / mid), 10 * log10(hi / mid));
    ok(lo * 2 < mid && hi > 2 * mid, w);
}

static void t_ptgt(void)
{
    uint32_t e, same = 1, moved = 0;
    ok(TP[P_PTGT].def == PT_BOTH && TP[P_PTGT].min == PT_BOTH && TP[P_PTGT].max == PT_VCO2 &&
       !strcmp(TP[P_PTGT].label, "P>VCO") && !strcmp(TP[P_PTGT].names[PT_VCO2], "VCO2"),
       "P>VCO: BOTH VCO1 VCO2, BOTH by default");
    for (e = 0; e < NENGINES; e++) {                   /* the others ignore it; PALOMINO and MARE do not */
        if (e == ENGI_GONE)                            /* (1.0: LOFI's retired place: no engine) */
            continue;
        uint32_t pi = e == ENGI_PALOMINO ? host_pi(e, "MID STRINGS") : host_pi(e, "INIT");   /* (PALOMINO: both VCOs heard,
                                                                                         * MIX 0; its INIT is VCO-1 alone) */
        uint64_t h0 = chord_fresh(e, pi, 0), h1 = chord_fresh(e, pi, set_vco1), h2 = chord_fresh(e, pi, set_vco2);
        if (e == ENGI_PALOMINO)
            moved = h1 != h0 && h2 != h0 && h1 != h2;
        else if (e == ENGI_MARE)                      /* (MARE's: tests/mare_test.c, LFO > PIT on one VCO) */
            continue;
        else if (h1 != h0 || h2 != h0)
            same = 0, printf("  %s: P>VCO changes its sound\n", ENGINES[e]->name);
    }
    ok(same && moved, "P>VCO: every engine but PALOMINO and MARE sounds the same with VCO1 / VCO2 (bit for bit), PALOMINO "
       "not (MARE's: tests/mare_test.c)");
}

static project_t pj;
static void t_store(void)
{
    track_t *a = &trk[0], *b = &trk[1];
    uint32_t i, k, good;
    quiet();
    for (i = 0; i < NSTEP; i++)
        trk[2].step[i].n = 0;
    for (i = P_SPREAD; i < P_E0; i++)
        a->p[i] = b->p[i] = TP[i].def;
    a->p[P_SPREAD] = 77, b->p[P_SPREAD] = 33;
    a->p[P_PTGT] = PT_VCO2, b->p[P_PTGT] = PT_VCO1;
    a->p[P_M2DST] = MT_ERATE, b->p[P_M10DST] = MT_ERATE;
    proj_capture(&pj);
    ok(PROJ_X10_CK == 56u && PROJ_X11_CK < NSTEP && proj_x10_ok(pj.t[2].micro) && proj_x11_ok(pj.t[2].micro),
       "project: 1.0's check byte where 1.0 reads it (56), the later values after it with their own");
    a->p[P_SPREAD] = b->p[P_SPREAD] = 0, a->p[P_PTGT] = b->p[P_PTGT] = PT_BOTH;
    a->p[P_M2DST] = b->p[P_M10DST] = MT_OFF;
    proj_apply(&pj, 1);
    ok(a->p[P_SPREAD] == 77 && b->p[P_SPREAD] == 33 && a->p[P_PTGT] == PT_VCO2 && b->p[P_PTGT] == PT_VCO1 &&
       a->p[P_M2DST] == MT_ERATE && b->p[P_M10DST] == MT_ERATE, "project: P>VCO of both sounds kept, 1.0's values too, ERATE");
    for (i = PROJ_X11_AT; i <= PROJ_X11_CK; i++)       /* (as 1.0 wrote it: nothing after its check byte) */
        pj.t[2].micro[i] = 0;
    memset(&proj_xss[proj_x10_at(&pj)], 0, sizeof proj_xss[0]);   /* (Stallion: nor the block beside it) */
    proj_apply(&pj, 1);
    ok(a->p[P_SPREAD] == 77 && b->p[P_SPREAD] == 33 && a->p[P_PTGT] == TP[P_PTGT].def && b->p[P_PTGT] == TP[P_PTGT].def,
       "project: a 1.0 project: P>VCO at its default, 1.0's values kept");
    {   /* (integration-1.1) track 3's nudges: 1.0's block, P>VCO and its check, then REVERB 2's SHIM and its check */
        int16_t keep = song.g[G_RSHIM];
        a->p[P_PTGT] = PT_VCO2, b->p[P_PTGT] = PT_VCO1, song.g[G_RSHIM] = 99;
        proj_capture(&pj);
        a->p[P_PTGT] = b->p[P_PTGT] = PT_BOTH, song.g[G_RSHIM] = 0;
        proj_apply(&pj, 1);
        good = a->p[P_PTGT] == PT_VCO2 && b->p[P_PTGT] == PT_VCO1 && song.g[G_RSHIM] == 99 && PROJ_X11_CK == 59u &&
               PROJ_X10_SH == 60u;
        pj.t[2].micro[PROJ_X10_SH] = pj.t[2].micro[PROJ_X10_SH + 1u] = 0;   /* (a P>VCO-only project) */
        memset(&proj_xss[proj_x10_at(&pj)], 0, sizeof proj_xss[0]);   /* (Stallion: nor the block beside it) */
        proj_apply(&pj, 1);
        good &= a->p[P_PTGT] == PT_VCO2 && song.g[G_RSHIM] == 0;
        song.g[G_RSHIM] = 99;
        proj_capture(&pj);
        for (i = PROJ_X11_AT; i <= PROJ_X11_CK; i++)   /* (SHIM without P>VCO's part) */
            pj.t[2].micro[i] = 0;
        memset(&proj_xss[proj_x10_at(&pj)], 0, sizeof proj_xss[0]);   /* (Stallion: nor the block beside it) */
        a->p[P_PTGT] = PT_VCO1, song.g[G_RSHIM] = 0;
        proj_apply(&pj, 1);
        good &= a->p[P_PTGT] == TP[P_PTGT].def && song.g[G_RSHIM] == 99 && a->p[P_SPREAD] == 77;
        ok(good, "project: P>VCO (57..59) and REVERB 2's SHIM (60, 61) side by side in track 3's nudges, each on its own check");
        song.g[G_RSHIM] = keep, a->p[P_PTGT] = b->p[P_PTGT] = PT_BOTH;
    }
    {   /* user presets */
        up_rec_t r;
        int16_t v[P_COUNT], out[P_COUNT], def[P_COUNT];
        for (i = 0; i < P_COUNT; i++)
            def[i] = i >= P_E0 ? 0 : TP[i].def;
        for (i = 0; i < P_COUNT; i++)
            v[i] = def[i];
        v[P_SPREAD] = 90, v[P_PTGT] = PT_VCO1;
        v[P_M1SRC] = MS_KEY, v[P_M1DST] = MT_ERATE, v[P_M1AMT] = 50;   /* (ERATE in a slot of the record's 4 bits) */
        v[P_M9SRC] = MS_VEL, v[P_M9DST] = MT_ERATE, v[P_M9AMT] = -20;
        memset(&r, 0, sizeof r);
        r.used = UP_USED, r.name[0] = 'T';
        up_rec_set(&r, v);
        up_params(&r, out, def);
        ok(out[P_PTGT] == PT_VCO1 && out[P_SPREAD] == 90 && out[P_M1DST] == MT_ERATE && out[P_M1AMT] == 50 &&
           out[P_M9DST] == MT_ERATE && out[P_M9AMT] == -20, "user presets: P>VCO back, 1.0's values too, ERATE in slots 1 and 9");
        /* P>VCO, SLOPE and FILTER 2 together, CUT2 / RES2 in slots 2, 6, 7 (preset format 2: every value a byte) */
        for (i = 0; i < P_COUNT; i++)
            v[i] = def[i];
        v[P_SPREAD] = 90, v[P_PTGT] = PT_VCO2, v[P_FSLOPE] = FS_48, v[P_F2MODEL] = FM_OTA, v[P_F2CUT] = 3;
        v[P_F2RES] = 127, v[P_F2ROUTE] = F2_SPLIT, v[P_F2LINK] = 0;
        v[P_M1SRC] = MS_KEY, v[P_M1DST] = MT_ERATE, v[P_M1AMT] = 50;
        v[P_M2SRC] = MS_MAC1, v[P_M2DST] = MT_CUT2, v[P_M2AMT] = -30;
        v[P_M6SRC] = MS_VEL, v[P_M6DST] = MT_RES2, v[P_M6AMT] = 63;
        v[P_M7SRC] = MS_VEL, v[P_M7DST] = MT_RES2, v[P_M7AMT] = 1;
        up_rec_set(&r, v);
        up_params(&r, out, def);
        good = 1;
        for (k = 0; k < P_COUNT; k++)
            good &= out[k] == v[k] || k >= P_E0;
        ok(good && MT_CUT2 == 16 && MT_RES2 == 17,
           "user presets: P>VCO, SLOPE, FILTER 2 and CUT2 / RES2 (slots 2, 6, 7); ERATE, slot 1");
    }
}

static void t_pages(void)
{
    uint32_t i, found = 0;
    for (i = 0; i < NELEM(PAGES); i++)
        if (!strcmp(PAGES[i].title, "MOD"))
            found = PAGES[i].id[0] == P_VELF && PAGES[i].id[1] == P_MWDST && PAGES[i].id[2] == P_ATDST &&
                    PAGES[i].id[3] == P_PTGT;
    ok(found, "PAGES: MOD holds VEL>F MW> AT> P>VCO");
}

int main(void)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NPART; i++)
        host_preset(&trk[i], TRK_DEF[i][0], TRK_DEF[i][1]);
    clipper_proj_neutral();
    trk[1].p[P_LYMODE] = LY_OFF;
    t_ptgt();
    t_erate();
    t_keycut();
    t_store();
    t_pages();
    printf(fails ? "synth11: %d FAILED\n" : "synth11: all passed\n", fails);
    return fails;
}
