/* SPDX-License-Identifier: GPL-3.0-only */
/* RODEO (firmware/src/eng_rodeo.c, after zvenson's OberVave; CLIPPER_SYNTH 1):
 *   build/host/rodeo_test
 *   engine     the 25th engine, after PALOMINO (the stores' numbers kept), named RODEO (ENG's name too)
 *   OSC        OSC 1 in tune (A4: 440 Hz); OSC 2 +12, MIX +63: 880 Hz alone; DTUNE +63: OSC 2 50 cents above
 *   VCF        each mode's response, sines straight through the filter (RES 0, the cutoff at step 64, ~710 Hz): LP12
 *              ~12 dB an octave above, LP24 ~24, HP12 ~12 below, BP12 ~6 either side, NOTCH deep at the cutoff;
 *              MORPH through the engine: SHAPE at the middle LP, a quarter out a notch, at the end HP
 *   SYNC       OSC 2 synced, alone, a fifth up: at OSC 1's pitch; SHAPE sweeps the timbre, not the pitch;
 *              band-limited at C7 (little energy off its harmonics)
 *   XMOD       OSC 2 a fifth up bends OSC 1: the sidebands f1 - f2 (110 Hz under 220) and off OSC 1's harmonics
 *   bounded    RES 127, XMOD at the top, two BOTHs, every mode, low and high
 *   presets    every one bounded, audible, its voices free after the release (a chord through the whole mix)
 *   CPU        host instructions a sample and voice (8 voices, macOS: the kernel's count), RODEO's heaviest preset
 *              under ROD_COST_MAX; PONY and PALOMINO beside it
 *   KYBD       (Stallion, EDIT 3) 86 % is the old tracking (ROD_KTRK) sample for sample, every preset's values, notes
 *              24..108; 0 (the default, every factory preset: the originals measured none) no tracking (the filter's
 *              gain at 2640 Hz the same on notes 45 57 69), 100 one octave of cutoff an octave (LP12: ~12 dB an
 *              octave more there); shown on RODEO alone; kept by a project (an older block: 0) and a user preset
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>
#define PROJ_HOST 1
static const uint8_t TRK_DEF[NPART][2] = {{ENGI_HYBRID, 0}, {1, 0}, {4, 5}};   /* (ui.c, Clipper) */
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? TRK_DEF[i][0] : 0u; }
#include "../firmware/src/project.c"
#define UP_HOST 1
#include "../firmware/src/upreset.c"
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define ROD_COST_MAX 210.0         /* host instructions a sample and voice (RODEO ~160 at most; PONY ~265, PALOMINO ~255 here) */

static int bad;
static void ok(int c, const char *what)
{
    printf("rodeo: %-100s %s\n", what, c ? "ok" : "FAIL");
    bad += !c;
}
static double db(double x) { return 20.0 * log10(x > 1e-12 ? x : 1e-12); }

#define N 16384
static int32_t buf[N];
static uint32_t rod_rel;                              /* != 0: OSC 2 that far (Q32) on from OSC 1 at the start */
static int32_t rod_cut;                               /* the voice's cutoff offset (vmod_t cutoff, Q8 steps) */
static int32_t rod_kick;                              /* != 0: the SVF's low-pass state set to it at the start */

/* one voice straight through rodeo_render; shape: SHAPE (0..127 << 8, 64 << 8 the middle) */
static void rod_direct(track_t *t, double hz, int32_t shape)
{
    voice_t v;
    vmod_t m;
    uint32_t b;
    int32_t p16 = (int32_t)lrint((69.0 + 12.0 * log2(hz / 440.0)) * 16.0);
    memset(&v, 0, sizeof v);
    memset(&m, 0, sizeof m);
    v.note = 69;
    rodeo_note_on(t, &v);
    if (rod_rel)
        v.ph[1] = v.ph[0] + rod_rel;
    if (rod_kick)
        v.s[1] = rod_kick;
    m.inc = (uint32_t)(hz / FS * 4294967296.0);
    m.pitch16 = p16;
    m.amp0 = m.amp1 = 32767;
    m.shape = shape;
    m.cutoff = rod_cut;
    memset(buf, 0, sizeof buf);
    for (b = 0; b + CTL <= N; b += CTL)
        rodeo_render(t, &v, buf + b, CTL, &m);
}
static double mag(double f, uint32_t a, uint32_t n)
{
    double re = 0, im = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / n);
        re += buf[a + i] * w * cos(2 * M_PI * f * i / FS);
        im += buf[a + i] * w * sin(2 * M_PI * f * i / FS);
    }
    return sqrt(re * re + im * im) / n;
}
/* the share of the energy of buf[a .. a + n) off the harmonics of f0 (bins within 1.5 bins of k f0 count as on) */
static double off_harmonics(double f0, uint32_t a, uint32_t n)
{
    double on = 0, all = 0;
    uint32_t k, i, nb = n / 2;
    for (k = 1; k < nb; k++) {
        double f = (double)k * FS / n, h = f / f0, e, re = 0, im = 0;
        for (i = 0; i < n; i++) {
            double w = 0.5 - 0.5 * cos(2 * M_PI * i / n), x = buf[a + i] * w, ph = 2 * M_PI * k * i / n;
            re += x * cos(ph);
            im += x * sin(ph);
        }
        e = re * re + im * im;
        all += e;
        if (fabs(h - floor(h + 0.5)) * f0 < 1.5 * FS / n)
            on += e;
    }
    return all > 0 ? 1.0 - on / all : 0;
}
static void plain(track_t *t)
{
    host_preset(t, ENGI_RODEO, 0);
    t->p[P_E0] = OW_SAW, t->p[P_E1] = OW_SAW, t->p[P_E2] = 0, t->p[P_E3] = 0;
    t->p[P_E4] = -64, t->p[P_E5] = 127, t->p[P_E6] = 0, t->p[P_E7] = OM_LP24;
    t->p[P_OKYB] = ROD_KYB_OLD, t->p[P_OPW] = 2;        /* (KYBD, PW0: the tracking and width these checks were written
                                                        * with, ROD_KTRK and 53 %) */
}

