/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* The MATRIX page on the FM-1's screen (the harness of tests/ui_pages_test.c):
 *   order    SOURCE steps through every code once, DESTINATION through every code the engine uses
 *            (engines.c mx_dst_live; RODEO, FM6, WHEEL, SAMPLE, PALOMINO, STAMPEDE each skip their dead ones), grouped by
 *            module as the web editor's patch bay reads left to right (web/editor.html PB_IN); the stored codes do not
 *            change
 *   colours  the selected cable's line (FONT_L) in its source's signal colour, the same classes as the web editor's
 *            pbSrcType: pitch blue, gate yellow, envelope green, LFO / modulation orange, performance purple, +V white;
 *            each readable on black (WCAG AA, 4.5:1) under every palette
 *   CLEAR    MATRIX 2 > CLEAR: two detents within 1.5 s zero all 24 slots' source, destination and amount; one does not
 *   SLOT     1..24 (preset format 2): the page shows the selected one's six (19..24 for 24), the numbers two digits
 *   notes    MARE: CUT2 FOLLOWS CUT1 while no cable goes into CUT2; VCO2 FOLLOWS VCO1 while one goes into VCO1 and none
 *            into VCO2; an ARP cable in the layer's sound while the layer is on: LAYER ARPEGGIO: UNUSED; every note
 *            within the 240 px line in FONT_S
 *   header   every source and destination (each engine's E1..E8 names) with amount -100 within the width, its letters
 *            a pixel closer at most
 *   screens: DIR/matrix-*.ppm */
#define UI_PAGES_HARNESS_ONLY
#include "ui_pages_test.c"

static void ok(int c, const char *what) { check(c, what); }
static int page_idx(const char *t)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (!strcmp(PAGES[i].title, t))
            return (int)i;
    return -1;
}
static int go_page(const char *t)
{
    int i = page_idx(t);
    if (i < 0)
        return 0;
    open_family(PAGES[i].fam);
    ui.page = (uint8_t)i;
    ui.fam_last[PAGES[i].fam] = (uint8_t)i;
    page_entered();
    ui.force = 1;
    frame();
    return 1;
}
static void knob(uint32_t k, int32_t s) { encs[panel.enc[EN_K1 + k]] = s; frame(); }
static void redraw(void) { ui.force = 1; frame(); frame(); }

/* the web panel's order, left to right (web/editor.html PB_MAIN then PB_STRIP; OFF first) */
static const uint8_t WANT_SRC[31] = {MS_OFF, MS_LFO1, MS_SH, MS_NOISE, MS_LFO2, MS_DC, MS_ENV1, MS_ENV2, MS_E2END,
                                     MS_KEY, MS_VEL, MS_GATE, MS_AT, MS_PAT, MS_CAT, MS_MW, MS_BEND, MS_RIB, MS_CCA,
                                     MS_CCB, MS_LFO3, MS_RAND, MS_SLIDE, MS_SEQ1, MS_SEQ2, MS_SEQ3, MS_SEQ4, MS_MAC1,
                                     MS_MAC2, MS_MAC3, MS_MAC4};
static const uint8_t WANT_DST[37] = {MT_OFF, MT_ARP, MT_L1RATE, MT_SYNC1, MT_L2RATE, MT_SYNC2, MT_PITCH, MT_PIT1,
                                     MT_SHAPE, MT_XMOD, MT_PIT2, MT_RING, MT_SEMI, MT_SEM34, MT_CUT, MT_FFM, MT_CUT2, MT_CUTB, MT_HPF,
                                     MT_RES2, MT_TRIG1, MT_E1AMT, MT_TRIG2, MT_ERATE, MT_DLY, MT_REV, MT_CHO, MT_AMP,
                                     MT_PAN, MT_E1, MT_E1 + 1, MT_E1 + 2, MT_E1 + 3, MT_E1 + 4, MT_E1 + 5, MT_E1 + 6,
                                     MT_E1 + 7};
_Static_assert(MS_NSRC == 31 && MT_COUNT == 37, "the counts this test walks");
/* (web/test_web.mjs checks ui_synth.c's MX_SRC_ORD / MX_DST_ORD against the web panel itself; here these copies
 * against ui_synth.c) */

