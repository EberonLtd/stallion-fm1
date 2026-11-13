/* SPDX-License-Identifier: GPL-3.0-only */
/* VOICE = UNISON (and UNI2 / UNI4) about as loud as POLY, on every engine. Unison starts its extra voices at random
 * phases (voice.c unison_spread) and at a lower level (the velocity: about the same sum). In phase they stack: 8
 * voices at 0.36 are +9 dB over one. unison_spread moved ph[0] only, and most engines keep more oscillators (ph[1],
 * ph[2], operators, a wave sequence's step, a sample's read position): their voices stacked in phase (ANALOG SINE PAD
 * +13.7 dB, PHASE CZ CHOIR +12.8, FMX +11 .. +15, MACRO CHORD / SWARM +13, SAMPLE +7.6 on average). Each engine now
 * spreads all of its oscillators by the same time shift (their relative phases, the timbre, as they were); SAMPLE
 * starts a voice up to four periods later (silence first; at most 256 source samples). FM6 scales UNI4 / UNI2 as
 * UNISON (its operators' levels ignore the velocity when their KVS is 0: UNI4 was +6 dB at random phases, +12 in phase).
 * For every preset of every engine, notes 36 and 60 (velocity 100, chorus, delay, reverb off): the rms over
 * 0.05 .. 0.55 s in UNISON (8 voices, the preset's DETUNE) and in UNI4 (Clipper) against POLY. Fails when a render is
 * more than +8 dB over POLY or an engine's mean over +5 dB. Exit status: failed checks.
 * (Stallion) Every preset's renders from the same state whatever ran before: each preset in a child process forked from
 * main's power-on state, the random generators (libc.c rng, voice.c's note-on ones) seeded from the engine and the
 * preset (and the draw). Before, one shared generator ran on through the engines in order: a preset added to one engine
 * moved which preset of a later one drew the unlucky phases. A unison render is the power mean of ND draws (their own
 * random phases): one draw of 8 phases frozen over the window (a low bass note, its detune beating slower than 0.5 s)
 * spreads -11 .. +6 dB around its mean, a few of the ~3400 renders over +8 dB by chance whichever the seed; in phase
 * (a level fault) every draw stacks. The engines' presets run in parallel children. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#if CLIPPER_SYNTH
#define TAPL mix_l
#define TAPR mix_r
#else
#define TAPL acc[0].l
#define TAPR acc[0].r
#endif
static int32_t o[CTL * 2];
static int no_vint;                                  /* (palomino_draws: VINTAGE 0, its drift moves the level by design) */
static uint32_t draw;                                /* (palomino_draws: the draw, its own random phases) */
static void seed_for(uint32_t e, uint32_t p)
{
    uint32_t h = ((e * 64u + p) * 2654435761u) ^ (draw * 40503u + 0x9E3779B9u);
    rng_state = h | 1u;
    uni_ph_seed = h * 3u + 1u, note_seed = h * 5u + 3u, uni_seed = h * 7u + 5u, puni_seed = h * 11u + 7u;
    pend_seed = h * 13u + 9u;
}
/* fn(e, p, r) in a child forked from main's state (spawn), its n results back in r (reap: 0 when it ran) */
#define NR 16u                                       /* results a child sends, at most */
typedef struct { pid_t pid; int fd; } child_t;
static child_t spawn(void (*fn)(uint32_t, uint32_t, double *), uint32_t e, uint32_t p)
{
    int fd[2];
    child_t c = {-1, -1};
    fflush(stdout);
    if (pipe(fd))
        return c;
    if (!(c.pid = fork())) {
        double r[NR] = {0};
        close(fd[0]);
        fn(e, p, r);
        if (write(fd[1], r, sizeof r) != (ssize_t)sizeof r)
            _exit(2);
        _exit(0);
    }
    close(fd[1]);
    c.fd = fd[0];
    if (c.pid < 0)
        close(fd[0]), c.fd = -1;
    return c;
}
static int reap(child_t c, double *r)
{
    int st = 0;
    size_t got = 0;
    double b[NR];
    while (c.fd >= 0 && got < sizeof b) {
        ssize_t k = read(c.fd, (char *)b + got, sizeof b - got);
        if (k <= 0)
            break;
        got += (size_t)k;
    }
    if (c.fd >= 0)
        close(c.fd);
    if (c.pid > 0)
        waitpid(c.pid, &st, 0);
    if (c.pid <= 0 || got != sizeof b || !WIFEXITED(st) || WEXITSTATUS(st))
        return -1;
    memcpy(r, b, sizeof b);
    return 0;
}
static double play(uint32_t e, uint32_t p, uint32_t mode, uint32_t note)
{
    uint32_t i, b, k, any = 1;
    double s2 = 0;
    for (k = 0; k < NPART; k++)
        trk_all_off(&trk[k]);
    for (b = 0; b < 6000u && any; b++) {             /* the last note rung out */
        mix_block(o, CTL);
        for (any = 0, i = 0; i < NVOICE; i++)
            any |= trk[0].v[i].active;
    }
    for (i = 0; i < P_E0; i++)                       /* the preset as ui.c apply_preset_to loads it */
        trk[0].p[i] = TP[i].def;
    host_preset(&trk[0], e, p);
    trk[0].p[P_AMODE] = 0, trk[0].p[P_DLY] = trk[0].p[P_REV] = 0, trk[0].p[P_CHOR] = 0;
    trk[0].p[P_VOICE] = (int16_t)mode;
#if CLIPPER_SYNTH
    if (no_vint)
        trk[0].p[P_VINT] = 0;
#endif
    for (b = 0; b < 16; b++)
        mix_block(o, CTL);
#if CLIPPER_SYNTH
    trk[0].l2ph = trk[0].l3ph = 0;                   /* (the part's free LFOs, LFO 2 and LFO 3 FREE, from the same place
                                                      * for every render: they run on through the rings-out, so POLY and
                                                      * each unison draw met them at another moment; MARE SPACE DRIFT's
                                                      * LFO 3 > CUT moves a note 17 dB over its cycle: UNISON +7.7 .. +8.9
                                                      * dB "over POLY", +3.7 at one place) */
    trk[0].l2rnd = trk[0].l3rnd = 0, trk[0].l2cx = trk[0].l3cx = 0;
#endif
    seed_for(e, p);
    trk_note_on(&trk[0], note, 100);
    for (b = 0; b < 760; b++) {
        mix_block(o, CTL);
        if (b >= 69)
            for (i = 0; i < CTL; i++) {
                double x = (TAPL[i] + TAPR[i]) / 2.0;
                s2 += x * x;
            }
    }
    trk_note_off(&trk[0], note);
    return sqrt(s2 / ((760 - 69) * CTL));
}
#if CLIPPER_SYNTH
#define NM 2u
static const uint32_t MODE[NM] = { V_UNISON, V_PUNI4 };
static const char *const MN[NM] = { "UNISON", "UNI4" };
#else
#define NM 1u
static const uint32_t MODE[NM] = { V_UNISON };
static const char *const MN[NM] = { "UNISON" };
#endif
/* (in a child) preset p of engine e: each unison mode over POLY (dB) at notes 36 and 60, r[m * 2 + k]: the power mean
 * of ND draws */