static void t_engine(void)
{
    ok(NENGINES >= 26u && ENGI_RODEO == 25u && ENGINES[ENGI_RODEO] == &ENG_RODEO && !strcmp(ENG_RODEO.name, "RODEO") &&
           !strcmp(N_ENGNAME[ENGI_RODEO], "RODEO") && !strcmp(N_ENGNAME[ENGI_PALOMINO], "PALOMINO") &&
           !strcmp(N_ENGNAME[ENGI_STAMPEDE], "STAMPEDE"),
       "the 26th engine, after STAMPEDE, named RODEO (and ENG's names: PALOMINO, STAMPEDE, RODEO)");
}

static void t_osc(void)
{
    track_t *t = &trk[0];
    double a, b, c;
    char w[200];
    plain(t);
    rod_direct(t, 440.0, 64 << 8);
    a = mag(440, N / 2, N / 2), b = mag(415.3, N / 2, N / 2), c = mag(466.2, N / 2, N / 2);
    ok(a > 8 * b && a > 8 * c, "OSC 1: in tune (a saw at A4: 440 Hz, not beside it)");
    t->p[P_E4] = 63, t->p[P_E2] = 12;
    rod_direct(t, 440.0, 64 << 8);
    a = mag(880, N / 2, N / 2), b = mag(440, N / 2, N / 2);
    snprintf(w, sizeof w, "OSC 2 +12, MIX +63: 880 Hz, no 440 (%.0f vs %.0f)", a, b);
    ok(a > 20 * b, w);
    t->p[P_E2] = 0, t->p[P_E3] = 63;                   /* DTUNE +63: OSC 2 at 440 x 2^(50 / 1200) */
    rod_direct(t, 440.0, 64 << 8);
    a = mag(452.9, N / 2, N / 2), b = mag(440, N / 2, N / 2);
    snprintf(w, sizeof w, "DTUNE +63: OSC 2 50 cents above (452.9 Hz %.0f, 440 Hz %.0f)", a, b);
    ok(a > 8 * b, w);
}

/* a sine of f straight through the SVF in a mode (RES 0, cut step 64), its gain in dB after the settling */
static double svf_gain(uint32_t mode, double f, int32_t mq)
{
    rod_svf_t c1, c2;
    int32_t s[4] = {0, 0, 0, 0}, i, n = 32768;
    double re = 0, im = 0;
    rod_coef(&c1, 64 << 8, 8192);
    rod_coef(&c2, 64 << 8, 5793);
    for (i = 0; i < n; i++) {
        int32_t x = (int32_t)lrint(30000.0 * sin(2 * M_PI * f * i / FS)), lp, bp, hp, y;
        rod_svf(&c1, s, x, &lp, &bp, &hp);
        switch (mode) {
        case OM_LP12: y = lp; break;
        case OM_HP12: y = hp; break;
        case OM_BP12: y = bp; break;
        case OM_NOTCH: y = hp + lp; break;
        case OM_MORPH: y = lp + (((hp - lp) * mq) >> 7); break;
        default: y = rod_svf_lp(&c2, s + 2, lp); break;
        }
        if (i >= n / 2)                                /* (its own frequency only: not the rounding's noise) */
            re += y * cos(2 * M_PI * f * i / FS), im += y * sin(2 * M_PI * f * i / FS);
    }
    return db(2.0 * sqrt(re * re + im * im) / (n / 2) / 30000.0);
}

