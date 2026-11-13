/* SPDX-License-Identifier: GPL-3.0-only */
/* PALOMINO (firmware/src/eng_palomino.c, after the early-1980s flagship analog poly's voice; CLIPPER_SYNTH 1):
 *   build/host/palomino_test
 *   engine     the 24th engine, after VECTOR (the stores' numbers kept), named PALOMINO
 *   presets    every one bounded, audible, its voices free after the release (a chord through the whole mix)
 *   VCO-1      in tune (a saw at A4: 440 Hz); VCO-2's interval (+12: 880 Hz); MIX: -64 VCO-1 alone, +63 VCO-2 alone
 *   XMOD       VCO-2 bends VCO-1's pitch at audio rate: sidebands (energy off VCO-1's harmonics) come with XMOD
 *   SYNC       VCO-2 slaved to VCO-1: alone and a fifth up, it plays at VCO-1's pitch; SHAPE sweeps its timbre,
 *              not its pitch; band-limited (a synced high note: little energy off its harmonics)
 *   VCF        the OTA filter: 12 dB brighter than 24 dB above the cutoff
 *   waves      the original's six on both VCOs (W1 + SINE NOISE, W2 + TRI SQR, appended): each its spectrum, the new
 *              ones band-limited at C7, VCO-1's NOISE its own; SYNC with every W2 (NOISE: none to restart) and with
 *              VCO-1 on NOISE (its phase runs on); XMOD from every W2; every pair bounded
 *   P>VCO      the LFO's and ENV's pitch (vmod_t pmod) to BOTH VCOs, VCO1 or VCO2 alone; the rest of the pitch to both;
 *              through voice.c: an LFO on VCO-1 only leaves VCO-2 steady
 *   bounded    XMOD and RES at the top, SYNC, a loud chord
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

static int bad;
static void ok(int c, const char *what)
{
    printf("palomino: %-92s %s\n", what, c ? "ok" : "FAIL");
    bad += !c;
}
static double db(double x) { return 20.0 * log10(x > 1e-12 ? x : 1e-12); }

#define N 16384
static int32_t buf[N];

/* one voice straight through palomino_render; shape: SHAPE (0..127 << 8, 64 << 8 the middle); pal_pmod: the LFO's and
 * ENV's pitch in it (vmod_t pmod, 1/4096 semitone, a multiple of 256), on top of hz as voice.c puts it */
