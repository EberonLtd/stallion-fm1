/* SPDX-License-Identifier: GPL-3.0-only */
/* The OTA filter (firmware/src/dsp.c ota_*: the 4-pole OTA filter chip's idea, in the 1980s DCO and flagship polys)
 * and PONY on it:
 *   build/host/ota_cascade_test
 * 1. the response, resonance 0: flat well below the cutoff, about -12 dB at it, 24 dB an octave above (-40 dB or
 *    less two octaves up); the 12 dB tap (stage 2, the flagship poly's -12 dB/oct): flat below, gentler above
 * 2. the bass drops as the resonance rises (the OTA chip's: its input only partly compensated), 5 to 12 dB at the top;
 *    the LADDER keeps it (its passband compensated)
 * 3. resonance: at the top it sings on its own at the cutoff (an impulse, then still ringing a second later, its pitch
 *    within a semitone), bounded; at 100 it dies away; the 12 dB tap sings too (the loop closed at stage 4)
 * 4. bounded: a minute of full-scale noise with the cutoff and resonance jumping every block, both taps
 * 5. the saturation: a hot input comes out compressed, a quiet one clean (THD < 1 %)
 * 6. PONY: the sub locked to the DCO (the old sub phase does not change the sound); its bass held as the resonance
 *    rises (PONY compensates the OTA's passband, eng_pony.c: the instrument measured; the filter alone still thins)
 * 7. ANALOG with MODL = OTA: darker than open, bounded
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

static int bad;
static void check(const char *what, int ok)
{
    printf("ota: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static double db(double x) { return 20.0 * log10(x > 1e-12 ? x : 1e-12); }

static int cut_index(double hz)
{
    int i, best = 0;
    for (i = 0; i < 128; i++)
        if (fabs(log(CUTOFF_HZ[i] / hz)) < fabs(log(CUTOFF_HZ[best] / hz)))
            best = i;
    return best;
}

/* the gain (dB) for a sine of hz at amplitude amp; tap 0: 24 dB (stage 4), 1: 12 dB (stage 2) */
static double ota_gain(int32_t cut, int32_t res, double hz, double amp, int tap)
{
    ladder_t c;
    int32_t s[4] = {0, 0, 0, 0};
    double e_in = 0, e_out = 0;
    int i, n = FS;
    ota_coef(&c, cut, res);
    for (i = 0; i < n; i++) {
        int32_t x = (int32_t)lrint(amp * sin(2 * M_PI * hz * i / FS)), y = ota_tick(&c, x, s, tap);
        if (i >= n / 2) {
            e_in += (double)x * x;
            e_out += (double)y * y;
        }
    }
    return db(sqrt(e_out / e_in));
}
static double ladder_gain(int32_t cut, int32_t res, double hz, double amp)
{
    ladder_t c;
    int32_t s[4] = {0, 0, 0, 0};
    double e_in = 0, e_out = 0;
    int i, n = FS;
    ladder_coef(&c, cut, res);
    for (i = 0; i < n; i++) {
        int32_t x = (int32_t)lrint(amp * sin(2 * M_PI * hz * i / FS)), y = ladder_tick(&c, x, s);
        if (i >= n / 2) {
            e_in += (double)x * x;
            e_out += (double)y * y;
        }
    }
    return db(sqrt(e_out / e_in));
}

static double pitch_of(const int32_t *b, int n)
{
    int i, z = 0, first = -1, last = -1;
    for (i = 1; i < n; i++)
        if (b[i - 1] < 0 && b[i] >= 0) {
            if (first < 0)
                first = i;
            last = i;
            z++;
        }
    return z > 1 ? (double)(z - 1) * FS / (last - first) : 0;
}