#define ND 4u
static void preset_ds(uint32_t e, uint32_t p, double *r)
{
    uint32_t k, m;
    for (k = 0; k < 2u; k++) {
        uint32_t note = k ? 60u : 36u;
        double r0 = play(e, p, V_POLY, note);
        for (m = 0; m < NM; m++) {
            double s2 = 0;
            for (draw = 1; draw <= ND; draw++) {
                double x = play(e, p, MODE[m], note);
                s2 += x * x;
            }
            draw = 0;
            r[m * 2u + k] = 20 * log10((sqrt(s2 / ND) + 1) / (r0 + 1));
        }
    }
}
/* engine e: in each unison mode, the mean unison / POLY ratio (dB) of its presets at notes 36 and 60 */
static void engine(uint32_t e, int *fails)
{
    const engine_t *en = ENGINES[e];
    uint32_t p, k, m, n = 0;
    double sum[NM][2], worst[NM], mean, ds[NR];
    const char *wn[NM];
    static child_t ch[64];
    for (m = 0; m < NM; m++)
        sum[m][0] = sum[m][1] = 0, worst[m] = -99, wn[m] = "";
    for (p = 0; p < en->npresets && p < 64u; p++)
        ch[p] = spawn(preset_ds, e, p);
    for (p = 0; p < en->npresets && p < 64u; p++, n++) {
        if (reap(ch[p], ds)) {
            printf("unison: %-7s %-14s: its render did not finish  FAIL\n", en->name, en->presets[p].name);
            ++*fails;
            continue;
        }
        for (k = 0; k < 2u; k++) {
            uint32_t note = k ? 60u : 36u;
            for (m = 0; m < NM; m++) {
                double d = ds[m * 2u + k];
                if (d > 8.0) {
                    printf("unison: %-7s %-6s %-14s note %u: %+.1f dB over POLY (%u draws)  FAIL\n", en->name, MN[m],
                           en->presets[p].name, note, d, ND);
                    ++*fails;
                }
                if (d > worst[m])
                    worst[m] = d, wn[m] = en->presets[p].name;
                sum[m][k] += d;
            }
        }
    }
    for (m = 0; n && m < NM; m++) {
        mean = (sum[m][0] + sum[m][1]) / (2 * n);
        printf("unison: %-7s %-6s mean %+5.1f dB over POLY (note 36 %+5.1f, 60 %+5.1f; %3u presets; the most %+5.1f, %s)%s\n",
               en->name, MN[m], mean, sum[m][0] / n, sum[m][1] / n, n, worst[m], wn[m], mean > 5.0 ? "  FAIL" : "");
        if (mean > 5.0)
            ++*fails;
    }
}
#if CLIPPER_SYNTH
/* (Stallion) PALOMINO's stacks the same level at every note: voice.c uni_phases gave each voice's VCO-2 a random phase
 * (and VCO-1 ran on from the voice's last note), the same UNISON note -15 .. +7 dB over POLY at random; palomino_spread
 * starts each voice at a fixed offset from the lead's VCO-1, VCO-2 with it. Each preset's note 60 played JD times in a
 * row, VINTAGE 0 (the draws move on): its spread (the loudest - the quietest, dB), the mean over the presets. What is left is the
 * lead's own VCO-1 to VCO-2 relation at an interval (VCO-1 runs free: a POLY note varies so too, up to ~3 dB) */
