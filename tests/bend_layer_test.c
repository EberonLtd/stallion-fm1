/* SPDX-License-Identifier: GPL-3.0-only */
/* Stallion voice layer R23, R24, R38-R41 and touch R19, R20, R24 (voice.c track_render, the BEND page, the matrix's new
 * sources and destinations; params.c, upreset.c, project.c):
 *   build/host/bend_layer_test [GOLDEN]
 * 1. as before: every preset of every engine, a chord under a MIDI bend moving up, down and back (a child each: the
 *    state from power-on), bit for bit as the tree before the BEND page rendered it (tests/bend_golden.txt, written
 *    there: BL_GOLDEN_ONLY=1 GOLDEN_UPDATE=1): the defaults (GLO range, both VCOs, VCF 0, ALL), lever and ribbon 0
 * 2. RANGE UP / DOWN apart (+7 / -3), the lever added and the sum limited, the ribbon beyond it (RIB RANGE alone),
 *    the bend's part: a layer in MULTI keeps its own
 * 3. a two-VCO engine (PALOMINO, STAMPEDE): VCO2 off bends VCO 1 alone, VCO1 off VCO 2 alone (the other within 1 cent);
 *    a one-VCO engine bends whatever the switches say
 * 4. VCF BEND: the cutoff with the bend (brighter up, darker down), nothing at 0
 * 5. TARGET LEAD: the newest held note alone (the others within 1 cent), the next newest once it is let go; MONO as ALL
 * 6. the matrix: BEND (MIDI and the lever), POLAT (the voice's own), CHNAT, RIBBN reach PITCH; PIT1 / PIT2 one VCO each
 *    (one VCO: as PITCH), XMOD / HPF as the engine's EDIT value moved (nothing on an engine without it); R41: <= 32
 * 7. stores: user presets and projects (track 4's nudges, and the block beside the project) keep the BEND page and the
 *    new matrix codes; older ones load the defaults
 * 8. MPE: a member channel's bend as before
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>
#include <sys/wait.h>
#ifndef BL_GOLDEN_ONLY
#define PROJ_HOST 1
static const uint8_t TRK_DEF[NPART][2] = {{ENGI_HYBRID, 0}, {1, 0}, {4, 5}};   /* (ui.c, Clipper) */
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? TRK_DEF[i][0] : 0u; }
#include "../firmware/src/project.c"
#define UP_HOST 1
#include "../firmware/src/upreset.c"
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("bend: %-104s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static int32_t o[CTL * 2];

/* ---------------------------------------------------- 1. as before --- */
static const uint8_t NOTES[4] = {48, 55, 64, 72};
static uint64_t render_preset(uint32_t e, uint32_t pi)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t b, i;
    host_tracks_init();
    for (i = 0; i < NPART; i++)
        host_preset(&trk[i], i ? 1u : e, i ? 0u : pi);
    clipper_proj_neutral();
    trk[1].p[P_LYMODE] = LY_OFF;
    for (b = 0; b < 4u; b++)
        mix_block(o, CTL);
    for (i = 0; i < 4u; i++)
        trk_note_on(&trk[0], NOTES[i], 60u + 20u * i);
    for (b = 0; b < 220u; b++) {
        static const int16_t BEND[6] = {0, 32764, -32768, 12000, -5000, 0};
        part_ctl[0].bend = BEND[b / 40u];
        if (b == 170u)
            for (i = 0; i < 4u; i++)
                trk_note_off(&trk[0], NOTES[i]);
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
    }
    return h;
}
typedef uint64_t (*hfn_t)(uint32_t, uint32_t);
static uint64_t child(hfn_t f, uint32_t a, uint32_t b)   /* f(a, b) from power-on, in a child */
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    pid = fork();
    if (pid == 0) {
        close(fd[0]);
        h = f(a, b);
        _exit(write(fd[1], &h, sizeof h) == (ssize_t)sizeof h ? 0 : 2);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != (ssize_t)sizeof h)
        h = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}