static int32_t pal_pmod;
static void pal_direct(track_t *t, double hz, int32_t shape)
{
    voice_t v;
    vmod_t m;
    uint32_t b;
    int32_t p16 = (int32_t)lrint((69.0 + 12.0 * log2(hz / 440.0)) * 16.0);
    memset(&v, 0, sizeof v);
    memset(&m, 0, sizeof m);
    v.ph[2] = 0x2545F491u;
    v.pitch16 = p16;
    palomino_note_on(t, &v);                          /* (the noises' seeds; ph[2] kept) */
    m.inc = (uint32_t)(hz * pow(2.0, pal_pmod / 49152.0) / FS * 4294967296.0);
    m.pitch16 = p16 + (pal_pmod >> 8);
    m.pmod = pal_pmod;
    m.amp0 = m.amp1 = 32767;
    m.shape = shape;
    memset(buf, 0, sizeof buf);
    for (b = 0; b + CTL <= N; b += CTL)
        palomino_render(t, &v, buf + b, CTL, &m);
}
/* VCO-2's frequency under pal_direct(hz): the note in 1/16 semitones, + semi, + its 5 cents (hy_inc 205) */
static double vco2_hz(double hz, int semi)
{
    double p16 = (double)lrint((69.0 + 12.0 * log2(hz / 440.0)) * 16.0);
    return 440.0 * pow(2.0, (p16 / 16.0 + semi + 205.0 / 4096.0 - 69.0) / 12.0);
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
/* the share of the energy of buf[a .. a + n) off the harmonics of f0 (a DFT, bins within 1.5 bins of k f0 count as
 * on) */
static double off_harmonics(double f0, uint32_t a, uint32_t n)
{
    static double re[N], im[N];
    double on = 0, all = 0;
    uint32_t k, i, nb = n / 2;
    for (k = 1; k < nb; k++) {
        double f = (double)k * FS / n, h = f / f0, e;
        re[k] = im[k] = 0;
        for (i = 0; i < n; i += 1) {
            double w = 0.5 - 0.5 * cos(2 * M_PI * i / n), x = buf[a + i] * w, ph = 2 * M_PI * k * i / n;
            re[k] += x * cos(ph);
            im[k] += x * sin(ph);
        }
        e = re[k] * re[k] + im[k] * im[k];
        all += e;
        if (fabs(h - floor(h + 0.5)) * f0 < 1.5 * FS / n)
            on += e;
    }
    return all > 0 ? 1.0 - on / all : 0;
}
static void plainj(track_t *t)
{
    host_preset(t, ENGI_PALOMINO, 0);
    t->p[P_E0] = PV1_SAW, t->p[P_E1] = PV2_SAW, t->p[P_E2] = 0, t->p[P_E3] = 0;
    t->p[P_E4] = -64, t->p[P_E5] = 127, t->p[P_E6] = 0, t->p[P_E7] = PVM_24;
}

static void t_engine(void)
{
    ok(NENGINES >= 24u && ENGI_PALOMINO == 23u && ENGINES[ENGI_PALOMINO] == &ENG_PALOMINO &&
           !strcmp(ENG_PALOMINO.name, "PALOMINO"), "the 24th engine, after VECTOR, named PALOMINO");
}

static void run_mix(uint32_t nb, double *peak, int *inb)
{
    static int32_t o[CTL * 2];
    uint32_t b, i;
    double e = 0, best = 0;
    uint32_t cnt = 0;
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
    for (pi = 0; pi < ENG_PALOMINO.npresets; pi++) {
        double r;
        int inb, inb2;
        for (i = 0; i < NPART; i++)
            trk_all_off(&trk[i]);
        run_mix(600, &r, &inb);
        host_preset(&trk[0], ENGI_PALOMINO, pi);
        trk[0].p[P_AMODE] = 0;
        for (i = 0; i < 3u; i++)
            trk_note_on(&trk[0], ch[i], 100);
        run_mix(1400, &r, &inb);
        for (i = 0; i < 3u; i++)
            trk_note_off(&trk[0], ch[i]);
        for (i = 0; i < 40u && sounding(&trk[0]); i++) {
            double r2;
            run_mix(500, &r2, &inb2);
            inb = inb && inb2;
        }
        if (!inb)
            good = 0, printf("  %s: out of bounds\n", ENG_PALOMINO.presets[pi].name);
        if (r < 300.0)
            loud = 0, printf("  %s: quiet (%.0f)\n", ENG_PALOMINO.presets[pi].name, r);
        if (sounding(&trk[0]))
            freed = 0, printf("  %s: %u voices left\n", ENG_PALOMINO.presets[pi].name, (unsigned)sounding(&trk[0]));
    }
    snprintf(w, sizeof w, "all %u presets bounded, audible, their voices free after the release",
             (unsigned)ENG_PALOMINO.npresets);
    ok(good && loud && freed && ENG_PALOMINO.npresets >= 12u, w);
}

static void t_vco(void)
{
    track_t *t = &trk[0];
    double a, b, c, d;
    char w[160];
    plainj(t);
    pal_direct(t, 440.0, 64 << 8);
    a = mag(440, N / 2, N / 2), b = mag(415.3, N / 2, N / 2), c = mag(466.2, N / 2, N / 2);
    ok(a > 8 * b && a > 8 * c, "VCO-1: in tune (a saw at A4: 440 Hz, not beside it)");
    t->p[P_E4] = 63, t->p[P_E2] = 12;                  /* VCO-2 alone, an octave up */
    pal_direct(t, 440.0, 64 << 8);
    a = mag(880, N / 2, N / 2), b = mag(440, N / 2, N / 2);
    snprintf(w, sizeof w, "VCO-2 +12, MIX +63: 880 Hz, no 440 (%.0f vs %.0f)", a, b);
    ok(a > 20 * b, w);
    t->p[P_E4] = -64;                                  /* VCO-1 alone again: no 880-only part */
    pal_direct(t, 440.0, 64 << 8);
    c = mag(440, N / 2, N / 2), d = mag(660, N / 2, N / 2);
    t->p[P_E2] = 7, t->p[P_E4] = 0;                    /* both, a fifth apart: 660 comes in */
    pal_direct(t, 440.0, 64 << 8);
    snprintf(w, sizeof w, "MIX -64 VCO-1 alone (660 Hz %.1f dB under 440), MIX 0 both (660 Hz %.1f dB)",
             db(d / c), db(mag(660, N / 2, N / 2) / mag(440, N / 2, N / 2)));
    ok(db(d / c) < -30.0 && db(mag(660, N / 2, N / 2) / mag(440, N / 2, N / 2)) > -12.0, w);
}

static void t_xmod(void)
{
    track_t *t = &trk[0];
    double o0, o1;
    char w[160];
    plainj(t);
    t->p[P_E0] = PV1_TRI, t->p[P_E1] = PV2_SIN, t->p[P_E2] = 7;   /* a triangle, the sine a fifth up */
    pal_direct(t, 220.0, 64 << 8);
    o0 = off_harmonics(220.0, N / 2, 4096);
    t->p[P_E3] = 100;
    pal_direct(t, 220.0, 64 << 8);
    o1 = off_harmonics(220.0, N / 2, 4096);
    snprintf(w, sizeof w, "XMOD 100: sidebands off VCO-1's harmonics (%.1f %% of the energy, %.2f %% without)",
             100 * o1, 100 * o0);
    ok(o0 < 0.03 && o1 > 0.2, w);                   /* (o0: the window's leakage, 220 Hz is not on a bin) */
}

static void t_sync(void)
{
    track_t *t = &trk[0];
    double a, b, o, s0, s1;
    char w[200];
    plainj(t);
    t->p[P_E4] = 63, t->p[P_E2] = 7, t->p[P_E7] = PVM_24S;   /* VCO-2 alone, a fifth up, synced */
    pal_direct(t, 220.0, 64 << 8);
    a = mag(220, N / 2, N / 2), b = mag(329.6, N / 2, N / 2);
    o = off_harmonics(220.0, N / 2, 4096);
    snprintf(w, sizeof w, "SYNC: VCO-2 alone, a fifth up, plays at VCO-1's 220 Hz (%.0f vs 330 Hz %.0f; %.2f %% off)",
             a, b, 100 * o);
    ok(a > 4 * b && o < 0.02, w);
    s0 = mag(660, N / 2, N / 2);
    pal_direct(t, 220.0, (64 + 40) << 8);              /* SHAPE up: VCO-2 higher under the sync */
    s1 = mag(660, N / 2, N / 2);
    o = off_harmonics(220.0, N / 2, 4096);
    snprintf(w, sizeof w, "SYNC: SHAPE sweeps the timbre (660 Hz %.1f dB), the pitch stays (%.2f %% off)", db(s1 / s0), 100 * o);
    ok(fabs(db(s1 / s0)) > 3.0 && o < 0.02, w);
    t->p[P_E2] = 5;
    pal_direct(t, 2093.0, (64 + 30) << 8);             /* C7, synced: band-limited */
    o = off_harmonics(2093.0, N / 2, 4096);
    snprintf(w, sizeof w, "SYNC at C7: band-limited (%.2f %% of the energy off its harmonics)", 100 * o);
    ok(o < 0.03, w);
}

static void t_slope(void)
{
    track_t *t = &trk[0];
    double h24, h12;
    char w[160];
    plainj(t);
    t->p[P_E5] = 50;
    pal_direct(t, 110.0, 64 << 8);
    h24 = mag(2200, N / 2, N / 2);
    t->p[P_E7] = PVM_12;
    pal_direct(t, 110.0, 64 << 8);
    h12 = mag(2200, N / 2, N / 2);
    snprintf(w, sizeof w, "VCF 12 dB brighter than 24 dB above the cutoff (2.2 kHz %+.1f dB)", db(h12 / h24));
    ok(db(h12 / h24) > 10.0, w);
}

/* the normalised correlation of buf[a .. a + n) and c[] */
static double corr(const int32_t *c, uint32_t a, uint32_t n)
{
    double xy = 0, xx = 0, yy = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        xy += (double)buf[a + i] * c[i];
        xx += (double)buf[a + i] * buf[a + i];
        yy += (double)c[i] * c[i];
    }
    return xx > 0 && yy > 0 ? xy / sqrt(xx * yy) : 1.0;
}
static void t_waves(void)
{
    static int32_t keep[N / 2];
    static const char *const n1[6] = {"TRI", "SAW", "PLS", "SQR", "SINE", "NOISE"};
    static const char *const n2[6] = {"SINE", "SAW", "PLS", "NOISE", "TRI", "SQR"};
    track_t *t = &trk[0];
    double a, b, c, o;
    uint32_t k, w1, w2, m, i, good;
    int32_t mx = 0;
    char w[200];
    good = ENG_PALOMINO.edit[0].max == 5 && ENG_PALOMINO.edit[1].max == 5;
    for (k = 0; good && k < 6u; k++)
        good = !strcmp(ENG_PALOMINO.edit[0].names[k], n1[k]) && !strcmp(ENG_PALOMINO.edit[1].names[k], n2[k]);
    ok(good, "W1 TRI SAW PLS SQR + SINE NOISE, W2 SINE SAW PLS NOISE + TRI SQR (appended: the stores' values kept)");
    plainj(t);
    t->p[P_E0] = PV1_SIN;                              /* VCO-1 SINE alone: no harmonics */
    pal_direct(t, 440.0, 64 << 8);
    a = mag(440, N / 2, N / 2), b = mag(880, N / 2, N / 2), c = mag(1320, N / 2, N / 2);
    snprintf(w, sizeof w, "VCO-1 SINE: 440 Hz, no harmonics (2nd %.1f dB, 3rd %.1f dB)", db(b / a), db(c / a));
    ok(db(b / a) < -40.0 && db(c / a) < -40.0, w);
    t->p[P_E4] = 63, t->p[P_E1] = PV2_TRI;             /* VCO-2 TRI alone: odd harmonics, the 3rd at 1/9 */
    pal_direct(t, 220.0, 64 << 8);
    a = mag(220, N / 2, N / 2), b = mag(440, N / 2, N / 2), c = mag(660, N / 2, N / 2);
    snprintf(w, sizeof w, "VCO-2 TRI: odd harmonics (3rd %.1f dB, -19 the ideal; 2nd %.1f dB)", db(c / a), db(b / a));
    ok(fabs(db(c / a) + 19.1) < 1.5 && db(b / a) < -40.0, w);
    t->p[P_E1] = PV2_SQR;                              /* VCO-2 SQR: the 3rd at 1/3, whatever SHAPE; the 2nd about
                                                       * -30 dB (PAL_PW0: the original's 49 % rest width) */
    pal_direct(t, 220.0, (64 + 50) << 8);
    a = mag(220, N / 2, N / 2), b = mag(440, N / 2, N / 2), c = mag(660, N / 2, N / 2);
    snprintf(w, sizeof w, "VCO-2 SQR: a 49 %% square, SHAPE aside (3rd %.1f dB, -9.5 the ideal; 2nd %.1f dB, -30 at 49 %%)",
             db(c / a), db(b / a));
    ok(fabs(db(c / a) + 9.5) < 1.5 && fabs(db(b / a) + 30.0) < 3.0, w);
    plainj(t);                                         /* the new pitched ones at C7: band-limited */
    t->p[P_E0] = PV1_SIN;
    pal_direct(t, 2093.0, 64 << 8);
    o = off_harmonics(2093.0, N / 2, 4096);
    t->p[P_E4] = 63, t->p[P_E1] = PV2_TRI;
    pal_direct(t, 2093.0, 64 << 8);
    a = off_harmonics(vco2_hz(2093.0, 0), N / 2, 4096);
    t->p[P_E1] = PV2_SQR;
    pal_direct(t, 2093.0, 64 << 8);
    c = off_harmonics(vco2_hz(2093.0, 0), N / 2, 4096);
    snprintf(w, sizeof w, "C7, band-limited: VCO-1 SINE, VCO-2 TRI, SQR (%.2f, %.2f, %.2f %% off the harmonics)",
             100 * o, 100 * a, 100 * c);
    ok(o < 0.03 && a < 0.03 && c < 0.03, w);
    plainj(t);                                         /* the two NOISEs: VCO-1's its own */
    t->p[P_E0] = PV1_NOIS, t->p[P_E1] = PV2_NOIS;
    pal_direct(t, 220.0, 64 << 8);
    o = off_harmonics(220.0, N / 2, 4096);
    memcpy(keep, buf + N / 2, sizeof keep);
    t->p[P_E4] = 63;
    pal_direct(t, 220.0, 64 << 8);
    c = corr(keep, N / 2, N / 2);
    snprintf(w, sizeof w, "VCO-1 NOISE: broadband (%.0f %% off 220 Hz's harmonics), not VCO-2's (correlation %.3f)",
             100 * o, c);
    ok(o > 0.6 && fabs(c) < 0.05, w);
    for (k = 0, good = 1; k < 6u; k++) {               /* SYNC with every W2: at VCO-1's pitch */
        if (k == PV2_NOIS)
            continue;
        plainj(t);
        t->p[P_E4] = 63, t->p[P_E2] = 7, t->p[P_E7] = PVM_24S, t->p[P_E1] = (int16_t)k;
        pal_direct(t, 220.0, (64 + 20) << 8);
        o = off_harmonics(220.0, N / 2, 4096);
        if (o > 0.03)
            good = 0, printf("  SYNC, W2 %s: %.2f %% off\n", n2[k], 100 * o);
    }
    plainj(t);                                         /* .. and VCO-1 on NOISE: its phase still restarts VCO-2 */
    t->p[P_E0] = PV1_NOIS, t->p[P_E4] = 63, t->p[P_E2] = 7, t->p[P_E7] = PVM_12S, t->p[P_E1] = PV2_TRI;
    pal_direct(t, 220.0, (64 + 20) << 8);
    o = off_harmonics(220.0, N / 2, 4096);
    snprintf(w, sizeof w, "SYNC with every W2 at VCO-1's pitch, VCO-1 on NOISE too (%.2f %% off)", 100 * o);
    ok(good && o < 0.03, w);
    for (k = 0, good = 1; k < 6u; k++) {               /* XMOD from every W2: VCO-1 moves */
        double o0;
        plainj(t);
        t->p[P_E0] = PV1_TRI, t->p[P_E1] = (int16_t)k, t->p[P_E2] = 7;
        pal_direct(t, 220.0, 64 << 8);
        o0 = off_harmonics(220.0, N / 2, 4096);
        t->p[P_E3] = 100;
        pal_direct(t, 220.0, 64 << 8);
        o = off_harmonics(220.0, N / 2, 4096);
        if (!(o > o0 + 0.1))
            good = 0, printf("  XMOD, W2 %s: %.2f %% off (%.2f %% without)\n", n2[k], 100 * o, 100 * o0);
    }
    ok(good, "XMOD from every W2 (NOISE too) bends VCO-1 (sidebands off its harmonics)");
    for (w1 = 0; w1 < 6u; w1++)                        /* every pair, both slopes, SYNC, XMOD and RES at the top */
        for (w2 = 0; w2 < 6u; w2++)
            for (m = 0; m < 4u; m++) {
                plainj(t);
                t->p[P_E0] = (int16_t)w1, t->p[P_E1] = (int16_t)w2, t->p[P_E7] = (int16_t)m;
                t->p[P_E3] = 127, t->p[P_E6] = 127, t->p[P_E4] = 0, t->p[P_E2] = 19;
                pal_direct(t, 55.0 * (m + 1), (64 + 63) << 8);
                for (i = 0; i < N; i++)
                    if (abs(buf[i]) > mx)
                        mx = abs(buf[i]);
            }
    snprintf(w, sizeof w, "every W1 x W2 x MODE, XMOD and RES at the top: bounded (peak %d)", mx);
    ok(mx < (1 << 21), w);
}

