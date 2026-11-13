/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* (Stallion 1.0) Every factory preset centred: the factory presets' rule 5, left and right within
 * 1 dB. Each preset of each engine as ui.c apply_preset_to loads it (DELAY and REVERB sends 0: the factory sounds dry),
 * a held single note (C4) and a held four-note chord (Cm7), 1.5 s each from the note-on: the rms of the mix's left and
 * right before the master's limiter. A preset in a UNISON mode (UNISON, UNI2, UNI4) draws random start phases at each
 * note-on (one take swings up to ~2 dB either way, the stack symmetric): its left and right are summed over NTAKE
 * note-ons. A preset through a slow stereo chorus (PONY's CHOR or an INSERT PONY chorus type, fx.c pony_chorus: 0.53 / 0.87 /
 * 9.75 Hz; or a CHORUS send, fx_buses: its LFO at CRATE) is summed over NTAKE note-ons too, the chorus LFO started at
 * k / NTAKE of a turn: its two taps move in opposite directions, so a held note's left and right swap over the LFO's
 * cycle, and one 1.5 s window from one fixed LFO phase (shorter than a 0.53 Hz cycle, not a whole number of a 0.87 Hz
 * one, a decaying sound weighted to its start) measured where the cycle stood, not the sound: PONY POLY STAB's note
 * -1.49 dB from its one phase, -1.45 .. +1.23 dB over the 16 phases, -0.06 dB summed over them (and -0.07 dB over
 * 16 whole LFO cycles); STAMPEDE RIBBON LEAD's chord -1.40 .. +1.40 dB by phase (a mirror half a turn on), 0.00 dB
 * summed. Fails a preset whose left and right differ by more than 1 dB in either.
 * And the high band alike (the rms alone does not see a high-pass alone in one ear whose level matches the other's
 * low-pass: the same rms, a fizz or a squeak in one ear). Above 2 kHz (two 2-pole high-passes)
 * left and right within HF_LIMIT_DB, wherever that band is more than HF_FLOOR_DB under the louder side's whole
 * (a dark sound's few highs not counted).
 *   cc -O2 -w -DCLIPPER_SYNTH=1 -Ibuild/gen -Ifirmware/src -Ifirmware/hal tests/balance_test.c -lm -o build/host/balance_test
 *   VERBOSE=1: every preset's figures. Exit status: 1 when a preset is over 1 dB. (After presets work's lr_check.c) */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define LIMIT_DB 1.0
#define HF_HZ 2000.0
#define HF_LIMIT_DB 6.0
#define HF_FLOOR_DB 30.0
/* MARE's SPACING showcase sounds (the left, VCF 1, brighter above 1-2 kHz by up to ~16 dB) may differ
 * above HF_HZ up to SPACING_REF_DB; their overall L/R level is held to LIMIT_DB as every other preset's */
#define SPACING_REF_DB 16.0
/* (and TRAVEL PARA: its SPACE 8 above 6 dB at 2 kHz) */
static const char *const SPACING_REF_OK[] = {"SPACE DRIFT", "HIGH SWITCH", "FIFTH PEDAL", "WIDE PEDAL", "TRAVEL PARA", 0};
static int spacing_ref_ok(uint32_t e, const char *name)
{
    uint32_t i;
    if (e != ENGI_MARE)
        return 0;
    for (i = 0; SPACING_REF_OK[i]; i++)
        if (!strcmp(SPACING_REF_OK[i], name))
            return 1;
    return 0;
}
#define NTAKE 16u

typedef struct { double l, r, hl, hr; } meas_t;