static void t_golden(const char *path)
{
    static char name[NENGINES * 64][32];
    static uint64_t want[NENGINES * 64];
    uint32_t n = 0, e, pi, diff = 0, missing = 0, total = 0;
    char w[200], line[96];
    FILE *f;
    int upd = getenv("GOLDEN_UPDATE") != 0;
    if (!upd && (f = fopen(path, "r"))) {
        while (n < NELEM(want) && fgets(line, sizeof line, f)) {
            unsigned long long x;
            if (line[0] != '#' && sscanf(line, "%31s %llx", name[n], &x) == 2)
                want[n++] = x;
        }
        fclose(f);
    }
    f = upd ? fopen(path, "w") : 0;
    if (f)
        fprintf(f, "# every engine x preset under a MIDI bend (tests/bend_layer_test.c): FNV-1a 64, as before the BEND page\n");
    for (e = 0; e < NENGINES; e++)
        for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
            char nm[32];
            uint64_t h = child(render_preset, e, pi);
            uint32_t k;
            snprintf(nm, sizeof nm, "%s/%02u", ENGINES[e]->name, pi);
            total++;
            if (f) {
                fprintf(f, "%s %016llx\n", nm, (unsigned long long)h);
                continue;
            }
            for (k = 0; k < n && strcmp(name[k], nm); k++)
                ;
            if (k == n)
                missing++;
            else if (want[k] != h) {
                if (diff < 8)
                    printf("bend:   %s differs\n", nm);
                diff++;
            }
        }
    if (f) {
        fclose(f);
        printf("bend: %s written (%u renders)\n", path, total);
        return;
    }
    snprintf(w, sizeof w, "every preset of every engine (%u) under a MIDI bend as before, bit for bit (%u differ, %u not in %s)",
             total, diff, missing, path);
    check(w, n > 0 && !diff && !missing);
}

#ifndef BL_GOLDEN_ONLY
/* ---------------------------------------------- the pitch, measured --- */
/* one sound on part 1 (engine e's first preset, everything else at its defaults: no glide, vibrato, drift) */
static void plain(uint32_t e)
{
    uint32_t i, k;
    host_tracks_init();
    for (i = 0; i < NPART; i++)
        host_preset(&trk[i], i ? 1u : e, 0);
    clipper_proj_neutral();
    for (i = 0; i < P_E0; i++)
        trk[0].p[i] = TP[i].def;
    trk[1].p[P_LYMODE] = LY_OFF;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;   /* (the voice alone) */
    if (e == ENGI_PALOMINO)                            /* (VCO 2 at VCO 1's pitch, no sync, no cross mod) */
        trk[0].p[P_E2] = 0, trk[0].p[P_E3] = 0, trk[0].p[P_E7] = 1;
    if (e == ENGI_STAMPEDE)
        trk[0].p[P_E1] = 0, trk[0].p[P_E2] = 0;
    for (k = 0; k < NPART; k++)
        part_ctl[k].bend = part_ctl[k].lever = part_ctl[k].ribbon = 0, part_ctl[k].at = 0;
    touch_cfg.rib_range = 12;
    midi_bend = 0;
    for (i = 0; i < 4u; i++)
        mix_block(o, CTL);
}
static voice_t *vof(uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (trk[0].v[i].active && trk[0].v[i].gate && trk[0].v[i].note == note)
            return &trk[0].v[i];
    return 0;
}
/* VCO k's phase step over a block of note's voice (the block after the next: the change landed) */
static uint32_t step(uint32_t note, uint32_t k)
{
    voice_t *v;
    uint32_t a;
    mix_block(o, CTL);
    v = vof(note);
    if (!v)
        return 1;
    a = v->ph[k];
    mix_block(o, CTL);
    return v->ph[k] - a;
}
static double cents(uint32_t d, uint32_t d0) { return 1200.0 * log2((double)d / (double)d0); }
static int near(double c, double want) { return fabs(c - want) <= 1.0; }