static void t_filter(void)
{
    double fc = 30.0 * pow(16000.0 / 30.0, 64.0 / 127.0), a, b, c;
    char w[200];
    a = svf_gain(OM_LP12, fc * 3, 0), b = svf_gain(OM_LP12, fc * 6, 0), c = svf_gain(OM_LP12, fc / 4, 0);
    snprintf(w, sizeof w, "LP12: %.1f dB an octave above (%.0f -> %.0f Hz), passband %.1f dB", a - b, fc * 3, fc * 6, c);
    ok(a - b > 10.0 && a - b < 14.0 && fabs(c) < 1.0, w);
    a = svf_gain(OM_LP24, fc * 3, 0), b = svf_gain(OM_LP24, fc * 6, 0), c = svf_gain(OM_LP24, fc / 4, 0);
    snprintf(w, sizeof w, "LP24: %.1f dB an octave above, passband %.1f dB", a - b, c);
    ok(a - b > 21.0 && a - b < 27.0 && fabs(c) < 1.0, w);
    a = svf_gain(OM_HP12, fc / 3, 0), b = svf_gain(OM_HP12, fc / 6, 0), c = svf_gain(OM_HP12, fc * 4, 0);
    snprintf(w, sizeof w, "HP12: %.1f dB an octave below (%.0f -> %.0f Hz), passband %.1f dB", a - b, fc / 3, fc / 6, c);
    ok(a - b > 10.0 && a - b < 14.0 && fabs(c) < 1.0, w);
    {
        double up = svf_gain(OM_BP12, fc * 4, 0) - svf_gain(OM_BP12, fc * 8, 0);
        double dn = svf_gain(OM_BP12, fc / 4, 0) - svf_gain(OM_BP12, fc / 8, 0);
        double pk = svf_gain(OM_BP12, fc, 0);
        snprintf(w, sizeof w, "BP12: %.1f dB an octave above, %.1f below, its peak at the cutoff (%.1f dB vs %.1f, %.1f)",
                 up, dn, pk, svf_gain(OM_BP12, fc * 4, 0), svf_gain(OM_BP12, fc / 4, 0));
        ok(up > 4.5 && up < 7.5 && dn > 4.5 && dn < 7.5 && pk > svf_gain(OM_BP12, fc * 2, 0) && pk > svf_gain(OM_BP12, fc / 2, 0), w);
    }
    a = svf_gain(OM_NOTCH, fc, 0), b = svf_gain(OM_NOTCH, fc / 8, 0), c = svf_gain(OM_NOTCH, fc * 8, 0);
    snprintf(w, sizeof w, "NOTCH: %.1f dB at the cutoff, %.1f / %.1f dB three octaves either side", a, b, c);
    ok(a < -30.0 && fabs(b) < 1.0 && fabs(c) < 1.0, w);
}

/* MORPH through the engine: a saw at 55 Hz, the cutoff at step 64; the level of its harmonics near fc / 4, fc and
 * fc * 4 against the same saw's with the filter wide open (LP12, step 127: (rodeo-types) LP24's four equal poles
 * take ~2 dB off the top even wide open, the 4-pole filter chip's way) */
static void morph_bands(track_t *t, int32_t shape, double *lo, double *mid, double *hi)
{
    static int32_t ref[N];
    double fc = 30.0 * pow(16000.0 / 30.0, 64.0 / 127.0), f0 = 55.0;
    double fl = f0 * floor(fc / 4 / f0 + 0.5), fm = f0 * floor(fc / f0 + 0.5), fh = f0 * floor(fc * 4 / f0 + 0.5);
    double rl, rm, rh;
    plain(t);
    t->p[P_E7] = OM_LP12;
    rod_direct(t, f0, 64 << 8);
    memcpy(ref, buf, sizeof buf);
    rl = mag(fl, N / 2, N / 2), rm = mag(fm, N / 2, N / 2), rh = mag(fh, N / 2, N / 2);
    t->p[P_E5] = 64 + 27 * ROD_KTRK / 16, t->p[P_E7] = OM_MORPH;   /* (the key tracking: A1 is 27 semitones under C4, ROD_KTRK) */
    rod_direct(t, f0, shape);
    *lo = db(mag(fl, N / 2, N / 2) / rl), *mid = db(mag(fm, N / 2, N / 2) / rm), *hi = db(mag(fh, N / 2, N / 2) / rh);
    (void)ref;
}
static void t_sem(void)
{
    track_t *t = &trk[0];
    double l0, m0, h0, l1, m1, h1, l2, m2, h2;
    char w[220];
    morph_bands(t, 64 << 8, &l0, &m0, &h0);
    morph_bands(t, (64 + 32) << 8, &l1, &m1, &h1);
    morph_bands(t, 127 << 8, &l2, &m2, &h2);
    snprintf(w, sizeof w, "MORPH, SHAPE middle: LP (fc/4 %.1f, fc %.1f, 4fc %.1f dB)", l0, m0, h0);
    ok(l0 > -3.0 && h0 < -20.0, w);
    snprintf(w, sizeof w, "MORPH, SHAPE a quarter out: a notch (fc/4 %.1f, fc %.1f, 4fc %.1f dB)", l1, m1, h1);
    ok(m1 < l1 - 12.0 && m1 < h1 - 12.0, w);
    snprintf(w, sizeof w, "MORPH, SHAPE at the end: HP (fc/4 %.1f, fc %.1f, 4fc %.1f dB)", l2, m2, h2);
    ok(h2 > -3.0 && l2 < -20.0, w);
}

/* (rodeo-types) LP24 as the 4-pole filter chip: the resonance a feedback round four equal poles, so the passband falls
 * as RES rises (the 1981 model's thinning bass) while the cutoff's harmonic rises; LP12's passband stays */