static void t_response(void)
{
    static const double FC[] = {100, 400, 1000, 3000, 8000};
    unsigned k;
    char w[160];
    for (k = 0; k < sizeof FC / sizeof FC[0]; k++) {
        int ci = cut_index(FC[k]);
        double fc = CUTOFF_HZ[ci];
        double lo = ota_gain(ci << 8, 0, fc / 8, 8000, 0), at = ota_gain(ci << 8, 0, fc, 8000, 0);
        double up = fc * 4 < FS * 0.45 ? ota_gain(ci << 8, 0, fc * 4, 8000, 0) : -99;
        double lo2 = ota_gain(ci << 8, 0, fc / 8, 8000, 1);
        double up2 = fc * 4 < FS * 0.45 ? ota_gain(ci << 8, 0, fc * 4, 8000, 1) : -30;
        snprintf(w, sizeof w, "24 dB at %5.0f Hz: fc/8 %+5.1f, fc %+5.1f (-12), 4 fc %+6.1f (<= -40); 12 dB: fc/8 %+5.1f, 4 fc %+6.1f",
                 fc, lo, at, up, lo2, up2);
        check(w, fabs(lo) < 1.0 && at < -9.0 && at > -15.0 && up <= -40.0 && fabs(lo2) < 1.0 && up2 > up + 10.0 &&
                     up2 < -15.0);
    }
}

static void t_bassdrop(void)
{
    int ci = cut_index(1000);
    double o0 = ota_gain(ci << 8, 0, 60, 8000, 0), o1 = ota_gain(ci << 8, 120, 60, 8000, 0);
    double l0 = ladder_gain(ci << 8, 0, 60, 8000), l1 = ladder_gain(ci << 8, 120, 60, 8000);
    char w[160];
    snprintf(w, sizeof w, "the bass (60 Hz, cutoff 1 kHz) at RES 120: OTA %+.1f dB (5..12 down), LADDR %+.1f dB (3 dB less)",
             o1 - o0, l1 - l0);
    check(w, o1 - o0 < -5.0 && o1 - o0 > -12.0 && o1 - o0 < l1 - l0 - 3.0);
}

static void t_resonance(void)
{
    int ci = cut_index(440), i, n = FS * 2, tap;
    ladder_t c;
    static int32_t y[FS * 2];
    char w[160];
    for (tap = 0; tap < 2; tap++) {
        int32_t s[4] = {0, 0, 0, 0}, mx = 0, tail = 0;
        double f;
        ota_coef(&c, ci << 8, 127);
        for (i = 0; i < n; i++) {
            y[i] = ota_tick(&c, i < 32 ? 20000 : 0, s, tap);
            if (abs(y[i]) > mx)
                mx = abs(y[i]);
            if (i > FS && abs(y[i]) > tail)
                tail = abs(y[i]);
        }
        f = pitch_of(y + FS, FS);
        snprintf(w, sizeof w, "%s RES 127 at %d Hz: sings on its own (peak %d, a second later %d), at %.0f Hz",
                 tap ? "12 dB" : "24 dB", CUTOFF_HZ[ci], mx, tail, f);
        check(w, tail > 2000 && mx < 140000 && f > 0 && fabs(12 * log2(f / CUTOFF_HZ[ci])) < 1.0);
    }
    {
        int32_t s[4] = {0, 0, 0, 0}, tail = 0;
        ota_coef(&c, ci << 8, 100);
        for (i = 0; i < n; i++) {
            int32_t v = ota_tick(&c, i < 32 ? 20000 : 0, s, 0);
            if (i > FS && abs(v) > tail)
                tail = abs(v);
        }
        snprintf(w, sizeof w, "RES 100: rings, then dies away (a second later %d)", tail);
        check(w, tail < 4);
    }
}