static void t_range(void)
{
    uint32_t r0, up, dn, lv, sum, rib, rib2, lv2;
    char w[220];
    plain(ENGI_PALOMINO);
    trk_note_on(&trk[0], 60, 100);
    r0 = step(60, 0);
    trk[0].p[P_BUP] = 7, trk[0].p[P_BDN] = 3;
    part_ctl[0].bend = 32764;
    up = step(60, 0);
    part_ctl[0].bend = -32768;
    dn = step(60, 0);
    part_ctl[0].bend = 0, part_ctl[0].lever = 32767;
    lv = step(60, 0);
    part_ctl[0].bend = 32764;
    sum = step(60, 0);
    part_ctl[0].bend = 0, part_ctl[0].lever = -32767;
    lv2 = step(60, 0);
    part_ctl[0].lever = 0, part_ctl[0].ribbon = 32767;
    trk[0].p[P_BUP] = trk[0].p[P_BDN] = 2;            /* (touch R19: a ribbon of 12 on a sound of RANGE 2) */
    rib = step(60, 0);
    part_ctl[0].lever = 32767;
    rib2 = step(60, 0);
    snprintf(w, sizeof w, "RANGE UP 7 / DOWN 3: %+.1f / %+.1f ct; the lever %+.1f / %+.1f, with MIDI's %+.1f (limited)",
             cents(up, r0), cents(dn, r0), cents(lv, r0), cents(lv2, r0), cents(sum, r0));
    check(w, near(cents(up, r0), 700) && near(cents(dn, r0), -300) && near(cents(lv, r0), 700) &&
             near(cents(lv2, r0), -300) && near(cents(sum, r0), 700));
    snprintf(w, sizeof w, "the ribbon x RIB RANGE 12 beyond RANGE 2: %+.1f ct; with the lever %+.1f", cents(rib, r0), cents(rib2, r0));
    check(w, near(cents(rib, r0), 1200) && near(cents(rib2, r0), 1400));
    {   /* RIB RANGE 0: no pitch; GLO: the global range (GLO > MIDI > BEND 5) */
        uint32_t z, g;
        touch_cfg.rib_range = 0, part_ctl[0].lever = 0;
        z = step(60, 0);
        part_ctl[0].ribbon = 0, part_ctl[0].lever = 0;
        trk[0].p[P_BUP] = trk[0].p[P_BDN] = -1;
        midi_bend = 5, part_ctl[0].bend = -32768;
        g = step(60, 0);
        snprintf(w, sizeof w, "RIB RANGE 0: the ribbon leaves the pitch (%+.2f ct); RANGE GLO: the global 5 (%+.1f ct)",
                 cents(z, r0), cents(g, r0));
        check(w, z == r0 && near(cents(g, r0), -500));
    }
    {   /* MULTI: the layer on its own channel keeps its own bend (touch R20: the lever bends the keys' part) */
        trk[1].p[P_LYMODE] = LY_MULTI;
        snprintf(w, sizeof w, "MULTI: part 2 follows its own controls (ctl_part %u), part 1 its own", ctl_part(&trk[1]));
        check(w, ctl_part(&trk[1]) == 1u && ctl_part(&trk[0]) == 0u);
        trk[1].p[P_LYMODE] = LY_LAYER;
        check("LAYER: the layer's sound follows the keys' part (part 1's bend, lever, ribbon)", ctl_part(&trk[1]) == 0u);
    }
}

static void t_vcos(void)
{
    static const uint32_t EN[2] = {ENGI_PALOMINO, ENGI_STAMPEDE};
    uint32_t k;
    char w[240];
    for (k = 0; k < 2u; k++) {
        uint32_t a0, b0, a1, b1, a2, b2, a3, b3;
        plain(EN[k]);
        trk_note_on(&trk[0], 60, 100);
        a0 = step(60, 0), b0 = step(60, 1);
        trk[0].p[P_BUP] = 5;
        part_ctl[0].bend = 32764, part_ctl[0].ribbon = 16384;   /* (+5, + the ribbon's 6: on the same VCOs) */
        trk[0].p[P_BVCO2] = 0;
        a1 = step(60, 0), b1 = step(60, 1);
        trk[0].p[P_BVCO2] = 1, trk[0].p[P_BVCO1] = 0;
        a2 = step(60, 0), b2 = step(60, 1);
        trk[0].p[P_BVCO2] = 0;
        a3 = step(60, 0), b3 = step(60, 1);
        snprintf(w, sizeof w, "%s: VCO2 off: VCO 1 %+.1f ct, VCO 2 %+.1f; VCO1 off: %+.1f, %+.1f; both off %+.1f %+.1f",
                 ENGINES[EN[k]]->name, cents(a1, a0), cents(b1, b0), cents(a2, a0), cents(b2, b0), cents(a3, a0), cents(b3, b0));
        check(w, near(cents(a1, a0), 1100) && near(cents(b1, b0), 0) && near(cents(a2, a0), 0) &&
                 near(cents(b2, b0), 1100) && a3 == a0 && b3 == b0);
    }
    {   /* a one-VCO engine (DIGITAL): the switches pass */
        uint32_t a0, a1;
        plain(1);
        trk_note_on(&trk[0], 60, 100);
        a0 = step(60, 0);
        trk[0].p[P_BUP] = 4, trk[0].p[P_BVCO1] = trk[0].p[P_BVCO2] = 0;
        part_ctl[0].bend = 32764;
        a1 = step(60, 0);
        snprintf(w, sizeof w, "DIGITAL (one VCO): both switches off, still bent (%+.1f ct)", cents(a1, a0));
        check(w, near(cents(a1, a0), 400));
    }
}

/* VCF BEND: ANALOG (a saw) at a low cutoff, the bend's pitch range 0: the brightness (the first difference's energy over the
 * signal's) with the bend up, at rest, down */