static void t_lp24_res(void)
{
    track_t *t = &trk[0];
    double fc = 30.0 * pow(16000.0 / 30.0, 64.0 / 127.0), f0 = 55.0, l[2][2], m[2][2];
    double fl = f0 * floor(fc / 4 / f0 + 0.5), fm = f0 * floor(fc / f0 + 0.5);
    int mo, r;
    char w[220];
    for (mo = 0; mo < 2; mo++)
        for (r = 0; r < 2; r++) {
            plain(t);
            t->p[P_E5] = 64 + 27 * ROD_KTRK / 16, t->p[P_E6] = r ? 100 : 0, t->p[P_E7] = mo ? OM_LP24 : OM_LP12;
            rod_direct(t, f0, 64 << 8);
            l[mo][r] = db(mag(fl, N / 2, N / 2)), m[mo][r] = db(mag(fm, N / 2, N / 2));
        }
    snprintf(w, sizeof w, "LP24 RES 100: passband %+.1f dB, the cutoff %+.1f dB against RES 0 (LP12: %+.1f, %+.1f)",
             l[1][1] - l[1][0], m[1][1] - m[1][0], l[0][1] - l[0][0], m[0][1] - m[0][0]);
    ok(l[1][1] - l[1][0] < -4.0 && m[1][1] - m[1][0] > 3.0 && fabs(l[0][1] - l[0][0]) < 3.0, w);
}

static void t_sync(void)
{
    track_t *t = &trk[0];
    double a, b, o, s0, s1;
    char w[220];
    plain(t);
    t->p[P_E4] = 63, t->p[P_E2] = 7, t->p[P_E1] = OW_SYSAW, t->p[P_E3] = 0;
    rod_direct(t, 220.0, 64 << 8);
    a = mag(220, N / 2, N / 2), b = mag(329.6, N / 2, N / 2);
    o = off_harmonics(220.0, N / 2, 4096);
    snprintf(w, sizeof w, "SYNC: OSC 2 alone, a fifth up, plays at OSC 1's 220 Hz (%.0f vs 330 Hz %.0f; %.2f %% off)",
             a, b, 100 * o);
    ok(a > 4 * b && o < 0.02, w);
    s0 = mag(660, N / 2, N / 2);
    rod_direct(t, 220.0, (64 + 40) << 8);
    s1 = mag(660, N / 2, N / 2);
    o = off_harmonics(220.0, N / 2, 4096);
    snprintf(w, sizeof w, "SYNC: SHAPE sweeps the timbre (660 Hz %.1f dB), the pitch stays (%.2f %% off)", db(s1 / s0), 100 * o);
    ok(fabs(db(s1 / s0)) > 3.0 && o < 0.02, w);
    t->p[P_E2] = 5, t->p[P_E1] = OW_SYPLS;
    rod_direct(t, 2093.0, (64 + 30) << 8);
    o = off_harmonics(2093.0, N / 2, 4096);
    snprintf(w, sizeof w, "SYNC (pulse) at C7: band-limited (%.2f %% of the energy off its harmonics)", 100 * o);
    ok(o < 0.03, w);
}

/* (Stallion) at an exact interval the sidebands' share follows OSC 2's start against OSC 1 (4 .. 25 %: a quarter, as
 * note-on sets it, 8.8 %): the mean over eight starts, as notes from free-running oscillators average */
static void t_xmod(void)
{
    track_t *t = &trk[0];
    double o0 = 0, o1 = 0, s0 = 0, s1 = 0;
    uint32_t k;
    char w[220];
    for (k = 0; k < 8u; k++) {
        rod_rel = k * 0x20000000u + 0x10000000u;
        plain(t);
        t->p[P_E2] = 7, t->p[P_E5] = 127;              /* OSC 1 alone heard (MIX -64), OSC 2 a fifth up */
        rod_direct(t, 220.0, 64 << 8);
        o0 += off_harmonics(220.0, N / 2, 4096) / 8, s0 += mag(110, N / 2, N / 2) / 8;
        t->p[P_E3] = -50;
        rod_direct(t, 220.0, 64 << 8);
        o1 += off_harmonics(220.0, N / 2, 4096) / 8, s1 += mag(110, N / 2, N / 2) / 8;
    }
    rod_rel = 0;
    snprintf(w, sizeof w, "XMOD -50: sidebands (110 Hz +%.0f dB; %.1f %% of the energy off OSC 1's harmonics, %.2f %% without)",
             db(s1 / s0), 100 * o1, 100 * o0);
    ok(o0 < 0.03 && o1 > 0.1 && db(s1 / s0) > 20.0, w);
}

static void t_bounded(void)
{
    track_t *t = &trk[0];
    uint32_t i, m, k;
    int32_t mx = 0;
    char w[200];
    for (m = 0; m < 6u; m++)
        for (k = 0; k < 3u; k++) {
            plain(t);
            t->p[P_E0] = OW_BOTH, t->p[P_E1] = k == 2u ? OW_SYBTH : OW_BOTH, t->p[P_E4] = 0;
            t->p[P_E3] = k == 1u ? -64 : 0, t->p[P_E2] = 19, t->p[P_E6] = 127, t->p[P_E7] = (int16_t)m;
            t->p[P_E5] = k ? 90 : 40;
            rod_direct(t, k ? 1760.0 : 55.0, (64 + 63) << 8);
            for (i = 0; i < N; i++)
                if (abs(buf[i]) > mx)
                    mx = abs(buf[i]);
        }
    snprintf(w, sizeof w, "RES 127, XMOD at the top, two BOTHs, SYNC, every mode, low and high: bounded (peak %d)", mx);
    ok(mx < (1 << 21), w);
}

