/* SPDX-License-Identifier: GPL-3.0-only */
/* The plate's tank at rest (firmware/src/fx.c plate_run: PLATE, HALL, SHIMMER):
 *   build/host/plate_idle_test
 * 1. nothing sent (a sound on REV 0, the type PLATE / HALL): the tank rests after half a second, silent
 * 2. a tail rings out in full: HALL, a saw sent for a second, then silence: still audible after 2 s, at rest only
 *    once its output stayed at the last bits (PL_QUIET_LVL, -84 dBFS) for half a second, or (rounding's limit cycles
 *    on the longest HALL) under PL_LONG_LVL after 20 s without a send; a quiet tail (a note 40 dB down) not cut short
 * 3. sent to again: the wet back within 200 ms (the pre-delay and the lines), as loud as from a cleared tank
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

static int bad;
static void check(const char *what, int ok)
{
    printf("plate_idle: %-90s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint32_t ph;
static int32_t div_in = 2, never_rest;                  /* the saw's divisor; 1: the tank never at rest (the reference) */
static int32_t tail[2][FS * 23 / CTL * CTL];
/* ms of the plate with the saw sent (or not); the wet's peak, *first: the ms of the first wet sample (or -1); rec:
 * the left wet kept */
static int32_t run_rec(uint32_t ms, int send, int32_t *first, int32_t *rec)
{
    uint32_t b, i, nb = ms * (FS / 1000u) / CTL, inc = (uint32_t)(146.83 / FS * 4294967296.0);
    int32_t in[CTL], wl[CTL], wr[CTL], pk = 0;
    if (first)
        *first = -1;
    for (b = 0; b < nb; b++) {
        for (i = 0; i < CTL; i++) {
            in[i] = send ? osc_saw(ph, inc) / div_in : 0;
            ph += inc;
            wl[i] = wr[i] = 0;
        }
        if (never_rest)
            pl_quiet = 0;
        plate_run(in, wl, wr, CTL, 32767, 32767);
        for (i = 0; i < CTL; i++) {
            if (rec)
                rec[b * CTL + i] = wl[i];
            int32_t a = abs(wl[i]) > abs(wr[i]) ? abs(wl[i]) : abs(wr[i]);
            pk = a > pk ? a : pk;
            if (a && first && *first < 0)
                *first = (int32_t)((b * CTL + i) * 1000u / FS);
        }
    }
    return pk;
}
static int32_t run(uint32_t ms, int send, int32_t *first)
{
    return run_rec(ms, send, first, 0);
}

int main(void)
{
    char w[200];
    int32_t pk, p2, first, ms;
    host_tracks_init();
    memset(&pl, 0, sizeof pl);
    song.g[G_RTYPE] = RT_HALL, song.g[G_RSIZE] = 127, song.g[G_RDAMP] = 0, song.g[G_RPRE] = 100;   /* the longest */

    pk = run(1000, 0, 0);
    snprintf(w, sizeof w, "nothing sent: at rest after 1 s (quiet for %u blocks of %u), silent (peak %d)",
             (unsigned)pl_quiet, (unsigned)PL_QUIET, (int)pk);
    check(w, pl_quiet >= PL_QUIET && pk == 0);

    pk = run(1000, 1, 0);
    p2 = run(2000, 0, 0);
    snprintf(w, sizeof w, "HALL at its longest, 1 s sent: the tail 2 s on still rings (peak %d of %d), not at rest", (int)p2,
             (int)pk);
    check(w, p2 > 1000 && pl_quiet < PL_QUIET);
    for (ms = 2000; ms < 60000 && pl_quiet < PL_QUIET; ms += 100)
        p2 = run(100, 0, 0);
    snprintf(w, sizeof w, "then at rest %.1f s after the send stopped, its last 100 ms at the last bits (peak %d, <= %d)",
             ms / 1000.0, (int)p2, PL_LONG_LVL);
    check(w, pl_quiet >= PL_QUIET && p2 <= PL_LONG_LVL && ms < 30000);

    p2 = run(1000, 1, &first);
    snprintf(w, sizeof w, "sent to again: the wet back after %d ms (< 200), as loud (peak %d against %d from a cleared tank)",
             (int)first, (int)p2, (int)pk);
    check(w, first >= 0 && first < 200 && p2 > pk * 3 / 4 && p2 < pk * 4 / 3);

    song.g[G_RSIZE] = 90, song.g[G_RDAMP] = 30, song.g[G_RPRE] = 10;   /* a quiet note (40 dB down), a mid HALL */
    div_in = 200;
    {
        uint32_t k, n = FS * 22 / CTL * CTL, rest_at = 0;
        int32_t worst = 0, top = 0;
        for (k = 0; k < 2u; k++) {
            never_rest = (int32_t)k;
            memset(&pl, 0, sizeof pl), pl_quiet = pl_silent = 0, ph = 0;
            run_rec(1000, 1, 0, tail[k]);
            run_rec(21000, 0, 0, tail[k] + FS / CTL * CTL);
            if (!k)
                rest_at = pl_quiet >= PL_QUIET;
        }
        for (k = 0; k < n; k++) {
            int32_t d = abs(tail[0][k] - tail[1][k]);
            worst = d > worst ? d : worst, top = abs(tail[1][k]) > top ? abs(tail[1][k]) : top;
        }
        snprintf(w, sizeof w, "a quiet note's tail (peak %d): at rest by 22 s, against never at rest at most %d apart (<= %d)",
                 (int)top, (int)worst, PL_QUIET_LVL + 1);
        check(w, rest_at && worst <= PL_QUIET_LVL + 1);
    }
    printf(bad ? "plate_idle: %d FAILED\n" : "plate_idle: all passed\n", bad);
    return bad;
}
