/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* (Stallion) The factory presets' patching side tables (core.h preset_patch_t, params.c PATCH_TABLES, each engine's
 * ..._PATCHES): every factory preset of every engine loaded as the FM-1 loads it (ui.c apply_preset_to, after the
 * matrix was filled with junk), then its 24 matrix slots compared with what its x[] and its table entry say:
 *   - a slot the entry names holds exactly that cable (source, destination, amount), none pruned (a live destination)
 *   - every other slot clear (or as x[] set it), for every preset, with an entry or without
 *   - the entry's extras set; each entry's slots 1..24, each named once, none also set by x[]; each amount in AMT's
 *     range -64..63 (a load clamps it: the sound would not be its table's)
 *   - each entry points at a preset of its engine (never INIT), one entry a preset, in the table's order; each
 *     showcase patch (by name) has its entry, 3 cables or more
 * and prints the tables' flash cost.
 *   cc -O2 -w -DCLIPPER_SYNTH=1 -Ibuild/gen -Ifirmware/src -Ifirmware/hal tests/preset_patch_test.c -lm -o build/host/preset_patch_test */
#include "editor_host.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("preset patch: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* the showcase patches (MATRIX-SHOWCASE.md), by name */
static const struct { uint32_t e; const char *names[16]; } WANT[] = {
    {ENGI_PONY, {"TOUCH STRING", "VEL BRASS", "PWM WHEEL", "MORPH BASS"}},
    {ENGI_PALOMINO, {"MORPH PAD", "SQR BASS", "OCTAVE LEAD", "RIPPLE PAD"}},   /* (showcase cables) */
    {ENGI_STAMPEDE, {"VEL BRASS", "TOUCH STRING", "DRIFT PAD", "RIBBON LEAD", "PRESS BEND"}},
    {ENGI_RODEO, {"TOUCH BRASS", "WHEEL STRING", "XMOD KEYS", "SUS PAD"}},
    {ENGI_MARE, {"HANGING RIFF", "RIFF ECHOES", "PEDAL SEQ", "FAT HUG PAD", "SPACE DRIFT", "GLIDE STRING", "HIGH SWITCH", "SPLIT CHORDS", "TRAVEL PARA", "SLOW BLOOM", "PWM LEAD", "SPACE SEQ", "SHRED LEAD", "SWITCH SEQ"}},   /* (SQUARE LEAD: patched, 2 cables, not a showcase) */
    {ENGI_MARE, {"FIFTH PEDAL", "PIPE BASS", "STACK BASS", "SWEEP BASS", "PARA BASS", "FIFTH SEQ", "BRASS SWELL", "BERLIN SEQ", "PARA BRASS", "WIDE PEDAL", "WHEEL KEYS", "BENDY ORGAN", "SPACE STRING", "VOYAGE PLUCK", "DARK CHORDS"}},   /* (the bank as it ships: every sound but INIT and RUBBER BASS patched) */
    {ENGI_FMX, {"FM BASS", "WARM BASS", "BRASS LEAD", "DRIFT PAD", "SOFT STRINGS", "FM PIANO", "SYNTH PLUCK",
                "BRIGHT CLAV", "FM LEAD", "SOFT LEAD"}},   /* (patched) */
    {ENGI_WAVE, {"WT STRINGS", "SYNC BRASS", "SOFT KEYS"}},
    {ENGI_VECTOR, {"EVOLVE PAD", "BREATH PAD", "VECTOR KEYS", "VECTOR PLUCK", "BRASS PAD", "PULSE PAD", "SOFT LEAD"}},
    {ENGI_WSEQ, {"CHIP STEPS", "SWEEP KEYS"}},
    {ENGI_VPS, {"RESO PAD", "PD PLUCK"}},
};

/* the engine whose table holds pr, -1: none */
static int engine_of(const preset_t *pr)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (pr >= ENGINES[e]->presets && pr < ENGINES[e]->presets + ENGINES[e]->npresets)
            return (int)e;
    return -1;
}