static void t_bounded(void)
{
    ladder_t c;
    int32_t s[2][4] = {{0}}, mxo = 0, mxs = 0;
    uint32_t r = 1, i, b, tap;
    int finite = 1;
    for (b = 0; b < 60u * FS / CTL; b++) {
        int32_t cut, res;
        r = r * 1664525u + 1013904223u;
        cut = (b & 7u) == 0 ? 127 << 8 : (b & 7u) == 1 ? 0 : (int32_t)((r >> 8) % (128u << 8));
        res = (b & 3u) == 0 ? 127 : (int32_t)((r >> 20) % 128u);
        ota_coef(&c, cut, res);
        for (i = 0; i < CTL; i++) {
            int32_t x;
            r = r * 1664525u + 1013904223u;
            x = (int32_t)(r >> 15) - 65536;
            x = x * 2;                                   /* full scale and beyond (+-131072, the clamp) */
            for (tap = 0; tap < 2; tap++) {
                int32_t y = ota_tick(&c, x, s[tap], (int)tap), j;
                if (abs(y) > mxo)
                    mxo = abs(y);
                for (j = 0; j < 4; j++)
                    if (abs(s[tap][j]) > mxs)
                        mxs = abs(s[tap][j]);
            }
        }
    }
    finite = mxo < 400000 && mxs < 2000000000;
    {
        char w[160];
        snprintf(w, sizeof w, "a minute of full-scale noise, cutoff and RES jumping, both taps: out %d, states %d", mxo, mxs);
        check(w, finite);
    }
}

static void t_saturation(void)
{
    int ci = cut_index(8000), i, n = FS;
    ladder_t c;
    char w[160];
    double h1q = 0, hnq = 0, gq, gh;
    int32_t s[4] = {0, 0, 0, 0};
    double re[6] = {0}, im[6] = {0};
    ota_coef(&c, ci << 8, 0);
    for (i = 0; i < n; i++) {                            /* quiet: 220 Hz at 2000, THD from the first 5 harmonics */
        int32_t x = (int32_t)lrint(2000 * sin(2 * M_PI * 220 * i / FS)), y = ota_tick(&c, x, s, 0), h;
        if (i >= n / 2)
            for (h = 1; h < 6; h++) {
                re[h] += y * cos(2 * M_PI * 220 * h * i / FS);
                im[h] += y * sin(2 * M_PI * 220 * h * i / FS);
            }
    }
    h1q = sqrt(re[1] * re[1] + im[1] * im[1]);
    for (i = 2; i < 6; i++)
        hnq += re[i] * re[i] + im[i] * im[i];
    hnq = sqrt(hnq);
    gq = ota_gain(ci << 8, 0, 220, 2000, 0);
    gh = ota_gain(cut_index(300) << 8, 0, 2000, 120000, 0) - ota_gain(cut_index(300) << 8, 0, 2000, 4000, 0);
    snprintf(w, sizeof w, "quiet in: clean (THD %.2f %%, gain %+.1f dB); a hot 2 kHz into a 300 Hz cutoff: %+.1f dB "
             "compressed", 100.0 * hnq / h1q, gq, gh);
    check(w, hnq / h1q < 0.01 && fabs(gq) < 1.0 && gh < -1.0);
}

/* one PONY voice rendered straight through pony_render (no voice allocation, no envelope) */
static void pony_direct(track_t *t, uint32_t ph1, int32_t *out, uint32_t n, double hz)
{
    voice_t v;
    vmod_t m;
    uint32_t b;
    memset(&v, 0, sizeof v);
    memset(&m, 0, sizeof m);
    v.ph[1] = ph1;
    v.ph[2] = 0x2545F491u;
    v.pitch16 = 45 * 16;
    m.inc = (uint32_t)(hz / FS * 4294967296.0);
    m.pitch16 = 45 * 16;
    m.amp0 = m.amp1 = 32767;
    m.shape = 64 << 8;
    memset(out, 0, n * sizeof *out);
    for (b = 0; b + CTL <= n; b += CTL)
        pony_render(t, &v, out + b, CTL, &m);
}
static double mag_of(const int32_t *b, uint32_t n, double f)
{
    double re = 0, im = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / n);
        re += b[i] * w * cos(2 * M_PI * f * i / FS);
        im += b[i] * w * sin(2 * M_PI * f * i / FS);
    }
    return sqrt(re * re + im * im) / n;
}

