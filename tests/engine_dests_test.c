/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* Dead matrix destinations are impossible, not warned about (engines.c mx_dst_live, mx_prune), on the
 * real firmware (tests/editor_host.c, CLIPPER_SYNTH=1):
 *   mask      mx_dst_live against the engines' own facts, found here on their own: FLTFM / RING only on MARE, TRIG1
 *             dead where engine_t ownenv, XMOD / HPF only where engine_t xmod / hpf, SHAPE dead where the engine's
 *             eng_*.c never reads m->shape, E1..E8 dead where its EDIT label is "-" or none (and a mode's descriptor
 *             never makes a dead slot live or a live one "-"); the rest live (CUT on FMX / WHEEL kept: FILTER 2)
 *   presets   every factory preset of every engine, loaded: no slot into a dead destination
 *   change    MARE -> RODEO (FLTFM, RING), PALOMINO -> ANALOG (XMOD), ANALOG -> FM6 (TRIG1), ANALOG -> WHEEL (SHAPE):
 *             a sound brought to the part with the other engine (MORPH / UNDO's snap_put, a user preset, a project,
 *             anything else: the main loop's pass) has those slots cleared (source, destination, amount 0), the live
 *             ones kept; the sound bit for bit what it was with the dead slots in (they did nothing), SLIDE (MPE's
 *             SLIDE-to-cutoff) and NOISE (its random numbers) into RODEO's RING / FLTFM included (voice.c mx_load)
 *   editor    INFO's per-engine masks are mx_dst_live; SET (scopes 0 and 2) and TRACK_PARAM of a dead destination
 *             refused (the reply the value kept), a live one taken; UP_PUT stores no dead slot; a slot cleared by the
 *             main loop's pass is pushed to a watching editor (CHANGED)
 *   24 slots  (preset format 2) slots 13..24 (protocol 15 ids 141..176) under the same rules: SET scope 2 refuses a dead
 *             destination, UP_PUT and the main loop's pass clear one, mx_prune_p walks every slot */
#include <sys/wait.h>
#include "editor_host.c"

static int bad;
static void check(const char *what, int ok) { printf("edest: %-104s %s\n", what, ok ? "ok" : "FAIL"); bad |= !ok; }

static uint8_t fr[64][700];
static uint32_t frn[64], nfr;
static void grab(const uint8_t *f, uint32_t n) { if (nfr < 64) { memcpy(fr[nfr], f, n); frn[nfr++] = n; } }
static uint32_t v14(uint8_t *o, int32_t v) { uint32_t u = (uint32_t)(v + 8192); o[0] = u & 127u; o[1] = (u >> 7) & 127u; return 2; }
static int32_t rv(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 7) - 8192; }
static const uint8_t *req(uint32_t cmd, const uint8_t *a, uint32_t na, uint32_t *n)
{
    uint8_t f[700];
    uint32_t i;
    f[0] = ED_HDR0; f[1] = ED_HDR1; f[2] = ED_HDR2; f[3] = (uint8_t)cmd;
    memcpy(f + 4, a, na);
    nfr = 0;
    fm1_ms += 3;
    usb_sysex(f, 4 + na);
    rx_take();
    for (i = nfr; i-- > 0;)
        if (fr[i][4] == cmd && cmd != ED_CHANGED) { *n = frn[i] - 6u; return fr[i] + 5; }
    *n = 0;
    return 0;
}

