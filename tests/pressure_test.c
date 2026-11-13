/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* Voice pressure on a slower chip (CLIPPER_SYNTH 1; run_tests.sh builds it with NVOICE=8 and 16):
 *   build/host/pressure_test        (VERBOSE=1: every voice given up)
 * A take played on the SMK-37 (take 5: PONY PAD, a triad every ~0.8 s, each let go before the next, its long release
 * ringing into the next chord; 34 s, as captured from the keyboard) replayed on part 1 from power-on (the run-time
 * correction at its boot value), each half's render time from a CPU model, judged as audio.c judges it (shed_voice at
 * a half's start, shed_judge at its end, vc_half fed the time):
 *   the model   fm1-emu's measured cost of PONY PAD (tools/vcost_bench.tsv: the render with no voice 583 q, a voice 306 q
 *               on one core, its voices split over both cores 46 q, the part 45 q + its effects 223 q; q = 1/4096 of
 *               the half) x the chip's slowness: 1 x (fm1-emu), 2 x (the SMK-37: a held triad at 69 % where fm1-emu
 *               shows 34 %), 2.5 x (slower still). Every voice rendered counts, the fading ones too.
 *   boot        the correction starts at the build's value (VC_R0_FM1: FM-1, VC_R0_SMK: SMK-37) and learns the chip
 * Checks, each run: no late half (over 100 %); the overload shedder never fires (the voices are given up at the note-on,
 * before the chord renders) and no held note lost (both not at 2.5 x: a held triad alone takes 92 % there, over the
 * budget: nothing but the chord's own notes left to give, its third sounds and the shedder thins it, faded); no voice louder than -42 dB cut: a voice given up fades over 5 ms or
 * more (never restarted in place at its level); and at the end the correction within 15 % of the chip's slowness.
 * Exit status: failed checks. Main (no admission) fails at 2 x: the tails' fades take the chord changes over 85 %. */
#define main hostsim_main
#include "hostsim.c"
#undef main
/* (Stallion 1.0) the pad these checks play: PONY's factory bank has no PONY PAD now; WARM PAD is its closest (the same
 * waves, pulse width, sub, HPF and chorus, a little darker and slower) */
#define PONY_PAD "WARM PAD"
#include <math.h>

#define LOUD (1 << 17)                               /* -42 dB */
#define CUT_MS 5.0
#define BLK_MS (1000.0 * CTL / FS)
/* fm1-emu's PONY PAD (tools/vcost_bench.tsv), q = 1/4096 of the half */
#define M_BASE 583
#define M_VOICE 306
#define M_SPLIT 46
#define M_PART (45 + 223)

typedef struct { uint16_t ms; uint8_t note, vel; } ev_t;
static const ev_t SEQ[] = {   /* take 5 (scratch live/take5/midi.tsv): ms, note, velocity (0: off) */
    {195,62,0},{200,65,0},{235,69,0},{375,71,95},{375,64,95},{387,67,90},{995,64,0},
    {1012,67,0},{1041,71,0},{1198,72,95},{1204,65,95},{1209,69,86},{1749,69,0},{1755,72,0},
    {1772,65,0},{2185,67,95},{2198,64,102},{2198,60,102},{2753,60,0},{2782,64,0},{2811,67,0},
    {2997,69,102},{3015,65,102},{3015,62,95},{3548,62,0},{3571,65,0},{3594,69,0},{3809,71,90},
    {3821,67,95},{3821,64,90},{4314,64,0},{4325,67,0},{4348,71,0},{4523,72,102},{4529,69,111},
    {4535,65,102},{5428,69,0},{5439,65,0},{5439,72,0},{5725,67,95},{5736,64,102},{5747,60,90},
    {5985,60,0},{5996,64,0},{6025,67,0},{6194,69,102},{6194,62,95},{6200,65,111},{6449,62,0},
    {6466,65,0},{6496,69,0},{6664,67,102},{6664,71,95},{6671,64,90},{7023,64,0},{7035,67,0},
    {7064,71,0},{7250,72,95},{7262,69,102},{7268,65,90},{7900,69,0},{7910,65,0},{7917,72,0},
    {8759,64,11},{8817,60,1},{8846,67,5},{8955,64,0},{8961,60,0},{8976,67,0},{9364,64,49},
    {9381,67,30},{9387,60,33},{9496,60,0},{9506,64,0},{9518,67,0},{9775,65,69},{9775,69,61},
    {9787,62,58},{9925,62,0},{9936,65,0},{9947,69,0},{10203,71,61},{10215,64,55},{10220,67,61},
    {10366,67,0},{10366,64,0},{10389,71,0},{10627,72,65},{10633,69,78},{10639,65,55},{10789,69,0},
    {10795,65,0},{10806,72,0},{11057,74,69},{11074,67,61},{11074,71,65},{11207,71,0},{11213,67,0},
    {11230,74,0},{11562,67,73},{11567,64,78},{11579,60,55},{11717,60,0},{11717,64,0},{11723,67,0},
    {12031,69,73},{12038,65,73},{12044,62,69},{12141,65,0},{12147,62,0},{12158,69,0},{12472,71,86},
    {12478,64,86},{12491,67,82},{12599,67,0},{12610,64,0},{12622,71,0},{12914,72,90},{12914,65,86},
    {12914,69,86},{13023,69,0},{13039,65,0},{13046,72,0},{28619,64,1},{28630,67,1},{28672,60,1},
    {29322,64,0},{29322,67,0},{29322,60,0},{29594,67,30},{29606,64,29},{29624,60,29},{29821,64,0},
    {29821,60,0},{29844,67,0},{30053,64,61},{30053,67,49},{30064,60,45},{30220,60,0},{30220,64,0},
    {30237,67,0},{30447,67,45},{30447,64,55},{30464,60,42},{30621,60,0},{30621,64,0},{30639,67,0},
    {30842,67,42},{30848,64,58},{30859,60,49},{31033,64,0},{31033,60,0},{31050,67,0},{31242,67,52},
    {31248,64,61},{31265,60,52},{31440,60,0},{31440,64,0},{31457,67,0},{32199,67,42},{32212,64,58},
    {32227,60,42},{32768,60,0},{32773,64,0},{32791,67,0},{33012,67,65},{33018,64,82},{33030,60,69},
    {33220,60,0},{33220,64,0},{33238,67,0},{33401,67,61},{33407,64,82},{33412,60,73},{33574,60,0},
    {33580,64,0},{33591,67,0},{33766,67,58},{33772,64,78},{33784,60,69},{33945,60,0},{33958,64,0},
    {33974,67,0},{34119,67,65},{34126,64,82},{34138,60,73},{34282,60,0},{34300,64,0},{34311,67,0},
};
#define NSEQ (sizeof SEQ / sizeof SEQ[0])
#define BOOT_MS 2000u                                /* power-on to the first note: nothing sounds */

static int32_t o[2 * CTL];
static int bad;
static void check(int ok, const char *what)
{
    printf("pressure: %s  %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static double db(int32_t env) { return env > 0 ? 20 * log10(env / 16777216.0) : -200; }

static uint32_t model_q(uint32_t n)                  /* a part's n voices, fm1-emu */
{
    if (!n)
        return 0;
    return n < 2u ? M_VOICE + M_PART : (n + 1u) / 2u * M_VOICE + M_SPLIT + M_PART;
}

typedef struct {
    uint32_t late, over85, sheds, steals, held_lost, given, loud, cut, inplace, notes;
    double peak, shortest, ratio;
} res_t;

/* the take on PONY PAD, the chip slow100 / 100 x fm1-emu, the correction from r0 (Q8) */
static void run(res_t *r, uint32_t slow100, uint32_t r0)
{
    track_t *T = &trk[0];
    const uint32_t nb = (uint32_t)((BOOT_MS + SEQ[NSEQ - 1u].ms + 3000u) * (uint64_t)FS / 1000u / CTL);
    uint32_t b, k = 0, j, p, shed0, steals0 = 0, acc = 0;
    uint8_t over = 0, req = 0, late = 0, held[128];
    int shed_now = 0;
    struct { uint32_t at; int32_t env0; uint8_t on, note; } g[NPART * NVOICE];
    memset(r, 0, sizeof *r);
    memset(g, 0, sizeof g);
    memset(held, 0, sizeof held);
    r->shortest = 1e9;
    for (p = 0; p < NPART; p++)
        memset(&trk[p], 0, sizeof trk[p]);           /* power-on */
    host_tracks_init();
    host_preset(T, ENGI_PONY, host_pi(ENGI_PONY, PONY_PAD));   /* the closest pad to the take's PONY PAD (POLY) */
    T->p[P_DLY] = 10, T->p[P_REV] = 40, T->p[P_VINT] = 24, T->p[P_E7] = 1;   /* PONY PAD's sends, VINTAGE and CHOR I as the
                                                                             * take and the cost model had them */
#ifdef VC_BUDGET
    vc_ratio = (uint16_t)r0;
    vc_base = VC_BASE;
    vc_acc = 0;
    steals0 = vc_steals;
#else
    (void)r0;
#endif
    shed0 = shed_count;
    for (b = 0; b < nb; b++) {
        voice_t before[NPART * NVOICE];
        uint32_t n[NPART];
        for (p = 0; p < NPART; p++)
            memcpy(&before[p * NVOICE], trk[p].v, sizeof trk[p].v);
        if (b % 8u == 0u) {                          /* a half starts: audio.c's shed */
            shed_now = 0;
            if (req) {
                req = 0;
#ifdef STEAL_BLOCKS
                shed_now = shed_voice(late);
#else
                shed_voice();
#endif
            }
        }
        while (k < NSEQ && (uint64_t)(BOOT_MS + SEQ[k].ms) * FS / 1000u <= (uint64_t)b * CTL) {
            if (SEQ[k].vel) {
                trk_note_on(T, SEQ[k].note, SEQ[k].vel);
                held[SEQ[k].note] = 1;
                r->notes++;
            } else {
                trk_note_off(T, SEQ[k].note);
                held[SEQ[k].note] = 0;
            }
            k++;
        }
        for (p = 0; p < NPART; p++)                  /* the voices given up before this block's render */
            for (j = 0; j < NVOICE; j++) {
                const voice_t *v = &trk[p].v[j], *p0 = &before[p * NVOICE + j];
                uint32_t gi = p * NVOICE + j;
                if (!p0->active || p0->stage == 4u)
                    continue;
                if (v->stage == 4u) {
                    r->given++;
                    if (p0->env >= LOUD)
                        r->loud++, g[gi].on = 1, g[gi].at = b, g[gi].env0 = p0->env, g[gi].note = p0->note;
                    if (getenv("VERBOSE"))
                        printf("  %7.3f s p%u v%u (%2d, %s, %.1f dB) given up%s\n", b * BLK_MS / 1000.0, p, j, p0->note,
                               p0->gate ? "held" : "released", db(p0->env), shed_now && b % 8u == 0u ? " (shed)" : "");
                } else if (v->active && v->note != p0->note && p0->env >= LOUD) {
                    r->inplace++;                    /* restarted in place at its level: a cut */
                    if (getenv("VERBOSE"))
                        printf("  %7.3f s p%u v%u (%2d, %.1f dB) restarted in place for %d\n", b * BLK_MS / 1000.0, p,
                               j, p0->note, db(p0->env), v->note);
                }
            }
        for (p = 0; p < NPART; p++) {                /* the block's cost: every voice rendered (the fading ones too) */
            n[p] = 0;
            for (j = 0; j < NVOICE; j++)
                n[p] += trk[p].v[j].active;
            acc += model_q(n[p]);
        }
        mix_block(o, CTL);
        for (j = 0; j < NPART * NVOICE; j++)         /* the fades: how long */
            if (g[j].on) {
                const voice_t *v = &trk[j / NVOICE].v[j % NVOICE];
                if (v->active && v->stage != 4u && v->note == g[j].note && !(v->pend & VP_ON)) {
                    g[j].on = 0;                     /* (its note played again within the fade: it goes on) */
                } else if (!(v->active && v->stage == 4u)) {
                    double ms = (b + 1u - g[j].at) * BLK_MS;
                    r->shortest = ms < r->shortest ? ms : r->shortest;
                    r->cut += ms < CUT_MS;
                    g[j].on = 0;
                }
            }
        for (j = 0; j < 128u; j++)                   /* a key held: its note sounds (or waits for its voice's fade) */
            if (held[j] == 1u) {
                uint32_t i, s = 0;
                for (i = 0; i < NVOICE; i++)
                    s += (T->v[i].active && T->v[i].stage != 4u && T->v[i].note == j) ||
                         ((T->v[i].pend & VP_ON) && T->v[i].pend_note == j);
                if (!s) {
                    r->held_lost++, held[j] = 2;     /* (counted once) */
                    if (getenv("VERBOSE"))
                        printf("  %7.3f s held note %u lost\n", b * BLK_MS / 1000.0, j);
                }
            }
        if (b % 8u == 7u) {                          /* a half ends: its time on the chip, judged as audio.c does */
            uint32_t q = (uint32_t)(((uint64_t)M_BASE * 8u + acc) * slow100 / 800u), ov = q * 100u > 85u * 4096u;
            double pct = q * 100.0 / 4096;
            r->peak = pct > r->peak ? pct : r->peak;
            r->late += q > 4096u;
            r->over85 += ov;
#ifdef STEAL_BLOCKS
            if (shed_judge(&over, shed_now, ov))
                req = 1;
#else
            over = (uint8_t)(over << 1 | ov);
            if ((over & 3u) == 3u)
                req = 1;
#endif
            late = q > 4096u;
#ifdef VC_BUDGET
            vc_half((uint32_t)((uint64_t)q * VC_HALF_US / 4096u));
#endif
            acc = 0;
        }
    }
    r->sheds = shed_count - shed0;
#ifdef VC_BUDGET
    r->steals = vc_steals - steals0;
    r->ratio = vc_ratio / 256.0;
#endif
    if (r->shortest > 1e8)
        r->shortest = 0;
}

static void report(const char *name, uint32_t slow100, uint32_t r0, int fits)
{
    res_t r;
    char w[400];
    run(&r, slow100, r0);
    printf("pressure: %s: %u notes, peak %.0f %%, %u halves over 85 %%, %u late, %u shed, %u given up at the note-on "
           "(%u voices given up in all, %u louder than -42 dB: shortest fade %.1f ms, %u under %.0f ms, %u restarted in "
           "place), %u held notes lost, correction %.2f\n", name, r.notes, r.peak, r.over85, r.late, r.sheds, r.steals,
           r.given, r.loud, r.shortest, r.cut, CUT_MS, r.inplace, r.held_lost, r.ratio);
    if (fits) {
        snprintf(w, sizeof w, "%s: no late half, the shedder never fires", name);
        check(!r.late && !r.sheds, w);
        snprintf(w, sizeof w, "%s: no held note lost", name);
        check(!r.held_lost, w);
    } else {                                         /* (2.5 x: a held triad alone takes 92 %, over the budget: the
                                                      * third note sounds, the shedder thins it after two halves) */
        snprintf(w, sizeof w, "%s: no late half", name);
        check(!r.late, w);
    }
    snprintf(w, sizeof w, "%s: loud voices given up fade over >= %.0f ms (none cut, none restarted in place)", name, CUT_MS);
    check(!r.cut && !r.inplace, w);
#ifdef VC_BUDGET
    snprintf(w, sizeof w, "%s: the correction learnt the chip (%.2f, want %.2f +- 15 %%)", name, r.ratio, slow100 / 100.0);
    check(fabs(r.ratio - slow100 / 100.0) <= 0.15 * slow100 / 100.0, w);
#else
    check(0, "the run-time correction (load-steal's vc_half): none");
#endif
}

int main(void)
{
#ifndef VC_R0_SMK
#define VC_R0_SMK 256u
#define VC_R0_FM1 256u
#endif
    report("FM-1, fm1-emu's speed (1 x)", 100, VC_R0_FM1, 1);
    report("SMK-37 build, a chip as fast as fm1-emu (1 x)", 100, VC_R0_SMK, 1);
    report("SMK-37, 2 x slower than fm1-emu", 200, VC_R0_SMK, 1);
    report("SMK-37, 2.5 x slower (a held triad 92 %: the held notes over the budget given up)", 250, VC_R0_SMK, 0);
    report("FM-1 build on a 2 x slower chip", 200, VC_R0_FM1, 1);
    printf("pressure (NVOICE %u): %d failed\n", NVOICE, bad);
    return bad != 0;
}