static void t_pony(void)
{
    static int32_t a[8192], b[8192];
    track_t *t = &trk[0];
    char w[160];
    host_preset(t, ENGI_PONY, 0);
    t->p[P_E0] = JW_SAW, t->p[P_E1] = 0, t->p[P_E2] = 127, t->p[P_E3] = 0;   /* a saw and the sub, no noise */
    t->p[P_E4] = 1, t->p[P_E5] = 100, t->p[P_E6] = 0;
    pony_direct(t, 0u, a, 8192, 110.0);
    pony_direct(t, 0x5A3C0000u, b, 8192, 110.0);     /* another sub phase (its top bit the same) */
    check("PONY: the sub locked to the DCO (another old sub phase: the same sound)", !memcmp(a, b, sizeof a));
    {
        double m0, m1, m2;
        t->p[P_E5] = 60, t->p[P_E6] = 0;
        pony_direct(t, 0u, a, 8192, 55.0);
        m0 = mag_of(a + 4096, 4096, 55.0);
        t->p[P_E6] = 96;
        pony_direct(t, 0u, a, 8192, 55.0);
        m1 = mag_of(a + 4096, 4096, 55.0);
        t->p[P_E6] = 120;
        pony_direct(t, 0u, a, 8192, 55.0);
        m2 = mag_of(a + 4096, 4096, 55.0);
        snprintf(w, sizeof w, "PONY: the bass held as the resonance rises (55 Hz: RES 96 %+.1f dB, 120 singing %+.1f dB; "
                 "the OTA alone -6.8)", db(m1 / m0), db(m2 / m0));
        check(w, fabs(db(m1 / m0)) < 1.5 && db(m2 / m0) > -3.5 && db(m2 / m0) < 3.0);
    }
}

static void t_analog(void)
{
    static int32_t a[8192];
    track_t *t = &trk[0];
    double open, dark;
    int good = 1;
    uint32_t i;
    char w[160];
    host_preset(t, 0, 0);
    t->p[P_FMODEL] = FM_OTA;
    check("MODL: OTA is a model, after HP; its name OTA", FM_OTA == FM_LAST && !strcmp(N_FMODEL[FM_OTA - 1], "OTA"));
    {
        fmod_t f;
        int32_t sv[2] = {0, 0}, ls[4] = {0, 0, 0, 0}, x, y;
        double eo = 0, ed = 0;
        fmod_coef(&f, FM_OTA, 127 << 8, 0);
        for (i = 0; i < 8192; i++) {
            x = (int32_t)lrint(16000 * sin(2 * M_PI * 3000.0 * i / FS));
            y = fmod_tick(&f, x, sv, ls);
            a[i] = y;
            if (i > 4096)
                eo += (double)y * y;
        }
        open = sqrt(eo / 4096);
        fmod_coef(&f, FM_OTA, cut_index(500) << 8, 0);
        memset(ls, 0, sizeof ls);
        for (i = 0; i < 8192; i++) {
            x = (int32_t)lrint(16000 * sin(2 * M_PI * 3000.0 * i / FS));
            y = fmod_tick(&f, x, sv, ls);
            if (abs(y) > 140000)
                good = 0;
            if (i > 4096)
                ed += (double)y * y;
        }
        dark = sqrt(ed / 4096);
    }
    snprintf(w, sizeof w, "ANALOG MODL OTA: 3 kHz through a 500 Hz cutoff %.1f dB below open, bounded", db(dark / open));
    check(w, good && db(dark / open) < -30.0);
}

int main(void)
{
    host_tracks_init();
    t_response();
    t_bassdrop();
    t_resonance();
    t_bounded();
    t_saturation();
    t_pony();
    t_analog();
    printf(bad ? "ota: %d FAILED\n" : "ota: all passed\n", bad);
    return bad;
}