static void run_mix(uint32_t nb, double *peak, int *inb)
{
    static int32_t o[CTL * 2];
    uint32_t b, i, cnt = 0;
    double e = 0, best = 0;
    *inb = 1;
    for (b = 0; b < nb; b++) {
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            if (o[i] > (1 << 23) || o[i] < -(1 << 23))
                *inb = 0;
            e += (double)o[i] * o[i];
        }
        if (++cnt == 64u) {
            double r = sqrt(e / (64.0 * 2 * CTL));
            best = r > best ? r : best;
            e = 0, cnt = 0;
        }
    }
    *peak = best;
}
static uint32_t sounding(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return n;
}
static void t_presets(void)
{
    static const uint8_t ch[3] = {48, 55, 64};
    uint32_t pi, i, good = 1, loud = 1, freed = 1;
    char w[200];
    for (pi = 0; pi < ENG_RODEO.npresets; pi++) {
        double r;
        int inb, inb2;
        for (i = 0; i < NPART; i++)
            trk_all_off(&trk[i]);
        run_mix(600, &r, &inb);
        host_preset(&trk[0], ENGI_RODEO, pi);
        trk[0].p[P_AMODE] = 0;
        for (i = 0; i < 3u; i++)
            trk_note_on(&trk[0], ch[i], 100);
        run_mix(1400, &r, &inb);
        for (i = 0; i < 3u; i++)
            trk_note_off(&trk[0], ch[i]);
        for (i = 0; i < 60u && sounding(&trk[0]); i++) {
            double r2;
            run_mix(500, &r2, &inb2);
            inb = inb && inb2;
        }
        if (getenv("ROD_VERBOSE"))
            printf("  %-12s rms %.0f\n", ENG_RODEO.presets[pi].name, r);
        if (!inb)
            good = 0, printf("  %s: out of bounds\n", ENG_RODEO.presets[pi].name);
        if (r < 300.0)
            loud = 0, printf("  %s: quiet (%.0f)\n", ENG_RODEO.presets[pi].name, r);
        if (sounding(&trk[0]))
            freed = 0, printf("  %s: %u voices left\n", ENG_RODEO.presets[pi].name, (unsigned)sounding(&trk[0]));
    }
    snprintf(w, sizeof w, "all %u presets bounded, audible, their voices free after the release", (unsigned)ENG_RODEO.npresets);
    ok(good && loud && freed && ENG_RODEO.npresets >= 24u && ENG_RODEO.npresets <= 32u, w);
}

/* ------------------------------------------------------------------ CPU --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
#pragma GCC push_options
#pragma GCC optimize("no-inline")
static void rod_counted(uint32_t nb)
{
    static int32_t o[CTL];
    uint32_t k;
    for (k = 0; k < nb; k++)
        track_render(&trk[0], o, CTL);
}
#pragma GCC pop_options
/* host instructions a sample of e's preset pi with v voices held (a chord), the idle part's taken off, / v */
static double cost_of(uint32_t e, uint32_t pi, uint32_t nv)
{
    static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    uint64_t a, b, c, d;
    uint32_t i, nb = FS / CTL;
    for (i = 0; i < NPART; i++) {
        trk_all_off(&trk[i]);
        memset(trk[i].v, 0, sizeof trk[i].v);            /* (no voice left from the one before) */
    }
    host_tracks_init();
    host_preset(&trk[0], e, pi);
    trk[0].p[P_SUS] = 127, trk[0].p[P_AMODE] = 0, trk[0].p[P_VOICE] = V_POLY, trk[0].p[P_ATK] = 0;
    rod_counted(16);
    a = instr_now();
    rod_counted(nb);
    b = instr_now();
    for (i = 0; i < nv; i++)
        trk_note_on(&trk[0], NOTES[i], 100);
    rod_counted(FS / 4u / CTL);
    c = instr_now();
    rod_counted(nb);
    d = instr_now();
    return ((double)(d - c) - (double)(b - a)) / ((double)nb * CTL * nv);
}
static void t_cpu(void)
{
    uint32_t pi, worst_p = 0;
    double worst = 0, sum = 0, c, pony = 0, jup = 0;
    char w[220];
    if (!instr_now()) {
        printf("rodeo: CPU: no instruction count on this host (macOS proc_pid_rusage); not measured\n");
        return;
    }
    for (pi = 0; pi < ENG_RODEO.npresets; pi++) {
        c = cost_of(ENGI_RODEO, pi, 8);
        sum += c;
        if (getenv("ROD_VERBOSE"))
            printf("  %-12s %5.0f\n", ENG_RODEO.presets[pi].name, c);
        if (c > worst)
            worst = c, worst_p = pi;
    }
    for (pi = 0; pi < ENGINES[ENGI_PONY]->npresets; pi++)
        pony = fmax(pony, cost_of(ENGI_PONY, pi, 8));
    for (pi = 0; pi < ENGINES[ENGI_PALOMINO]->npresets; pi++)
        jup = fmax(jup, cost_of(ENGI_PALOMINO, pi, 8));
    snprintf(w, sizeof w, "CPU: %.0f host instructions a sample and voice at most (%s; mean %.0f; PONY %.0f, PALOMINO %.0f; limit %.0f)",
             worst, ENG_RODEO.presets[worst_p].name, sum / ENG_RODEO.npresets, pony, jup, ROD_COST_MAX);
    ok(worst <= ROD_COST_MAX, w);
}