static uint64_t bright(uint32_t vcf, uint32_t bend)   /* (in a child: the double's bits) */
{
    double s = 0, d = 0, r;
    uint64_t h;
    uint32_t b, i;
    int32_t last = 0;
    plain(0);
    trk[0].p[P_E0] = 0, trk[0].p[P_E4] = 50, trk[0].p[P_E5] = trk[0].p[P_E6] = 0, trk[0].p[P_BUP] = trk[0].p[P_BDN] = 0, trk[0].p[P_BVCF] = (int16_t)vcf;
    part_ctl[0].bend = (int16_t)bend;
    trk_note_on(&trk[0], 48, 100);
    for (b = 0; b < 20u; b++)
        mix_block(o, CTL);
    for (b = 0; b < 200u; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t x = o[2u * i];
            s += (double)x * x;
            d += (double)(x - last) * (x - last);
            last = x;
        }
    }
    r = d / (s > 1 ? s : 1);
    memcpy(&h, &r, sizeof h);
    return h;
}
static double brightness(uint32_t vcf, int32_t bend)
{
    uint64_t h = child(bright, vcf, (uint32_t)bend);
    double r;
    memcpy(&r, &h, sizeof r);
    return r;
}
static uint64_t vcf_hash(uint32_t vcf, uint32_t bend)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t b, i;
    plain(0);
    trk[0].p[P_E4] = 40, trk[0].p[P_BUP] = trk[0].p[P_BDN] = 0, trk[0].p[P_BVCF] = (int16_t)vcf;
    trk_note_on(&trk[0], 48, 100);
    part_ctl[0].bend = (int16_t)bend;
    for (b = 0; b < 60u; b++) {
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
    }
    return h;
}
static void t_vcf(void)
{
    double up = brightness(63, 16384), mid = brightness(63, 0), dn = brightness(63, -16384), z = brightness(0, 16384);
    char w[200];
    snprintf(w, sizeof w, "VCF BEND 63 (RANGE 0), half a bend: brightness up %.5f, at rest %.5f, down %.5f; VCF 0 up %.5f",
             up, mid, dn, z);
    check(w, up > mid * 2.0 && dn < mid * 0.8 && z == mid);
    check("VCF BEND 0: the bend leaves the filter (RANGE 0: bit for bit as at rest)",
          child((hfn_t)vcf_hash, 0, 32764) == child((hfn_t)vcf_hash, 0, 0) &&
          child((hfn_t)vcf_hash, 63, 32764) != child((hfn_t)vcf_hash, 63, 0));
}

static void t_lead(void)
{
    uint32_t a0, b0, c0, a1, b1, c1, a2, b2;
    char w[240];
    plain(ENGI_PALOMINO);
    trk[0].p[P_BUP] = 2, trk[0].p[P_BTGT] = BT_LEAD;
    trk_note_on(&trk[0], 48, 100);
    trk_note_on(&trk[0], 55, 100);
    trk_note_on(&trk[0], 60, 100);
    a0 = step(48, 0), b0 = step(55, 0), c0 = step(60, 0);
    part_ctl[0].bend = 32764, part_ctl[0].lever = 0;
    a1 = step(48, 0), b1 = step(55, 0), c1 = step(60, 0);
    trk_note_off(&trk[0], 60);
    a2 = step(48, 0), b2 = step(55, 0);
    snprintf(w, sizeof w, "LEAD: the newest note %+.2f ct, the others %+.2f %+.2f; it let go: the next newest %+.2f, %+.2f",
             cents(c1, c0), cents(a1, a0), cents(b1, b0), cents(b2, b0), cents(a2, a0));
    check(w, near(cents(c1, c0), 200) && fabs(cents(a1, a0)) <= 1.0 && fabs(cents(b1, b0)) <= 1.0 &&
             near(cents(b2, b0), 200) && fabs(cents(a2, a0)) <= 1.0);
    {   /* ALL: every voice; MONO with LEAD: as ALL */
        uint32_t m0, m1;
        trk[0].p[P_BTGT] = BT_ALL;
        a1 = step(48, 0);
        trk_all_off(&trk[0]);
        part_ctl[0].bend = 0;
        trk[0].p[P_BTGT] = BT_LEAD, trk[0].p[P_VOICE] = V_MONO;
        trk_note_on(&trk[0], 48, 100);
        m0 = step(48, 0);
        part_ctl[0].bend = 32764;
        m1 = step(48, 0);
        snprintf(w, sizeof w, "ALL: an older note bent too (%+.1f ct); MONO with LEAD: as ALL (%+.1f ct)", cents(a1, a0), cents(m1, m0));
        check(w, near(cents(a1, a0), 200) && near(cents(m1, m0), 200));
    }
}