/* ---- the engines' facts, found without mx_dst_live: the engine's name and whether its eng_*.c reads m->shape */
static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *s;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    s = malloc((size_t)n + 1u);
    if (fread(s, 1, (size_t)n, f) != (size_t)n) n = 0;
    s[n] = 0;
    fclose(f);
    return s;
}
static void strip_comments(char *s)                     /* block and line comments to spaces (strings kept) */
{
    char *p = s;
    int q = 0;
    for (; *p; p++) {
        if (*p == '"' && (p == s || p[-1] != '\\')) q = !q;
        if (q) continue;
        if (p[0] == '/' && p[1] == '*') { for (; *p && !(p[0] == '*' && p[1] == '/'); p++) *p = ' '; if (*p) { p[0] = p[1] = ' '; p++; } }
        else if (p[0] == '/' && p[1] == '/') for (; *p && *p != '\n'; p++) *p = ' ';
    }
}
static const char *const ENG_FILES[] = {"acid", "analog", "stampede", "digital", "drawbar", "fm6", "fmx", "formant", "grain",
    "hybrid", "pony", "palomino", "macro", "mare", "noise", "rodeo", "phase", "phys", "sample", "strings",
    "terrain", "trio", "vector", "vps", "wave", "wseq"};
/* 1: engine nm's file reads m->shape, 0: it does not, -1: no file names it */
static int reads_shape(const char *nm)
{
    uint32_t i;
    char path[128], want[32];
    snprintf(want, sizeof want, "\"%s\"", nm);
    for (i = 0; i < NELEM(ENG_FILES); i++) {
        char *s, *e, *b;
        int r;
        snprintf(path, sizeof path, "firmware/src/eng_%s.c", ENG_FILES[i]);
        if (!(s = slurp(path))) continue;
        strip_comments(s);
        e = strstr(s, "static const engine_t ENG_");
        b = e ? strchr(e, '{') : 0;
        if (b && (b = strchr(b, '"')) && !strncmp(b, want, strlen(want))) {
            r = strstr(s, "m->shape") != 0;
            free(s);
            return r;
        }
        free(s);
    }
    return -1;
}

#define BIT(d) (1ull << (d))
static uint64_t expect_live(uint32_t ei)
{
    const engine_t *e = ENGINES[ei];
    uint64_t m = BIT(MT_COUNT) - 1u;
    uint32_t k;
    if (strcmp(e->name, "MARE")) m &= ~(BIT(MT_FFM) | BIT(MT_RING) | BIT(MT_SEMI) | BIT(MT_SEM34));   /* (SEMI too) */
    if (e->ownenv) m &= ~BIT(MT_TRIG1);
    if (!e->xmod) m &= ~BIT(MT_XMOD);
    if (!e->hpf) m &= ~BIT(MT_HPF);
    if (reads_shape(e->name) == 0) m &= ~BIT(MT_SHAPE);
    for (k = 0; k < 8u; k++)
        if (!e->edit[k].label || !e->edit[k].label[0] || !strcmp(e->edit[k].label, "-")) m &= ~BIT(MT_E1 + k);
    return m;
}

static void slot_set(int16_t *p, uint32_t k, int s, int d, int a)
{
    p[mx_pid(k)] = (int16_t)s, p[mx_pid(k) + 1u] = (int16_t)d, p[mx_pid(k) + 2u] = (int16_t)a;
}
static int slot_is(const int16_t *p, uint32_t k, int s, int d, int a)
{
    return p[mx_pid(k)] == s && p[mx_pid(k) + 1u] == d && p[mx_pid(k) + 2u] == a;
}
static uint32_t dead_slots(const track_t *t)            /* slots of t into a destination its engine does not use */
{
    uint64_t live = expect_live(t->eng_req);
    uint32_t k, n = 0;
    for (k = 0; k < MX_SLOTS; k++) {
        int32_t d = t->p[mx_pid(k) + 1u];
        n += d > MT_OFF && d < MT_COUNT && !(live >> d & 1u);
    }
    return n;
}
static void sound_on(track_t *t, uint32_t e)            /* engine e at once (as host_preset), its first preset */
{
    set_engine_of(t, e);
    t->engine = t->eng_req;
    t->p[P_AMODE] = 0;
}