int main(void)
{
    uint32_t e, pi, k, i, nent = 0, ncab = 0, nfail = 0, nrun = 0;
    char w[200];
    harness_init();
    /* the tables point where they mean */
    for (k = 0; k < NELEM(PATCH_TABLES); k++)
        for (i = 0; i < PATCH_TABLES[k].n; i++) {
            const preset_patch_t *pp = &PATCH_TABLES[k].t[i];
            int pe = engine_of(pp->pr);
            uint32_t j, used = 0, xs = 0, dup = 0, range = 0;
            nent++, ncab += pp->n;
            for (j = 0; j < 8u; j += 2u) {           /* the matrix slots its x[] sets */
                uint32_t f;
                int s = pp->pr->x[j] > 0 ? mx_slot_of((uint32_t)(pp->pr->x[j] - 1), &f) : -1;
                if (s >= 0)
                    xs |= 1u << s;
            }
            for (j = 0; j < pp->n; j++) {
                uint32_t sl = pp->c[j].slot;
                range |= sl < 1u || sl > MX_SLOTS || pp->c[j].src <= MS_OFF || pp->c[j].src >= MS_NSRC ||
                         pp->c[j].dst <= MT_OFF || pp->c[j].dst >= MT_COUNT || pp->c[j].amt < -64 || pp->c[j].amt > 63;
                if (sl >= 1u && sl <= MX_SLOTS) {
                    dup |= (used >> (sl - 1u)) & 1u;
                    used |= 1u << (sl - 1u);
                }
            }
            snprintf(w, sizeof w, "%s %s: %u cables, slots 1..24 once each, none of x[]'s, amounts -64..63; after the entry before it",
                     pe >= 0 ? ENGINES[pe]->name : "?", pe >= 0 ? pp->pr->name : "?", (unsigned)pp->n);
            {   /* a second entry for the same preset (more extras, maybe more cables) right after its
                 * first: its slots none of the first's */
                int more = i && PATCH_TABLES[k].t[i - 1u].pr == pp->pr;
                if (more) {
                    const preset_patch_t *p0 = &PATCH_TABLES[k].t[i - 1u];
                    for (j = 0; j < p0->n; j++)
                        dup |= (used >> (p0->c[j].slot - 1u)) & 1u;
                }
                check(w, pe >= 0 && !dup && !range && !(used & xs) && (pp->n || more) && pp->pr != ENGINES[pe]->presets &&
                         (!i || (engine_of(PATCH_TABLES[k].t[i - 1u].pr) == pe && PATCH_TABLES[k].t[i - 1u].pr <= pp->pr)));
            }
        }
    for (k = 0; k < NELEM(WANT); k++)                /* the showcase: each has its entry */
        for (i = 0; i < 16u && WANT[k].names[i]; i++) {
            uint32_t q = host_pi(WANT[k].e, WANT[k].names[i]);
            const preset_patch_t *pp = preset_patch(&ENGINES[WANT[k].e]->presets[q]);
            snprintf(w, sizeof w, "%s %s: the showcase patch has its cables (3 or more)", ENGINES[WANT[k].e]->name,
                     WANT[k].names[i]);
            check(w, pp && (pp->n >= 3u || !strcmp(WANT[k].names[i], "PRESS BEND")));
        }
    /* every preset loads exactly its slots */
    for (e = 0; e < NENGINES; e++)
        for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
            const preset_t *pr = &ENGINES[e]->presets[pi];
            const preset_patch_t *pp = preset_patch(pr);
            int16_t want[MX_SLOTS][3];
            uint32_t j, ok = 1;
            int16_t xp[P_E0];
            if (is_drum(&trk[0]))
                continue;
            memset(want, 0, sizeof want);
            for (k = 0; k < P_E0; k++)               /* x[] as preset_extras applies it, onto the defaults */
                xp[k] = TP[k].def;
            for (j = 0; j < 8u; j += 2u)
                if (pr->x[j] > 0 && pr->x[j] - 1 < P_E0)
                    xp[pr->x[j] - 1] = (int16_t)clamp(pr->x[j + 1u], TP[pr->x[j] - 1].min, TP[pr->x[j] - 1].max);
            for (k = 0; k < MX_SLOTS; k++)
                for (j = 0; j < 3u; j++)
                    want[k][j] = xp[mx_pid(k) + j];
            for (; pp; pp = preset_patch_from(pr, pp))
                for (j = 0; j < pp->n; j++) {
                    int16_t *s = want[pp->c[j].slot - 1u];
                    s[0] = pp->c[j].src, s[1] = pp->c[j].dst, s[2] = pp->c[j].amt;
                }
            pp = preset_patch(pr);
            for (k = 0; k < MX_SLOTS; k++)           /* junk in every slot first: a load sets them all */
                for (j = 0; j < 3u; j++)
                    trk[0].p[mx_pid(k) + j] = (int16_t)(j == 2u ? 33 : j ? MT_CUT : MS_LFO1);
            set_engine_of(&trk[0], e);
            apply_preset_to(&trk[0], pi);
            for (k = 0; k < MX_SLOTS; k++)
                for (j = 0; j < 3u; j++)
                    ok &= trk[0].p[mx_pid(k) + j] == want[k][j];
            for (; pp; pp = preset_patch_from(pr, pp))
                for (j = 0; j < 8u; j += 2u)             /* its extras (every entry's) */
                    if (pp->x[j] > 0 && pp->x[j] - 1 < P_E0 && pp->x[j] - 1 != P_SPREAD)
                        ok &= trk[0].p[pp->x[j] - 1] == clamp(pp->x[j + 1u], TP[pp->x[j] - 1].min, TP[pp->x[j] - 1].max);
            nrun++;
            if (!ok) {   /* (pp walked to 0 above: ask again which message) */
                snprintf(w, sizeof w, "%s %s: %s", ENGINES[e]->name, pr->name,
                         preset_patch(pr) ? "exactly its table's cables and extras, the other slots clear" : "every slot clear (or as x[])");
                check(w, ok);
            }
            nfail += !ok;
        }
    snprintf(w, sizeof w, "all %u factory presets: their slots exactly as their tables say", (unsigned)nrun);
    check(w, !nfail);
    printf("preset patch: %u entries (%u B each), %u cables (%u B each): %u B of tables, + %u B the list of tables\n",
           (unsigned)nent, (unsigned)sizeof(preset_patch_t), (unsigned)ncab, (unsigned)sizeof(pcable_t),
           (unsigned)(nent * sizeof(preset_patch_t) + ncab * sizeof(pcable_t)), (unsigned)sizeof PATCH_TABLES);
    printf("preset patch: %d failed\n", bad);
    return bad != 0;
}
