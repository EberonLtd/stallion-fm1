/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol 15 (Stallion, specs/stallion R42, R43, R44) on the real firmware (tests/editor_host.c, CLIPPER_SYNTH=1):
 * scope 2 reaches every id below P_COUNT (GET, SET, DESC, CHANGED), its DESC as the FM-1 shows it and in the ranges SET
 * clamps to, scope 3 the FM-1's settings, the values 127.. in DUMP / TRACK_DUMP / UP_GET / UP_PUT, and protocol 14's
 * bytes as they were (an older editor keeps working). Also run under ASan + UBSan (tests/editor_fuzz.sh). */
#include "editor_host.c"

static int bad;
static void check(const char *what, int ok) { printf("%-100s %s\n", what, ok ? "ok" : "FAIL"); bad |= !ok; }

static uint8_t fr[64][700];                             /* the frames of the last request (pushes, then the reply) */
static uint32_t frn[64], nfr;
static void grab(const uint8_t *f, uint32_t n) { if (nfr < 64) { memcpy(fr[nfr], f, n); frn[nfr++] = n; } }
static uint32_t v14(uint8_t *o, int32_t v) { uint32_t u = (uint32_t)(v + 8192); o[0] = u & 127u; o[1] = (u >> 7) & 127u; return 2; }
static int32_t rv(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 7) - 8192; }
/* a request; the reply's bytes after the header (a = &frame[5]), its length; 0 = no reply */
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
/* the strings and the tail of a DESC reply from p (after fmt min max def): label, unit, then (scope 2, 3) n names, flags */
typedef struct { uint32_t fmt; int32_t min, max, def; char label[16], unit[16]; uint32_t n, flags, len; char names[64][16]; } desc_t;
static int desc_parse(const uint8_t *p, uint32_t len, uint32_t at, int ext, desc_t *d)
{
    uint32_t i = at, k, j;
    memset(d, 0, sizeof *d);
    if (len < at + 7u) return 0;
    d->fmt = p[i++]; d->min = rv(p + i); i += 2; d->max = rv(p + i); i += 2; d->def = rv(p + i); i += 2;
    for (k = 0; i < len && p[i]; i++) if (k < 15u) d->label[k++] = (char)p[i];
    i++;
    for (k = 0; i < len && p[i]; i++) if (k < 15u) d->unit[k++] = (char)p[i];
    i++;
    if (ext) {
        if (i >= len) return 0;
        d->n = p[i++];
        for (j = 0; j < d->n && j < 64u; j++) {
            for (k = 0; i < len && p[i]; i++) if (k < 15u) d->names[j][k++] = (char)p[i];
            i++;
        }
        if (i >= len) return 0;
        d->flags = p[i++];
    } else if (d->fmt == F_ENUM) {
        for (j = 0; j <= (uint32_t)(d->max - d->min) && j < 64u; j++) {
            for (k = 0; i < len && p[i]; i++) if (k < 15u) d->names[j][k++] = (char)p[i];
            i++;
        }
    }
    d->len = i;
    return i <= len;
}
static int desc2(uint32_t id, desc_t *d)
{
    uint8_t a[3] = {2, (uint8_t)(id & 127u), (uint8_t)(id >> 7)};
    uint32_t n;
    const uint8_t *p = req(ED_DESC, a, 3, &n);
    return p && p[0] == 2 && (uint32_t)(p[1] | p[2] << 7) == id && desc_parse(p, n, 3, 1, d) && d->len == n;
}
static int set2(uint32_t id, int32_t v, int32_t *got)
{
    uint8_t a[5] = {2, (uint8_t)(id & 127u), (uint8_t)(id >> 7)};
    uint32_t n;
    const uint8_t *p;
    v14(a + 3, v);
    p = req(ED_SET, a, 5, &n);
    if (!p || n != 5u || p[0] != 2 || (uint32_t)(p[1] | p[2] << 7) != id) return 0;
    *got = rv(p + 3);
    return 1;
}
static void eng(uint32_t e)                              /* SET G_ENGSEL, as the editor changes the engine */
{
    uint8_t a[4] = {1, G_ENGSEL};
    uint32_t n;
    v14(a + 2, (int32_t)e);
    (void)req(ED_SET, a, 4, &n);
}