/* the signal colours (web/editor.html SIG_COL: --t1 --t3 --t2 --t4 --pu --fg) and the web's pbSrcType */
#define PURPLE RGB(196, 107, 255)
enum { SG_PITCH, SG_GATE, SG_ENV, SG_MOD, SG_PERF, SG_DC, SG_N };
static uint16_t sig_rgb(uint32_t g)
{
    return g == SG_PITCH ? TE_COL[0] : g == SG_GATE ? TE_COL[2] : g == SG_ENV ? TE_COL[1] : g == SG_MOD ? TE_COL[3]
           : g == SG_PERF ? PURPLE : C_WHITE;
}
static uint32_t web_type(uint32_t c)                 /* pbSrcType (PB_PERF: VEL MW AT MAC1-4 BEND POLAT CHNAT RIBBN CC A/B) */
{
    static const uint8_t PERF[] = {5, 7, 8, 16, 17, 18, 19, 20, 21, 22, 23, 29, 30};
    uint32_t i;
    if (c == 6u) return SG_PITCH;
    if (c == 27u || c == 28u) return SG_GATE;
    if (c == 3u || c == 4u) return SG_ENV;
    if (c == 26u) return SG_DC;
    for (i = 0; i < sizeof PERF; i++)
        if (PERF[i] == c) return SG_PERF;
    return SG_MOD;
}
/* pixels of colour c in screen rows y0..y1-1 */
static uint32_t count_col(uint32_t y0, uint32_t y1, uint16_t c)
{
    uint32_t n = 0, i;
    for (i = y0 * 240u; i < y1 * 240u; i++)
        n += swap16(screen[i]) == c;
    return n;
}
static uint32_t count_lit(uint32_t y0, uint32_t y1)
{
    uint32_t n = 0, i;
    for (i = y0 * 240u; i < y1 * 240u; i++)
        n += screen[i] != 0u;
    return n;
}
static double lin(double v) { v /= 255.0; return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
static double contrast_black(uint16_t c)
{
    double r = ((c >> 11) & 31) * 255.0 / 31, g = ((c >> 5) & 63) * 255.0 / 63, b = (c & 31) * 255.0 / 31;
    double l = 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
    return (l + 0.05) / 0.05;
}
/* the header band: the graph strip's top (Y_GRAPH ..), where the selected cable is drawn in FONT_L */
#define HEAD_Y0 Y_GRAPH
#define HEAD_Y1 (Y_GRAPH + 27u)
/* the notes band: the graph strip's last line */
#define NOTE_Y0 (Y_GRAPH + H_GRAPH - 16u)
#define NOTE_Y1 (Y_GRAPH + H_GRAPH)

static void slot_set(track_t *t, uint32_t k, int s, int d, int a)
{
    t->p[mx_pid(k)] = (int16_t)s;
    t->p[mx_pid(k) + 1u] = (int16_t)d;
    t->p[mx_pid(k) + 2u] = (int16_t)a;
}
static void slots_clear(track_t *t)
{
    uint32_t k;
    for (k = 0; k < MX_SLOTS; k++)
        slot_set(t, k, 0, 0, 0);
}
static uint32_t slots_sum(const track_t *t)
{
    uint32_t k, n = 0;
    for (k = 0; k < MX_SLOTS; k++)
        n += (t->p[mx_pid(k)] != 0) + (t->p[mx_pid(k) + 1u] != 0) + (t->p[mx_pid(k) + 2u] != 0);
    return n;
}
static void engine(track_t *t, uint32_t e)
{
    set_engine_of(t, e);
    apply_preset_to(t, 0);
    t->engine = t->eng_req;
}

/* the DESTINATION knob from OFF to its end on engine e: the codes it stops at are WANT_DST without dead (once each, in
 * order), and it stays at the last one */
static int dst_walk(track_t *t, uint32_t e, uint64_t dead)
{
    uint32_t i, n = 0, want = 0, last = 0;
    int good = 1;
    engine(t, e);
    slots_clear(t);
    frames(2);
    if (!go_page("MATRIX"))
        return 0;
    for (i = 0; i < MT_COUNT; i++) {
        int32_t d;
        if (dead >> WANT_DST[i] & 1u)
            continue;
        if (want++)
            knob(2, 1);
        d = t->p[mx_pid(0) + 1u];
        if (d != WANT_DST[i]) {
            printf("  %s: step %u at %s, wanted %s\n", ENGINES[e]->name, n, N_MDST[d % MT_COUNT], N_MDST[WANT_DST[i]]);
            good = 0;
        }
        last = WANT_DST[i];
        n++;
    }
    knob(2, 1);
    good = good && t->p[mx_pid(0) + 1u] == (int32_t)last;
    for (i = 0; i < 40u && t->p[mx_pid(0) + 1u]; i++)
        knob(2, -1);
    return good && t->p[mx_pid(0) + 1u] == MT_OFF;
}

int main(int argc, char **argv)
{
    uint32_t i, k;
    track_t *t = &trk[0];
    outdir = argc > 1 ? argv[1] : "build/host";
    bank_resolve();
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], i ? 1u : 0u); apply_preset_to(&trk[i], 0); trk[i].engine = trk[i].eng_req; }
    clipper_proj_neutral();
    go_home(); ui.force = 1; frames(4);

    engine(t, ENGI_MARE);
    slots_clear(t);
    frames(3);
    ok(go_page("MATRIX") && PAGES[ui.page].fam == FAM_LFO, "MATRIX: a page of the LFO family");
    ppm("matrix-empty");

    {   /* SOURCE: every code once, in the panel's order; then DESTINATION */
        int good = 1, seen[64] = {0};
        for (i = 0; i < 31u; i++) {
            if (i)
                knob(1, 1);
            if (t->p[mx_pid(0)] != WANT_SRC[i])
                good = 0;
            seen[t->p[mx_pid(0)] & 63]++;
        }
        knob(1, 1);
        good = good && t->p[mx_pid(0)] == WANT_SRC[30];
        for (i = 0; i < 31u; i++)
            good = good && seen[i] == 1;
        ok(good, "MATRIX: SOURCE visits all 31 codes once, in the web panel's order, and stops at the end");
        for (i = 0; i < 40u && t->p[mx_pid(0)]; i++)
            knob(1, -1);
        ok(t->p[mx_pid(0)] == MS_OFF, "MATRIX: SOURCE back to OFF at the start");
        ok(dst_walk(t, ENGI_MARE, (1ull << MT_HPF)), "MATRIX: DESTINATION on MARE visits its 36 live codes once (not HPF), in the web panel's order");
        for (i = 0; i < 40u && t->p[mx_pid(0) + 1u]; i++)
            knob(2, -1);
        ok(t->p[mx_pid(0) + 1u] == MT_OFF, "MATRIX: DESTINATION back to OFF");
    }
    {   /* the DESTINATION knob steps only through what the engine uses (each engine's dead codes, written
         * out here: FLTFM / RING off MARE, XMOD off PALOMINO / MARE, HPF off STAMPEDE, TRIG1 on FM6, SHAPE on WHEEL and
         * SAMPLE, SAMPLE's E6 and E8) */
#define B(d) (1ull << (d))
        const uint64_t MR = B(MT_FFM) | B(MT_RING) | B(MT_SEMI) | B(MT_SEM34), XH = B(MT_XMOD) | B(MT_HPF);   /* (SEMI) */
        ok(dst_walk(t, ENGI_RODEO, MR | XH), "MATRIX: DESTINATION on RODEO skips FLTFM, RING, XMOD, HPF");
        ok(dst_walk(t, ENGI_FM6, MR | XH | B(MT_TRIG1)), "MATRIX: DESTINATION on FM6 skips TRIG1 (own envelopes) too, keeps TRIG2");
        ok(dst_walk(t, 7u, MR | XH | B(MT_SHAPE)), "MATRIX: DESTINATION on WHEEL skips SHAPE too, keeps CUT and CUTB (FILTER 2)");
        ok(dst_walk(t, 4u, MR | XH | B(MT_SHAPE) | B(MT_E1 + 5u) | B(MT_E1 + 7u)), "MATRIX: DESTINATION on SAMPLE skips SHAPE, E6, E8 too");
        ok(dst_walk(t, ENGI_PALOMINO, MR | B(MT_HPF)), "MATRIX: DESTINATION on PALOMINO keeps XMOD, skips HPF");
        ok(dst_walk(t, ENGI_STAMPEDE, MR | B(MT_XMOD)), "MATRIX: DESTINATION on STAMPEDE keeps HPF, skips XMOD");
#undef B
        engine(t, ENGI_MARE);
        slots_clear(t);
        frames(2);
        go_page("MATRIX");
    }

    {   /* the selected cable's line in its source's colour, the same classes as the web editor's */
        int good = 1;
        for (i = 1; i < MS_NSRC; i++) {
            uint32_t g = web_type(i), j, n;
            slot_set(t, 0, (int)i, MT_CUT, 40);
            redraw();
            n = count_col(HEAD_Y0, HEAD_Y1, sig_rgb(g));
            for (j = 0; j < SG_N; j++)
                if (j != g && count_col(HEAD_Y0, HEAD_Y1, sig_rgb(j)) > n / 4u)
                    good = 0;
            if (n < 40u) {
                printf("  source %u (%s): %u pixels of its colour\n", i, N_MSRC[i], n);
                good = 0;
            }
        }
        ok(good, "MATRIX: the selected cable's line in its source's signal colour (as the web editor's pbSrcType)");
    }

    {   /* the longest line (NOISE > FLTFM -100) stays on the screen, its letters in FONT_L (20 rows tall or more) */
        uint32_t x, y, top = 999, bot = 0, edge = 0, right = 0;
        slot_set(t, 0, MS_NOISE, MT_FFM, -64);
        redraw();
        ppm("matrix-long");
        for (y = HEAD_Y0; y < HEAD_Y1; y++)
            for (x = 0; x < 240u; x++)
                if (screen[y * 240u + x]) {
                    top = y < top ? y : top, bot = y > bot ? y : bot;
                    edge += x >= 239u, right += x >= 200u;
                }
        ok(!edge && right > 20u && bot - top >= 19u, "MATRIX: the longest header fits the width, in the large font");
    }

    ok(!memcmp(WANT_SRC, MX_SRC_ORD, sizeof WANT_SRC) && !memcmp(WANT_DST, MX_DST_ORD, sizeof WANT_DST),
       "MATRIX: ui_synth.c's MX_SRC_ORD / MX_DST_ORD as this test's copies");

    {   /* every header: each engine's E1..E8 names, every source and destination, amount -100 (the widest): within
         * 236 px drawn at most a pixel closer a letter; each engine's widest drawn: no pixel at x 239 */
        uint32_t e, sr, ds, bad = 0, worst = 0, edge = 0, eng0 = t->eng_req;
        char line[28], val[12];
        const char *unit = 0;
        param_format(&TP[mx_pid(0) + 2u], -64, val, &unit);
        for (e = 0; e < NENGINES; e++) {
            const char *const *dn;
            uint32_t ws = 1, wd = 1, ww = 0;
            t->eng_req = (uint8_t)e;
            dn = track_desc(t, P_M1DST)->names;
            for (sr = 1; sr < MS_NSRC; sr++)
                for (ds = 1; ds < MT_COUNT; ds++) {
                    uint32_t tg = mx_head(line, sizeof line, N_MSRC[sr], dn[ds], val), n = (uint32_t)str_len(line);
                    int32_t w = text_w(&FONT_L, line) - (int32_t)(tg * n);
                    if (tg > 1u || w > 236) {
                        if (bad++ < 5u)
                            printf("  engine %s: \"%s\" %d px, %u closer\n", N_ENGNAME[e], line, w, tg);
                    }
                    if ((uint32_t)text_w(&FONT_L, line) > ww)
                        ww = (uint32_t)text_w(&FONT_L, line), ws = sr, wd = ds;
                    worst = ww > worst ? ww : worst;
                }
            if (e == ENGI_MARE || e == eng0 || e % 4u == 0u || e + 1u == NENGINES) {   /* (a sample drawn) */
                uint32_t x, y;
                slot_set(t, 0, (int)ws, (int)wd, -64);
                xv[X_MSLOT] = 1;
                redraw();
                for (y = HEAD_Y0; y < HEAD_Y1; y++)
                    for (x = 239; x < 240u; x++)
                        edge += screen[y * 240u + x] != 0u;
            }
        }
        t->eng_req = (uint8_t)eng0;
        slot_set(t, 0, 0, 0, 0);
        printf("  headers: the widest %u px before drawing closer\n", worst);
        ok(!bad && !edge, "MATRIX: every header (every engine's names, amount -100) within the width, a pixel closer at most");
    }
    {   /* the amount bars: +46, +62, +98 (and -100) each a different length, inside the screen, clear of the value */
        static const int16_t A[4] = {30, 40, 63, -64};   /* (shown +46, +62, +98, -100) */
        int32_t len[4], lo[4], hi[4], tx[4];
        uint32_t j, y = Y_GRAPH + 26u + 5u, x;
        char val[12];
        const char *unit = 0;
        xv[X_MSLOT] = 1;
        for (j = 0; j < 4u; j++) {
            slot_set(t, 0, MS_LFO1, MT_CUT, A[j]);
            redraw();
            param_format(&TP[mx_pid(0) + 2u], A[j], val, &unit);
            tx[j] = 166 + text_w(&FONT_S, val);       /* (the value's right end) */
            lo[j] = 240, hi[j] = -1;
            for (x = (uint32_t)tx[j]; x < 240u; x++)
                if (swap16(screen[(y + 1u) * 240u + x]) == C_WHITE) {
                    lo[j] = lo[j] < (int32_t)x ? lo[j] : (int32_t)x;
                    hi[j] = (int32_t)x;
                }
            len[j] = hi[j] >= lo[j] ? hi[j] - lo[j] + 1 : 0;
            printf("  amount %s: bar x %d..%d (%d px), the value ends at x %d\n", val, lo[j], hi[j], len[j], tx[j]);
        }
        {   /* (a picture: six cables, the amounts +46, +62, +98, -100, +25, -50) */
            static const int16_t AA[6] = {30, 40, 63, -64, 16, -32};
            static const uint8_t SS[6] = {MS_LFO1, MS_ENV2, MS_MW, MS_VEL, MS_LFO2, MS_AT};
            static const uint8_t DD[6] = {MT_CUT, MT_PITCH, MT_CUT, MT_PAN, MT_CUT, MT_PITCH};
            for (j = 0; j < 6u; j++)
                slot_set(t, j, SS[j], DD[j], AA[j]);
            redraw();
            ppm("matrix-amounts");
            for (j = 0; j < 6u; j++)
                slot_set(t, j, 0, 0, 0);
        }
        slot_set(t, 0, 0, 0, 0);
        ok(len[0] > 0 && len[0] < len[1] && len[1] < len[2] && hi[2] <= 238 && len[3] >= len[2] && lo[3] > tx[3] + 1,
           "MATRIX: the amount bars +46, +62, +98 three lengths, inside the screen; -100 as long, clear of its value");
    }
    {   /* the notes: each within the 240 px line in FONT_S */
        int good = 1;
        for (i = 0; i < 3u; i++)
            if (text_w(&FONT_S, MX_NOTE[i]) > 240) {
                printf("  note \"%s\": %d px\n", MX_NOTE[i], text_w(&FONT_S, MX_NOTE[i]));
                good = 0;
            }
        ok(good, "MATRIX: every note fits the 240 px line in FONT_S");
    }

    {   /* SLOT 1..24: slot 24 selected shows 19..24, its cable the header; slot 13 the group 13..18 */
        uint32_t a19, a13;
        slots_clear(t);
        ok(XD[X_MSLOT].min == 1 && XD[X_MSLOT].max == 24 && MX_SLOTS == 24u, "MATRIX: SLOT runs 1..24");
        slot_set(t, 23, MS_ENV2, MT_RING, 50);
        slot_set(t, 18, MS_LFO1, MT_CUT, 20);
        xv[X_MSLOT] = 24;
        redraw();
        a19 = count_lit(Y_GRAPH, Y_GRAPH + H_GRAPH);
        ppm("matrix-slot24");
        xv[X_MSLOT] = 13;
        redraw();
        a13 = count_lit(Y_GRAPH, Y_GRAPH + H_GRAPH);
        ok(mx_live(t, 23) && a19 > a13, "MATRIX: slot 24 selected: its group (19..24) drawn with its cables, more than 13..18 (empty)");
        slots_clear(t);
        xv[X_MSLOT] = 1;
    }

    /* six cables, one of each colour group */
    slots_clear(t);
    slot_set(t, 0, MS_KEY, MT_PIT1, 63);
    slot_set(t, 1, MS_GATE, MT_TRIG2, 63);
    slot_set(t, 2, MS_ENV2, MT_CUT, 40);
    slot_set(t, 3, MS_LFO1, MT_CUTB, -20);
    slot_set(t, 4, MS_MW, MT_FFM, 32);
    slot_set(t, 5, MS_DC, MT_E1 + 6, 30);
    xv[X_MSLOT] = 3;
    redraw();
    ppm("matrix-mixed");
    {   /* every palette: each group's colour on screen and readable on black */
        int good = 1;
        for (k = 0; k < NPALETTES; k++) {
            settings.palette = (uint8_t)k;
            palette_set(k);
            redraw();
            for (i = 0; i < SG_N; i++) {
                double cr = contrast_black(sig_rgb(i));
                if (!count_col(Y_GRAPH, Y_GRAPH + H_GRAPH, sig_rgb(i)) || cr < 4.5) {
                    printf("  palette %s, group %u: %u pixels, contrast %.1f\n", PALETTES[k].name, i,
                           count_col(Y_GRAPH, Y_GRAPH + H_GRAPH, sig_rgb(i)), cr);
                    good = 0;
                }
            }
            if (k == 0u)
                ppm("matrix-mixed-green");
        }
        settings.palette = 4;
        palette_set(4);
        ok(good, "MATRIX: six cables of six groups, each colour shown and 4.5:1 on black, under every palette");
    }

    {   /* the normals in words (MARE): CUT2 (a cable into CUT2 here), VCO2 (one into VCO1, none into VCO2) */
        uint32_t lit_both, lit_none, lit_cut;
        redraw();                                     /* LFO1 > CUT2, KEY > VCO1: VCO2 follows, CUT2 its own */
        lit_both = count_lit(NOTE_Y0, NOTE_Y1);
        slot_set(t, 6, MS_LFO2, MT_PIT2, 10);         /* VCO2 its own: no note */
        redraw();
        lit_none = count_lit(NOTE_Y0, NOTE_Y1);
        slot_set(t, 3, 0, 0, 0);                      /* nothing into CUT2: CUT2 follows */
        redraw();
        lit_cut = count_lit(NOTE_Y0, NOTE_Y1);
        ppm("matrix-cut-normal");
        ok(lit_both > 0u && lit_none == 0u && lit_cut > 0u,
           "MATRIX: MARE's notes appear (VCO2 FOLLOWS VCO1, CUT2 FOLLOWS CUT1) and go when the jack is patched");
        slot_set(t, 6, 0, 0, 0);
        slots_clear(t);
        slot_set(t, 0, MS_LFO1, MT_PIT1, 20);         /* both notes at once */
        slot_set(t, 1, MS_ENV2, MT_CUT, 40);
        xv[X_MSLOT] = 1;
        redraw();
        ok(count_lit(NOTE_Y0, NOTE_Y1) > 0u, "MATRIX: both normals at once: shown");
        ppm("matrix-mare-normals");
        engine(t, ENGI_PALOMINO);
        slots_clear(t);
        slot_set(t, 0, MS_ENV2, MT_CUT, 40);
        redraw();
        ok(count_lit(NOTE_Y0, NOTE_Y1) == 0u, "MATRIX: no normals in words on another engine");
    }

    {   /* an ARP cable in the layer's sound while the layer is on: LAYER ARPEGGIO: UNUSED (the page shows part 1's sound; the
         * device keeps song.sel 0, ui_input.c) */
        track_t *l = &trk[1];
        uint32_t own, lay, off, none;
        engine(l, ENGI_PALOMINO);
        slots_clear(l);
        slot_set(t, 1, MS_MW, MT_ARP, 40);            /* part 1's own ARP cable: it plays, nothing to say */
        redraw();
        own = count_lit(NOTE_Y0, NOTE_Y1);
        slot_set(t, 1, 0, 0, 0);
        slot_set(l, 4, MS_MW, MT_ARP, 40);
        l->p[P_LYMODE] = LY_LAYER;
        redraw();
        lay = count_lit(NOTE_Y0, NOTE_Y1);
        ok(mx_notes(t) == 1u, "MATRIX: mx_notes: the layer's ARP cable only (another engine: no normals)");
        ppm("matrix-arp-layer");
        l->p[P_LYMODE] = LY_OFF;
        redraw();
        off = count_lit(NOTE_Y0, NOTE_Y1);
        l->p[P_LYMODE] = LY_MULTI;
        slot_set(l, 4, MS_MW, MT_ARP, 0);             /* (amount 0: the cable plays nothing) */
        redraw();
        none = count_lit(NOTE_Y0, NOTE_Y1);
        l->p[P_LYMODE] = LY_OFF;
        slots_clear(l);
        ok(own == 0u && lay > 0u && off == 0u && none == 0u,
           "MATRIX: LAYER ARPEGGIO: UNUSED while the layer is on and its sound has an ARP cable; not for part 1's own");
    }

    {   /* MATRIX 2 > CLEAR: two detents within 1.5 s */
        int pg = page_idx("MATRIX 2");
        uint64_t lv = mx_dst_live(t->engine) & ~1ull;   /* (engine-dests) live destinations only: the main loop's pass
                                                     * clears a dead one, which is not what CLEAR is about */
        uint32_t d = 0;
        for (k = 0; k < MX_SLOTS; k++) {
            do d = d % (MT_COUNT - 1u) + 1u; while (!((lv >> d) & 1u));
            slot_set(t, k, 1 + (int)k, (int)d, 5 + (int)k);
        }
        ok(pg >= 0 && PAGES[pg].fam == FAM_LFO && pg == page_idx("MATRIX") + 1 && go_page("MATRIX 2"),
           "MATRIX 2: the page after MATRIX, LFO family");
        knob(0, 1);
        frames(100);                                  /* (1.6 s: the arm runs out) */
        ok(slots_sum(t) == 72u, "MATRIX 2: CLEAR once does nothing");
        knob(0, 1);
        frames(100);
        ok(slots_sum(t) == 72u, "MATRIX 2: CLEAR again after 1.5 s: still nothing (armed afresh)");
        knob(0, 1);
        frames(20);
        knob(0, 1);
        ok(slots_sum(t) == 0u, "MATRIX 2: CLEAR twice within 1.5 s zeroes all 72 values (24 slots)");
        frames(70);
        go_page("UNDO");
        knob(0, -1);
        frames(70);
        ok(slots_sum(t) == 72u && t->p[mx_pid(11) + 2u] == 16 && t->p[mx_pid(23) + 2u] == 28, "MATRIX 2: UNDO brings the cleared cables back (slot 24 too)");
        for (k = 0; k < MX_SLOTS; k++)
            slot_set(t, k, 0, 0, 0);
        go_page("MATRIX 2");
        knob(0, 1);
        frames(5);
        knob(0, -1);
        frames(5);
        slot_set(t, 0, MS_LFO1, MT_CUT, 10);
        knob(0, -1);
        ok(slots_sum(t) == 3u, "MATRIX 2: CLEAR turned left does nothing");
        ppm("matrix-clear");
    }
    printf(fails ? "matrix_ui: %d FAILED\n" : "matrix_ui: all passed\n", fails);
    return fails;
}
