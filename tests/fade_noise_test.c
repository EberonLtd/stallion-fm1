/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP Pro #4: "3.5mm & USB audio output has distorted noise while notes fading out". The mix floored each part to
 * 16 LSB of the output (fx.c mix_part_run: x >> 4 at the mute / duck gain, always applied at rest since
 * mulq15(32767, 32767) is 32766, and again at the PAN gain; b >> 2 at LEVEL), the MASTER to 4 LSB (mix >> 2), USB AUDIO =
 * FULL to 4 LSB, DIST's drive to 4 x its gain: an error of ~5.5 LSB rms whatever the level, all of it on the note's
 * harmonics (distortion, not hiss), so a note fading out turned into a buzz (THD+N -8 dB at -60 dBFS). Now rounded
 * (dsp.c MULQ12R): the error is the 16-bit output's own rounding.
 * Each case holds a note that decays slowly (SUS 0), renders the output as the codec (and USB) gets it and, over
 * windows of 8192 samples, fits the note (its harmonics, amplitudes moving as a quadratic) by least squares: what is
 * left is the error. Checked between -70 and -30 dBFS (808 SLIDE: -40): its rms under 1 LSB (an ideal 16-bit rounding:
 * 0.29; it was 5 to 6.5), and the THD+N of the window nearest -60 dBFS under -28 dB (it was -12 to -19). Cases: a
 * clean sine (no drive, no FX) at MASTER 4096 and at 2048 (the power-on level), the same through USB AUDIO = FULL
 * (MASTER ignored), and 808 SLIDE (sine, DRV, DIST 30) at 4096.
 * The DAC's 24-bit words (audio.c: out << OUT_SHIFT plus fx.c's out_lo, the bits under Q15): the same sine at MASTER
 * 128 (3 %), from -70 to -30 dBFS before the MASTER (-100 to -60 at the output), measured in LSB of Q15: its error rms
 * under 0.05 LSB (Q15 alone: 0.29, a staircase of a few steps a period), and the THD+N of the window nearest -90 dBFS
 * at the output under -28 dB (Q15 alone: about -7). At MASTER 4096, the limiter idle, out_lo stays 0: the DAC gets
 * out << OUT_SHIFT.
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define NBLK 13000u                                     /* ~9.4 s: the sine from -6 to below -80 dBFS */
#define WIN 8192
static int32_t cap[NBLK * CTL];
static double incq[NBLK];                               /* the note's phase step a block (2^32 a turn) */
static int fails;
static void check(int ok, const char *what) { printf("fade: %-96s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

/* least squares fit of H harmonics of w (each with a constant, linear and quadratic amplitude) and a line; returns the
 * rms of what is left, *sig the fit's rms */
static double fit_left(const double *y, double w, int H, double *sig)
{
    enum { KMAX = 6 * 16 + 2 };
    static double A[KMAX][KMAX], bv[KMAX], phi[KMAX], c[KMAX];
    int K = 6 * H + 2, i, j, k, n;
    double e2 = 0, s2 = 0;
    memset(A, 0, sizeof A), memset(bv, 0, sizeof bv);
    for (n = 0; n < WIN; n++) {
        double t = (n - WIN / 2.0) / WIN;
        for (i = 1, k = 0; i <= H; i++) {
            double s = sin(i * w * n), cs = cos(i * w * n);
            phi[k++] = s, phi[k++] = cs, phi[k++] = s * t, phi[k++] = cs * t, phi[k++] = s * t * t, phi[k++] = cs * t * t;
        }
        phi[k++] = 1, phi[k++] = t;
        for (i = 0; i < K; i++) {
            bv[i] += phi[i] * y[n];
            for (j = 0; j <= i; j++)
                A[i][j] += phi[i] * phi[j];
        }
    }
    for (i = 0; i < K; i++)                             /* Cholesky, then the two triangular solves */
        for (j = 0; j <= i; j++) {
            double s = A[i][j];
            for (k = 0; k < j; k++)
                s -= A[i][k] * A[j][k];
            A[i][j] = i == j ? sqrt(s > 1e-30 ? s : 1e-30) : s / A[j][j];
        }
    for (i = 0; i < K; i++) {
        double s = bv[i];
        for (k = 0; k < i; k++)
            s -= A[i][k] * c[k];
        c[i] = s / A[i][i];
    }
    for (i = K - 1; i >= 0; i--) {
        double s = c[i];
        for (k = i + 1; k < K; k++)
            s -= A[k][i] * c[k];
        c[i] = s / A[i][i];
    }
    for (n = 0; n < WIN; n++) {
        double t = (n - WIN / 2.0) / WIN, f = 0;
        for (i = 1, k = 0; i <= H; i++, k += 6) {
            double s = sin(i * w * n), cs = cos(i * w * n);
            f += (c[k] + c[k + 2] * t + c[k + 4] * t * t) * s + (c[k + 1] + c[k + 3] * t + c[k + 5] * t * t) * cs;
        }
        e2 += (y[n] - f - c[K - 2] - c[K - 1] * t) * (y[n] - f - c[K - 2] - c[K - 1] * t);
        s2 += f * f;
    }
    *sig = sqrt(s2 / WIN);
    return sqrt(e2 / WIN);
}

/* sine: the analog engine's SIN, no drive, no FX; else 808 SLIDE as it comes (its DIST), dry (no sends). usb: capture
 * USB AUDIO = FULL's stream instead of the codec's; dac: the DAC's words (out << OUT_SHIFT + out_lo), in LSB of Q15.
 * Windows from lo to top dBFS: the error rms under eb; the THD+N of the one nearest at under -28 dB */
static void run_at(const char *name, int sine, int32_t master, int usb, int dac, int H, int lo, int top, int at,
                   double eb)
{
    static int32_t o[2 * CTL];
    static double y[WIN];
    track_t *t = &trk[0];
    uint32_t b, i, n, lo_nz = 0;
    double worst = 0, wlev = 0, at60 = 1e9, thdn60 = 0, d60 = 1e9, sc = dac ? 1.0 / (1 << OUT_FRAC) : 1.0;
    char s[240];
    host_tracks_init();
    for (i = 0; i < NVOICE; i++)                        /* (the case before: its note, held, gone) */
        memset(&t->v[i], 0, sizeof t->v[i]);
    t->nmono = 0, t->mono_note = 0;
    song.master_q12 = (uint32_t)master;
    host_preset(t, 0, host_pi(0, "SLIDE BOOM"));        /* ANALOG SLIDE BOOM, by name */
    t->p[P_DIST] = 30;                                  /* (its DIST send then: the factory sounds are dry since 1.0) */
#if CLIPPER_SYNTH
    t->p[P_ED_FX] = -4;                                 /* (Stallion) its trim when the bounds were set: the re-levelling
                                                         * (tools/level_presets.py, +5 dB) put the drive's own grit into
                                                         * the -70..-40 dBFS window, not the rounding */
#endif
    if (sine) {
        t->p[P_E0] = 3, t->p[P_E1] = 0, t->p[P_E2] = 0, t->p[P_E3] = 0, t->p[P_E4] = 127, t->p[P_E5] = 0;
        t->p[P_E6] = 0, t->p[P_DIST] = 0, t->p[P_ED_PIT] = 0;
        t->p[P_ATK] = 0, t->p[P_DEC] = 118, t->p[P_SUS] = 0, t->p[P_REL] = 60;
    }
    t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    usb_full_now = (uint8_t)usb;
    for (b = 0; b < 4; b++)
        mix_block(o, CTL);
    trk_note_on(t, 60, 100);
    for (b = 0; b < NBLK; b++) {
        uint32_t ph = t->v[0].ph[0], g0 = (uint32_t)lim_dac.g, nolim;
        mix_block(o, CTL);
        nolim = g0 == 32768u && lim_dac.g == 32768;     /* (the limiter's gain 1 over the block) */
        incq[b] = (double)(uint32_t)(t->v[0].ph[0] - ph) / CTL;
        for (i = 0; i < CTL; i++) {
            cap[b * CTL + i] = usb ? usb_out[2 * i] : dac ? (o[2 * i] << OUT_FRAC) + out_lo[0][i] : o[2 * i];
            lo_nz += nolim && (out_lo[0][i] != 0 || out_lo[1][i] != 0);
        }
    }
    usb_full_now = 0;
    for (n = 0; n + WIN <= NBLK * CTL; n += WIN) {
        double rms = 0, lev, left, sig;
        for (i = 0; i < WIN; i++)
            rms += (double)cap[n + i] * sc * cap[n + i] * sc;
        lev = 20 * log10(sqrt(rms / WIN) * sqrt(2.0) / 32767 + 1e-12);   /* dBFS of a sine as loud */
        if (lev > top || lev < lo)
            continue;
        for (i = 0; i < WIN; i++)
            y[i] = cap[n + i] * sc;
        left = fit_left(y, 2 * M_PI * incq[(n + WIN / 2) / CTL] / 4294967296.0, H, &sig);
        if (left > worst)
            worst = left, wlev = lev;
        if (fabs(lev - at) < d60)
            d60 = fabs(lev - at), at60 = lev, thdn60 = 20 * log10(left / sig);
    }
    snprintf(s, sizeof s, "%s: error while it fades (%d..%d dBFS) at most %.3f LSB rms (at %.0f dBFS), bound %g",
             name, lo, top, worst, wlev, eb);
    check(worst > 0 && worst < eb, s);
    snprintf(s, sizeof s, "%s: THD+N at %.0f dBFS %.1f dB, bound -28", name, at60, thdn60);
    check(d60 < 5 && thdn60 < -28, s);
    if (dac && master == 4096) {
        snprintf(s, sizeof s, "%s: the DAC gets out << OUT_FRAC where the limiter is idle (samples with low bits: %u)", name, lo_nz);
        check(lo_nz == 0, s);
    }
}
static void run(const char *name, int sine, int32_t master, int usb, int H, int top)
{
    run_at(name, sine, master, usb, 0, H, -70, top, -60, 1.0);
}

int main(void)
{
    run("sine, MASTER 4096", 1, 4096, 0, 1, -30);
    run("sine, MASTER 2048 (power-on)", 1, 2048, 0, 1, -30);
    run("sine, USB AUDIO = FULL", 1, 2048, 1, 1, -30);
    run("808 SLIDE (DRV, DIST 30), MASTER 4096", 0, 4096, 0, 15, -40);   /* (from -40: its drive's own grit, -58 dB under
                                                         * the note, follows the note: eng_analog.c, not the fade) */
    run_at("sine, DAC 24-bit, MASTER 128 (3 %)", 1, 128, 0, 1, 1, -100, -60, -90, 0.05);
    run_at("sine, DAC 24-bit, MASTER 4096", 1, 4096, 0, 1, 1, -70, -30, -60, 1.0);
    printf("fade: %d failed\n", fails);
    return fails;
}
