/* SPDX-License-Identifier: GPL-3.0-only */
/* The plate's output taps (firmware/src/fx.c PL_TAP_L / PL_TAP_R: PLATE, HALL, SHIMMER):
 *   build/host/plate_taps_test
 * 1. every tap inside its line (a tap past the end reads a short delay, then for a moment the neighbouring line's
 *    memory: the left output jumped between the two each time line AA wrapped, 22050 / 1334 = 16.5 times a second:
 *    the chop players heard on PLATE and HALL, louder with the send, ROOM clean)
 * 2. a steady saw through HALL: neither side's level moves at AA's (or AB's) wrap rate (under 0.5 %)
 * 3. both sides alike: their levels within 3 dB, and decorrelated (|r| < 0.5: the paper's wide image)
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

static int bad;
static void check(const char *what, int ok)
{
    printf("plate_taps: %-90s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
#define NSMP (FS * 6)
static float L[NSMP], R[NSMP];

static double comp(const float *y, double f)     /* the level's component at f Hz, % of its mean, from 2 s */
{
    enum { B = 32 };
    uint32_t a = FS * 2, nb = (NSMP - a) / B, k, i;
    static double env[NSMP / B];
    double mean = 0, re = 0, im = 0, c = 1 - exp(-2 * M_PI * 120.0 / FS), lp = 0;
    for (i = 0; i < NSMP; i++) {
        lp += c * ((double)y[i] * y[i] - lp);
        if (i >= a && (i - a) % B == 0 && (i - a) / B < nb)
            env[(i - a) / B] = sqrt(lp > 0 ? lp : 0);
    }
    for (k = 0; k < nb; k++)
        mean += env[k];
    mean /= nb;
    for (k = 0; k < nb; k++) {
        double t = (double)k * B / FS;
        re += (env[k] - mean) * cos(2 * M_PI * f * t);
        im += (env[k] - mean) * sin(2 * M_PI * f * t);
    }
    return 100 * 2 * sqrt(re * re + im * im) / nb / mean;
}

int main(void)
{
    uint32_t b, i, ph = 0, inc = (uint32_t)(146.83 / FS * 4294967296.0), k, inside = 1;
    int32_t in[CTL], wl[CTL], wr[CTL];
    double fa = 22050.0 / PL_LEN[PL_AA], fb = 22050.0 / PL_LEN[PL_AB], worst = 0, el = 0, er = 0, elr = 0;
    char w[200];
    for (k = 0; k < 7u; k++) {
        if (PL_TAP_L[k].d > PL_LEN[PL_TAP_L[k].k] || PL_TAP_R[k].d > PL_LEN[PL_TAP_R[k].k] || !PL_TAP_L[k].d || !PL_TAP_R[k].d)
            inside = 0, printf("  tap %u: left line %u at %u (of %u), right line %u at %u (of %u)\n", (unsigned)k,
                               (unsigned)PL_TAP_L[k].k, (unsigned)PL_TAP_L[k].d, (unsigned)PL_LEN[PL_TAP_L[k].k],
                               (unsigned)PL_TAP_R[k].k, (unsigned)PL_TAP_R[k].d, (unsigned)PL_LEN[PL_TAP_R[k].k]);
    }
    check("every output tap inside its line (7 a side)", inside);

    host_tracks_init();
    memset(&pl, 0, sizeof pl);
    song.g[G_RTYPE] = RT_HALL, song.g[G_RSIZE] = 90, song.g[G_RDAMP] = 60, song.g[G_RPRE] = 0;
    for (b = 0; b < NSMP / CTL; b++) {
        for (i = 0; i < CTL; i++) {
            in[i] = osc_saw(ph, inc) / 2;
            ph += inc;
            wl[i] = wr[i] = 0;
        }
        plate_run(in, wl, wr, CTL, 32767, 32767);
        for (i = 0; i < CTL; i++)
            L[b * CTL + i] = (float)wl[i], R[b * CTL + i] = (float)wr[i];
    }
    {
        double c[8] = {comp(L, fa), comp(R, fa), comp(L, 2 * fa), comp(R, 2 * fa), comp(L, fb), comp(R, fb),
                       comp(L, 2 * fb), comp(R, 2 * fb)};
        for (k = 0; k < 8u; k++)
            worst = c[k] > worst ? c[k] : worst;
        snprintf(w, sizeof w, "HALL, a steady saw: the level at the lines' wrap rates (%.1f, %.1f Hz and twice): left %.1f, "
                 "right %.1f %% (< 0.5)", fa, fb, c[0] > c[2] ? c[0] : c[2], c[1] > c[3] ? c[1] : c[3]);
        check(w, worst < 0.5);
    }
    for (i = FS * 2u; i < NSMP; i++)
        el += (double)L[i] * L[i], er += (double)R[i] * R[i], elr += (double)L[i] * R[i];
    snprintf(w, sizeof w, "the two sides alike: left %.1f dB against right, correlation %.2f (within 3 dB, |r| < 0.5)",
             10 * log10(el / er), elr / sqrt(el * er));
    check(w, fabs(10 * log10(el / er)) < 3.0 && fabs(elr / sqrt(el * er)) < 0.5);
    printf(bad ? "plate_taps: %d FAILED\n" : "plate_taps: all passed\n", bad);
    return bad;
}