/* VCO-2's frequency at a note (1/16 semitones) over a second of palomino_render: its phase's travel */
static double vco2_travel(track_t *t, int32_t p16, double *ph1hz)
{
    voice_t v;
    vmod_t m;
    uint32_t b, o2, o1;
    double d2 = 0, d1 = 0;
    memset(&v, 0, sizeof v);
    memset(&m, 0, sizeof m);
    v.pitch16 = m.pitch16 = p16;
    m.inc = pitch_inc((uint32_t)p16);
    m.amp0 = m.amp1 = 32767;
    m.shape = 64 << 8;
    palomino_note_on(t, &v);
    for (b = 0; b + CTL <= (uint32_t)FS; b += CTL) {
        o2 = v.ph[1], o1 = v.ph[0];
        memset(buf, 0, CTL * sizeof buf[0]);
        palomino_render(t, &v, buf, CTL, &m);
        d2 += (double)(uint32_t)(v.ph[1] - o2);
        d1 += (double)(uint32_t)(v.ph[0] - o1);
    }
    *ph1hz = d1 / 4294967296.0 * FS / b;
    return d2 / 4294967296.0 * FS / b;
}
static void t_low(void)
{
    const param_desc_t *d = &ENG_PALOMINO.edit[2];
    track_t *t = &trk[0];
    char val[8], val2[8], val3[8], w[200];
    const char *u, *u2, *u3;
    double c4, c5, h1, lo = 1e9, hi = 0, o;
    uint32_t i, wv, m;
    int32_t mx = 0;
    param_format(d, -25, val, &u);
    param_format(d, -24, val2, &u2);
    param_format(d, 7, val3, &u3);
    snprintf(w, sizeof w, "VCO2: -25 .. +24, the bottom shown LOW (\"%s%s\", \"%s %s\", \"%s %s\")", val, u, val2, u2, val3, u3);
    ok(d->min == -25 && d->max == 24 && d->def == 0 && !strcmp(val, "LOW") && !*u && !strcmp(val2, "-24") &&
       !strcmp(u2, "st") && !strcmp(val3, "+7"), w);
    plainj(t);
    t->p[P_E2] = -25, t->p[P_E4] = 63;
    c4 = vco2_travel(t, 60 * 16, &h1);
    c5 = vco2_travel(t, 72 * 16, &h1);
    snprintf(w, sizeof w, "LOW: VCO-2 8 octaves down, following the keys (C4 %.3f Hz, C5 %.3f Hz; VCO-1 at C5 %.1f Hz)", c4, c5, h1);
    ok(fabs(c4 - 261.63 / 256.0) < 0.01 && fabs(c5 / c4 - 2.0) < 0.01 && fabs(h1 - 523.25) < 1.0, w);
    plainj(t);                                         /* XMOD from a LOW sine: VCO-1's pitch wobbles at about 1.7 Hz */
    t->p[P_E1] = PV2_SIN, t->p[P_E2] = -25, t->p[P_E3] = 4;
    {   /* VCO-1's mean frequency per window of 1024 samples, from its phase's travel */
        voice_t v;
        vmod_t mm;
        uint32_t b, j;
        memset(&v, 0, sizeof v);
        memset(&mm, 0, sizeof mm);
        v.pitch16 = mm.pitch16 = 69 * 16;
        mm.inc = pitch_inc(69 * 16);
        mm.amp0 = mm.amp1 = 32767;
        mm.shape = 64 << 8;
        palomino_note_on(t, &v);
        for (b = 0; b < 40u; b++) {
            double tr = 0, f;
            for (j = 0; j < 1024u; j += CTL) {
                uint32_t o1 = v.ph[0];
                memset(buf, 0, CTL * sizeof buf[0]);
                palomino_render(t, &v, buf, CTL, &mm);
                tr += (double)(uint32_t)(v.ph[0] - o1);
            }
            f = tr / 4294967296.0 * FS / 1024.0;
            lo = f < lo ? f : lo, hi = f > hi ? f : hi;
        }
    }
    snprintf(w, sizeof w, "LOW + XMOD 4: VCO-1 wobbles slowly (A4 between %.1f and %.1f Hz over 1 s)", lo, hi);
    ok(lo < 430.0 && hi > 450.0 && hi < 600.0 && lo > 300.0, w);
    plainj(t);                                         /* SYNC: VCO-2 LOW alone, restarted at VCO-1's pitch */
    t->p[P_E2] = -25, t->p[P_E4] = 63, t->p[P_E7] = PVM_24S;
    pal_direct(t, 220.0, (64 + 63) << 8);
    o = off_harmonics(220.0, N / 2, 4096);
    snprintf(w, sizeof w, "LOW + SYNC: VCO-2 restarted at VCO-1's 220 Hz (%.2f %% off its harmonics, 220 Hz %.0f)", 100 * o,
             mag(220, N / 2, N / 2));
    ok(o < 0.03 && mag(220, N / 2, N / 2) > 1.0, w);
    for (wv = 0; wv < 6u; wv++)                        /* every W2, both slopes, SYNC, XMOD and RES at the top: bounded */
        for (m = 0; m < 4u; m++) {
            plainj(t);
            t->p[P_E1] = (int16_t)wv, t->p[P_E2] = -25, t->p[P_E7] = (int16_t)m;
            t->p[P_E3] = 127, t->p[P_E6] = 127, t->p[P_E4] = 0;
            pal_direct(t, 55.0 * (m + 1), (64 + 63) << 8);
            for (i = 0; i < N; i++)
                if (abs(buf[i]) > mx)
                    mx = abs(buf[i]);
        }
    snprintf(w, sizeof w, "LOW with every W2 x MODE, XMOD and RES at the top: bounded (peak %d)", mx);
    ok(mx < (1 << 21), w);
}

