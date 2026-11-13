/* SPDX-License-Identifier: GPL-3.0-only */
/* Stallion: the numbers the installer page states, from the firmware itself (web/make_site.py fills the page's
 * {{ENGINES}} {{SOUNDS}} {{FILTERS}} {{TUNINGS}} with them, so the page never says a stale count): one JSON line
 *   engines   the engines (NENGINES, but the empty place LOFI left: core.h ENGI_GONE)   sounds    their factory presets
 *   filters   the filter models (FM_SVF..FM_LAST) tunings   the TUNING's systems but USER (voice.c TU_N - 1)
 * --presets: every factory preset instead (tools/gen_bank_files.py writes presets-bank-<engine>.txt from it)
 *   cc -O1 -w -DCLIPPER_SYNTH=1 -Ibuild/gen -Ifirmware/src -Ifirmware/hal tools/site_counts.c -lm -o site_counts */
#define main hostsim_main
#include "../tests/hostsim.c"
#undef main

int main(int argc, char **argv)
{
    uint32_t e, n = 0, ne = 0;
    if (argc > 1 && !strcmp(argv[1], "--presets")) {   /* (tools/gen_bank_files.py) every factory preset: engine,
                                                       * its name, the preset's index and name, tab-separated */
        for (e = 0; e < NENGINES; e++)
            for (n = 0; n < ENGINES[e]->npresets; n++)
                printf("%u\t%s\t%u\t%s\n", (unsigned)e, ENGINES[e]->name, (unsigned)n, ENGINES[e]->presets[n].name);
        return 0;
    }
    for (e = 0; e < NENGINES; e++) {
        n += ENGINES[e]->npresets;
        ne += ENGINES[e]->name[0] != 0;              /* (1.0: 3 has none, core.h ENGI_GONE) */
    }
    printf("{\"engines\": %u, \"sounds\": %u, \"filters\": %u, \"tunings\": %u}\n", (unsigned)ne, (unsigned)n,
           (unsigned)(FM_LAST - FM_SVF + 1), (unsigned)(TU_N - 1));
    return 0;
}