/* (Stallion) the top of RES sings (ROD_SOSC2, ROD_SOSC4): RES 121 and below as before (the goldens: every preset under
 * 121), 124 .. 127 a steady sine at the cutoff from a tiny start, bounded, in LP24, LP12, BP12 and MORPH; with a note
 * playing, the ring over the note */
static double zc_hz(uint32_t a, uint32_t n)            /* rising zero crossings of buf[a .. a + n) */
{
    uint32_t i, k = 0;
    double first = -1, last = -1;
    for (i = a + 1; i < a + n; i++)
        if (buf[i - 1] < 0 && buf[i] >= 0) {
            double x = i - 1 + (double)-buf[i - 1] / (double)(buf[i] - buf[i - 1]);
            if (first < 0)
                first = x;
            else
                k++;
            last = x;
        }
    return k ? k * (double)FS / (last - first) : 0;
}
static void t_selfosc(void)
{
    track_t *t = &trk[0];
    static const uint32_t MODES[] = {OM_LP24, OM_LP12, OM_BP12, OM_MORPH};
    static const char *const MN[] = {"LP24", "LP12", "BP12", "MORPH"};
    static const int RES[3] = {121, 124, 127};
    uint32_t k, i, r, pmax = 0;
    char w[240];
    for (k = 0; k < NELEM(MODES); k++) {
        double lv[3], grow = 0, hz = 0;
        for (r = 0; r < 3u; r++) {
            double e3 = 0, e4 = 0;
            plain(t);
            t->p[P_E0] = OW_OFF, t->p[P_E1] = OW_OFF, t->p[P_E5] = 70, t->p[P_E6] = (int16_t)RES[r];
            t->p[P_E7] = (int16_t)MODES[k];
            rod_kick = 64;                              /* (a whisper: -60 dB of the state's range) */
            rod_direct(t, 110.0, 64 << 8);
            rod_kick = 0;
            for (i = 0; i < N; i++) {
                uint32_t a = (uint32_t)(buf[i] < 0 ? -buf[i] : buf[i]);
                pmax = a > pmax ? a : pmax;
            }
            for (i = N / 2; i < N * 3 / 4; i++)
                e3 += (double)buf[i] * buf[i];
            for (i = N * 3 / 4; i < N; i++)
                e4 += (double)buf[i] * buf[i];
            lv[r] = 10 * log10(e4 / (N / 4) + 1);
            if (r == 2u)
                grow = 10 * log10((e4 + 1) / (e3 + 1)), hz = zc_hz(N * 3 / 4, N / 4);
        }
        snprintf(w, sizeof w, "RES top, %s, no input: the last 90 ms at RES 121 %.0f dB, 124 %.0f dB, 127 %.0f dB (%.0f Hz, "
                 "steady %+.2f dB)", MN[k], lv[0], lv[1], lv[2], hz, grow);
        ok(lv[0] < 40.0 && lv[1] > 60.0 && lv[2] > 60.0 && fabs(grow) < 0.5 && hz > 100.0, w);
    }
    snprintf(w, sizeof w, "RES top: bounded (the largest sample %u)", pmax);
    ok(pmax < 65001u, w);
    {   /* with a note: the ring over it (LP24, CUT 70, A2), RES 127 against 121 */
        double a[2];
        for (r = 0; r < 2u; r++) {
            double e = 0;
            plain(t);
            t->p[P_E0] = OW_SAW, t->p[P_E4] = -64, t->p[P_E5] = 70, t->p[P_E6] = r ? 127 : 121, t->p[P_E7] = OM_LP24;
            rod_direct(t, 110.0, 64 << 8);
            for (i = N / 2; i < N; i++)
                e += (double)buf[i] * buf[i];
            a[r] = 10 * log10(e / (N / 2) + 1);
        }
        snprintf(w, sizeof w, "RES top, LP24 under A2: RES 127 %+.1f dB over RES 121", a[1] - a[0]);
        ok(a[1] - a[0] > 2.0, w);
    }
}

