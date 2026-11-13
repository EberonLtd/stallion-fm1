/* SPDX-License-Identifier: GPL-3.0-only */
/* The plate's tank (firmware/src/fx.c plate_run: PLATE, HALL, SHIMMER): its moving diffusers, gentle enough
 *   build/host/plate_test
 * A steady saw (ANALOG, no LFO, no detune) through the whole mix with the reverb sent, the wet alone (with REV less
 * without), its level envelope (squared, a 4-pole low-pass at 40 Hz: no block-rate artefact), from 3 s on:
 * 1. ROOM steady (the reference: its wobble 2..35 Hz under 2 %)
 * 2. PLATE and HALL steady too, at C3 and C4: under 6 % (the diffusers moving +-12 samples at 1 Hz shifted the pitch
 *    of the recirculating tail enough to beat: 7 to 29 %, worst on HALL's long tail, the "helicopter" players heard)
 * 3. bounded, and still a reverb: HALL's wet at least -30 dBFS-ish loud (not silenced)
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

static int bad;
static void check(const char *what, int ok)
{
    printf("plate: %-94s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
#define NSMP (FS * 8)
static float dry[NSMP], with[NSMP];

static void render(int rt, int rev, int note, float *dst)
{
    static int32_t o[CTL * 2];
    uint32_t b, i;
    host_tracks_init();
    memset(&pl, 0, sizeof pl);
    host_preset(&trk[0], 0, 0);                                        /* ANALOG, a plain saw */
    trk[0].p[P_E0] = 0, trk[0].p[P_E2] = 0, trk[0].p[P_E3] = 0;
    trk[0].p[P_LD_PIT] = trk[0].p[P_LD_FLT] = trk[0].p[P_LD_SHP] = trk[0].p[P_LD_AMP] = 0;
    trk[0].p[P_VINT] = 0, trk[0].p[P_SPREAD] = 0, trk[0].p[P_ATK] = 0, trk[0].p[P_SUS] = 127, trk[0].p[P_REL] = 60;
    trk[0].p[P_REV] = (int16_t)rev, trk[0].p[P_DLY] = 0, trk[0].p[P_CHOR] = 0, trk[0].p[P_DIST] = 0;
    song.g[G_RTYPE] = (int16_t)rt, song.g[G_RSIZE] = 90, song.g[G_RDAMP] = 60, song.g[G_RPRE] = 0;
    for (b = 0; b < 200; b++)
        mix_block(o, CTL);
    trk_note_on(&trk[0], (uint32_t)note, 100);
    for (b = 0; b < NSMP / CTL; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            dst[b * CTL + i] = (float)(o[2 * i] + o[2 * i + 1]);
    }
}
/* the wet's level wobble 2..35 Hz (of its mean); *rms its level, *pk its peak */
static double wet_wobble(int rt, int note, double *rms, double *pk)
{
    static double env[NSMP / 100];
    double c = 1 - exp(-2 * M_PI * 40.0 / FS), l1 = 0, l2 = 0, l3 = 0, l4 = 0, mean = 0, band = 0, e = 0;
    uint32_t i, k, f, a = FS * 3 / 100, nb = NSMP / 100 - a;
    render(rt, 0, note, dry);
    render(rt, 120, note, with);
    *pk = 0;
    for (i = 0; i < NSMP; i++) {
        double x = (double)with[i] - dry[i];
        l1 += c * (x * x - l1), l2 += c * (l1 - l2), l3 += c * (l2 - l3), l4 += c * (l3 - l4);
        if (i % 100u == 0)
            env[i / 100u] = sqrt(l4 > 0 ? l4 : 0);
        if (i >= FS * 3u)
            e += x * x, *pk = fabs(x) > *pk ? fabs(x) : *pk;
    }
    *rms = sqrt(e / (NSMP - FS * 3u));
    for (k = 0; k < nb; k++)
        mean += env[a + k];
    mean /= nb;
    for (f = 1; f < nb / 2; f++) {
        double re = 0, im = 0, hz = f * 441.0 / nb, m;
        if (hz < 2 || hz > 35)
            continue;
        for (k = 0; k < nb; k++) {
            re += (env[a + k] - mean) * cos(2 * M_PI * f * k / nb);
            im += (env[a + k] - mean) * sin(2 * M_PI * f * k / nb);
        }
        m = 2 * sqrt(re * re + im * im) / nb;
        band += m * m;
    }
    return sqrt(band) / mean;
}

int main(void)
{
    static const char *const RTN[3] = {"ROOM", "PLATE", "HALL"};
    static const int NOTE[2] = {48, 60};
    char w[200];
    double rms, pk, wb, worst = 0, hall_rms = 0, maxpk = 0;
    int rt, n, wrt = 0, wn = 0;
    wb = wet_wobble(0, 60, &rms, &pk);
    snprintf(w, sizeof w, "ROOM, a saw at C4: the wet's level wobble 2..35 Hz %.2f %% (the reference; < 2)", 100 * wb);
    check(w, wb < 0.02);
    for (rt = 1; rt <= 2; rt++)
        for (n = 0; n < 2; n++) {
            wb = wet_wobble(rt, NOTE[n], &rms, &pk);
            maxpk = pk > maxpk ? pk : maxpk;
            if (rt == 2 && n == 1)
                hall_rms = rms;
            if (wb > worst)
                worst = wb, wrt = rt, wn = NOTE[n];
        }
    snprintf(w, sizeof w, "PLATE and HALL, a saw at C3 and C4: the worst wobble 2..35 Hz %.2f %% (%s, note %d; < 6)",
             100 * worst, RTN[wrt], wn);
    check(w, worst < 0.06);
    snprintf(w, sizeof w, "still a reverb: HALL's wet at C4 %.0f rms (> 300), the peak %.0f (bounded)", hall_rms, maxpk);
    check(w, hall_rms > 300.0 && maxpk < 400000.0);
    printf(bad ? "plate: %d FAILED\n" : "plate: all passed\n", bad);
    return bad;
}