/* the pairs: from, to, the two destinations dead on "to" (live on "from") */
static const struct { uint32_t from, to; uint8_t d1, d2; const char *what; } PAIRS[] = {
    {ENGI_MARE, ENGI_RODEO, MT_FFM, MT_RING, "MARE -> RODEO (FLTFM, RING)"},
    {ENGI_PALOMINO, 0u, MT_XMOD, MT_XMOD, "PALOMINO -> ANALOG (XMOD)"},
    {0u, ENGI_FM6, MT_TRIG1, MT_TRIG1, "ANALOG -> FM6 (TRIG1)"},
    {0u, 7u, MT_SHAPE, MT_SHAPE, "ANALOG -> WHEEL (SHAPE)"},
};
/* the sound: two live slots, the two dead ones (sources that draw no random numbers), a live one after them */
static void patch(int16_t *p, uint32_t i)
{
    uint32_t k;
    for (k = 0; k < MX_SLOTS; k++) slot_set(p, k, 0, 0, 0);
    slot_set(p, 0, MS_LFO1, MT_PITCH, 20);
    slot_set(p, 1, MS_ENV2, MT_AMP, -12);
    slot_set(p, 2, MS_LFO2, PAIRS[i].d1, 40);
    slot_set(p, 3, MS_ENV1, PAIRS[i].d2, -30);
    slot_set(p, 4, MS_MW, MT_PAN, 25);
}
static int pruned_ok(const track_t *t, uint32_t i)
{
    return slot_is(t->p, 0, MS_LFO1, MT_PITCH, 20) && slot_is(t->p, 1, MS_ENV2, MT_AMP, -12) && slot_is(t->p, 2, 0, 0, 0) &&
           slot_is(t->p, 3, 0, 0, 0) && slot_is(t->p, 4, MS_MW, MT_PAN, 25) && t->eng_req == PAIRS[i].to;
}

/* ---- the sound, bit for bit: a hash of n blocks of the mix with a chord held */
static uint64_t render_hash(uint32_t nb)
{
    static int32_t o[CTL * 2];
    uint64_t h = 1469598103934665603ull;
    uint32_t b, i;
    trk_note_on(&trk[0], 48, 100);
    trk_note_on(&trk[0], 55, 90);
    trk_note_on(&trk[0], 64, 110);
    for (b = 0; b < nb; b++) {
        if (b == nb * 3u / 4u) { trk_note_off(&trk[0], 48); trk_note_off(&trk[0], 55); trk_note_off(&trk[0], 64); }
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
    }
    return h;
}
/* the part as it is (pruned) against the same with the dead slots put back raw (put): forked, so both start from one
 * state */
static int same_sound_put(void (*put)(uint32_t), uint32_t i, uint64_t *ha, uint64_t *hb);
static void put_pair(uint32_t i)
{
    slot_set(trk[0].p, 2, MS_LFO2, PAIRS[i].d1, 40);   /* (the dead ones in again, past mx_prune) */
    slot_set(trk[0].p, 3, MS_ENV1, PAIRS[i].d2, -30);
}
static uint8_t dsrc[2];                                 /* (put_dsrc: the sources into RODEO's RING, FLTFM) */
static void put_dsrc(uint32_t i)
{
    (void)i;
    slot_set(trk[0].p, 2, dsrc[0], MT_RING, 40);
    slot_set(trk[0].p, 3, dsrc[1], MT_FFM, -30);
}
static int same_sound(uint32_t i, uint64_t *ha, uint64_t *hb) { return same_sound_put(put_pair, i, ha, hb); }
static int same_sound_put(void (*put)(uint32_t), uint32_t i, uint64_t *ha, uint64_t *hb)
{
    int pf[2];
    pid_t pid;
    uint64_t a = 0, b;
    int st;
    if (pipe(pf)) return 0;
    pid = fork();
    if (pid == 0) {
        close(pf[0]);
        put(i);
        a = render_hash(700);
        if (write(pf[1], &a, sizeof a) != sizeof a) _exit(3);
        _exit(0);
    }
    close(pf[1]);
    b = render_hash(700);
    if (read(pf[0], &a, sizeof a) != sizeof a) a = 0;
    close(pf[0]);
    waitpid(pid, &st, 0);
    *ha = a, *hb = b;
    return a == b && a != 0;
}