#define JD 8u
#define JD_SPREAD 2.5                                /* dB, the mean over the presets (random VCO-2 phases: ~7.5) */
static uint32_t jd_mode;
static void preset_draws(uint32_t e, uint32_t p, double *r)   /* (in a child) the JD draws in a row, VCO-1 running on */
{
    for (draw = 1; draw <= JD; draw++)
        r[draw - 1u] = 20 * log10(play(e, p, MODE[jd_mode], 60u) + 1);
    draw = 0;
}
static void palomino_draws(int *fails)
{
    const engine_t *en = ENGINES[ENGI_PALOMINO];
    uint32_t p, m, d;
    no_vint = 1;
    for (m = 0; m < NM; m++) {
        double sum = 0, worst = 0;
        const char *wn = "";
        static child_t ch[64];
        jd_mode = m;
        for (p = 0; p < en->npresets && p < 64u; p++)
            ch[p] = spawn(preset_draws, ENGI_PALOMINO, p);
        for (p = 0; p < en->npresets && p < 64u; p++) {
            double lo = 1e9, hi = -1e9, ls[NR];
            if (reap(ch[p], ls)) {
                printf("unison: PALOMINO %-14s: its draws did not finish  FAIL\n", en->presets[p].name);
                ++*fails;
                continue;
            }
            for (d = 0; d < JD; d++) {
                double l = ls[d];
                lo = l < lo ? l : lo, hi = l > hi ? l : hi;
            }
            sum += hi - lo;
            if (hi - lo > worst)
                worst = hi - lo, wn = en->presets[p].name;
        }
        printf("unison: PALOMINO %-6s the same note %u times: %4.1f dB apart on average (the most %4.1f, %s; limit %.1f)%s\n",
               MN[m], JD, sum / en->npresets, worst, wn, JD_SPREAD, sum / en->npresets > JD_SPREAD ? "  FAIL" : "");
        if (sum / en->npresets > JD_SPREAD)
            ++*fails;
    }
    no_vint = 0;
}
#endif
int main(void)
{
    int fails = 0;
    uint32_t e;
    host_tracks_init();
    for (e = 0; e < NENGINES; e++)
        engine(e, &fails);
#if CLIPPER_SYNTH
    palomino_draws(&fails);
#endif
    printf("unison: %d failed\n", fails);
    return fails;
}