/* (Stallion) EDIT 3 KYBD */
static int32_t buf2[N];
static double gain_at(track_t *t, int note, int kyb, int cut, double f)   /* the filter's gain at f (dB), note's saw */
{
    double hz = 440.0 * pow(2.0, (note - 69) / 12.0), a;
    t->p[P_OKYB] = (int16_t)kyb, t->p[P_E5] = (int16_t)cut;
    rod_direct(t, hz, 64 << 8);
    a = mag(f, N / 2, N / 2);
    t->p[P_OKYB] = 0, t->p[P_E5] = 127;
    rod_direct(t, hz, 64 << 8);
    return db(a / mag(f, N / 2, N / 2));
}
static void t_kybd(void)
{
    track_t *t = &trk[0];
    static const int notes[] = {24, 45, 60, 69, 84, 108};
    uint32_t pi, k, diff = 0, runs = 0, wrong = 0, wrongw = 0, page = 0, i;
    int32_t g1[3];
    double gz[3], gf[3], gd[3];
    char w[220];
    ok(TP[P_OKYB].def == 0 && ROD_KYB_OLD == 86 && TP[P_OKYB].min == 0 && TP[P_OKYB].max == 100 &&
           !strcmp(TP[P_OKYB].label, "KYBD") && TP[P_OKYB].fmt == F_PCT,
       "KYBD: 0..100 %, default 0 (the old tracking at 86 %)");
    for (pi = 0; pi < ENG_RODEO.npresets; pi++) {       /* the default: the old (note - C4) x ROD_KTRK, through the render */
        for (k = 0; k < NELEM(notes); k++) {
            int32_t d = (notes[k] - 60) * 16;
            host_preset(t, ENGI_RODEO, pi);
            t->p[P_OKYB] = ROD_KYB_OLD, rod_cut = 0;
            rod_direct(t, 440.0 * pow(2.0, (notes[k] - 69) / 12.0), 64 << 8);
            memcpy(buf2, buf, sizeof buf);
            t->p[P_OKYB] = 0, rod_cut = d * ROD_KTRK;
            rod_direct(t, 440.0 * pow(2.0, (notes[k] - 69) / 12.0), 64 << 8);
            diff += memcmp(buf, buf2, sizeof buf) != 0;
            runs++;
        }
        for (i = 0; i < P_E0; i++)                     /* (ui.c apply_preset_to: the rest of the sound its defaults) */
            t->p[i] = TP[i].def;
        host_preset(t, ENGI_RODEO, pi);
        wrong += t->p[P_OKYB] != 0;
        wrongw += t->p[P_OPW] != (pi == 19u || pi == 23u || pi == 25u || pi == 26u || pi == 27u || pi == 31u ? 0 : 1);
    }
    rod_cut = 0;
    snprintf(w, sizeof w, "KYBD 86 %%: the old tracking (note - C4) x ROD_KTRK, every preset's values, 6 notes, "
                          "sample for sample (%u of %u differ)", diff, runs);
    ok(!diff, w);
    for (i = 0, diff = 0; i < 2401u; i++) {
        int32_t d = (int32_t)i - 1200;
        diff += rod_kt(d, ROD_KYB_OLD) != d * ROD_KTRK || rod_kt(d, 0) != 0;
    }
    ok(!diff && ROD_KTRK == 16, "KYBD 86 %: (note - C4) x ROD_KTRK (16) exactly, every note and bend (+-75 semitones); KYBD 0: none");
    ok(!wrong, "KYBD in the factory sounds: 0 on every one (INIT too)");
    ok(!wrongw, "PW0 in the factory sounds: 46.8 % on the SVF-voice type (BP SWEEP PAD, SOLO LEAD, PULSE LEAD, SYNTH BASS, SVF BASS, "
                "MORPH PLUCK), 49.5 % on the others (INIT too)");
    ok(TP[P_OPW].def == 1 && TP[P_OPW].max == 2 && !strcmp(TP[P_OPW].label, "PW0") && !strcmp(N_OPW[0], "46.8") &&
           !strcmp(N_OPW[1], "49.5") && !strcmp(N_OPW[2], "53") && ROD_PW_REST[1] == ROD_PW0 && ROD_PW_REST[2] == 0x87AE147Bu,
       "PW0: 46.8, 49.5 (the default, ROD_PW0), 53 (the width every sound had before, exactly)");
    {   /* the width measured: OSC 1's pulse alone, the filter open, 110 Hz: the share of a period above its mean */
        static const double want[3] = {0.4675, 0.495, 0.53};
        double duty[3];
        int okd = 1;
        plain(t);
        t->p[P_E0] = OW_PLS, t->p[P_E1] = OW_OFF, t->p[P_E4] = -64, t->p[P_E7] = OM_LP12;
        for (k = 0; k < 3u; k++) {
            double mean = 0;
            uint32_t hi = 0;
            t->p[P_OPW] = (int16_t)k;
            rod_direct(t, 110.0, 64 << 8);
            for (i = N / 2; i < N / 2 + 4009u; i++)          /* (10 periods: 4009 samples at 44.1 kHz) */
                mean += buf[i];
            mean /= 4009.0;
            for (i = N / 2; i < N / 2 + 4009u; i++)
                hi += buf[i] > mean;
            duty[k] = hi / 4009.0;
            okd &= fabs(duty[k] - want[k]) < 0.006;
        }
        snprintf(w, sizeof w, "PW0: the pulse rests at %.3f, %.3f, %.3f (46.75, 49.5, 53 %%)", duty[0], duty[1], duty[2]);
        ok(okd, w);
    }
    plain(t);                                          /* (LP12, RES 0, a saw: the gain at 2640 Hz, harmonic 24 / 12 / 6) */
    t->p[P_E7] = OM_LP12, t->p[P_E4] = -64;
    for (k = 0; k < 3u; k++) {
        gz[k] = gain_at(t, 45 + 12 * (int)k, 0, 40, 2640.0);
        gf[k] = gain_at(t, 45 + 12 * (int)k, 100, 40, 2640.0);
        gd[k] = gain_at(t, 45 + 12 * (int)k, ROD_KYB_OLD, 40, 2640.0);
    }
    snprintf(w, sizeof w, "KYBD 0: the filter fixed while the notes move (gain at 2640 Hz on A2 A3 A4: %.1f %.1f %.1f dB)",
             gz[0], gz[1], gz[2]);
    ok(fabs(gz[1] - gz[0]) < 0.5 && fabs(gz[2] - gz[1]) < 0.5 && gz[0] < -20.0, w);
    snprintf(w, sizeof w, "KYBD 100: the cutoff an octave an octave (LP12 at 2640 Hz: %+.1f, %+.1f dB an octave; 86 %%: %+.1f, %+.1f)",
             gf[1] - gf[0], gf[2] - gf[1], gd[1] - gd[0], gd[2] - gd[1]);
    ok(fabs(gf[1] - gf[0] - 12.0) < 1.5 && fabs(gf[2] - gf[1] - 12.0) < 1.5 && gd[1] - gd[0] > 8.5 && gd[1] - gd[0] < gf[1] - gf[0], w);
    {   /* full: 16 x 100 / 86 = 18.6 steps (Q8) a sixteenth, CUTOFF_HZ's 18.69 */
        double oct = (double)rod_kt(192, 100) / 256.0 / (127.0 / log2(16000.0 / 30.0));
        snprintf(w, sizeof w, "KYBD 100: %.3f octaves of cutoff an octave of the keys", oct);
        ok(fabs(oct - 1.0) < 0.01, w);
    }
    for (i = 0; i < NPAGES; i++)                       /* EDIT 3 with KYBD: on RODEO, not on another engine */
        if (PAGES[i].id[0] == P_OKYB) {
            uint32_t s = song.sel;
            trk[s].eng_req = ENGI_RODEO;
            g1[0] = page_hidden(i);
            trk[s].eng_req = ENGI_PONY;
            g1[1] = page_hidden(i);
            trk[s].eng_req = ENGI_MARE;
            g1[2] = page_hidden(i);
            page += !g1[0] && g1[1] && g1[2] && !strcmp(PAGES[i].title, "EDIT 3");
        }
    ok(page == 1u, "EDIT 3 KYBD: shown on RODEO, hidden on PONY and MARE (one page)");
}
static void t_kybd_stored(void)
{
    static project_t pj;
    track_t *a = &trk[0], *m = &trk[1];
    int okp, okm, okd, oko;
    host_tracks_init();
    host_preset(a, ENGI_RODEO, 26);                     /* SYNTH BASS */
    host_preset(m, ENGI_MARE, 0);
    a->p[P_OKYB] = 37;
    m->p[P_MFFM] = 7, m->p[P_MRING] = -12, m->p[P_MW34] = 85;
    proj_capture(&pj);
    a->p[P_OKYB] = 99, m->p[P_OKYB] = 5;
    m->p[P_MFFM] = m->p[P_MRING] = -25, m->p[P_MW34] = 0;
    proj_apply(&pj, 1);
    okp = a->p[P_OKYB] == 37, okm = m->p[P_MFFM] == 7 && m->p[P_MRING] == -12 && m->p[P_MW34] == 85;
    oko = m->p[P_OKYB] == 0;
    ok(okp && okm && oko, "a project keeps RODEO's KYBD (part 1: 37 %) and MARE's VCO3 VCO4 WAV34 (part 2; its KYBD the default)");
    a->p[P_OKYB] = 0;
    proj_capture(&pj);
    a->p[P_OKYB] = 50;
    proj_apply(&pj, 1);
    ok(a->p[P_OKYB] == 0, "a project keeps KYBD 0");
    {   /* the block as a build before KYBD wrote it: MARE's VCO3 byte at its default (0) for a RODEO sound */
        proj_xs_t *x = &proj_xss[proj_x10_at(&pj)];
        x->xm[0] = 0;
        x->xchk = proj_hash(x, __builtin_offsetof(proj_xs_t, xchk));
        a->p[P_OKYB] = 12;
        proj_apply(&pj, 1);
        okd = a->p[P_OKYB] == 0 && a->p[P_MFFM] == TP[P_MFFM].def;
        ok(okd, "a project block from before KYBD: RODEO's default, 0");
    }
    {   /* a user preset */
        up_rec_t r;
        int16_t v[P_COUNT], out[P_COUNT], def[P_COUNT];
        uint32_t i, kv;
        int same = 1;
        for (kv = 0; kv <= 100u; kv += 50u) {
            for (i = 0; i < P_COUNT; i++)
                def[i] = i >= P_E0 ? 0 : TP[i].def, v[i] = def[i];
            v[P_OKYB] = (int16_t)kv;
            memset(&r, 0, sizeof r);
            r.used = UP_USED, r.name[0] = 'R', r.engine = ENGI_RODEO;
            up_rec_set(&r, v);
            up_params(&r, out, def);
            for (i = 0; i < P_COUNT; i++)
                same &= out[i] == v[i];
        }
        ok(same, "a user preset keeps KYBD (0, 50, 100 %)");
    }
}

int main(void)
{
    host_tracks_init();
    t_engine();
    t_osc();
    t_filter();
    t_sem();
    t_lp24_res();
    t_sync();
    t_xmod();
    t_bounded();
    t_presets();
    t_kybd();
    t_kybd_stored();
    t_selfosc();
    if (!getenv("ROD_NOCPU"))
        t_cpu();
    printf(bad ? "rodeo: %d FAILED\n" : "rodeo: all passed\n", bad);
    return bad;
}