int main(void)
{
    uint8_t a[700];
    const uint8_t *p;
    uint32_t n, i, k, e, ok;
    char w[300];
    harness_init();
    rx_hook = grab;

    /* ---- the mask */
    for (e = 0, ok = 1; e < NENGINES; e++)
        if (e != ENGI_GONE && mx_dst_live(e) != expect_live(e)) {   /* (3: no engine, core.h) */
            printf("  %s: mx_dst_live %llx, the engine's facts %llx\n", ENGINES[e]->name, (unsigned long long)mx_dst_live(e),
                   (unsigned long long)expect_live(e));
            ok = 0;
        }
    check("mx_dst_live: every engine's mask from engine_t (ownenv, xmod, hpf, EDIT labels), MARE, eng_*.c's m->shape", ok);
    {   /* the facts themselves, as documented, so a slip in the finding above shows */
        char sh[200] = "";
        uint32_t nsh = 0, okn = 1;
        for (e = 0; e < NENGINES; e++) {
            int r = e == ENGI_GONE ? 1 : reads_shape(ENGINES[e]->name);   /* (3: no engine, core.h) */
            if (r < 0) { printf("  no eng_*.c names %s\n", ENGINES[e]->name); okn = 0; }
            if (r == 0) { snprintf(sh + strlen(sh), sizeof sh - strlen(sh), " %s", ENGINES[e]->name); nsh++; }
        }
        snprintf(w, sizeof w, "SHAPE dead on the engines that never read m->shape:%s", sh);
        check(w, okn && nsh == 8u && !strcmp(sh, " SAMPLE WHEEL PHYS HYBRID ACID STRINGS MACRO WSEQ"));
        ok = (expect_live(ENGI_FMX) >> MT_CUT & 1u) && (expect_live(7u) >> MT_CUT & 1u) && (expect_live(7u) >> MT_CUTB & 1u) &&
             !(expect_live(ENGI_FM6) >> MT_TRIG1 & 1u) && !(expect_live(ENGI_FMX) >> MT_TRIG1 & 1u) &&
             (expect_live(ENGI_FM6) >> MT_TRIG2 & 1u) && (expect_live(ENGI_MARE) >> MT_FFM & 1u) &&
             !(expect_live(ENGI_RODEO) >> MT_RING & 1u) && (expect_live(ENGI_PALOMINO) >> MT_XMOD & 1u) &&
             (expect_live(ENGI_STAMPEDE) >> MT_HPF & 1u) && !(expect_live(4u) >> (MT_E1 + 5u) & 1u) && !(expect_live(4u) >> (MT_E1 + 7u) & 1u);
        check("kept: CUT / CUTB on FMX and WHEEL (FILTER 2 moves them); TRIG2 on FM6; FLTFM on MARE; XMOD on PALOMINO; "
              "HPF on STAMPEDE; SAMPLE's E6 / E8 dead", ok);
    }
    {   /* a mode's EDIT descriptor (desc hook: NOISE, PHYS) never turns a dead slot live or a live one "-" */
        track_t *t = &trk[2];
        ok = 1;
        for (e = 0; e < NENGINES; e++) {
            const engine_t *en = ENGINES[e];
            int32_t v;
            if (!en->desc) continue;
            sound_on(t, e);
            for (v = en->edit[0].min; v <= en->edit[0].max; v++)
                for (k = 0; k < 8u; k++) {
                    const param_desc_t *d;
                    int dead;
                    t->p[P_E0] = (int16_t)v;
                    d = en->desc(t, k);
                    if (!d) d = &en->edit[k];
                    dead = !d->label || !d->label[0] || !strcmp(d->label, "-");
                    if (dead != !(expect_live(e) >> (MT_E1 + k) & 1u)) { printf("  %s mode %d E%u %s\n", en->name, v, k + 1u, d->label); ok = 0; }
                }
        }
        check("a mode's EDIT names (NOISE, PHYS) agree with the mask: no E slot dead in one mode and live in another", ok);
    }

    /* ---- every factory preset, loaded: no dead slot */
    {
        track_t *t = &trk[2];
        uint32_t np = 0, bad_p = 0;
        for (e = 0; e < NENGINES; e++) {
            sound_on(t, e);
            for (k = 0; k < ENGINES[e]->npresets; k++) {
                apply_preset_to(t, k);
                np++;
                if (dead_slots(t)) { printf("  %s preset %u (%s): %u dead slots\n", ENGINES[e]->name, k, ENGINES[e]->presets[k].name, dead_slots(t)); bad_p++; }
            }
        }
        snprintf(w, sizeof w, "every factory preset of every engine loaded: no slot into a dead destination (%u presets)", np);
        check(w, np > 300u && !bad_p);
    }

    /* ---- the pairs, by each way a sound comes to the part */
    for (i = 0; i < NELEM(PAIRS); i++) {
        track_t *t = &trk[0];
        syn_snap_t s;
        uint64_t ha, hb;
        int r1, r2, r3, r4;
        song.sel = 0;
        /* MORPH / UNDO: a snapshot with the other engine (snap_put) */
        sound_on(t, PAIRS[i].from);
        patch(t->p, i);
        snap_take(&s, t);
        s.eng = (uint8_t)PAIRS[i].to;
        snap_put(t, &s);
        t->engine = t->eng_req;
        r1 = pruned_ok(t, i);
        /* the sound, bit for bit, with and without the dead slots */
        for (k = 0; k < NVOICE; k++) t->v[k].active = t->v[k].gate = 0;
        audio_blocks(40);
        snprintf(w, sizeof w, "%s: the sound bit for bit the same as with the dead slots in", PAIRS[i].what);
        ok = same_sound(i, &ha, &hb);
        if (!ok) printf("  hashes %016llx %016llx\n", (unsigned long long)ha, (unsigned long long)hb);
        check(w, ok);
        /* a user preset stored with the other engine (as an older firmware or another tool could leave it) */
        sound_on(t, PAIRS[i].from);
        patch(t->p, i);
        t->eng_req = (uint8_t)PAIRS[i].to;               /* (raw: the record says "to" with the dead slots) */
        up_store(UP_SLOTS - 1u, "EDEST");
        sound_on(t, 1u);
        r2 = !up_load(UP_SLOTS - 1u) && pruned_ok(t, i);
        /* a project */
        sound_on(t, PAIRS[i].from);
        patch(t->p, i);
        t->eng_req = (uint8_t)PAIRS[i].to;
        proj_capture(&proj_slot[3]);
        sound_on(t, 1u);
        project_apply(&proj_slot[3]);
        transport_req = 0;
        r3 = pruned_ok(t, i);
        /* anything else: the main loop's pass (UI frame) */
        sound_on(t, PAIRS[i].to);
        patch(t->p, i);
        ui_frame();
        r4 = pruned_ok(t, i);
        snprintf(w, sizeof w, "%s: dead slots cleared, live kept: MORPH/UNDO %s, user preset %s, project %s, main loop %s",
                 PAIRS[i].what, r1 ? "yes" : "no", r2 ? "yes" : "no", r3 ? "yes" : "no", r4 ? "yes" : "no");
        check(w, r1 && r2 && r3 && r4);
    }
    {   /* the layer swap: each sound as its new part's engine is its own: nothing lost */
        sound_on(&trk[0], ENGI_MARE);
        patch(trk[0].p, 0);
        sound_on(&trk[1], 0u);
        layer_swap();
        check("layer swap: MARE's FLTFM / RING cables go with MARE to part 2 (its engine there), none cleared",
              trk[1].eng_req == ENGI_MARE && slot_is(trk[1].p, 2, MS_LFO2, MT_FFM, 40) && slot_is(trk[1].p, 3, MS_ENV1, MT_RING, -30));
    }
    {   /* sources whose use alone changes the sound: SLIDE (MPE: SLIDE goes to the cutoff when no slot takes
         * it) and NOISE (a random number drawn per voice) into RODEO's dead RING / FLTFM, and the only slots there (no
         * slot left once pruned). A dead slot counts for nothing in the voice (voice.c mx_load), so with or without it
         * the sound is bit for bit the same: SLIDE still on the cutoff, the random numbers as they were */
        static const struct { uint8_t s1, s2; const char *what; } SRC[] = {
            {MS_SLIDE, MS_SLIDE, "SLIDE -> RING on RODEO (MPE, SLIDE moved): the sound bit for bit the same as with it in"},
            {MS_NOISE, MS_NOISE, "NOISE -> RING, FLTFM on RODEO (the only slots): the sound bit for bit the same as with them in"},
            {MS_SLIDE, MS_NOISE, "SLIDE -> RING, NOISE -> FLTFM on RODEO with a live LFO1 -> PITCH: bit for bit the same"},
        };
        track_t *t = &trk[0];
        uint64_t ha, hb;
        for (i = 0; i < NELEM(SRC); i++) {
            song.sel = 0;
            sound_on(t, ENGI_RODEO);
            for (k = 0; k < MX_SLOTS; k++) slot_set(t->p, k, 0, 0, 0);
            if (i == 2u) slot_set(t->p, 0, MS_LFO1, MT_PITCH, 20);
            dsrc[0] = SRC[i].s1, dsrc[1] = SRC[i].s2;
            mpe_mode = 1u;                               /* (the notes on channel 2, its SLIDE at +40) */
            mpe_cur_ch = 2u;
            mpe_slide[2] = 40;
            for (k = 0; k < NVOICE; k++) t->v[k].active = t->v[k].gate = 0;
            audio_blocks(40);
            ok = same_sound_put(put_dsrc, 0u, &ha, &hb);
            if (!ok) printf("  hashes %016llx %016llx\n", (unsigned long long)ha, (unsigned long long)hb);
            check(SRC[i].what, ok);
            mpe_mode = 0u, mpe_cur_ch = 0u, mpe_slide[2] = 0;
        }
    }

    /* ---- the editor */
    song.sel = 0;
    sound_on(&trk[0], ENGI_RODEO);
    sound_on(&trk[1], ENGI_FM6);
    p = req(ED_INFO, a, 0, &n);
    ok = 0;
    if (p) {   /* version, 5 counts, the engines' names, NTRK, 15, P_COUNT (2), then MT_COUNT and the masks */
        uint32_t j = 0, m, nb = (MT_COUNT + 6u) / 7u;
        while (j < n && p[j]) j++;
        j += 6u;
        for (m = 0; m < NENGINES; m++) { while (j < n && p[j]) j++; j++; }
        j += 4u;
        ok = j < n && p[j] == MT_COUNT && j + 1u + NENGINES * nb + 1u + NENG_ORDER == n;   /* (1.0: then the display order) */
        for (m = 0; ok && m < NENGINES; m++) {
            uint64_t got = 0;
            for (k = 0; k < nb; k++) got |= (uint64_t)p[j + 1u + m * nb + k] << (7u * k);
            ok = m == ENGI_GONE || got == expect_live(m);
        }
        for (m = 0, j += 1u + NENGINES * nb; ok && m <= NENG_ORDER; m++)
            ok = p[j + m] == (m ? ENG_ORDER[m - 1u] : NENG_ORDER);
    }
    check("INFO: after P_COUNT, MT_COUNT and each engine's live destinations (7 bits a byte), as the engines' facts", ok);
    {
        uint32_t did = ed_of_p(mx_pid(5) + 1u);         /* slot 6's DESTINATION (the patch's cable 6) */
        uint32_t pid6 = mx_pid(5) + 1u;
        trk[0].p[pid6] = MT_CUT;
        a[0] = 0; a[1] = (uint8_t)did; v14(a + 2, MT_FFM);
        p = req(ED_SET, a, 4, &n);
        ok = p && n == 4u && rv(p + 2) == MT_CUT && trk[0].p[pid6] == MT_CUT;
        a[0] = 2; a[1] = (uint8_t)(did & 127u); a[2] = (uint8_t)(did >> 7); v14(a + 3, MT_RING);
        p = req(ED_SET, a, 5, &n);
        ok = ok && p && n == 5u && rv(p + 3) == MT_CUT && trk[0].p[pid6] == MT_CUT;
        a[0] = 0; a[1] = (uint8_t)did; v14(a + 2, MT_SHAPE);
        p = req(ED_SET, a, 4, &n);
        ok = ok && p && rv(p + 2) == MT_SHAPE && trk[0].p[pid6] == MT_SHAPE;
        check("SET (scopes 0, 2) of slot 6's DESTINATION on RODEO: FLTFM, RING refused (the reply says CUT, kept); SHAPE taken", ok);
        a[0] = 1; a[1] = (uint8_t)ed_of_p(mx_pid(0) + 1u); v14(a + 2, MT_TRIG1);
        trk[1].p[mx_pid(0) + 1u] = MT_OFF;
        p = req(ED_TRACK_PARAM, a, 4, &n);
        ok = p && n == 4u && rv(p + 2) == MT_OFF && trk[1].p[mx_pid(0) + 1u] == MT_OFF;
        v14(a + 2, MT_TRIG2);
        p = req(ED_TRACK_PARAM, a, 4, &n);
        ok = ok && p && rv(p + 2) == MT_TRIG2 && trk[1].p[mx_pid(0) + 1u] == MT_TRIG2;
        check("TRACK_PARAM of part 2 (FM6): TRIG1 refused, TRIG2 taken", ok);
    }
    {   /* UP_PUT: a RODEO record with a FLTFM slot is stored without it */
        int16_t v[P_COUNT];
        uint32_t m = 0, j;
        memcpy(v, trk[0].p, sizeof v);
        slot_set(v, 2, MS_LFO2, MT_FFM, 40);
        slot_set(v, 3, MS_LFO1, MT_CUT, 20);
        slot_set(v, 15, MS_ENV1, MT_PAN, 10);            /* (preset format 2: slots 13..24 the same) */
        slot_set(v, 23, MS_LFO1, MT_RING, -40);
        a[m++] = (uint8_t)(UP_SLOTS - 2u); a[m++] = ENGI_RODEO;
        for (j = 0; j < 4u; j++) a[m++] = (uint8_t)"EDST"[j];
        a[m++] = 0;
        for (j = 0; j < P_EDN; j++) m += v14(a + m, v[p_of_ed(j)]);
        for (j = 0; j < 16u; j++) { a[m++] = 60; a[m++] = 0; }
        a[m++] = (uint8_t)(P_EDHN & 127u); a[m++] = (uint8_t)(P_EDHN >> 7);
        for (j = 0; j < P_EDHN; j++) m += v14(a + m, v[P_EDH0 + j]);
        p = req(ED_UP_PUT, a, m, &n);
        ok = p && n >= 2u && p[1] == 0u && up_used(UP_SLOTS - 2u);
        if (ok) {
            up_values(up_rec(UP_SLOTS - 2u), v);
            ok = slot_is(v, 2, 0, 0, 0) && slot_is(v, 3, MS_LFO1, MT_CUT, 20) && slot_is(v, 15, MS_ENV1, MT_PAN, 10) &&
                 slot_is(v, 23, 0, 0, 0);
        }
        check("UP_PUT: a RODEO record with LFO2 -> FLTFM (slot 3), LFO1 -> RING (slot 24) stored without them; CUT, PAN kept", ok);
    }
    {   /* WATCH: a slot the main loop's pass clears is pushed (CHANGED): here one the editor was told of before the
         * pass ran (a dead value no path of the firmware leaves now: put in by hand, pushed by the editor's sync alone) */
        uint32_t did = ed_of_p(mx_pid(7) + 1u), got = 0, saw = 0, j, t;
        a[0] = 1;
        p = req(ED_WATCH, a, 1, &n);
        for (t = 0; t < 3; t++) { nfr = 0; ui_frame(); rx_take(); }
        slot_set(trk[0].p, 7, MS_LFO1, MT_RING, 30);
        for (t = 0; t < 6; t++) {
            nfr = 0;
            fm1_ms += 25;
            ed_service();
            rx_take();
            for (j = 0; j < nfr; j++)
                saw += fr[j][4] == ED_CHANGED && fr[j][5] == 0 && fr[j][6] == did && rv(fr[j] + 7) == MT_RING;
        }
        p = p && saw == 1u ? p : 0;
        for (t = 0; t < 6; t++) {
            nfr = 0;
            ui_frame();
            rx_take();
            for (j = 0; j < nfr; j++)
                if (fr[j][4] == ED_CHANGED && fr[j][5] == 0 && fr[j][6] == did && rv(fr[j] + 7) == 0) got++;
        }
        check("WATCH: slot 8's RING on RODEO cleared by the main loop, CHANGED pushes its DESTINATION 0", p && got == 1u &&
              slot_is(trk[0].p, 7, 0, 0, 0));
    }
    {   /* (preset format 2) slots 13..24, device-only (protocol 15 ids 141..176, scope 2): the same rules as 1..12 */
        uint32_t id24 = ed15_of_p(mx_pid(23) + 1u), pid24 = mx_pid(23) + 1u, t;
        song.sel = 0;
        trk[0].p[pid24] = MT_CUT;
        a[0] = 2; a[1] = (uint8_t)(id24 & 127u); a[2] = (uint8_t)(id24 >> 7); v14(a + 3, MT_RING);
        p = req(ED_SET, a, 5, &n);
        ok = id24 == 175u && trk[0].eng_req == ENGI_RODEO && p && n == 5u && rv(p + 3) == MT_CUT && trk[0].p[pid24] == MT_CUT;
        v14(a + 3, MT_SHAPE);
        p = req(ED_SET, a, 5, &n);
        ok = ok && p && rv(p + 3) == MT_SHAPE && trk[0].p[pid24] == MT_SHAPE;
        check("SET scope 2 of slot 24's DESTINATION (id 175) on RODEO: RING refused (the reply says CUT, kept); SHAPE taken", ok);
        slot_set(trk[0].p, 12, MS_LFO1, MT_RING, 30);    /* (put in by hand: the main loop's pass clears them) */
        slot_set(trk[0].p, 19, MS_ENV1, MT_PAN, -10);
        slot_set(trk[0].p, 23, MS_LFO2, MT_FFM, 25);
        for (t = 0; t < 3; t++) { nfr = 0; ui_frame(); rx_take(); }
        check("the main loop's pass on RODEO: slot 13's RING and slot 24's FLTFM cleared, slot 20's PAN kept",
              slot_is(trk[0].p, 12, 0, 0, 0) && slot_is(trk[0].p, 19, MS_ENV1, MT_PAN, -10) && slot_is(trk[0].p, 23, 0, 0, 0));
        for (k = 0; k < MX_SLOTS; k++) slot_set(trk[0].p, k, 0, 0, 0);
        slot_set(trk[0].p, 17, MS_LFO1, MT_RING, 30);
        slot_set(trk[0].p, 18, MS_LFO1, MT_CUT, 30);
        n = mx_prune_p(trk[0].p, ENGI_MARE);
        i = mx_prune_p(trk[0].p, ENGI_RODEO);
        check("mx_prune_p covers all 24 slots: slot 18's RING kept on MARE, cleared on RODEO; slot 19's CUT kept on both",
              MX_SLOTS == 24u && n == 0u && i == 1u && slot_is(trk[0].p, 17, 0, 0, 0) && slot_is(trk[0].p, 18, MS_LFO1, MT_CUT, 30));
        slot_set(trk[0].p, 18, 0, 0, 0);
    }
    printf(bad ? "ENGINE DESTINATIONS TEST FAILED\n" : "engine destinations test passed\n");
    return bad;
}