/* ------------------------------------------------------- the matrix --- */
static void slot(uint32_t k, uint32_t src, uint32_t dst, int32_t amt)
{
    uint32_t id = mx_pid(k);
    trk[0].p[id] = (int16_t)src, trk[0].p[id + 1u] = (int16_t)dst, trk[0].p[id + 2u] = (int16_t)amt;
}
/* a chord from power-on through the mix, set by mode m (below), engine e: its hash */
static uint64_t mx_hash(uint32_t e, uint32_t m)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t b, i;
    plain(e);
    touch_cfg.rib_range = 0, part_ctl[0].ribbon = 32767;        /* (RIBBN as a constant source: no pitch) */
    if (e == ENGI_PALOMINO)
        trk[0].p[P_E3] = 0;
    if (m == 1u)
        slot(7, MS_RIB, MT_XMOD, 63);
    else if (m == 2u)                                 /* (32767 x 63 / 64 of the range: + 125) */
        trk[0].p[P_E3] = (int16_t)clamp(trk[0].p[P_E3] + 125, ENGINES[e]->edit[3].min, ENGINES[e]->edit[3].max);
    else if (m == 3u)
        slot(2, MS_RIB, MT_HPF, 63);
    else if (m == 4u)
        trk[0].p[P_E4] = (int16_t)clamp(trk[0].p[P_E4] + 125, ENGINES[e]->edit[4].min, ENGINES[e]->edit[4].max);
    for (i = 0; i < 3u; i++)
        trk_note_on(&trk[0], NOTES[i], 100);
    for (b = 0; b < 80u; b++) {
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
    }
    return h;
}
static void t_matrix(void)
{
    char w[260];
    check("R41: 31 sources (5 bits in the stores) and 37 destinations (6 bits; SEMI, SEM34 among them), every one named",
          MS_NSRC == 31 && MT_COUNT == 37 && MS_NSRC <= 32 && MT_COUNT <= 64 && TP[P_M1SRC].max == MS_NSRC - 1 &&
          TP[P_M7DST].max == MT_COUNT - 1 && !strcmp(N_MSRC[MS_RIB], "RIBBN") && !strcmp(N_MDST[MT_HPF], "HPF") &&
          MS_MAC4 == 19 && MT_RES2 == 17);
    {   /* BEND (MIDI's and the lever's), at RANGE 0: through the matrix alone */
        uint32_t r0, b1, l1, s1;
        plain(1);
        trk[0].p[P_BUP] = trk[0].p[P_BDN] = 0;
        slot(0, MS_BEND, MT_PITCH, 20);
        trk_note_on(&trk[0], 60, 100);
        r0 = step(60, 0);
        part_ctl[0].bend = 16384;
        b1 = step(60, 0);
        part_ctl[0].bend = 0, part_ctl[0].lever = 16384;
        l1 = step(60, 0);
        part_ctl[0].bend = 32767;
        s1 = step(60, 0);
        snprintf(w, sizeof w, "BEND > PITCH at RANGE 0: MIDI's %+.1f ct, the lever's %+.1f (the same), both full %+.1f",
                 cents(b1, r0), cents(l1, r0), cents(s1, r0));
        check(w, cents(b1, r0) > 50 && b1 == l1 && cents(s1, r0) > cents(b1, r0) * 1.9);
    }
    {   /* POLAT: the voice's own; CHNAT: every voice; RIBBN at RIB RANGE 0 */
        uint32_t a0, c0, a1, c1, a2, c2, r1;
        voice_t *v;
        plain(1);
        slot(0, MS_PAT, MT_PITCH, 20);
        trk_note_on(&trk[0], 48, 100);
        trk_note_on(&trk[0], 60, 100);
        a0 = step(48, 0), c0 = step(60, 0);
        v = vof(60);
        if (v)
            v->pat = 127;
        a1 = step(48, 0), c1 = step(60, 0);
        slot(0, MS_CAT, MT_PITCH, 20);
        if (v)
            v->pat = 0;
        part_ctl[0].at = 127;
        a2 = step(48, 0), c2 = step(60, 0);
        part_ctl[0].at = 0;
        slot(0, MS_RIB, MT_PITCH, 20);
        touch_cfg.rib_range = 0, part_ctl[0].ribbon = 32767;
        r1 = step(48, 0);
        snprintf(w, sizeof w, "POLAT > PITCH: its note %+.1f ct, the other %+.2f; CHNAT: both %+.1f %+.1f; RIBBN (RIB RANGE 0) %+.1f",
                 cents(c1, c0), cents(a1, a0), cents(c2, c0), cents(a2, a0), cents(r1, a0));
        check(w, cents(c1, c0) > 100 && a1 == a0 && near(cents(c2, c0), cents(c1, c0)) && near(cents(a2, a0), cents(c1, c0)) &&
                 cents(r1, a0) > 100);
    }
    {   /* PIT1 / PIT2: one VCO each (PALOMINO), as PITCH on one VCO (DIGITAL) */
        uint32_t a0, b0, a1, b1, a2, b2, d0, d1, d2;
        plain(ENGI_PALOMINO);
        part_ctl[0].at = 127;
        trk_note_on(&trk[0], 60, 100);
        a0 = step(60, 0), b0 = step(60, 1);
        slot(8, MS_CAT, MT_PIT2, 20);
        a1 = step(60, 0), b1 = step(60, 1);
        slot(8, MS_CAT, MT_PIT1, 20);
        a2 = step(60, 0), b2 = step(60, 1);
        plain(1);
        part_ctl[0].at = 127;
        trk_note_on(&trk[0], 60, 100);
        slot(3, MS_CAT, MT_PITCH, 20);
        d0 = step(60, 0);
        slot(3, MS_CAT, MT_PIT1, 20);
        d1 = step(60, 0);
        slot(3, MS_CAT, MT_PIT2, 20);
        d2 = step(60, 0);
        snprintf(w, sizeof w, "PALOMINO: PIT2 moves VCO 2 (%+.1f ct, VCO 1 %+.2f), PIT1 VCO 1 (%+.1f, VCO 2 %+.2f); DIGITAL: PIT1 = PIT2 = PITCH",
                 cents(b1, b0), cents(a1, a0), cents(a2, a0), cents(b2, b0));
        check(w, cents(b1, b0) > 100 && a1 == a0 && cents(a2, a0) > 100 && b2 == b0 && d1 == d0 && d2 == d0);
    }
    {   /* XMOD, HPF: the engine's EDIT value, moved by the slot as by the knob; none: nothing */
        int jx = child(mx_hash, ENGI_PALOMINO, 1) == child(mx_hash, ENGI_PALOMINO, 2) &&
                 child(mx_hash, ENGI_PALOMINO, 1) != child(mx_hash, ENGI_PALOMINO, 0);
        int mx = child(mx_hash, ENGI_MARE, 1) == child(mx_hash, ENGI_MARE, 2) &&
                 child(mx_hash, ENGI_MARE, 1) != child(mx_hash, ENGI_MARE, 0);
        int ch = child(mx_hash, ENGI_STAMPEDE, 3) == child(mx_hash, ENGI_STAMPEDE, 4) &&
                 child(mx_hash, ENGI_STAMPEDE, 3) != child(mx_hash, ENGI_STAMPEDE, 0);
        int no = child(mx_hash, ENGI_STAMPEDE, 1) == child(mx_hash, ENGI_STAMPEDE, 0) && child(mx_hash, 1, 3) == child(mx_hash, 1, 0) &&
                 child(mx_hash, 1, 1) == child(mx_hash, 1, 0) && child(mx_hash, ENGI_PALOMINO, 3) == child(mx_hash, ENGI_PALOMINO, 0);
        snprintf(w, sizeof w, "XMOD: PALOMINO's XMOD %s, MARE's SY/FM %s; HPF: STAMPEDE's HPF %s; none (STAMPEDE XMOD, DIGITAL, PALOMINO HPF) %s",
                 jx ? "ok" : "no", mx ? "ok" : "no", ch ? "ok" : "no", no ? "nothing" : "moved");
        check(w, jx && mx && ch && no);
    }
}

