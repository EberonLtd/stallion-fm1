/* SPDX-License-Identifier: GPL-3.0-only */
/* Stallion R6: every name the firmware shows, as it holds them at run time, one a line ("kind<TAB>where<TAB>text"),
 * for tests/denylist_test.py: the engines' names, page titles and presets, their EDIT labels and value names, the
 * sound's and the global parameters' labels and value names, the drum kits.
 *   cc -O1 -w -DCLIPPER_SYNTH=1 -Ibuild/gen -Ifirmware/src -Ifirmware/hal tests/denylist_dump.c -lm -o denylist_dump */
#define main hostsim_main
#include "hostsim.c"
#undef main

static void names(const char *where, const param_desc_t *d)
{
    char val[32], last[32] = "";
    const char *unit;
    int32_t v;
    if (!d->label)
        return;
    printf("label\t%s\t%s\n", where, d->label);
    if (!d->names)
        return;
    for (v = d->min; v <= d->max; v++) {               /* as the screen shows them (params.c param_format) */
        param_format(d, v, val, &unit);
        if (strcmp(val, last))
            printf("value\t%s %s\t%s\n", where, d->label, val);
        snprintf(last, sizeof last, "%s", val);
    }
}

int main(void)
{
    uint32_t e, k;
    char w[48];
    for (e = 0; e < NENGINES; e++) {
        const engine_t *g = ENGINES[e];
        printf("engine\t%u\t%s\n", e, g->name);
        printf("title\t%s\t%s\n", g->name, g->page_title[0] ? g->page_title[0] : "");
        printf("title\t%s\t%s\n", g->name, g->page_title[1] ? g->page_title[1] : "");
        for (k = 0; k < g->npresets; k++)
            printf("preset\t%s %u\t%s\n", g->name, k, g->presets[k].name);
        for (k = 0; k < 8u; k++) {
            snprintf(w, sizeof w, "%s E%u", g->name, k);
            names(w, &g->edit[k]);
        }
    }
    for (k = 0; k < NENGINES; k++)
        printf("engine\tENG %u\t%s\n", k, GP[G_ENGSEL].names[k]);
    for (k = 0; k < P_COUNT; k++) {
        snprintf(w, sizeof w, "P%u", k);
        names(w, &TP[k]);
    }
    for (k = 0; k < G_COUNT; k++) {
        snprintf(w, sizeof w, "G%u", k);
        names(w, &GP[k]);
    }
    for (k = 0; k < DRUM_KITS; k++)
        printf("kit\t%u\t%s\n", k, DRUM_KIT_NAMES[k]);
    return 0;
}
