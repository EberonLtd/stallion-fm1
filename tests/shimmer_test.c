/* SPDX-License-Identifier: GPL-3.0-only */
/* The SHIMMER reverb's octave-up pitch shifter (firmware/src/fx.c shimmer_step, at the reverb's 22.05 kHz):
 *   build/host/shimmer_test
 * 1. no flutter: a diffuse input (white noise, as a reverb tail is: its two taps half a window apart are unrelated)
 *    comes out at a steady power: the power's wobble at the taps' crossing rate (22050 / (PS_WIN / 2), ~49 Hz) under
 *    1 dB (equal-power fades; triangles dip 3 dB at each crossing: the "helicopter" players heard)
 * 2. still an octave up: a 500 Hz sine comes out at 1 kHz
 * 3. bounded: full-scale noise stays in range
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

static int bad;
static void check(const char *what, int ok)
{
    printf("shimmer: %-92s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
#define RS 22050
#define NS (RS * 4)
static int32_t yb[NS];

static void ps_reset(void)
{
    memset(ps_buf, 0, sizeof ps_buf);
    memset(&ps, 0, sizeof ps);
}

/* the wobble (dB, peak to trough of the component) of the power of yb[a .. a + n) at f Hz: the power in 1 ms blocks,
 * its DFT at f against its mean */
static double power_wobble(uint32_t a, uint32_t n, double f)
{
    enum { B = 22 };
    uint32_t nb = n / B, i, k;
    double mean = 0, re = 0, im = 0, amp;
    static double pw[NS / B];
    for (k = 0; k < nb; k++) {
        double e = 0;
        for (i = 0; i < B; i++)
            e += (double)yb[a + k * B + i] * yb[a + k * B + i];
        pw[k] = e / B;
        mean += pw[k];
    }
    mean /= nb;
    for (k = 0; k < nb; k++) {
        double t = (double)k * B / RS;
        re += (pw[k] - mean) * cos(2 * M_PI * f * t);
        im += (pw[k] - mean) * sin(2 * M_PI * f * t);
    }
    amp = 2.0 * sqrt(re * re + im * im) / nb;          /* the component's amplitude, power units */
    return 10.0 * log10((mean + amp) / (mean - amp > 1e-9 ? mean - amp : 1e-9));
}

int main(void)
{
    uint32_t i, r = 12345;
    double f_cross = (double)RS / (PS_WIN / 2u), w;
    char msg[200];
    host_tracks_init();

    ps_reset();                                        /* 1. white noise in */
    for (i = 0; i < NS; i++) {
        int32_t x;
        r = r * 1664525u + 1013904223u;
        x = ((int32_t)(r >> 16) - 32768) / 4;
        yb[i] = shimmer_step(x);
    }
    w = power_wobble(RS, NS - RS, f_cross);
    snprintf(msg, sizeof msg, "noise in: the power's wobble at the taps' crossing rate (%.1f Hz) %.2f dB (< 1)", f_cross, w);
    check(msg, w < 1.0);

    ps_reset();                                        /* 2. 500 Hz in: 1 kHz out */
    for (i = 0; i < NS; i++)
        yb[i] = shimmer_step((int32_t)lrint(8000 * sin(2 * M_PI * 500.0 * i / RS)));
    {
        double m5 = 0, m10 = 0, re, im;
        uint32_t n = RS, a = RS;
        for (re = im = 0, i = 0; i < n; i++)
            re += yb[a + i] * cos(2 * M_PI * 1000.0 * i / RS), im += yb[a + i] * sin(2 * M_PI * 1000.0 * i / RS);
        m10 = sqrt(re * re + im * im);
        for (re = im = 0, i = 0; i < n; i++)
            re += yb[a + i] * cos(2 * M_PI * 500.0 * i / RS), im += yb[a + i] * sin(2 * M_PI * 500.0 * i / RS);
        m5 = sqrt(re * re + im * im);
        snprintf(msg, sizeof msg, "500 Hz in: an octave up (1 kHz %.0f against 500 Hz %.0f)", m10 / n, m5 / n);
        check(msg, m10 > 8 * m5);
    }

    ps_reset();                                        /* 3. bounded */
    {
        int32_t mx = 0;
        for (i = 0; i < NS; i++) {
            int32_t x, y;
            r = r * 1664525u + 1013904223u;
            x = (int32_t)(r >> 16) - 32768;
            y = shimmer_step(x);
            mx = abs(y) > mx ? abs(y) : mx;
        }
        snprintf(msg, sizeof msg, "full-scale noise: bounded (peak %d)", mx);
        check(msg, mx < 40000);
    }
    printf(bad ? "shimmer: %d FAILED\n" : "shimmer: all passed\n", bad);
    return bad;
}