/* ------------------------------------------------------------ stores --- */
static void bend_set(int16_t *p, int alt)
{
    p[P_BUP] = alt ? 0 : 7, p[P_BDN] = alt ? 24 : -1, p[P_BVCO1] = alt ? 1 : 0, p[P_BVCO2] = alt ? 0 : 1;
    p[P_BVCF] = alt ? 63 : -63, p[P_BTGT] = alt ? BT_ALL : BT_LEAD;
}
static int bend_is_def(const int16_t *p)
{
    uint32_t i;
    int d = 1;
    for (i = P_BUP; i < P_E0; i++)
        d &= p[i] == TP[i].def;
    return d;
}
static void t_stores(void)
{
    char w[200];
    {   /* a user preset (preset format 2: every value a byte): the BEND page; RIBBN > HPF in slot 1, BEND > PIT1
         * in slot 3, POLAT > XMOD in slot 8 */
        up_rec_t r;
        int16_t v[P_COUNT], out[P_COUNT], def[P_COUNT];
        uint32_t i;
        int same = 1, old = 1;
        for (i = 0; i < P_COUNT; i++)
            def[i] = i >= P_E0 ? 0 : TP[i].def, v[i] = def[i];
        bend_set(v, 0);
        v[P_F2MODEL] = FM_OTA, v[P_F2LINK] = 0;
        v[P_M1SRC] = MS_RIB, v[P_M1DST] = MT_HPF, v[P_M1AMT] = -20;
        v[P_M3SRC] = MS_BEND, v[P_M3DST] = MT_PIT1, v[P_M3AMT] = 33;
        v[P_M6SRC] = MS_CAT, v[P_M6DST] = MT_PIT2, v[P_M6AMT] = 1;
        v[P_M8SRC] = MS_PAT, v[P_M8DST] = MT_XMOD, v[P_M8AMT] = 63;
        memset(&r, 0, sizeof r);
        r.used = UP_USED, r.name[0] = 'B';
        up_rec_set(&r, v);
        up_params(&r, out, def);
        for (i = 0; i < P_COUNT; i++)
            same &= out[i] == v[i];
        (void)old;                                     /* (preset format 2: no older Stallion record to read) */
        snprintf(w, sizeof w, "a user preset keeps the BEND page and RIBBN > HPF, BEND > PIT1, CHNAT > PIT2, POLAT > XMOD");
        check(w, same && old);
    }
    {   /* projects: track 4's nudges (no drum hits), the block beside the project (with them); older ones */
        static project_t pj;
        uint32_t i, k;
        int nud, side, onud, oside;
        host_tracks_init();
        host_preset(&trk[0], ENGI_STAMPEDE, 0), host_preset(&trk[1], ENGI_PALOMINO, 0);   /* (engine-dests: HPF and XMOD
                                                         * live there, a project load clears a dead one) */
        bend_set(trk[0].p, 0), bend_set(trk[1].p, 1);
        trk[0].p[P_M2SRC] = MS_RIB, trk[0].p[P_M2DST] = MT_HPF, trk[0].p[P_M2AMT] = 9;
        trk[1].p[P_M9SRC] = MS_CAT, trk[1].p[P_M9DST] = MT_XMOD, trk[1].p[P_M9AMT] = -9;
        trk[0].p[P_FSLOPE] = FS_48;
        proj_capture(&pj);
        memset(&proj_xss[proj_x10_at(&pj)], 0, sizeof proj_xss[0]);   /* (the nudges alone) */
        for (k = 0; k < 2u; k++)
            for (i = P_BUP; i < P_E0; i++)
                trk[k].p[i] = TP[i].def;
        trk[0].p[P_M2SRC] = trk[1].p[P_M9SRC] = 0;
        proj_apply(&pj, 1);
        nud = trk[0].p[P_BUP] == 7 && trk[0].p[P_BDN] == -1 && trk[0].p[P_BVCO1] == 0 && trk[0].p[P_BVCF] == -63 &&
              trk[0].p[P_BTGT] == BT_LEAD && trk[1].p[P_BUP] == 0 && trk[1].p[P_BDN] == 24 && trk[1].p[P_BVCO2] == 0 &&
              trk[1].p[P_BVCF] == 63 && trk[0].p[P_M2SRC] == MS_RIB && trk[0].p[P_M2DST] == MT_HPF &&
              trk[1].p[P_M9SRC] == MS_CAT && trk[1].p[P_M9DST] == MT_XMOD && trk[0].p[P_FSLOPE] == FS_48;
        /* with a drum hit: the block beside the project alone */
        bend_set(trk[0].p, 0), bend_set(trk[1].p, 1);
        trk[3].dstep[5].on[0] = 1;
        proj_capture(&pj);
        for (k = 0; k < 2u; k++)
            for (i = P_BUP; i < P_E0; i++)
                trk[k].p[i] = TP[i].def;
        proj_apply(&pj, 1);
        side = trk[0].p[P_BUP] == 7 && trk[0].p[P_BDN] == -1 && trk[0].p[P_BVCO1] == 0 && trk[0].p[P_BVCO2] == 1 &&
               trk[0].p[P_BVCF] == -63 && trk[0].p[P_BTGT] == BT_LEAD && trk[1].p[P_BUP] == 0 && trk[1].p[P_BDN] == 24 &&
               trk[1].p[P_BVCO1] == 1 && trk[1].p[P_BVCO2] == 0 && trk[1].p[P_BVCF] == 63 && trk[1].p[P_BTGT] == BT_ALL &&
               trk[0].p[P_FSLOPE] == FS_48;
        {   /* the block as 1.3 wrote it (nothing after FILTER 2: zeros) */
            proj_xs_t *x = &proj_xss[proj_x10_at(&pj)];
            memset(&x->m[2u * x->n11 + 1u + 2u * x->nf], 0, PROJ_XS_M - (2u * x->n11 + 1u + 2u * x->nf));
            x->chk = proj_hash(x, __builtin_offsetof(proj_xs_t, chk));   /* (and no extension after it) */
            memset(&x->xn, 0, sizeof *x - __builtin_offsetof(proj_xs_t, xn));
            bend_set(trk[0].p, 0), bend_set(trk[1].p, 1);
            proj_apply(&pj, 1);
            oside = bend_is_def(trk[0].p) && bend_is_def(trk[1].p) && trk[0].p[P_FSLOPE] == FS_48;
        }
        {   /* track 4's nudges as 1.3 wrote them: 6 values a sound (SLOPE .. LINK) */
            int8_t *m;
            trk[3].dstep[5].on[0] = 0;
            proj_capture(&pj);
            memset(&proj_xss[proj_x10_at(&pj)], 0, sizeof proj_xss[0]);
            m = pj.t[3].micro;
            memset(m, 0, NSTEP);
            m[0] = (int8_t)CLIPPER_PROJ_MARK_11, m[1] = 6;
            for (k = 0; k < 2u; k++)
                for (i = 0; i < 6u; i++)
                    m[2u + 6u * k + i] = (int8_t)(uint8_t)(TP[P_FSLOPE + i].def - TP[P_FSLOPE + i].min);
            m[2] = (int8_t)(FS_48 - TP[P_FSLOPE].min);
            m[14] = (int8_t)proj_xf_ck(m, 6);
            pj.sum = proj_sum(&pj);
            bend_set(trk[0].p, 0), bend_set(trk[1].p, 1);
            proj_apply(&pj, 1);
            onud = bend_is_def(trk[0].p) && bend_is_def(trk[1].p) && trk[0].p[P_FSLOPE] == FS_48;
        }
        snprintf(w, sizeof w, "projects keep the BEND page and the new codes: track 4's nudges %s, beside it %s; 1.3's: defaults %s %s",
                 nud ? "ok" : "no", side ? "ok" : "no", oside ? "ok" : "no", onud ? "ok" : "no");
        check(w, nud && side && oside && onud);
    }
    {   /* the page: BEND, BEND 2 in LFO; the device's own (the editor passes over them) */
        uint32_t i;
        int pg = 0, pg2 = 0;
        char val[12];
        const char *unit = 0;
        for (i = 0; i < NPAGES; i++) {
            if (!strcmp(PAGES[i].title, "BEND") && PAGES[i].fam == FAM_LFO)
                pg = PAGES[i].id[0] == P_BUP && PAGES[i].id[1] == P_BDN && PAGES[i].id[2] == P_BVCF && PAGES[i].id[3] == P_BTGT;
            if (!strcmp(PAGES[i].title, "BEND 2") && PAGES[i].fam == FAM_LFO)
                pg2 = PAGES[i].id[0] == P_BVCO1 && PAGES[i].id[1] == P_BVCO2;
        }
        param_format(&TP[P_BUP], -1, val, &unit);
        snprintf(w, sizeof w, "LFO > BEND (UP DOWN VCF TARGT), BEND 2 (VCO1 VCO2); UP at its default: %s; not the editor's (P_EDN %u)",
                 val, (unsigned)P_EDN);
        check(w, pg && pg2 && !strcmp(val, "GLO") && P_EDN == 127u && P_BUP >= (int)P_EDH0 && P_BTGT < P_E0 &&
                 ed_of_p(P_E0) == 119u);
    }
}