int main(void)
{
    uint8_t a[700];
    const uint8_t *p;
    uint32_t n, i, k, ok, e;
    desc_t d;
    harness_init();
    rx_hook = grab;
    TSEL->p[P_BTGT] = 0;
    /* INFO: protocol 14's bytes, then 15 and P_ALL */
    p = req(ED_INFO, a, 0, &n);
    ok = p && n >= 6u;
    for (i = 0; ok && i < n && p[i]; i++) ;
    i++;
    ok = ok && p[i] == NENGINES && p[i + 1] == 127u && p[i + 2] == G_COUNT && p[i + 4] == 119u;
    {
        uint32_t j = i + 5, m;
        for (m = 0; m < NENGINES && j < n; m++) { while (j < n && p[j]) j++; j++; }
        check("INFO: protocol 14's counts (127 values, P_E0 119), then NTRK, 15 and P_ALL (2 bytes), then "
              "MT_COUNT and each engine's live destinations, (1.0) the display order at the end",
              ok && j + 4u + 1u + NENGINES * ((MT_COUNT + 6u) / 7u) + 1u + NENG_ORDER == n && p[j] == NTRK && p[j + 1] == 15u &&
              (uint32_t)(p[j + 2] | p[j + 3] << 7) == P_COUNT && P_COUNT == 184u && p[j + 4] == MT_COUNT);
    }
    {   /* TRACK_PARAM id 127 + a 14-bit id: part B's values past 126 (FILTER 2, BEND, matrix 13..24) */
        uint32_t k15 = ed15_of_p(P_M13AMT);
        uint8_t q[6] = {1, 127, (uint8_t)(k15 & 127u), (uint8_t)(k15 >> 7)};
        v14(q + 4, 21);
        trk[1].p[P_M13AMT] = 0;
        p = req(ED_TRACK_PARAM, q, 6, &n);
        check("TRACK_PARAM 1, 127, a 14-bit id, value: part B's matrix slot 13 (past id 126) written and read back",
              p && n == 6u && p[0] == 1u && p[1] == 127u && trk[1].p[P_M13AMT] == 21 && rv(p + 4) == 21);
    }
    {   /* PRESET's part: 1 loads part B (the layer, as EDIT > LAYER > SOUND); A unchanged */
        uint8_t q[3] = {(uint8_t)ENGI_RODEO, 2, 1};
        uint32_t e0 = TSEL->eng_req, p0 = TSEL->preset;
        p = req(ED_PRESET, q, 3, &n);
        check("PRESET engine, preset, part 1: part B's sound (the layer), part A untouched",
              p && n == 2u && trk[1].eng_req == ENGI_RODEO && trk[1].preset == 2u && TSEL->eng_req == e0 && TSEL->preset == p0);
    }
    check("protocol 15 ids: 0..126 as protocol 14's, 127.. the device's own in core.h order (SLOPE 127, BUP 133, BTGT 138)",
          p_of_ed15(0) == P_LEVEL && p_of_ed15(118) == P_PTGT && p_of_ed15(119) == P_E0 && p_of_ed15(126) == P_E7 &&
          p_of_ed15(127) == P_FSLOPE && p_of_ed15(128) == P_F2MODEL && p_of_ed15(132) == P_F2LINK && p_of_ed15(133) == P_BUP &&
          p_of_ed15(138) == P_BTGT && ed15_of_p(P_BTGT) == 138u && ed15_of_p(P_E0) == 119u && ed15_of_p(P_PTGT) == 118u);
    check("protocol 15 ids 141..176: the matrix's slots 13..24 (SRC DST AMT each)",
          p_of_ed15(141) == P_M13SRC && p_of_ed15(142) == P_M13DST && p_of_ed15(176) == P_M24AMT && ed15_of_p(P_M24AMT) == 176u &&
          ed15_of_p(mx_pid(12)) == 141u && ed15_of_p(mx_pid(23) + 2u) == 176u);
    /* DESC scope 2 of every id, every engine: parses, as track_desc, ranges as SET clamps them */
    for (e = 0, ok = 1; e < NENGINES; e++) {
        eng(e);
        for (k = 0; k < P_COUNT; k++) {
            uint32_t pid = p_of_ed15(k);
            const param_desc_t *s = ed_tdesc(TSEL, pid), *t = track_desc(TSEL, pid);
            if (!desc2(k, &d) || strncmp(d.label, t->label, 12) || d.min != s->min || d.max != s->max || d.fmt != t->fmt) {
                printf("  engine %u id %u (%s): desc %s %d..%d fmt %u, static %d..%d\n", e, k, t->label, d.label, d.min, d.max,
                       d.fmt, s->min, s->max);
                ok = 0;
            }
        }
    }
    check("DESC scope 2: every id 0..176 on every engine, the FM-1's descriptor, in the ranges SET clamps to", ok);
    {   /* the engines whose EDIT page follows a mode (PHYS's MODEL, NOISE's MODE: E1): every mode, the same ranges */
        static const uint32_t EM[] = {ENGI_PHYS, ENGI_NOISE};
        int32_t g, m;
        for (e = 0, ok = 1; e < 2; e++) {
            eng(EM[e]);
            for (m = ed_tdesc(TSEL, P_E0)->min; m <= ed_tdesc(TSEL, P_E0)->max; m++) {
                set2(119, m, &g);
                for (k = 119; k < 127; k++) {
                    const param_desc_t *s = ed_tdesc(TSEL, p_of_ed15(k)), *t = track_desc(TSEL, p_of_ed15(k));
                    if (!desc2(k, &d) || strncmp(d.label, t->label, 12) || d.min != s->min || d.max != s->max) {
                        printf("  engine %u mode %d id %u: %s %d..%d, static %d..%d\n", EM[e], m, k, d.label, d.min, d.max, s->min, s->max);
                        ok = 0;
                    }
                }
            }
        }
        check("DESC scope 2: PHYS's every MODEL, NOISE's every MODE: their labels, in the ranges SET clamps to", ok);
    }
    eng(0u);
    desc2(127, &d);
    ok = !strcmp(d.label, "SLOPE");
    desc2(133, &d);
    ok = ok && !strcmp(d.label, TP[P_BUP].label) && d.fmt == F_BRNG && d.min == -1;
    check("DESC scope 2: SLOPE (127), the BEND page's UP (133, BRNG, GLO at -1)", ok);
    {   /* protocol 14's DESC of the same ids: as before (no tail), and 127 is past it */
        uint32_t n0;
        a[0] = 0; a[1] = 10;
        p = req(ED_DESC, a, 2, &n0);
        ok = p && desc_parse(p, n0, 2, 0, &d) && d.len == n0;
        a[0] = 0; a[1] = 127;
        check("DESC scope 0: protocol 14's bytes (no tail); id 127 gets no reply", ok && !req(ED_DESC, a, 2, &n0));
    }
    /* SET / GET scope 2: clamps, the device's own value */
    {
        int32_t g = 0, g2 = 0;
        ok = set2(133, 99, &g) && g == 24 && TSEL->p[P_BUP] == 24 && set2(138, BT_LEAD, &g2) && g2 == BT_LEAD;
        a[0] = 2; a[1] = 133 & 127; a[2] = 1;
        p = req(ED_GET, a, 3, &n);
        check("SET scope 2 of BEND UP 99 -> 24 (clamped), TARGT LEAD; GET scope 2 reads it", ok && p && n == 5u && rv(p + 3) == 24);
        a[0] = 2; a[1] = 184 & 127; a[2] = 184 >> 7;
        check("GET scope 2 of id 184 (P_ALL): no reply", !req(ED_GET, a, 3, &n));
    }
    /* (preset format 2) the matrix's slots 13..24 by scope 2: SET, GET, DESC (a DST's E1..E8 the engine's names) */
    eng(ENGI_MARE);
    {
        int32_t g1 = 0, g2 = 0, g3 = 0;
        uint32_t k2;
        ok = set2(141, MS_CCB, &g1) && g1 == MS_CCB && TSEL->p[P_M13SRC] == MS_CCB &&
             set2(175, MT_RING, &g2) && g2 == MT_RING && TSEL->p[P_M24DST] == MT_RING &&
             set2(176, -99, &g3) && g3 == -64 && TSEL->p[P_M24AMT] == -64;
        a[0] = 2; a[1] = 175 & 127; a[2] = 175 >> 7;
        p = req(ED_GET, a, 3, &n);
        check("SET / GET scope 2 of slots 13 and 24 (SRC CC B, DST RING, AMT -99 -> -64)", ok && p && n == 5u && rv(p + 3) == MT_RING);
        ok = desc2(142, &d) && d.n == MT_COUNT && d.fmt == F_ENUM;
        for (k2 = 0; ok && k2 < 8u; k2++)
            ok = !strcmp(d.names[MT_E1 + k2], track_desc(TSEL, P_M1DST)->names[MT_E1 + k2]);
        check("DESC scope 2 of slot 13's DST: every destination, E1..E8 as the engine names them (as slot 1's)", ok);
        ok = desc2(176, &d) && d.min == -64 && d.max == 63 && desc2(174, &d) && d.n == MS_NSRC;
        check("DESC scope 2 of slot 24's AMT (-64..63) and SRC (every source)", ok);
        TSEL->p[P_M13SRC] = TSEL->p[P_M24DST] = TSEL->p[P_M24AMT] = 0;
    }
    /* the labels that follow a value (R41) */
    {
        int32_t g;
        char l0[16], l1[16], l2[16];
        set2(122, -10, &g); desc2(122, &d); strcpy(l0, d.label);
        set2(122, 10, &g); desc2(122, &d); strcpy(l1, d.label);
        set2(122, 0, &g); desc2(122, &d); strcpy(l2, d.label);
        check("MARE's 4th EDIT value: FM / SYNC / SY/FM as its value is, flags: follows (bit 0) and drives (bit 1)",
              !strcmp(l0, "FM") && !strcmp(l1, "SYNC") && !strcmp(l2, "SY/FM") && d.flags == 3u);
        desc2(119, &d);
        k = d.n;
        desc2(119 + 0, &d);
    }
    eng(ENGI_STAMPEDE);
    desc2(119, &d);
    check("STAMPEDE's WAVE: an INT with its 9 names (the range split over them, as the FM-1 shows it)",
          d.fmt == F_INT && d.n == 9u && !strcmp(d.names[0], "NOIS") && !strcmp(d.names[8], "RING"));
    eng(ENGI_RODEO);
    {
        int32_t g;
        char l0[16];
        set2(122, -5, &g); desc2(122, &d); strcpy(l0, d.label);
        set2(122, 5, &g); desc2(122, &d);
        check("RODEO's 4th EDIT value: XMOD below 0, DTUNE above", !strcmp(l0, "XMOD") && !strcmp(d.label, "DTUNE"));
    }
    eng(0u);
    {
        int32_t g;
        set2(132, 0, &g); desc2(129, &d);
        ok = !strcmp(d.label, "CUT") && d.fmt == F_CUTOFF && (d.flags & 1u);
        set2(132, 1, &g); desc2(129, &d);
        ok = ok && !strcmp(d.label, "OFS") && d.fmt == F_COFS && !strcmp(d.unit, "st");
        desc2(132, &d);
        check("FILTER 2's CUT: CUT, with LINK on OFS (COFS, st) on ANALOG; it follows LINK (flags), LINK drives it", ok && (d.flags & 2u));
    }
    {   /* the matrix's sources and destinations: every name, the new ones too */
        desc_t s;
        desc2(ed15_of_p(P_M1SRC), &s);
        desc2(ed15_of_p(P_M1DST), &d);
        check("the matrix's SRC and DST: every name (sources to RIBBN, destinations to HPF)",
              s.n == (uint32_t)MS_NSRC && !strcmp(s.names[MS_RIB], "RIBBN") && d.n == (uint32_t)MT_COUNT && !strcmp(d.names[MT_HPF], "HPF"));
    }
    {   /* every slot's DST (1..12) names E1..E8 as the engine's EDIT values, on the screen (track_desc) and in DESC */
        uint32_t k, j, e, okd = 1, okt = 1;
        for (e = 0; e < NENGINES; e++) {
            const engine_t *en = ENGINES[e];
            if (e == ENGI_GONE)                          /* (1.0: retired, LOFI's; set_engine plays TRIO there) */
                continue;
            eng(e);
            for (k = 0; k < MX_SLOTS; k++) {
                uint32_t pid = mx_pid(k) + 1u;
                const char *const *tn = track_desc(TSEL, pid)->names;
                desc2(ed15_of_p(pid), &d);
                for (j = 0; j < 8u; j++) {
                    const char *l = en->edit[j].label;
                    const char *want = l && l[0] && l[0] != '-' ? l : (const char *[]){"E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8"}[j];
                    if (d.n != (uint32_t)MT_COUNT || strcmp(d.names[MT_E1 + j], want)) {
                        if (okd) printf("  %s slot %u E%u: DESC %s, want %s\n", N_ENGNAME[e], (unsigned)k + 1, (unsigned)j + 1,
                                        d.n > MT_E1 + j ? d.names[MT_E1 + j] : "-", want);
                        okd = 0;
                    }
                    if (strcmp(tn[MT_E1 + j], want)) okt = 0;
                }
            }
        }
        eng(0u);
        check("the matrix's DST of every slot (1..12) names E1..E8 as the engine's EDIT values: on the screen and in DESC", okd && okt);
    }
    /* DUMP: protocol 14's, and with the flag the values 127.. */
    TSEL->p[P_FSLOPE] = 2; TSEL->p[P_F2MODEL] = 3; TSEL->p[P_BVCF] = -40;
    p = req(ED_DUMP, a, 0, &n);
    ok = p && n == 2u + 2u * (127u + G_COUNT);
    a[0] = 1;
    p = req(ED_DUMP, a, 1, &n);
    {
        uint32_t x = 2u + 2u * (127u + G_COUNT);
        TSEL->p[P_M24DST] = MT_RING;
        p = req(ED_DUMP, a, 1, &n);
        check("DUMP: protocol 14's 127 + G_COUNT values; with flags 1: n 51, then SLOPE, FILTER 2, .., VCO3, VCO4, slots 13..24, WAV34",
              ok && p && n == x + 2u + 2u * P_EDHN && (p[x] | p[x + 1] << 7) == P_EDHN && rv(p + x + 2) == 2 && rv(p + x + 4) == 3 &&
              rv(p + x + 2 + 2 * (137 - 127)) == -40 && rv(p + x + 2 + 2 * (175 - 127)) == MT_RING);
        TSEL->p[P_M24DST] = 0;
    }
    a[0] = 1; a[1] = 1;
    p = req(ED_TRACK_DUMP, a, 2, &n);
    check("TRACK_DUMP track 1 with flags: + the values 127..", p && n == 3u + 254u + 2u + 2u * P_EDHN);
    /* WATCH 5: pushes of scope 2 and 3; WATCH 1 / 3: none */
    for (k = 0; k < 2; k++) {
        uint32_t got2 = 0, got3 = 0, j, t;
        a[0] = k ? 5 : 3;
        p = req(ED_WATCH, a, 1, &n);
        ok = p && p[0] == (k ? 5u : 3u);
        TSEL->p[P_BDN] = (int16_t)(k ? 9 : 8);
        ed_sput(ED_S_LIGHTS, k ? 4 : 3);
        for (t = 0; t < 6; t++) {
            nfr = 0;
            ui_frame();
            rx_take();
            for (j = 0; j < nfr; j++)
                if (fr[j][4] == ED_CHANGED) {
                    if (fr[j][5] == 2 && (fr[j][6] | fr[j][7] << 7) == 134 && rv(fr[j] + 8) == TSEL->p[P_BDN]) got2++;
                    if (fr[j][5] == 3 && fr[j][6] == ED_S_LIGHTS && rv(fr[j] + 7) == lights_lvl) got3++;
                }
        }
        check(k ? "WATCH 5: answered 5; BEND DOWN and LIGHTS changed on the FM-1 push CHANGED scope 2 (id 134) and 3"
                : "WATCH 3 (protocol 14): answered 3; no scope 2 / 3 push", ok && (k ? got2 == 1 && got3 == 1 : !got2 && !got3));
    }
    {   /* the editor's own SET scope 2 / 3: no push */
        uint32_t got = 0, j, t;
        int32_t g;
        set2(134, 3, &g);
        a[0] = 3; a[1] = ED_S_LIGHTS; v14(a + 2, 2);
        req(ED_SET, a, 4, &n);
        for (t = 0; t < 6; t++) { nfr = 0; ui_frame(); rx_take(); for (j = 0; j < nfr; j++) got += fr[j][4] == ED_CHANGED; }
        check("the editor's own SET scope 2 and 3: no CHANGED about them", !got && lights_lvl == 2);
    }
    /* scope 3: every setting */
    for (i = 0, ok = 1; i < ED_NSET; i++) {
        uint32_t id = ED_SIDS[i];
        int16_t now;
        const param_desc_t *s = ed_sget(id, &now);
        a[0] = 3; a[1] = (uint8_t)id;
        p = req(ED_DESC, a, 2, &n);
        if (!s || !p || p[0] != 3 || p[1] != id || !desc_parse(p, n, 2, 1, &d) || d.len != n || strcmp(d.label, s->label) ||
            d.min != s->min || d.max != s->max) { printf("  setting %u: DESC\n", id); ok = 0; continue; }
        v14(a + 2, d.max);
        p = req(ED_SET, a, 4, &n);
        if (!p || n != 4u || rv(p + 2) != d.max || (ed_sget(id, &now), now) != d.max) { printf("  setting %u: SET max\n", id); ok = 0; }
        v14(a + 2, d.min - 5);
        p = req(ED_SET, a, 4, &n);
        if (!p || rv(p + 2) != d.min) { printf("  setting %u: SET below\n", id); ok = 0; }
        v14(a + 2, d.def);
        req(ED_SET, a, 4, &n);
        p = req(ED_GET, a, 2, &n);
        if (!p || n != 4u || rv(p + 2) != d.def) { printf("  setting %u: GET\n", id); ok = 0; }
    }
    check("scope 3: every setting (TUNING, MIDI, MIDI 2, TOUCH 1..4, LIGHTS, BROWSE, STEP ADVANCE): DESC, SET clamps, GET", ok);
    a[0] = 3; a[1] = X_TCAL;
    ok = !req(ED_GET, a, 2, &n);
    a[1] = X_MORPH;
    ok = ok && !req(ED_GET, a, 2, &n) && !req(ED_DESC, a, 2, &n);
    check("scope 3: TOUCH CAL (an action) and the sound's pages (MORPH) get no reply", ok);
    a[0] = 3; a[1] = X_KTOUCH; v14(a + 2, KT_VEL);
    req(ED_SET, a, 4, &n);
    a[1] = X_ATOUT; v14(a + 2, 2);
    req(ED_SET, a, 4, &n);
    a[1] = ED_S_BROWSE; v14(a + 2, 1);
    req(ED_SET, a, 4, &n);
    check("scope 3 SET: KEY TOUCH VEL, AT OUT POLY, BROWSE ALGORITHM as the knob / the menu set them",
          touch_cfg.key_touch == KT_VEL && touch_get(X_ATOUT) == 2 && browse_mode == BR_ALGO);
    /* R43: UP_PUT with the values 127.., UP_GET gives them back; protocol 14's UP_PUT: their defaults */
    {
        static const int16_t X0[14] = {2, 3, 90, 40, 1, 1, 7, 12, 0, 1, -30, BT_LEAD, 12, -12};   /* (VCO3, VCO4) */
        int16_t X[P_EDHN], want[P_EDHN];
        uint32_t m = 0, j, q;
        uint64_t lv = mx_dst_live(0) & ~1ull;              /* (engine-dests) the record's engine (0)'s live destinations:
                                                         * UP_PUT stores no dead slot */
        uint32_t d = MT_COUNT;
        for (j = 0; j < P_EDHN; j++) {                     /* (then the slots 13..24: 6-bit DSTs, from the top, the amounts' ends) */
            uint32_t s = (j - 14u) / 3u, f = (j - 14u) % 3u;
            if (j >= 14u && f == 1u)
                do d = d > 1u ? d - 1u : MT_COUNT - 1u; while (!((lv >> d) & 1u));
            X[j] = j < 14u ? X0[j] : (int16_t)(f == 0u ? 1u + (s * 7u) % (MS_NSRC - 1u) : f == 1u ? d
                                                : (s & 1u ? -64 + (int32_t)s : 63 - (int32_t)s));
        }
        X[P_JRNG - P_EDH0] = 2, X[P_JKYB - P_EDH0] = 80, X[P_JVCA - P_EDH0] = 1, X[P_JCHR - P_EDH0] = 0x2A5;   /* (PONY's EDIT 3,
                                                         * after WAV34: 4', 80 %, GATE, STEPS ON) */
        X[P_OKYB - P_EDH0] = 30, X[P_OPW - P_EDH0] = 2;   /* (RODEO's KYBD 30 %, PW0 53) */
        for (q = 0; q < 3; q++) {                          /* 0: protocol 14's frame, 1: with all 14, 2: with 5 of them */
            m = 0;
            a[m++] = 3; a[m++] = 0u;
            memcpy(a + m, "P15 TEST", 9); m += 9;
            for (i = 0; i < 127u; i++) m += v14(a + m, ed_tdesc(TSEL, p_of_ed(i))->def);
            for (i = 0; i < 16u; i++) { a[m++] = 0; a[m++] = 0; }
            if (q) {
                uint32_t nx = q == 1 ? P_EDHN : 5u;
                a[m++] = (uint8_t)nx; a[m++] = 0;
                for (i = 0; i < nx; i++) m += v14(a + m, X[i]);
            }
            p = req(ED_UP_PUT, a, m, &n);
            ok = p && p[1] == 0;
            a[0] = 3; a[1] = 1;
            p = req(ED_UP_GET, a, 2, &n);
            {
                uint32_t x = 3u + 9u + 254u + 32u;
                for (j = 0; j < P_EDHN; j++)
                    want[j] = q == 1 || (q == 2 && j < 5u) ? X[j] : TP[P_EDH0 + j].def;
                ok = ok && p && n == x + 2u + 2u * P_EDHN && (p[x] | p[x + 1] << 7) == P_EDHN;
                for (j = 0; ok && j < P_EDHN; j++)
                    ok = rv(p + x + 2 + 2 * j) == want[j];
            }
            check(q == 0 ? "UP_PUT protocol 14's frame: SLOPE, FILTER 2, the BEND page at their defaults (UP_GET flags 1)"
                  : q == 1 ? "R43: UP_PUT with the 57 values 127.. (slots 13..24, WAV34, PONY's EDIT 3, RODEO's KYBD PW0 too): UP_GET gives every one back"
                           : "UP_PUT with 5 of them: those, the rest their defaults", ok);
        }
        a[0] = 3;
        p = req(ED_UP_GET, a, 1, &n);
        check("UP_GET without flags: protocol 14's bytes", p && n == 3u + 9u + 254u + 32u);
        for (i = 0; i < P_EDHN; i++) TSEL->p[P_EDH0 + i] = TP[P_EDH0 + i].def;
        a[0] = 3;
        req(ED_UP_LOAD, a, 1, &n);
        for (i = 0, ok = 1; i < P_EDHN; i++) ok &= TSEL->p[P_EDH0 + i] == want[i];
        up_boot();
        a[0] = 3; a[1] = 1;
        p = req(ED_UP_GET, a, 2, &n);
        ok = ok && p && rv(p + 3u + 9u + 254u + 32u + 2u + 2u * (138 - 127)) == want[11] &&
                 rv(p + 3u + 9u + 254u + 32u + 2u + 2u * (140 - 127)) == want[13] &&
                 rv(p + 3u + 9u + 254u + 32u + 2u + 2u * (175 - 127)) == want[175 - 127] &&
                 rv(p + 3u + 9u + 254u + 32u + 2u + 2u * (176 - 127)) == want[176 - 127];
        check("R43: the preset loads with them (slot 24 too), and keeps them after a power-on", ok);
    }
    printf("editor_p15_test: %s\n", bad ? "FAIL" : "ok");
    return bad;
}