/* a 2-pole Butterworth high-pass (RBJ), two in a row: 24 dB an octave under HF_HZ */
typedef struct { double b0, b1, b2, a1, a2, z1, z2; } bq_t;
static void bq_hp(bq_t *q, double f)
{
    double w = 2 * M_PI * f / FS, c = cos(w), al = sin(w) / (2 * 0.70710678), a0 = 1 + al;
    q->b0 = q->b2 = (1 + c) / 2 / a0, q->b1 = -(1 + c) / a0, q->a1 = -2 * c / a0, q->a2 = (1 - al) / a0;
    q->z1 = q->z2 = 0;
}
static double bq(bq_t *q, double x)
{
    double y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2, q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

/* one take: the energies of the left and the right added to sl, sr */
static void take(uint32_t e, uint32_t pi, const uint8_t *notes, uint32_t nn, uint32_t k, uint32_t lfo, double *sl,
                 double *sr, double *hl, double *hr)
{
    int32_t o[2 * CTL];
    uint32_t b, i, nb = (FS * 3u / 2u) / CTL, tail = (FS / 2u) / CTL;
    bq_t q[4];
    for (i = 0; i < 4u; i++)
        bq_hp(&q[i], HF_HZ);
    host_tracks_init();
    song.g[G_BPM] = 100;
    memset(trk[0].v, 0, sizeof trk[0].v);            /* (the same start whatever played before: the voices, the */
    memset(jc, 0, sizeof jc);                        /* PONY chorus's delay and LFO, the random generators) */
    rng_state = 0x1234567u + k * 0x9E3779B9u;
    uni_seed = 0x1234567u + k * 2654435761u, puni_seed = 0x2468ACEu + k * 40503u;
    host_preset(&trk[0], e, pi);
    if (e != ENGI_MARE)                             /* as ui.c apply_preset_to: the factory sounds dry (MARE's: */
        trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;       /* the sends its presets name, a design exception) */
    trk[0].p[P_AMODE] = 0;
    song.sel = 0;
    for (b = 0; b < FS / CTL; b++)                   /* the last one's tails die out */
        mix_block(o, CTL);
    if (lfo)                                         /* a chorus: its LFO from k / lfo of a turn (part 0: jc[0]) */
        jc[0].ph = fx.cho_ph = (uint32_t)(((uint64_t)k << 32) / lfo);
    for (i = 0; i < nn; i++)
        input_on(&trk[0], notes[i], 100);
    for (b = 0; b < nb + tail; b++) {
        if (b == nb)
            for (i = 0; i < nn; i++)
                input_off(&trk[0], notes[i]);
        mix_block(o, CTL);
        if (b < nb)
            for (i = 0; i < CTL; i++) {
                *sl += (double)lim_dac.dl[i] * lim_dac.dl[i];
                *sr += (double)lim_dac.dr[i] * lim_dac.dr[i];
                double yl = bq(&q[1], bq(&q[0], lim_dac.dl[i])), yr = bq(&q[3], bq(&q[2], lim_dac.dr[i]));
                *hl += yl * yl, *hr += yr * yr;
            }
    }
}

static void run(uint32_t e, uint32_t pi, const uint8_t *notes, uint32_t nn, uint32_t takes, uint32_t lfo, meas_t *ms)
{
    uint32_t k, n = ((FS * 3u / 2u) / CTL) * CTL * takes;
    double sl = 0, sr = 0, hl = 0, hr = 0;
    for (k = 0; k < takes; k++)
        take(e, pi, notes, nn, k, lfo, &sl, &sr, &hl, &hr);
    ms->l = 10 * log10(sl / n / (32768.0 * 32768.0) + 1e-20);
    ms->r = 10 * log10(sr / n / (32768.0 * 32768.0) + 1e-20);
    ms->hl = 10 * log10(hl / n / (32768.0 * 32768.0) + 1e-20);
    ms->hr = 10 * log10(hr / n / (32768.0 * 32768.0) + 1e-20);
}

/* the high band's left - right, 0 where it is more than HF_FLOOR_DB under the louder side's whole */
static double hf_diff(const meas_t *m)
{
    double top = m->l > m->r ? m->l : m->r;
    if ((m->hl > m->hr ? m->hl : m->hr) < top - HF_FLOOR_DB)
        return 0;
    return m->hl - m->hr;
}

/* the preset loads in a UNISON mode (its notes' random start phases) */
static int unison(uint32_t e, uint32_t pi)
{
    host_tracks_init();
    host_preset(&trk[0], e, pi);
    return trk[0].p[P_VOICE] >= V_UNISON;
}

/* the preset loads through a slow stereo chorus: PONY's CHOR or an INSERT PONY chorus type, or a CHORUS send */
static int chorus(uint32_t e, uint32_t pi)
{
    int32_t a, b, c, d;
    host_tracks_init();
    host_preset(&trk[0], e, pi);
    return pch_mode(&trk[0], &a, &b, &c, &d) != 0 || trk[0].p[P_CHOR] > 0;
}

/* (Stallion 1.0) one preset's figures, measured in a child forked from main before any sound: every preset from the state
 * the firmware boots in. In one process the part's free LFOs (1, 2 and 3: phases and S&H values), the MOD SEQ, the
 * effects, the master and the engines' own state went on from the preset before: 353 presets moved by more than 0.1 dB
 * when the order was reversed (PHYS OCTAVE PLUCK 12 dB, STAMPEDE GLIDE LEAD's chord 0.8 -> 1.1 dB apart: a failure) */
typedef struct { meas_t a, c; uint32_t takes, lfo, uni; } res_t;
typedef struct { pid_t pid; int fd; } child_t;
static child_t spawn(uint32_t e, uint32_t pi)
{
    static const uint8_t CH[4] = {60, 63, 67, 70}, N1[1] = {60};
    int fd[2];
    child_t c = {-1, -1};
    fflush(stdout);
    if (pipe(fd))
        return c;
    if (!(c.pid = fork())) {
        res_t r;
        close(fd[0]);
        r.lfo = chorus(e, pi) ? NTAKE : 0u;
        r.uni = (uint32_t)unison(e, pi);
        r.takes = r.uni || r.lfo ? NTAKE : 1u;
        run(e, pi, N1, 1, r.takes, r.lfo, &r.a);
        run(e, pi, CH, 4, r.takes, r.lfo, &r.c);
        _exit(write(fd[1], &r, sizeof r) == (ssize_t)sizeof r ? 0 : 2);
    }
    close(fd[1]);
    c.fd = fd[0];
    if (c.pid < 0)
        close(fd[0]), c.fd = -1;
    return c;
}
static int reap(child_t c, res_t *r)
{
    int st = 0;
    size_t got = 0;
    while (c.fd >= 0 && got < sizeof *r) {
        ssize_t k = read(c.fd, (char *)r + got, sizeof *r - got);
        if (k <= 0)
            break;
        got += (size_t)k;
    }
    if (c.fd >= 0)
        close(c.fd);
    if (c.pid > 0)
        waitpid(c.pid, &st, 0);
    return c.pid > 0 && got == sizeof *r && WIFEXITED(st) && !WEXITSTATUS(st) ? 0 : -1;
}

int main(void)
{
    uint32_t e, pi, n = 0, bad = 0, silent = 0, nuni = 0, ncho = 0, hbad = 0;
    child_t ch[256];                                 /* (npresets: a uint8_t) */
    double worst = 0, hworst = 0;
    const char *wn = "", *we = "", *hwn = "", *hwe = "";
    int verbose = getenv("VERBOSE") != 0;
    for (e = 0; e < NENGINES; e++) {
        for (pi = 0; pi < ENGINES[e]->npresets; pi++)    /* an engine's presets at once */
            ch[pi] = spawn(e, pi);
        for (pi = 0; pi < ENGINES[e]->npresets; pi++, n++) {
            const char *name = ENGINES[e]->presets[pi].name;
            res_t r;
            uint32_t takes;
            meas_t a, c;
            double d1, d4, d, h1, h4, h;
            int quiet, hfail;
            if (reap(ch[pi], &r)) {
                printf("balance: %-8s %2u %-13s its measure failed  FAIL\n", ENGINES[e]->name, (unsigned)pi, name);
                bad++;
                continue;
            }
            takes = r.takes, a = r.a, c = r.c;
            nuni += r.uni, ncho += r.lfo > 0u;
            d1 = fabs(a.l - a.r), d4 = fabs(c.l - c.r), d = d1 > d4 ? d1 : d4;
            h1 = fabs(hf_diff(&a)), h4 = fabs(hf_diff(&c)), h = h1 > h4 ? h1 : h4;
            quiet = a.l < -90 && a.r < -90 && c.l < -90 && c.r < -90;
            hfail = h > (spacing_ref_ok(e, name) ? SPACING_REF_DB : HF_LIMIT_DB);
            silent += quiet;
            hbad += hfail;
            if (d > worst)
                worst = d, wn = name, we = ENGINES[e]->name;
            if (h > hworst)
                hworst = h, hwn = name, hwe = ENGINES[e]->name;
            if (verbose || d > LIMIT_DB || quiet || hfail)
                printf("balance: %-8s %2u %-13s note L %6.1f R %6.1f (%+5.2f) | chord L %6.1f R %6.1f (%+5.2f) dB | "
                       ">2k note %+5.1f chord %+5.1f%s%s\n",
                       ENGINES[e]->name, (unsigned)pi, name, a.l, a.r, a.l - a.r, c.l, c.r, c.l - c.r,
                       hf_diff(&a), hf_diff(&c), r.lfo ? " (16 LFO phases)" : takes > 1u ? " (16 takes)" : "",
                       quiet ? "  SILENT" : d > LIMIT_DB ? "  FAIL" : hfail ? "  FAIL (high band)" : "");
            bad += d > LIMIT_DB || quiet || hfail;
        }
    }
    printf("balance: %u factory presets (%u in a UNISON mode, %u through a chorus: %u takes), a held note and a held chord: L and R within "
           "%.1f dB but %u (%u silent, %u by the band above %.0f Hz, within %.0f dB); the widest %.2f dB (%s %s), above "
           "%.0f Hz %.1f dB (%s %s)\n", (unsigned)n, (unsigned)nuni, (unsigned)ncho, NTAKE, LIMIT_DB, (unsigned)bad, (unsigned)silent,
           (unsigned)hbad, HF_HZ, HF_LIMIT_DB, worst, we, wn, HF_HZ, hworst, hwe, hwn);
    return bad != 0;
}