/* -------------------------------------------------------------- MPE --- */
static void t_mpe(void)
{
    uint32_t a0, a1;
    char w[160];
    plain(ENGI_PALOMINO);
    trk[0].p[P_BUP] = trk[0].p[P_BDN] = 0, trk[0].p[P_BVCO2] = 0, trk[0].p[P_BTGT] = BT_LEAD;
    mpe_mode = 1;                                     /* (48 ST) */
    mpe_cur_ch = 2;
    trk_note_on(&trk[0], 60, 100);
    mpe_cur_ch = 0;
    a0 = step(60, 0);
    mpe_bend[2] = 8192;                               /* (12 semitones) */
    a1 = step(60, 0);
    snprintf(w, sizeof w, "MPE: a member channel's own bend as before (%+.1f ct), whatever the BEND page says", cents(a1, a0));
    check(w, near(cents(a1, a0), 1200));
    mpe_bend[2] = 0, mpe_mode = 0;
}
#endif

int main(int argc, char **argv)
{
    const char *gold = argc > 1 ? argv[1] : "tests/bend_golden.txt";
    t_golden(gold);
    if (getenv("GOLDEN_UPDATE"))
        return 0;
#ifndef BL_GOLDEN_ONLY
    t_range();
    t_vcos();
    t_vcf();
    t_lead();
    t_matrix();
    t_stores();
    t_mpe();
#endif
    printf("bend: %s\n", bad ? "FAILED" : "all passed");
    return bad;
}