static void t_ptgt(void)
{
    static const char *const nm[3] = {"BOTH", "VCO1", "VCO2"};
    track_t *t = &trk[0];
    uint32_t k, good = 1;
    char w[200];
    ok(!strcmp(TP[P_PTGT].label, "P>VCO") && TP[P_PTGT].def == PT_BOTH && TP[P_PTGT].max == PT_VCO2,
       "P>VCO: BOTH (the default: as before), VCO1, VCO2");
    for (k = 0; k < 3u; k++) {                         /* an octave of LFO / ENV pitch on 220 Hz: which VCO takes it */
        double a1, b1, a2, b2;
        int up1 = k != PT_VCO2, up2 = k != PT_VCO1;
        plainj(t);
        t->p[P_PTGT] = (int16_t)k;
        pal_pmod = 12 * 4096;
        pal_direct(t, 220.0, 64 << 8);                  /* VCO-1 alone */
        a1 = mag(220, N / 2, N / 2), b1 = mag(440, N / 2, N / 2);
        t->p[P_E4] = 63;                               /* VCO-2 alone */
        pal_direct(t, 220.0, 64 << 8);
        a2 = mag(vco2_hz(220.0, 0), N / 2, N / 2), b2 = mag(vco2_hz(440.0, 0), N / 2, N / 2);
        pal_pmod = 0;
        if ((up1 ? !(b1 > 8 * a1) : !(a1 > 1.5 * b1)) || (up2 ? !(b2 > 8 * a2) : !(a2 > 1.5 * b2)))   /* (a saw: its 2nd at half) */
            good = 0, printf("  %s: VCO-1 220 / 440 Hz %.0f / %.0f, VCO-2 %.0f / %.0f\n", nm[k], a1, b1, a2, b2);
    }
    ok(good, "P>VCO: the LFO / ENV octave on both (BOTH), on VCO-1 alone (VCO1), on VCO-2 alone (VCO2)");
    {   /* through voice.c: a deep fast LFO on the pitch, VCO-2 alone: steady with VCO1, smeared with BOTH */
        static int32_t o[CTL * 2];
        double off[2];
        uint32_t b, i, j;
        for (j = 0; j < 2u; j++) {
            trk_all_off(t);
            plainj(t);
            t->p[P_E4] = 63, t->p[P_E5] = 127;
            t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
            t->p[P_SPREAD] = t->p[P_VINT] = 0, t->p[P_AMODE] = 0, t->p[P_VOICE] = V_POLY;
            t->p[P_ATK] = 0, t->p[P_SUS] = 127;
            t->p[P_LD_PIT] = 63, t->p[P_LRATE] = 100, t->p[P_ED_PIT] = 40;
            t->p[P_PTGT] = (int16_t)(j ? PT_VCO1 : PT_BOTH);
            trk_note_on(t, 57, 100);                   /* A3: 220 Hz */
            for (b = 0; b + CTL <= N; b += CTL) {
                mix_block(o, CTL);
                for (i = 0; i < CTL; i++)
                    buf[b + i] = o[2u * i];
            }
            trk_note_off(t, 57);
            off[j] = off_harmonics(vco2_hz(220.0, 0), N / 2, 4096);
        }
        snprintf(w, sizeof w, "P>VCO through voice.c: the LFO on VCO-1 only leaves VCO-2 steady (%.1f %% off its harmonics, "
                 "%.1f %% with BOTH)", 100 * off[1], 100 * off[0]);
        ok(off[1] < 0.03 && off[0] > 0.2, w);
        trk_all_off(t);
    }
}

static void t_bounded(void)
{
    track_t *t = &trk[0];
    uint32_t i, m, good = 1;
    int32_t mx = 0;
    for (m = 0; m < 4u; m++) {
        plainj(t);
        t->p[P_E3] = 127, t->p[P_E6] = 127, t->p[P_E7] = (int16_t)m, t->p[P_E4] = 0, t->p[P_E2] = 19;
        pal_direct(t, 55.0 * (m + 1), (64 + 63) << 8);
        for (i = 0; i < N; i++)
            if (abs(buf[i]) > mx)
                mx = abs(buf[i]);
    }
    good = mx < (1 << 21);
    {
        char w[160];
        snprintf(w, sizeof w, "XMOD and RES at the top, SYNC on and off, both slopes: bounded (peak %d)", mx);
        ok(good, w);
    }
}

int main(void)
{
    host_tracks_init();
    t_engine();
    t_vco();
    t_xmod();
    t_sync();
    t_slope();
    t_waves();
    t_low();
    t_ptgt();
    t_bounded();
    t_presets();
    printf(bad ? "palomino: %d FAILED\n" : "palomino: all passed\n", bad);
    return bad;
}
