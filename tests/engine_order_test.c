/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Eberon Limited (John Gorman) */
/* (Stallion 1.0) engine IDs and the display order (core.h ENGI_*, engines.c ENG_ORDER): the IDs are permanent (every
 * one as it was at 2c48fa6, LOFI's 3 retired: no name, no preset), the display order holds every live engine exactly
 * once and no retired ID, and for 1.0 it is the IDs' own order (nothing the player sees moved); ALGORITHM's next engine
 * and INFO follow it.
 *   cc -O2 -w -DCLIPPER_SYNTH=1 -Ibuild/gen -Ifirmware/src -Ifirmware/hal tests/engine_order_test.c -lm */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(const char *what, int ok)
{
    printf("engine order: %-92s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void)
{
    /* the IDs at 2c48fa6 (ENGINES[] then), "" where an engine was retired since */
    static const char *const AT_2C48FA6[] = {"ANALOG", "DIGITAL", "PHASE", "", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN",
                                             "FM6", "PHYS", "NOISE", "HYBRID", "WAVE", "ACID", "STRINGS", "MACRO", "VPS",
                                             "TERRAIN", "FMX", "WSEQ", "PONY", "VECTOR", "PALOMINO", "STAMPEDE", "RODEO",
                                             "MARE"};
    static const uint32_t ENGI[] = {ENGI_ANALOG, ENGI_DIGITAL, ENGI_PHASE, ENGI_GONE, ENGI_SAMPLE, ENGI_FORMANT, ENGI_TRIO,
                                    ENGI_DRAWBAR, ENGI_GRAIN, ENGI_FM6, ENGI_PHYS, ENGI_NOISE, ENGI_HYBRID, ENGI_WAVE,
                                    ENGI_ACID, ENGI_STRINGS, ENGI_MACRO, ENGI_VPS, ENGI_TERRAIN, ENGI_FMX, ENGI_WSEQ,
                                    ENGI_PONY, ENGI_VECTOR, ENGI_PALOMINO, ENGI_STAMPEDE, ENGI_RODEO, ENGI_MARE};
    uint32_t e, i, seen[NENGINES] = {0}, ok = 1, live = 0, prev = 0, mono = 1;
    char w[160];
    for (e = 0; e < NELEM(ENGI); e++)
        ok = ok && ENGI[e] == e;
    check("ENGI_* are the IDs they were at 2c48fa6 (ANALOG 0 .. MARE 26, LOFI's 3 retired)", ok && NENGINES == 27u);
    for (e = 0, ok = 1; e < NENGINES; e++)
        ok = ok && !strcmp(ENGINES[e]->name, AT_2C48FA6[e]);
    check("ENGINES[ID] is the engine that ID named at 2c48fa6 (3: no name)", ok);
    check("the retired 3: no preset, ENG_GONE; a sound naming it plays TRIO (eng_live)",
          ENGINES[ENGI_GONE]->npresets == 0u && eng_live(ENGI_GONE) == ENGI_TRIO && eng_live(ENGI_MARE) == ENGI_MARE);
    for (i = 0, ok = 1; i < NENG_ORDER; i++) {
        e = ENG_ORDER[i];
        ok = ok && e < NENGINES && ENGINES[e]->name[0] && ENGINES[e]->npresets && !seen[e]++;
        mono = mono && (i == 0u || e > prev), prev = e;
    }
    for (e = 0; e < NENGINES; e++)
        live += ENGINES[e]->name[0] != 0;
    snprintf(w, sizeof w, "ENG_ORDER: every live engine exactly once (%u of %u), no retired ID", (unsigned)NENG_ORDER,
             (unsigned)live);
    check(w, ok && NENG_ORDER == live && !seen[ENGI_GONE]);
    check("ENG_ORDER for 1.0: the IDs' own order (nothing the player sees moved)", mono);
    for (i = 0, ok = 1; i < NENG_ORDER; i++)
        ok = ok && eng_rank(ENG_ORDER[i]) == i;
    check("eng_rank: each engine's place; the retired 3 at TRIO's", ok && eng_rank(ENGI_GONE) == eng_rank(ENGI_TRIO));
    printf("engine order: %d failed\n", fails);
    return fails != 0;
}
