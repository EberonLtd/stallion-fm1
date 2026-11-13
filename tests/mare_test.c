/* SPDX-License-Identifier: GPL-3.0-only */
/* MARE (firmware/src/eng_mare.c, after a 2010s semi-modular paraphonic analog; CLIPPER_SYNTH 1):
 *   build/host/mare_test [nocost]
 *   engine     the 27th engine, after RODEO (the stores' numbers kept), named MARE (and in the ENG list), both cores
 *   VCOs       in tune (a saw at A4: 440 Hz); VCO2 +7: its fifth beside VCO 1
 *   WAVE       TRI (odd harmonics, the third ~-19 dB), SAW (the second -6 dB), SQR (no second, the third ~-9.5 dB), the
 *              narrow PLS (the second near the first); SHAPE moves it
 *   DTUNE      127: 20 cents down and up, not the note
 *   SYNC       VCO 2 synced: only VCO 1's harmonics (none at VCO 2's own pitch); FM: the sum and difference sidebands
 *   VCF        the ladder's low-pass ~24 dB an octave, its resonance's peak; STER with SPACE: the left side brighter
 *              (SPACE up), or the sides mixed toward the middle (SPACE far down: the left nearly closed), SPACE 0 no side
 *              at all; PARA: the high-pass and the low-pass summed, mono; SERI: a band, mono
 *   noise      +N: energy between the harmonics
 *   clean      a note from silence starts without a click; a note's output has no DC; silent once released
 *   bounded    RES at the top, the cutoffs at their ends, every wave, mode, SY/FM and SPACE, a loud chord
 *   unison     UNISON: every voice its own phases and noise state (uni_phases), VCO 2 in step with its VCO 1
 *   presets    every one bounded, audible, its voices free after the release; the SPACE presets wide (L != R),
 *              PARA's and SERI's mono
 *   PARA ASSIGN  LOW / HIGH: a chord the same VCO pitches in every key order and after any phrase; ROUND ROBIN
 *              ON turns the VCOs a strike, OFF repeats (MENU > SEQ)
 *   stores     a project and a user preset keep engine 26 and its values (SY/FM, SPACE below 0); RODEO's (25) as before
 *   cost       host instructions per sample and voice (8 voices held), every preset, against PONY, PALOMINO, STAMPEDE, RODEO
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
#define PROJ_HOST 1
static const uint8_t TRK_DEF[NPART][2] = {{ENGI_HYBRID, 0}, {1, 0}, {4, 5}};   /* (ui.c, Clipper) */
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? TRK_DEF[i][0] : 0u; }
#include "../firmware/src/project.c"
#define UP_HOST 1
#include "../firmware/src/upreset.c"

#define MAR_COST_MAX 420.0        /* host instructions per sample and voice, the heaviest preset (~300 on an Apple M host;
                                  * PALOMINO ~270, PONY ~315 measured alongside; a preset with
                                  * VCO 3 and 4 on: fm1-emu 381 q a voice; the admission counts them,
                                  * voice.c VC_MT_VCO, so the hardware stays in its budget with fewer voices) */

static int bad;
static void ok(int c, const char *what)
{
    printf("mare: %-100s %s\n", what, c ? "ok" : "FAIL");
    bad += !c;
}
static double db(double x) { return 20.0 * log10(x > 1e-12 ? x : 1e-12); }

#define N 16384
static int32_t buf[N], side[N];

/* one voice straight through mare_render at hz (pitch16: its note, for the keyboard tracking), the side kept */
static int32_t md_ffm, md_ring;                      /* the matrix's FLTFM and RING for mar_direct (Q15) */
static void mar_direct(track_t *t, double hz, int32_t p16, int32_t shape)
{
    voice_t v;
    vmod_t m;
    uint32_t b, i;
    memset(&v, 0, sizeof v);
    memset(&m, 0, sizeof m);
    m.ffm = md_ffm, m.ring = md_ring;
    v.pitch16 = p16;
    v.stage = 2;
    m.inc = (uint32_t)(hz / FS * 4294967296.0);
    m.pitch16 = p16;
    m.amp0 = m.amp1 = 32767;
    m.shape = shape;
    mare_note_on(t, &v);
    t->f2v = F2V_ESIDE;
    memset(buf, 0, sizeof buf);
    for (b = 0; b + CTL <= N; b += CTL) {
        for (i = 0; i < CTL; i++)
            f2_sd[0][i] = 0;
        mare_render(t, &v, buf + b, CTL, &m);
        for (i = 0; i < CTL; i++)
            side[b + i] = f2_sd[0][i];
    }
    t->f2v = 0;
}
static int32_t p16_of(double hz) { return (int32_t)lrint((69.0 + 12.0 * log2(hz / 440.0)) * 16.0); }
/* the magnitude at f of x[a .. a + n) (Hann window); ch: 0 the middle, 1 the left (mid + side), 2 the right */
static double magc(double f, uint32_t a, uint32_t n, int ch)
{
    double re = 0, im = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / n);
        double x = ch == 0 ? buf[a + i] : ch == 1 ? buf[a + i] + side[a + i] : buf[a + i] - side[a + i];
        re += x * w * cos(2 * M_PI * f * i / FS);
        im += x * w * sin(2 * M_PI * f * i / FS);
    }
    return sqrt(re * re + im * im) / n;
}
static double mag(double f, uint32_t a, uint32_t n) { return magc(f, a, n, 0); }
/* a plain sound: a saw, both VCOs at the note, no detune, the filter open, no resonance, no envelope, STER */
static void plain(track_t *t)
{
    host_preset(t, ENGI_MARE, 0);
    t->p[P_E0] = MTW_SAW, t->p[P_E1] = 0, t->p[P_E2] = 0, t->p[P_E3] = 0;
    t->p[P_E4] = 127, t->p[P_E5] = 0, t->p[P_E6] = 0, t->p[P_E7] = MTM_ST;
    t->p[P_ED_FLT] = 0;
    t->p[P_MFFM] = t->p[P_MRING] = MAR_VOFF;            /* VCO 3, VCO 4 off */
    t->p[P_VINT] = 0;                                  /* (no drift: DTUNE 0, VINTAGE 0) */
    {   /* whatever INIT brings, none of it here: no cable, no INSERT, no glide, POLY, no sends */
        uint32_t k;
        for (k = 0; k < MX_SLOTS; k++)
            t->p[mx_pid(k)] = MS_OFF, t->p[mx_pid(k) + 1u] = MT_OFF, t->p[mx_pid(k) + 2u] = 0;
        t->p[P_ITYPE] = 0, t->p[P_IMIX] = TP[P_IMIX].def, t->p[P_GLIDE] = 0, t->p[P_VOICE] = V_POLY;
        t->p[P_MW34] = TP[P_MW34].def, t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
        t->p[P_LD_PIT] = t->p[P_LD_FLT] = t->p[P_LD_SHP] = t->p[P_LD_AMP] = 0, t->p[P_ED_PIT] = t->p[P_ED_SHP] = 0;
    }
    md_ffm = md_ring = 0;                              /* FILTER FM, RING off */
    t->p[P_DIST] = 0;                                  /* (INIT drives the ladders a little) */
}

static void t_engine(void)
{
    const param_desc_t *d;
    ok(NENGINES >= 27u && ENGI_MARE == 26u && ENGINES[ENGI_MARE] == &ENG_MARE && !strcmp(ENG_MARE.name, "MARE") &&
           ENGINES[ENGI_RODEO] == &ENG_RODEO && ENGINES[ENGI_STAMPEDE] == &ENG_STAMPEDE, "the 27th engine, after RODEO, named MARE");
    ok(NELEM(N_ENGNAME) == NENGINES && !strcmp(N_ENGNAME[ENGI_MARE], "MARE") && !strcmp(N_ENGNAME[ENGI_RODEO], "RODEO"),
       "ENG: every engine named (RODEO, MARE)");
    ok(vs_ok(&ENG_MARE), "its voices on both cores (vs_ok)");
    trk[0].p[P_E3] = -10, d = mare_desc(&trk[0], 3);
    ok(d && !strcmp(d->label, "FM") && (trk[0].p[P_E3] = 0, d = mare_desc(&trk[0], 3)) && !strcmp(d->label, "SY/FM") &&
           (trk[0].p[P_E3] = 5, d = mare_desc(&trk[0], 3)) && !strcmp(d->label, "SYNC") && !mare_desc(&trk[0], 2),
       "SY/FM's label by its side: FM, SY/FM, SYNC");
}

static void t_vco(void)
{
    track_t *t = &trk[0];
    double a, b, c;
    char w[200];
    plain(t);
    mar_direct(t, 440.0, p16_of(440.0), 64 << 8);
    a = mag(440, N / 2, N / 2), b = mag(415.3, N / 2, N / 2), c = mag(466.2, N / 2, N / 2);
    ok(a > 8 * b && a > 8 * c, "in tune (a saw at A4: 440 Hz, not beside it)");
    t->p[P_E1] = 7;
    mar_direct(t, 440.0, p16_of(440.0), 64 << 8);
    a = mag(440, N / 2, N / 2), b = mag(440 * pow(2, 7 / 12.0), N / 2, N / 2);
    snprintf(w, sizeof w, "VCO2 +7: its fifth beside VCO 1 (659 Hz %+.1f dB against 440)", db(b / a));
    ok(fabs(db(b / a)) < 3.0, w);
}

/* the harmonics of a 220 Hz note, the filter open: h2 and h3 against h1 (dB) */
static void harm(track_t *t, int32_t wave, int32_t shape, double *h2, double *h3)
{
    double a;
    t->p[P_E0] = (int16_t)wave;
    mar_direct(t, 220.0, p16_of(220.0), shape);
    a = mag(220, N / 2, N / 2);
    *h2 = db(mag(440, N / 2, N / 2) / a), *h3 = db(mag(660, N / 2, N / 2) / a);
}
static void t_wave(void)
{
    track_t *t = &trk[0];
    double t2, t3, s2, s3, q2, q3, p2, p3, m2, m3;
    char w[260];
    plain(t);
    harm(t, MTW_TRI, 64 << 8, &t2, &t3);
    harm(t, MTW_SAW, 64 << 8, &s2, &s3);
    harm(t, MTW_SQR, 64 << 8, &q2, &q3);
    harm(t, MTW_PLS, 64 << 8, &p2, &p3);
    snprintf(w, sizeof w, "WAVE: TRI h2 %.0f h3 %.1f, SAW h2 %.1f h3 %.1f, SQR h2 %.0f h3 %.1f, PLS h2 %.1f dB", t2, t3, s2, s3, q2,
             q3, p2);
    ok(t2 < -30 && fabs(t3 + 19.1) < 2.5 && fabs(s2 + 6.0) < 1.5 && fabs(s3 + 9.5) < 1.5 && q2 < -30 && fabs(q3 + 9.5) < 1.5 &&
           p2 > -2.0, w);
    harm(t, MTW_SAW, (64 + 43) << 8, &m2, &m3);     /* SHAPE up ~ a third of the way: SAW becomes SQR */
    snprintf(w, sizeof w, "SHAPE moves WAVE: SAW + 43 steps of SHAPE plays the square (h2 %.0f dB)", m2);
    ok(m2 < -30, w);
}

static void t_detune(void)
{
    track_t *t = &trk[0];
    double a, b, c;
    char w[200];
    plain(t);
    t->p[P_E0] = MTW_TRI, t->p[P_E2] = 127;
    mar_direct(t, 440.0, p16_of(440.0), 64 << 8);
    a = mag(440.0 * pow(2, -20.0 / 1200), 0, N), b = mag(440.0 * pow(2, 20.0 / 1200), 0, N), c = mag(440, 0, N);
    snprintf(w, sizeof w, "DTUNE 127: 20 cents down and up (434.9 / 445.1 Hz), not 440 (%.0f %.0f vs %.0f)", a, b, c);
    ok(a > 2 * c && b > 2 * c, w);
}

static void t_sync_fm(void)
{
    track_t *t = &trk[0];
    double h, o, f0, s1, d1, s0, d0, f2 = 220.0 * pow(2, 7 / 12.0);
    char w[220];
    plain(t);
    t->p[P_E1] = 7, t->p[P_E3] = 1;                    /* SYNC, VCO 2 a fifth up (no offset) */
    mar_direct(t, 220.0, p16_of(220.0), 64 << 8);
    h = mag(660, N / 2, N / 2), o = mag(f2, N / 2, N / 2), f0 = mag(220, N / 2, N / 2);
    snprintf(w, sizeof w, "SYNC: VCO 1's harmonics only (330 Hz %.1f dB under 220; 660 Hz %+.1f dB)", db(o / f0), db(h / f0));
    ok(o < f0 / 30 && h > f0 / 10, w);
    t->p[P_E3] = 63;                                   /* swept 36 semitones up: still VCO 1's period, band-limited */
    mar_direct(t, 220.0, p16_of(220.0), 64 << 8);
    {
        double hh = mag(220 * 37, N / 2, N / 2), mid = mag(220 * 36.5, N / 2, N / 2);
        snprintf(w, sizeof w, "SYNC 63: the 37th harmonic of VCO 1 stands out (%.1f dB over the spectrum between)", db(hh / mid));
        ok(hh > 4 * mid, w);
    }
    t->p[P_E0] = MTW_TRI, t->p[P_E3] = 0, t->p[P_E4] = 127;
    mar_direct(t, 220.0, p16_of(220.0), 64 << 8);
    s0 = mag(220 + f2, N / 2, N / 2), d0 = mag(f2 - 220, N / 2, N / 2);
    t->p[P_E3] = -40;
    mar_direct(t, 220.0, p16_of(220.0), 64 << 8);
    s1 = mag(220 + f2, N / 2, N / 2), d1 = mag(f2 - 220, N / 2, N / 2);
    snprintf(w, sizeof w, "FM -40: the sum (550 Hz %+.1f dB) and difference (110 Hz %+.1f dB) sidebands against FM off", db(s1 / s0),
             db(d1 / d0));
    ok(db(s1 / s0) > 20.0 && db(d1 / d0) > 20.0, w);
}

/* the filters, on a 55 Hz saw (harmonics every 55 Hz), the keyboard tracking at 0 (C4's pitch16) */
static void t_vcf(void)
{
    track_t *t = &trk[0];
    double a1, a2, l, r, mx = 0, lo, hi, olo, ohi;
    uint32_t i;
    char w[260];
    plain(t);
    mar_direct(t, 55.0, 60 * 16, 64 << 8);
    olo = mag(110, N / 2, N / 2), ohi = mag(3520, N / 2, N / 2);
    t->p[P_E4] = 60;                                   /* ~585 Hz */
    mar_direct(t, 55.0, 60 * 16, 64 << 8);
    a1 = mag(1760, N / 2, N / 2) / (ohi * 2), a2 = mag(3520, N / 2, N / 2) / ohi;   /* (the saw: 1 / n) */
    snprintf(w, sizeof w, "STER: the ladder's low-pass, %.1f dB an octave (1.76 -> 3.52 kHz)", db(a2 / a1));
    ok(db(a2 / a1) < -20.0 && db(a2 / a1) > -28.0, w);
    {
        double r0 = mag(585, N / 2, N / 2), r1;
        t->p[P_E5] = 110;
        mar_direct(t, 55.0, 60 * 16, 64 << 8);
        r1 = mag(605, N / 2, N / 2) + mag(550, N / 2, N / 2);
        snprintf(w, sizeof w, "RES 110: the ladder rings at its cutoff (~585 Hz %+.1f dB against RES 0)", db(r1 / r0 / 2));
        ok(db(r1 / r0 / 2) > 6.0, w);
    }
    t->p[P_E5] = 0;
    for (i = 0; i < N; i++)
        mx = abs(side[i]) > mx ? abs(side[i]) : mx;
    ok(mx == 0, "STER, SPACE 0: no side at all (the left as the right)");
    /* the sides' levels matched (eng_mare.c bal): their timbres compared, 440 Hz against 110 Hz (the
     * right ladder at ~585 Hz, the left ~3 octaves under: 1.76 kHz is at the rounding's floor on both) */
    t->p[P_E6] = -63;                                  /* (SPACE moves VCF 1, the left, alone; below 0 down) */
    mar_direct(t, 55.0, 60 * 16, 64 << 8);
    l = magc(440, N / 2, N / 2, 1) / magc(110, N / 2, N / 2, 1), r = magc(440, N / 2, N / 2, 2) / magc(110, N / 2, N / 2, 2);
    /* the left ~3 octaves under the right holds little: the sides mixed toward the middle (eng_mare.c
     * mar_bal_gains, the darker side never lifted more than 6 dB), their timbres near */
    snprintf(w, sizeof w, "STER, SPACE -63: the left filter down, the sides mixed toward the middle (440 Hz against 110 Hz: "
             "%+.1f dB right against left)", db(r / l));
    ok(fabs(db(r / l)) < 6.0, w);
    t->p[P_E6] = 63;
    mar_direct(t, 55.0, 60 * 16, 64 << 8);
    l = magc(1760, N / 2, N / 2, 1) / magc(110, N / 2, N / 2, 1), r = magc(1760, N / 2, N / 2, 2) / magc(110, N / 2, N / 2, 2);
    snprintf(w, sizeof w, "STER, SPACE 63: the left one up, brighter (1.76 kHz against 110 Hz: %+.1f dB left against right)",
             db(l / r));
    ok(db(l / r) > 20.0, w);
    t->p[P_E6] = 0, t->p[P_E7] = MTM_PAR;
    mar_direct(t, 55.0, 60 * 16, 64 << 8);
    /* PARA: the instrument's parallel out, the high-pass and the low-pass summed, the same in both ears */
    lo = mag(110, N / 2, N / 2) / olo, hi = mag(3520, N / 2, N / 2) / ohi;
    mx = 0;
    for (i = 0; i < N; i++)
        mx = abs(side[i]) > mx ? abs(side[i]) : mx;
    snprintf(w, sizeof w, "PARA: the high-pass and the low-pass summed, mono (110 Hz %+.1f dB, 3.5 kHz %+.1f dB against "
             "open; side %g)", db(lo), db(hi), mx);
    ok(db(lo) > -12.0 && db(hi) > -12.0 && mx == 0, w);
    t->p[P_E7] = MTM_SER, t->p[P_E6] = -20;            /* (the high-pass under the low-pass: a band) */
    mar_direct(t, 55.0, 60 * 16, 64 << 8);
    lo = mag(110, N / 2, N / 2) / olo, hi = mag(3520, N / 2, N / 2) / ohi;
    mx = 0;
    for (i = 0; i < N; i++)
        mx = abs(side[i]) > mx ? abs(side[i]) : mx;
    {
        double md = mag(550, N / 2, N / 2) / (olo / 5);
        snprintf(w, sizeof w, "SERI: a band (550 Hz %+.1f dB, 110 Hz %.1f, 3.5 kHz %.1f), mono (side %g)", db(md), db(lo), db(hi), mx);
        ok(db(md) > -12.0 && db(lo) < -14.0 && db(hi) < -20.0 && mx == 0, w);
    }
}

static void t_noise(void)
{
    track_t *t = &trk[0];
    double a, b;
    char w[160];
    plain(t);
    mar_direct(t, 220.0, p16_of(220.0), 64 << 8);
    a = mag(330, N / 2, N / 2);
    t->p[P_E7] = MTM_STN;
    mar_direct(t, 220.0, p16_of(220.0), 64 << 8);
    b = mag(330, N / 2, N / 2);
    snprintf(w, sizeof w, "STR+N: noise between the harmonics (330 Hz %+.1f dB)", db(b / a));
    ok(db(b / a) > 20.0, w);
}

static void run_mix(uint32_t nb, double *peak, int *inb, double *sidef)
{
    static int32_t o[CTL * 2];
    uint32_t b, i, cnt = 0;
    double e = 0, best = 0, es = 0, em = 0;
    *inb = 1;
    for (b = 0; b < nb; b++) {
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            if (o[i] > (1 << 23) || o[i] < -(1 << 23))
                *inb = 0;
            e += (double)o[i] * o[i];
        }
        for (i = 0; i < CTL; i++) {
            double s = (double)o[2 * i] - o[2 * i + 1], m = (double)o[2 * i] + o[2 * i + 1];
            es += s * s, em += m * m;
        }
        if (++cnt == 64u) {
            double r = sqrt(e / (64.0 * 2 * CTL));
            best = r > best ? r : best;
            e = 0, cnt = 0;
        }
    }
    *peak = best;
    if (sidef)
        *sidef = em > 0 ? sqrt(es / em) : 0;
}
static uint32_t sounding(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return n;
}

/* a note from silence: no click (the first block's steps no bigger than the note's own); no DC; silent after */
static void t_clean(void)
{
    static int32_t o[CTL * 2];
    uint32_t b, i;
    int32_t d0 = 0, d1 = 0, last = 0;
    double sum = 0, sq = 0;
    int silent = 1;
    char w[200];
    host_tracks_init();
    host_preset(&trk[0], ENGI_MARE, 0);
    plain(&trk[0]);
    clipper_proj_neutral();
    trk[0].p[P_AMODE] = 0, trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_E0] = MTW_TRI, trk[0].p[P_E4] = 90, trk[0].p[P_E5] = 30, trk[0].p[P_E6] = 30;
    trk[0].p[P_ATK] = 0, trk[0].p[P_SUS] = 127;
    for (b = 0; b < 20u; b++)
        mix_block(o, CTL);
    trk_note_on(&trk[0], 57, 127);
    for (b = 0; b < 2101u; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t x = o[2 * i], d = abs(x - last);
            last = x;
            if (b < 2u)
                d0 = d > d0 ? d : d0;
            else if (b > 100u)
                d1 = d > d1 ? d : d1, sum += x, sq += (double)x * x;   /* (1.4 s: a part of a cycle left over, ~0) */
        }
    }
    snprintf(w, sizeof w, "a note from silence: no click (its first steps %d, the held note's %d)", d0, d1);
    ok(d0 <= 2 * d1 && d1 > 0, w);
    snprintf(w, sizeof w, "a held note: no DC (mean %.1f, RMS %.0f)", sum / (2000.0 * CTL), sqrt(sq / (2000.0 * CTL)));
    ok(fabs(sum / (2000.0 * CTL)) < sqrt(sq / (2000.0 * CTL)) / 200, w);
    trk_note_off(&trk[0], 57);
    for (b = 0; b < 3000u && sounding(&trk[0]); b++)
        mix_block(o, CTL);
    for (b = 0; b < 4000u; b++) {                      /* (the effects' tails: the factory sound dry, short) */
        mix_block(o, CTL);
        if (b >= 3000u)
            for (i = 0; i < 2u * CTL; i++)
                silent &= o[i] == 0;
    }
    ok(!sounding(&trk[0]) && silent, "released: the voice free, then the output exactly 0");
}

static void t_bounded(void)
{
    track_t *t = &trk[0];
    uint32_t i, k;
    int32_t mx = 0;
    char w[200];
    static const int8_t WV[4] = {MTW_TRI, MTW_SAW, MTW_SQR, MTW_PLS};
    for (k = 0; k < 24u; k++) {
        plain(t);
        t->p[P_E0] = WV[k % 4u], t->p[P_E1] = k & 1u ? 24 : -24, t->p[P_E2] = 127;
        t->p[P_E3] = (int16_t)(k % 3u == 0 ? -64 : k % 3u == 1 ? 63 : 0);
        t->p[P_E4] = k & 2u ? 0 : 127, t->p[P_E5] = 127, t->p[P_E6] = k & 4u ? -64 : 63, t->p[P_E7] = (int16_t)(k % 8u);   /* (SPLT, SPL+N too) */
        t->p[P_ED_FLT] = k & 1u ? 63 : -64;
        mar_direct(t, 27.5 * (k + 1), p16_of(27.5 * (k + 1)), k & 8u ? 0 : 127 << 8);
        for (i = 0; i < N; i++) {
            if (abs(buf[i] + side[i]) > mx)
                mx = abs(buf[i] + side[i]);
            if (abs(buf[i] - side[i]) > mx)
                mx = abs(buf[i] - side[i]);
        }
    }
    snprintf(w, sizeof w, "RES at the top, the cutoffs at their ends, every wave, mode, SY/FM, SPACE: bounded (peak %d)", mx);
    ok(mx < (1 << 21), w);
}

static void t_unison(void)
{
    uint32_t i, j, same = 0, nz0 = 0, n;
    char w[160];
    host_tracks_init();
    host_preset(&trk[0], ENGI_MARE, 0);
    trk[0].p[P_VOICE] = V_UNISON;
    trk_note_on(&trk[0], 48, 100);
    n = sounding(&trk[0]);
    for (i = 0; i < NVOICE; i++)
        for (j = i + 1; j < NVOICE; j++)
            if (trk[0].v[i].active && trk[0].v[j].active &&
                (trk[0].v[i].ph[0] == trk[0].v[j].ph[0] ||
                 trk[0].v[i].ph[2] == trk[0].v[j].ph[2]))
                same++;
    for (i = 0; i < NVOICE; i++)
        nz0 += trk[0].v[i].active && (!trk[0].v[i].ph[2] || trk[0].v[i].ph[1] != trk[0].v[i].ph[0]);
    snprintf(w, sizeof w, "UNISON: %u voices, its own phases (VCO 2 in step with VCO 1) and noise state (%u alike, %u wrong)", n, same, nz0);
    ok(n >= 4u && !same && !nz0, w);
    trk_note_off(&trk[0], 48);
}


/* the instrument's four VCOs, PARA, the drift, the square's level, EXP glide, ST/SP, the stereo delay */
static void mx_render(int32_t *l, int32_t *r, uint32_t nb)   /* nb blocks of the mix into l, r (0: skipped) */
{
    static int32_t o[CTL * 2];
    uint32_t b, i;
    for (b = 0; b < nb; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            if (l)
                l[b * CTL + i] = o[2 * i];
            if (r)
                r[b * CTL + i] = o[2 * i + 1];
        }
    }
}
static double mag_of(const int32_t *x, double f, uint32_t n)   /* the magnitude at f (Hann) */
{
    double re = 0, im = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / n);
        re += x[i] * w * cos(2 * M_PI * f * i / FS), im += x[i] * w * sin(2 * M_PI * f * i / FS);
    }
    return sqrt(re * re + im * im) / n;
}
static double rms_of(const int32_t *x, uint32_t n)
{
    double e = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        e += (double)x[i] * x[i];
    return sqrt(e / n + 1e-9);
}
static void mare_part(void)                         /* a plain MARE sound on part 1, as the test's plain() */
{
    host_tracks_init();
    host_preset(&trk[0], ENGI_MARE, 0);
    plain(&trk[0]);
    clipper_proj_neutral();
    trk[0].p[P_AMODE] = 0, trk[0].p[P_VOICE] = V_POLY, trk[0].p[P_ATK] = 0, trk[0].p[P_SUS] = 127;
    trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_CHOR] = 0;
    trk[0].p[P_E4] = 100;
    song.sel = 0;
}
/* MENU > SEQ > PARA ASSIGN and PARA ROUND ROBIN (mar_para; SLOW BLOOM: the same chords may
 * give a different triad each time): each VCO's heard pitch (its glide's note plus its interval) after
 * the keys settle. LOW and HIGH: a three-key chord 20 times, after 20 different
 * phrases and in every order of its keys, a block or a few apart or all in one: the same pitches each time, the keys
 * by pitch (LOW lowest first, HIGH highest first: VCO j to key j n / nv at its interval); one key the full stack; a key
 * let go re-voices the rest as the two keys held from the start would have them. ROUND ROBIN (PLAYED): C held, G
 * struck again and again: ON, each strike turns the shares one VCO on (strike i as strike i + nv, never as strike
 * i + 1); OFF, every strike the same (as PLAYED always was); ON under LOW, nothing turns. PLAYED with ROUND ROBIN OFF
 * (the defaults) renders bit for bit as before (against the renders before the change) */
static int32_t pv_iv(const track_t *t, uint32_t c)
{
    return c == 0u ? 0 : clamp(c == 1u ? t->p[P_E0 + 1] : c == 2u ? t->p[P_MFFM] : t->p[P_MRING], -24, 24);
}
static uint32_t pv_heard(const track_t *t, int32_t *h)   /* each sounding VCO's pitch (1/4096 semitone), 0: OFF */
{
    mar_part_t *A = mar_arena((track_t *)t);
    uint32_t c, n = 0;
    for (c = 0; c < 4u; c++) {
        int on = c < 2u || (c == 2u ? t->p[P_MFFM] : t->p[P_MRING]) > MAR_VOFF;
        h[c] = on ? A->pq[c] + pv_iv(t, c) * 4096 : 0;
        n += on && A->pq[c] == (int32_t)A->pn[c] << 12;   /* (settled) */
    }
    return n;
}
static uint32_t pv_nv(const track_t *t) { return mar_vcos(t->p, (uint8_t[4]){0}); }
static void pv_want(const track_t *t, const uint8_t *keys, uint32_t nk, int32_t *want)   /* keys in the mode's order */
{
    uint32_t c, k = 0, nv = pv_nv(t);
    for (c = 0; c < 4u; c++) {
        int on = c < 2u || (c == 2u ? t->p[P_MFFM] : t->p[P_MRING]) > MAR_VOFF;
        want[c] = on ? (keys[(k * nk) / nv] + pv_iv(t, c)) << 12 : 0;
        k += (uint32_t)on;
    }
}
static void pv_off(void)
{
    uint32_t k;
    for (k = 0; k < 128u; k++)
        trk_note_off(&trk[0], (uint8_t)k);
    mx_render(0, 0, 300);
}
static void t_para_assign(void)
{
    static const uint8_t CH[3] = {48, 52, 55}, ORD[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    static const char *const PR[2] = {"SLOW BLOOM", "TRAVEL PARA"}, *const MN[3] = {"PLAYED", "LOW", "HIGH"};
    char w[600];
    uint32_t pr, md, r, k, c;
    for (pr = 0; pr < 2u; pr++) {
        for (md = PA_LOW; md <= PA_HIGH; md++) {
            int32_t h[4], h0[4], want[4], one[4], two[4], rel[4];
            uint32_t same = 1, settled = 1, lcg = 12345u + pr * 7u + md;
            uint8_t ord[3], ce[3];
            mare_part();
            host_preset(&trk[0], ENGI_MARE, host_pi(ENGI_MARE, PR[pr]));
            trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_CHOR] = 0;
            trk[0].p[P_GLIDE] = 40, trk[0].p[P_GLMODE] = 1;   /* (gliding: each VCO from wherever the phrase left it) */
            para_assign = (uint8_t)md, para_rr = 1;           /* (ROUND ROBIN acts with PLAYED only) */
            para_hand = 1;                                  /* (E let go re-voices: HAND BACK; KEEP: t_para_keep) */
            for (r = 0; r < 20u; r++) {
                uint32_t nn = 2u + r % 5u, held[3] = {0, 0, 0}, nh = 0;
                for (k = 0; k < nn; k++) {                  /* a phrase: overlapping keys, some held into the next */
                    uint8_t x = (uint8_t)(36u + ((lcg = lcg * 1664525u + 1013904223u) >> 24) % 40u);
                    trk_note_on(&trk[0], x, 100), mx_render(0, 0, 3u + (lcg >> 28));
                    if (nh == 3u)
                        trk_note_off(&trk[0], (uint8_t)held[0]), held[0] = held[1], held[1] = held[2], nh = 2;
                    held[nh++] = x;
                }
                while (nh)
                    trk_note_off(&trk[0], (uint8_t)held[--nh]);
                mx_render(0, 0, (lcg >> 29) * 40u);         /* (the release's tail cut short, or not) */
                for (k = 0; k < 3u; k++) {
                    trk_note_on(&trk[0], CH[ORD[r % 6u][k]], 100);
                    if (r & 1u)
                        mx_render(0, 0, 1u + k * (r % 4u));
                }
                mx_render(0, 0, 400);
                settled &= pv_heard(&trk[0], h) == pv_nv(&trk[0]);
                if (!r)
                    memcpy(h0, h, sizeof h);
                same &= !memcmp(h, h0, sizeof h);
                for (k = 0; k < 3u; k++)
                    trk_note_off(&trk[0], CH[k]);
                mx_render(0, 0, 20);
            }
            for (k = 0; k < 3u; k++)                        /* the chord by pitch: LOW C E G, HIGH G E C */
                ord[k] = CH[md == PA_LOW ? k : 2u - k];
            pv_want(&trk[0], ord, 3, want);
            trk_note_on(&trk[0], 52, 100), mx_render(0, 0, 400);   /* one key: the full stack */
            pv_heard(&trk[0], one);
            for (c = 0; c < 4u; c++)
                same &= !one[c] || one[c] == (52 + pv_iv(&trk[0], c)) << 12;
            trk_note_on(&trk[0], 55, 100), trk_note_on(&trk[0], 48, 100), mx_render(0, 0, 400);
            trk_note_off(&trk[0], 52), mx_render(0, 0, 400);   /* E let go: as C and G held from the start */
            pv_heard(&trk[0], rel);
            pv_off();
            trk_note_on(&trk[0], 55, 100), mx_render(0, 0, 5), trk_note_on(&trk[0], 48, 100), mx_render(0, 0, 400);
            pv_heard(&trk[0], two);
            pv_off();
            ce[0] = md == PA_LOW ? 48 : 55, ce[1] = md == PA_LOW ? 55 : 48;
            pv_want(&trk[0], ce, 2, h);
            snprintf(w, sizeof w, "PARA ASSIGN %s (%s): C E G 20 times after 20 phrases, 6 key orders: the same VCO "
                     "pitches (%.0f %.0f %.0f %.0f), by pitch; one key the full stack; E let go: as C G held from the "
                     "start (%.0f %.0f %.0f %.0f)", MN[md], PR[pr], h0[0] / 4096.0, h0[1] / 4096.0, h0[2] / 4096.0,
                     h0[3] / 4096.0, rel[0] / 4096.0, rel[1] / 4096.0, rel[2] / 4096.0, rel[3] / 4096.0);
            ok(same && settled && !memcmp(h0, want, sizeof h0) && !memcmp(rel, two, sizeof rel) &&
               !memcmp(two, h, sizeof h), w);
        }
        for (md = 0; md < 3u; md++) {                       /* ROUND ROBIN: ON (PLAYED), OFF (PLAYED), ON under LOW */
            int32_t s[9][4];
            uint32_t nv, cyc = 1, turns = 1, rep = 1, n;
            mare_part();
            host_preset(&trk[0], ENGI_MARE, host_pi(ENGI_MARE, PR[pr]));
            trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_CHOR] = 0;
            trk[0].p[P_GLIDE] = 40, trk[0].p[P_GLMODE] = 1;
            para_assign = md == 2u ? PA_LOW : PA_PLAYED, para_rr = md != 1u;
            nv = pv_nv(&trk[0]);
            n = 2u * nv + 1u;
            trk_note_on(&trk[0], 48, 100), mx_render(0, 0, 400);   /* C held */
            for (r = 0; r < n; r++) {                       /* G struck again and again */
                trk_note_on(&trk[0], 55, 100), mx_render(0, 0, 400);
                pv_heard(&trk[0], s[r]);
                trk_note_off(&trk[0], 55), mx_render(0, 0, 40);
            }
            pv_off();
            for (r = 0; r + 1u < n; r++) {
                cyc &= r + nv >= n || !memcmp(s[r], s[r + nv], sizeof s[r]);
                turns &= memcmp(s[r], s[r + 1u], sizeof s[r]) != 0;
                rep &= !memcmp(s[r], s[r + 1u], sizeof s[r]);
            }
            snprintf(w, sizeof w, "PARA ROUND ROBIN %s%s (%s, %u VCOs): C held, G struck %u times: %s (%.0f %.0f %.0f "
                     "%.0f, then %.0f %.0f %.0f %.0f)", md == 1u ? "OFF" : "ON", md == 2u ? " under LOW" : "", PR[pr],
                     (unsigned)nv, (unsigned)n, md ? "the same every strike" : "a VCO further each strike, round in nv",
                     s[0][0] / 4096.0, s[0][1] / 4096.0, s[0][2] / 4096.0, s[0][3] / 4096.0, s[1][0] / 4096.0,
                     s[1][1] / 4096.0, s[1][2] / 4096.0, s[1][3] / 4096.0);
            ok(md ? rep : cyc && turns, w);
        }
    }
    para_assign = PA_PLAYED, para_rr = 0, para_hand = 0;
}
/* PARA NOTE-OFF: C E G held on four VCOs, E let go. KEEP (the default):
 * every VCO stays on its note (E's rings on under the shared envelope) until a key goes down, then the keys held share
 * them again; HAND BACK: E's VCOs go to C and G at once */
static void t_para_keep(void)
{
    char w[300];
    uint32_t hb;
    for (hb = 0; hb < 2u; hb++) {
        int32_t a[4], b[4], c[4];
        mare_part();
        host_preset(&trk[0], ENGI_MARE, host_pi(ENGI_MARE, "SLOW BLOOM"));
        trk[0].p[P_GLIDE] = 0;
        para_assign = PA_PLAYED, para_rr = 0, para_hand = (uint8_t)hb;
        trk_note_on(&trk[0], 48, 100), trk_note_on(&trk[0], 52, 100), trk_note_on(&trk[0], 55, 100);
        mx_render(0, 0, 100), pv_heard(&trk[0], a);
        trk_note_off(&trk[0], 52), mx_render(0, 0, 100), pv_heard(&trk[0], b);
        trk_note_on(&trk[0], 57, 100), mx_render(0, 0, 100), pv_heard(&trk[0], c);
        snprintf(w, sizeof w, "PARA NOTE-OFF %s: C E G held, E let go: %s (%.0f %.0f %.0f %.0f -> %.0f %.0f %.0f %.0f); "
                 "A struck: the keys share them again (%.0f %.0f %.0f %.0f)", hb ? "HAND BACK" : "KEEP",
                 hb ? "E's VCOs to C and G at once" : "every VCO on its note", a[0] / 4096.0, a[1] / 4096.0,
                 a[2] / 4096.0, a[3] / 4096.0, b[0] / 4096.0, b[1] / 4096.0, b[2] / 4096.0, b[3] / 4096.0,
                 c[0] / 4096.0, c[1] / 4096.0, c[2] / 4096.0, c[3] / 4096.0);
        ok((hb ? memcmp(a, b, sizeof a) != 0 : !memcmp(a, b, sizeof a)) && memcmp(b, c, sizeof b) != 0, w);
        pv_off();
    }
    para_hand = 0;
}
static void t_rebuild(void)
{
    static int32_t l[N], r[N];
    char w[220];
    double a, b, c, d;
    uint32_t i, k;
    /* VCO 3 and 4: OFF the two-VCO sound; +7 / -12 their fifth and sub-octave beside the note */
    mare_part();
    trk_note_on(&trk[0], 57, 100);
    mx_render(0, 0, 40), mx_render(l, 0, N / CTL);
    a = mag_of(l, 220.0, N), b = mag_of(l, 329.63, N), c = mag_of(l, 110.0, N);
    trk_note_off(&trk[0], 57), mx_render(0, 0, 200);
    mare_part();
    trk[0].p[P_MFFM] = 7, trk[0].p[P_MRING] = -12;
    trk_note_on(&trk[0], 57, 100);
    mx_render(0, 0, 40), mx_render(l, 0, N / CTL);
    snprintf(w, sizeof w, "VCO3 +7, VCO4 -12: the fifth (%+.1f dB) and the octave below (%+.1f dB) against VCO3 / VCO4 OFF",
             db(mag_of(l, 329.63, N) / b), db(mag_of(l, 110.0, N) / c));
    ok(db(mag_of(l, 329.63, N) / b) > 15.0 && db(mag_of(l, 110.0, N) / c) > 15.0 && sounding(&trk[0]) == 1u, w);
    trk_note_off(&trk[0], 57), mx_render(0, 0, 200);
    (void)a;
    /* the four VCOs at the note with DTUNE: four lines apart, so the fundamental beats (its level over 2 s moves) */
    mare_part();
    trk[0].p[P_E2] = 64, trk[0].p[P_MFFM] = 0, trk[0].p[P_MRING] = 0;
    trk_note_on(&trk[0], 45, 100);
    {
        double lo = 1e30, hi = 0;
        for (k = 0; k < 16u; k++) {
            mx_render(l, 0, (FS / 8) / CTL);
            a = mag_of(l, 110.0, (FS / 8) / CTL * CTL);
            lo = a < lo ? a : lo, hi = a > hi ? a : hi;
        }
        snprintf(w, sizeof w, "four VCOs, DTUNE 64: the fundamental beats (%.1f dB between its loudest and quietest eighth "
                 "of a second over 2 s)", db(hi / lo));
        ok(db(hi / lo) > 6.0, w);
    }
    trk_note_off(&trk[0], 45), mx_render(0, 0, 200);
    /* the drift: DTUNE 0 and VINTAGE 0 none (the VCOs exact: a steady fundamental); VINTAGE 127: each VCO its own walk */
    for (k = 0; k < 2u; k++) {
        double lo = 1e30, hi = 0;
        mare_part();
        trk[0].p[P_VINT] = (int16_t)(k ? 127 : 0), trk[0].p[P_MFFM] = 0, trk[0].p[P_MRING] = 0;
        trk_note_on(&trk[0], 45, 100);
        mx_render(0, 0, 40);
        for (i = 0; i < 24u; i++) {
            mx_render(l, 0, (FS / 8) / CTL);
            a = mag_of(l, 110.0 * pow(2, (k ? 0 : 0) / 1200.0), (FS / 8) / CTL * CTL);
            lo = a < lo ? a : lo, hi = a > hi ? a : hi;
        }
        d = db(hi / lo);
        if (!k)
            c = d;
        trk_note_off(&trk[0], 45), mx_render(0, 0, 200);
    }
    snprintf(w, sizeof w, "drift: DTUNE 0 VINTAGE 0 a steady note (%.2f dB over 3 s), VINTAGE 127 the VCOs wander (%.1f dB)", c, d);
    ok(c < 0.5 && d > 3.0, w);
    /* the square as loud as the saw's swing: its RMS ~ +4.8 dB over the saw's (both unfiltered) */
    for (k = 0; k < 2u; k++) {
        mare_part();
        trk[0].p[P_E0] = (int16_t)(k ? MTW_SQR : MTW_SAW), trk[0].p[P_E4] = 127;
        trk_note_on(&trk[0], 45, 100);
        mx_render(0, 0, 40), mx_render(l, 0, 64);
        if (k)
            b = rms_of(l, 64 * CTL);
        else
            a = rms_of(l, 64 * CTL);
        trk_note_off(&trk[0], 45), mx_render(0, 0, 200);
    }
    snprintf(w, sizeof w, "the square's level: %+.1f dB over the saw's (1.0, not 0.6: ~+4.8 dB before the ladder; "
             "the analog coupling and the mixer's knee take ~3 dB of it, the sound as designed; 0.6 reads ~-2.7)",
             db(b / a));
    ok(db(b / a) > 0.5, w);
    /* PARA: four keys, one voice, each key's pitch on a VCO; a key let go hands its VCOs back; PARA restarts nothing */
    mare_part();
    trk[0].p[P_VOICE] = V_PARA, trk[0].p[P_MFFM] = 0, trk[0].p[P_MRING] = 0, trk[0].p[P_ATK] = 60;
    trk_note_on(&trk[0], 48, 100), mx_render(0, 0, 20);
    trk_note_on(&trk[0], 52, 100), mx_render(0, 0, 20);
    trk_note_on(&trk[0], 55, 100), mx_render(0, 0, 20);
    trk_note_on(&trk[0], 59, 100);
    mx_render(0, 0, 60), mx_render(l, 0, N / CTL);
    {
        static const double F[4] = {130.81, 164.81, 196.0, 246.94};
        double lo = 1e30;
        for (k = 0; k < 4u; k++)
            lo = mag_of(l, F[k], N) < lo ? mag_of(l, F[k], N) : lo;
        a = mag_of(l, 138.59, N);                       /* (a pitch no key plays) */
        snprintf(w, sizeof w, "PARA: four keys on one voice (%u), each key's fundamental %.1f dB over a pitch none plays",
                 sounding(&trk[0]), db(lo / a));
        ok(sounding(&trk[0]) == 1u && db(lo / a) > 20.0, w);
    }
    a = trk[0].v[0].env;
    para_hand = 1;                                      /* (HAND BACK; KEEP, the default: t_para_keep) */
    trk_note_off(&trk[0], 59), trk_note_off(&trk[0], 55), trk_note_off(&trk[0], 52);
    mx_render(0, 0, 60), mx_render(l, 0, N / CTL);
    snprintf(w, sizeof w, "PARA: three keys up, the fourth's VCOs all on C3 (130.8 Hz %.1f dB over E3's 164.8)",
             db(mag_of(l, 130.81, N) / mag_of(l, 164.81, N)));
    ok(db(mag_of(l, 130.81, N) / mag_of(l, 164.81, N)) > 20.0 && trk[0].v[0].stage == 2u, w);
    trk_note_off(&trk[0], 48), mx_render(0, 0, 300);
    para_hand = 0;
    for (k = 0; k < 2u; k++) {                          /* PARA: a second key goes on in the same envelope; PARA+ restarts */
        uint32_t st;
        mare_part();
        trk[0].p[P_VOICE] = (int16_t)(k ? V_PARAT : V_PARA), trk[0].p[P_ATK] = 30, trk[0].p[P_SUS] = 100;
        trk_note_on(&trk[0], 48, 100), mx_render(0, 0, 600);
        trk_note_on(&trk[0], 55, 100), mx_render(0, 0, 1);
        st = trk[0].v[0].stage;
        if (k)
            b = st;
        else
            a = st;
        trk_note_off(&trk[0], 55), trk_note_off(&trk[0], 48), mx_render(0, 0, 400);
    }
    snprintf(w, sizeof w, "PARA: a second key leaves the envelope as it is (stage %.0f); PARA+ restarts it (stage %.0f)", a, b);
    ok(a == 2.0 && b == 1.0, w);
    /* PARA, GLIDE: a VCO glides to its new key (TIME: 1/3 of the way after a third of GLIDE's time, as the voice's) */
    {
        mar_part_t *A;
        int32_t q0, q1;
        mare_part();
        trk[0].p[P_VOICE] = V_PARA, trk[0].p[P_GLIDE] = 60, trk[0].p[P_GLMODE] = 1;
        trk_note_on(&trk[0], 48, 100), mx_render(0, 0, 20);
        trk_note_off(&trk[0], 48), trk_note_on(&trk[0], 60, 100);
        A = mar_arena(&trk[0]);
        q0 = A->pq[0];
        mx_render(0, 0, 10);
        q1 = A->pq[0];
        snprintf(w, sizeof w, "PARA, GLIDE 60: VCO 1 glides from C3 toward C4 (%.2f .. %.2f semitones after 10 blocks)",
                 q0 / 4096.0, q1 / 4096.0);
        ok(q0 >= 48 * 4096 && q1 > q0 && q1 < 60 * 4096, w);
        trk_note_off(&trk[0], 60), mx_render(0, 0, 200);
    }
    /* GLMOD EXP: the first half of the way in less than a third of the time the whole takes (a one-pole approach) */
    {
        uint32_t half = 0, near = 0;
        int32_t q0;
        mare_part();
        trk[0].p[P_VOICE] = V_MONO, trk[0].p[P_GLIDE] = 0, trk[0].p[P_GLMODE] = 2;
        trk_note_on(&trk[0], 48, 100), mx_render(0, 0, 20);   /* (no glide to the first: from wherever the last test left it) */
        trk[0].p[P_GLIDE] = 70;
        trk_note_on(&trk[0], 60, 100);
        q0 = (trk[0].v[0].pitch_cur << 8) + trk[0].v[0].pitch_frac;
        for (k = 1; k < 3000u && !near; k++) {
            int32_t q = (trk[0].v[0].pitch_cur << 8) + trk[0].v[0].pitch_frac;
            if (!half && q >= 54 * 16 * 256)
                half = k;
            if (q >= (60 * 16 - 1) * 256)
                near = k;
            mx_render(0, 0, 1);
        }
        snprintf(w, sizeof w, "GLMOD EXP: from %.2f, half the octave in %u blocks, to within 1/16 semitone in %u", q0 / 4096.0,
                 half, near);
        ok(q0 == 48 * 4096 && half && near && half * 4u < near, w);
        trk_note_off(&trk[0], 60), trk_note_off(&trk[0], 48), mx_render(0, 0, 200);
    }
    /* ST/SP: the left through ENV 2 (slow), the right through ENV 1 (fast): the left quieter early, both alike held */
    {
        mare_part();
        trk[0].p[P_E7] = MTM_STSP, trk[0].p[P_E2ATK] = 100, trk[0].p[P_E2SUS] = 127;
        trk[0].p[P_M1SRC] = MS_ENV2, trk[0].p[P_M1DST] = MT_CUT, trk[0].p[P_M1AMT] = 1;   /* (ENV 2 read: it runs) */
        trk_note_on(&trk[0], 57, 100);
        mx_render(0, 0, 4), mx_render(l, r, 32);
        a = rms_of(l, 32 * CTL), b = rms_of(r, 32 * CTL);
        mx_render(0, 0, 2000), mx_render(l, r, 64);
        c = rms_of(l, 64 * CTL), d = rms_of(r, 64 * CTL);
        snprintf(w, sizeof w, "ST/SP: the left on ENV 2 (its slow attack: %+.1f dB against the right early), alike once "
                 "both are held (%+.2f dB)", db(a / b), db(c / d));
        ok(db(a / b) < -6.0 && fabs(db(c / d)) < 1.0, w);
        trk_note_off(&trk[0], 57), mx_render(0, 0, 300);
    }
    /* PMSEQ: a preset's own MOD SEQ steps, packed as a user preset's; the factory presets without one: the shapes */
    {
        static const preset_mseq_t PM = PMSEQ(0, MSL(0, 3, 7, 12 - 5, -8, 7, -1, 1, 2, -2, 4, -4, 5, -5, 6, -6),
                                              MSL(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1), MSL(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
                                              MSL(-8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7));
        int8_t ms[MSEQ_LANES][MSEQ_STEPS];
        uint32_t pi2, kept = 1;
        mseq_unpack(ms, PM.st);
        for (pi2 = 0; pi2 < ENG_MARE.npresets; pi2++) {
            host_preset(&trk[0], ENGI_MARE, pi2);
            kept &= !memcmp(trk[0].ms, MSEQ_INIT, sizeof trk[0].ms) ||
                    preset_mseq((int8_t (*)[MSEQ_STEPS])ms, &ENG_MARE.presets[pi2]);
        }
        mseq_unpack(ms, PM.st);
        ok(ms[0][1] == 3 && ms[0][2] == 7 && ms[0][3] == 7 && ms[0][4] == -8 && ms[0][15] == -6 && ms[1][9] == 1 &&
               ms[3][0] == -8 && ms[3][15] == 7 && kept,
           "PMSEQ: 16 steps a lane packed two a byte (MSL), unpacked as written; a preset without an entry keeps the shapes");
    }
    /* SEMI: a lane of whole semitones (AMT 16: one a step), exact, the lane's glide stepping through them; SEM34 in
     * PARA: VCO 1 and 2 hold the key, VCO 3 and 4 move */
    {
        static const int8_t L[16] = {0, 3, 7, -5, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        static const double WANT[5] = {130.81, 155.56, 196.0, 98.0, 146.83};
        mar_part_t *A;
        uint32_t s0, good = 1;
        mare_part();
        memcpy(trk[0].ms[0], L, sizeof L);
        trk[0].p[P_MSKEY] = 1, trk[0].p[P_MSLEN] = 5, trk[0].p[P_MSRATE] = 4, trk[0].p[P_MSSMTH] = 60;
        trk[0].p[P_M1SRC] = MS_SEQ1, trk[0].p[P_M1DST] = MT_SEMI, trk[0].p[P_M1AMT] = 16;
        trk_note_on(&trk[0], 48, 100);
        A = mar_arena(&trk[0]);
        mx_render(0, 0, 2);
        for (s0 = 0; s0 < 5u; s0++) {                   /* (the steps as they come: each one's semitones exact) */
            uint32_t b, st = trk[0].ms_step;
            for (b = 0; b < 2000u && trk[0].ms_step == st; b++)
                mx_render(0, 0, 1);
            mx_render(0, 0, 30);                        /* (SMTH: its glide there, through whole semitones) */
            good &= A->semi == L[trk[0].ms_step % 5u];
        }
        trk_note_off(&trk[0], 48), mx_render(0, 0, 300);
        trk[0].ms[0][0] = -5, trk[0].p[P_MSLEN] = 1;    /* (one step, held: the pitch itself) */
        trk_note_on(&trk[0], 48, 100);
        mx_render(0, 0, 40), mx_render(l, 0, N / CTL);
        a = mag_of(l, 98.0, N), b = mag_of(l, 103.83, N), c = mag_of(l, 92.5, N);
        good &= A->semi == -5 && db(a / b) > 10.0 && db(a / c) > 10.0;
        trk_note_off(&trk[0], 48), mx_render(0, 0, 300);
        snprintf(w, sizeof w, "SEMI: SEQ1 x 16, five steps 0 3 7 -5 2: each step that many semitones (KEY: from the key, "
                 "exact through SMTH); -5 held: 98 Hz %+.1f / %+.1f dB over the semitones beside it", db(a / b), db(a / c));
        ok(good, w);
        mare_part();
        memcpy(trk[0].ms[0], L, sizeof L);
        trk[0].ms[0][0] = 7;
        trk[0].p[P_VOICE] = V_PARA, trk[0].p[P_MFFM] = 0, trk[0].p[P_MRING] = 12, trk[0].p[P_MSKEY] = 1, trk[0].p[P_MSLEN] = 1;
        trk[0].p[P_M1SRC] = MS_SEQ1, trk[0].p[P_M1DST] = MT_SEM34, trk[0].p[P_M1AMT] = 16;
        trk_note_on(&trk[0], 45, 100);
        mx_render(0, 0, 40), mx_render(l, 0, 256);
        a = mag_of(l, 110.0, 256 * CTL), b = mag_of(l, 164.81, 256 * CTL), c = mag_of(l, 220.0 * 1.4983 * 2, 256 * CTL);
        snprintf(w, sizeof w, "SEM34 in PARA: VCO 1 and 2 on the key (110 Hz %+.1f dB), VCO 3 a fifth up (+7: 164.8 Hz %+.1f "
                 "dB), each against the semitone below it (sem34 %d)", db(a / mag_of(l, 103.83, 256 * CTL)),
                 db(b / mag_of(l, 155.56, 256 * CTL)), A->sem34);
        ok(A->sem34 == 7 && db(b / mag_of(l, 155.56, 256 * CTL)) > 10.0 && db(a / mag_of(l, 103.83, 256 * CTL)) > 6.0 && c >= 0, w);
        trk_note_off(&trk[0], 45), mx_render(0, 0, 300);
    }
    /* DIST: no step at the block rate (every sample driven: no lines at f0 +- 1378 Hz); the open square through the VCOs' analog path (mar_ac): a crest of an analog square's, not a chip's */
    {
        double a1, sb, cr, pk = 0;
        mare_part();
        trk[0].p[P_E0] = MTW_SQR, trk[0].p[P_DIST] = 60, trk[0].p[P_E4] = 127;
        trk_note_on(&trk[0], 84, 100);
        mx_render(0, 0, 60), mx_render(l, 0, N / CTL);
        a1 = mag_of(l, 1046.5, N), sb = mag_of(l, 1046.5 + FS / (double)CTL, N);
        trk_note_off(&trk[0], 84), mx_render(0, 0, 300);
        mare_part();
        trk[0].p[P_E0] = MTW_SQR, trk[0].p[P_E4] = 127, trk[0].p[P_E6] = 0;
        trk_note_on(&trk[0], 36, 100);
        mx_render(0, 0, 60), mx_render(l, 0, N / CTL);
        for (i = 0; i < N; i++)
            pk = abs(l[i]) > pk ? abs(l[i]) : pk;
        cr = db(pk / rms_of(l, N));
        trk_note_off(&trk[0], 36), mx_render(0, 0, 300);
        snprintf(w, sizeof w, "DIST 60, C6 square: f0 + the block rate %.1f dB under the note (no block-rate step); the open "
                 "C2 square's crest %.1f dB (AC-coupled, its phase turned; a flat chip square's 0.4)", db(a1 / sb), cr);
        ok(db(a1 / sb) > 60.0 && cr > 3.0 && cr < 11.0, w);
    }
    /* the CPU admission: VCO 3 and 4 cost more (VC_MT_VCO each): fewer voices fit on the SMK-37 (its start: x 2) */
    {
        uint32_t n2, n4, r0 = vc_ratio;
        mare_part();
        vc_ratio = 512;
        n2 = vc_fit(&trk[0]);
        trk[0].p[P_MFFM] = 12, trk[0].p[P_MRING] = -12;
        n4 = vc_fit(&trk[0]);
        vc_ratio = (uint16_t)r0;
        snprintf(w, sizeof w, "admission at 2 x fm1-emu (the SMK-37): %u voices of a two-VCO MARE fit, %u of a four-VCO one", n2, n4);
        ok(n4 >= 2u && n4 < n2, w);
    }
    /* STDLY: a mono note in, its echoes centred (left and right within 1 dB) at TIME; FDBK 100 echoes, its tail rings */
    {
        mare_part();
        trk[0].p[P_E6] = 0;                             /* (no SPACE: a mono voice) */
        trk[0].p[P_ITYPE] = IT_SDLY, trk[0].p[P_IA] = 100, trk[0].p[P_IB] = 90, trk[0].p[P_IC] = 64, trk[0].p[P_IMIX] = 64;
        trk[0].p[P_REL] = 0;
        trk_note_on(&trk[0], 57, 100), mx_render(0, 0, 100), trk_note_off(&trk[0], 57);
        mx_render(0, 0, 400), mx_render(l, r, N / CTL);
        a = rms_of(l, N), b = rms_of(r, N);
        snprintf(w, sizeof w, "STDLY: the echoes of a released note centred (L %.0f R %.0f: %+.2f dB) and ringing",
                 a, b, db(a / b));
        ok(fabs(db(a / b)) < 1.0 && a > 30.0, w);
        trk[0].p[P_ITYPE] = IT_OFF, mx_render(0, 0, 400);
    }
}

static void t_presets(void)
{
    static const uint8_t ch[3] = {48, 55, 64};
    uint32_t pi, i, good = 1, loud = 1, freed = 1, wide = 1;
    char w[200];
    for (pi = 0; pi < ENG_MARE.npresets; pi++) {
        const preset_t *pr = &ENG_MARE.presets[pi];
        double r, sf;
        int inb, inb2, fil;
        for (i = 0; i < NPART; i++)
            trk_all_off(&trk[i]);
        run_mix(600, &r, &inb, 0);
        host_preset(&trk[0], ENGI_MARE, pi);
        trk[0].p[P_AMODE] = 0;
        trk[0].p[P_SPREAD] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;   /* (the width: the engine's own) */
        trk[0].p[P_ITYPE] = IT_OFF;     /* (and no INSERT: a chorus or a stereo delay widens any MODE) */
        for (i = 0; i < 3u; i++)
            trk_note_on(&trk[0], ch[i], 100);
        run_mix(1400, &r, &inb, &sf);
        for (i = 0; i < 3u; i++)
            trk_note_off(&trk[0], ch[i]);
        for (i = 0; i < 80u && sounding(&trk[0]); i++) {   /* (~29 s: BERLIN SEQ's release is 10 s) */
            double r2;
            run_mix(500, &r2, &inb2, 0);
            inb = inb && inb2;
        }
        if (!inb)
            good = 0, printf("  %s: out of bounds\n", pr->name);
        if (r < 300.0)
            loud = 0, printf("  %s: quiet (%.0f)\n", pr->name, r);
        if (sounding(&trk[0]))
            freed = 0, printf("  %s: %u voices left\n", pr->name, (unsigned)sounding(&trk[0]));
        fil = MTM_DEC[clamp(pr->e[7], MTM_ST, MTM_STSP)].fil;    /* (not e[7] % 3: MODE 6 .. 8 are SPLT, SPLN, ST/SP) */
        if (fil == MTF_ST && abs(pr->e[6]) >= 20 && sf < 0.05)
            wide = 0, printf("  %s: not wide (side / mid %.3f)\n", pr->name, sf);
        if ((fil == MTF_SER || fil == MTF_PAR) && sf > 0.001)
            wide = 0, printf("  %s: %s not mono (side / mid %.3f)\n", pr->name, fil == MTF_SER ? "SERI" : "PARA", sf);
    }
    snprintf(w, sizeof w, "all %u presets bounded, audible, their voices free after the release", (unsigned)ENG_MARE.npresets);
    ok(good && loud && freed && ENG_MARE.npresets >= 16u && ENG_MARE.npresets <= 32u, w);
    ok(wide, "the presets with SPACE (STER) wide (left and right apart), PARA's and SERI's mono");
}

/* the balance: SPACE moves the two filters apart, their sides' levels matched (eng_mare.c bal). A saw
 * pair (DTUNE 20) in STER, PARA and SPLT, SPACE 0 / 30 / 63 / -40, CUT 20 .. 120, RES 0 / 64 / 110: a held C4 and a held
 * Cm7, 1.5 s each, the mix's left and right before the limiter (as tests/balance_test.c), within 1 dB (a filter closed
 * past the note shown, not judged); the side kept (SPACE off 0: the timbres apart, the side / mid over 0.05).
 * VERBOSE=1: every figure */
static void bal_take(const uint8_t *notes, uint32_t nn, double *l, double *r, double *sf)
{
    int32_t o[2 * CTL];
    uint32_t b, i, nb = (FS * 3u / 2u) / CTL;
    double sl = 0, sr = 0, es = 0, em = 0;
    for (b = 0; b < FS / CTL; b++)
        mix_block(o, CTL);
    for (i = 0; i < nn; i++)
        trk_note_on(&trk[0], notes[i], 100);
    for (b = 0; b < nb; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            double a = lim_dac.dl[i], c = lim_dac.dr[i];
            sl += a * a, sr += c * c, es += (a - c) * (a - c), em += (a + c) * (a + c);
        }
    }
    for (i = 0; i < nn; i++)
        trk_note_off(&trk[0], notes[i]);
    for (b = 0; b < FS / CTL; b++)
        mix_block(o, CTL);
    *l = 10 * log10(sl / (nb * CTL) + 1e-9), *r = 10 * log10(sr / (nb * CTL) + 1e-9);
    *sf = em > 0 ? sqrt(es / em) : 0;
}
static void t_space_balance(void)
{
    static const uint8_t CH[4] = {60, 63, 67, 70}, N1[1] = {60};
    static const int8_t SP[4] = {0, 30, 63, -40};
    static const uint8_t CU[5] = {20, 40, 64, 90, 120}, RE[3] = {0, 64, 110}, MO[3] = {MTM_ST, MTM_PAR, MTM_SPLT};
    static const char *const MN[3] = {"STER", "PARA", "SPLT"};
    uint32_t mi, si, ci, ri, nbad = 0, nnar = 0, nrun = 0, nclosed = 0;
    double worst[3] = {0, 0, 0}, narrow = 9;
    int verbose = getenv("VERBOSE") != 0;
    char w[260];
    for (mi = 0; mi < 3u; mi++)
        for (si = 0; si < 4u; si++)
            for (ci = 0; ci < 5u; ci++)
                for (ri = 0; ri < 3u; ri++) {
                    double l1, r1, s1, l4, r4, s4, d;
                    uint32_t i;
                    host_tracks_init();              /* (the same start whatever ran before: as tests/balance_test.c) */
                    memset(trk[0].v, 0, sizeof trk[0].v);
                    for (i = 0; i < NPART; i++)
                        trk_all_off(&trk[i]);
                    plain(&trk[0]);
                    trk[0].p[P_E2] = 20, trk[0].p[P_E4] = CU[ci], trk[0].p[P_E5] = RE[ri];
                    trk[0].p[P_E6] = SP[si], trk[0].p[P_E7] = MO[mi];
                    trk[0].p[P_AMODE] = 0, trk[0].p[P_SPREAD] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
                    trk[0].p[P_ATK] = 0, trk[0].p[P_SUS] = 127, trk[0].p[P_REL] = 20;
                    trk[0].p[P_LD_FLT] = trk[0].p[P_LD_PIT] = trk[0].p[P_LD_SHP] = trk[0].p[P_LD_AMP] = 0;
                    song.sel = 0;
                    bal_take(N1, 1, &l1, &r1, &s1);
                    bal_take(CH, 4, &l4, &r4, &s4);
                    d = fabs(l1 - r1) > fabs(l4 - r4) ? fabs(l1 - r1) : fabs(l4 - r4);
                    {   /* judged: the filters open to the note (C4: the keyboard tracking's 0), the darker low-pass
                         * at CUT 24 (~100 Hz) or over, PARA's high-pass at CUT 110 (~6 kHz) or under; a filter closed
                         * further leaves its side more than 48 dB down, beyond the make-up: shown, not judged */
                        double c = CU[ci], v1 = c + 0.656 * SP[si], lp = mi == 1 || v1 > c ? c : v1;   /* (SPACE: VCF 1) */
                        if (lp < 24 || (mi == 1 && v1 > 110)) {
                            nclosed++;
                            if (verbose)
                                printf("  %s SPACE %3d CUT %3u RES %3u: a filter closed past the note (not judged): note "
                                       "%+5.2f, chord %+5.2f dB\n", MN[mi], SP[si], CU[ci], RE[ri], l1 - r1, l4 - r4);
                            continue;
                        }
                    }
                    nrun++;
                    worst[mi] = d > worst[mi] ? d : worst[mi];
                    if (SP[si] && mi == 0 && s1 < narrow)
                        narrow = s1;
                    nnar += SP[si] && mi == 0 && s1 < 0.03;
                    nbad += d > 1.0;
                    if (verbose || d > 1.0)
                        printf("  %s SPACE %3d CUT %3u RES %3u: note L %6.1f R %6.1f (%+5.2f, side/mid %.2f) | chord L %6.1f "
                               "R %6.1f (%+5.2f, %.2f)%s\n", MN[mi], SP[si], CU[ci], RE[ri], l1, r1, l1 - r1, s1, l4, r4,
                               l4 - r4, s4, d > 1.0 ? "  OVER" : "");
                }
    snprintf(w, sizeof w, "SPACE balanced: %u runs (STER PARA SPLT, SPACE 0 30 63 -40, CUT, RES), L and R within 1 dB "
             "(widest STER %.2f, PARA %.2f, SPLT %.2f dB; %u closed past the note)", (unsigned)nrun, worst[0], worst[1],
             worst[2], (unsigned)nclosed);
    ok(!nbad, w);
    snprintf(w, sizeof w, "SPACE off 0 (STER): the sides' timbres apart, side / mid at least %.3f", narrow);
    ok(!nnar, w);
}

static void t_stores(void)
{
    static project_t pj;
    up_rec_t r;
    int16_t v[P_COUNT], out[P_COUNT];
    uint32_t i;
    int same = 1;
    static const int16_t E[8] = {100, -12, 77, -33, 41, 99, -40, MTM_PARN};
    host_tracks_init();
    host_preset(&trk[0], ENGI_MARE, 3);
    host_preset(&trk[1], ENGI_RODEO, 2);
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = E[i];
    proj_capture(&pj);
    host_preset(&trk[0], 1, 0);
    host_preset(&trk[1], 1, 0);
    proj_apply(&pj, 1);
    for (i = 0; i < 8u; i++)
        same &= trk[0].p[P_E0 + i] == E[i];
    ok(trk[0].eng_req == ENGI_MARE && same && trk[1].eng_req == ENGI_RODEO &&
           trk[1].p[P_E0] == ENG_RODEO.presets[2].e[0] && trk[1].p[P_E3] == ENG_RODEO.presets[2].e[3],
       "a project keeps engine 26 (MARE) and its values (SY/FM, SPACE below 0); RODEO's (25) as before");
    for (i = 0; i < P_COUNT; i++)
        v[i] = i >= P_E0 ? E[i - P_E0] : TP[i].def;
    memset(&r, 0, sizeof r);
    r.used = UP_USED, r.engine = ENGI_MARE, r.name[0] = 'M';
    up_rec_set(&r, v);
    {                                                  /* (upreset.c up_values: mapped and clamped for its engine) */
        int16_t def[P_COUNT];
        for (i = 0; i < P_COUNT; i++)
            def[i] = i >= P_E0 ? ENGINES[r.engine]->edit[i - P_E0].def : TP[i].def;
        up_params(&r, out, def);
        for (i = P_E0; i < P_COUNT; i++)
            out[i] = (int16_t)clamp(out[i], ENGINES[r.engine]->edit[i - P_E0].min, ENGINES[r.engine]->edit[i - P_E0].max);
    }
    same = 1;
    for (i = 0; i < 8u; i++)
        same &= out[P_E0 + i] == E[i];
    ok(r.engine == ENGI_MARE && same, "a user preset keeps engine 26 and its values");
}

/* ------------------------------------------------------------------ cost --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
/* host instructions a sample, the mix with `notes` keys of preset pi of engine e held (one core: no split) */
static double mix_cost(uint32_t e, uint32_t pi, uint32_t notes)
{
    static int32_t o[2 * CTL];
    uint32_t i, nb = FS / CTL;
    uint64_t i0;
    host_tracks_init();
    for (i = 0; i < NPART; i++)                        /* (no voice left from the last case) */
        memset(trk[i].v, 0, sizeof trk[i].v);
    cpu1_units = 0;
    host_preset(&trk[0], e, pi);
    trk[0].p[P_AMODE] = 0;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_SUS] = 127, trk[0].p[P_ATK] = 0;        /* (every voice sounding throughout) */
    for (i = 0; i < notes; i++)
        trk_note_on(&trk[0], 48 + 3 * i, 100);
    for (i = 0; i < 64u; i++)
        mix_block(o, CTL);
    i0 = instr_now();
    for (i = 0; i < nb; i++)
        mix_block(o, CTL);
    return (double)(instr_now() - i0) / (nb * CTL);
}
static double engine_cost(uint32_t e, int print, uint32_t *worst_p, double *mean)
{
    uint32_t pi;
    double worst = 0, sum = 0;
    for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
        double c = (mix_cost(e, pi, 8) - mix_cost(e, pi, 0)) / 8;
        if (print)
            printf("mare:   %-14s %5.0f\n", ENGINES[e]->presets[pi].name, c);
        sum += c;
        if (c > worst)
            worst = c, *worst_p = pi;
    }
    *mean = sum / ENGINES[e]->npresets;
    return worst;
}
static void t_cost(void)
{
    static const uint8_t E[5] = {ENGI_MARE, ENGI_PONY, ENGI_PALOMINO, ENGI_STAMPEDE, ENGI_RODEO};
    double c[5], mean[5];
    uint32_t wp[5] = {0}, k;
    char w[300];
    if (!instr_now()) {
        printf("mare: cost: no instruction counter on this host (proc_pid_rusage); not measured\n");
        return;
    }
    printf("mare: cost, host instructions per sample and voice (8 voices held, one core):\n");
    for (k = 0; k < 5u; k++) {
        c[k] = engine_cost(E[k], !k, &wp[k], &mean[k]);
        printf("mare: %-8s heaviest %-14s %4.0f, mean %4.0f\n", ENGINES[E[k]]->name, ENGINES[E[k]]->presets[wp[k]].name, c[k],
               mean[k]);
    }
    snprintf(w, sizeof w, "cost: at most %.0f a sample and voice, mean %.0f (PALOMINO %.0f / %.0f; limit %.0f)", c[0], mean[0], c[2],
             mean[2], MAR_COST_MAX);
    ok(c[0] <= MAR_COST_MAX, w);
}

/* the MATRIX: MARE's normal, CUTOFF 1 into CUTOFF 2. STER, SPACE 0 (one filter for both sides): +V
 * into CUT (CUTOFF 1) darkens both, no side; a cable into CUTB (CUTOFF 2) breaks the normal, filter 2 back at its own
 * cutoff, the sides apart. And the new sources: +V into PITCH an octave up; S&H and NOISE bounded and moving */
static double patch_side(int32_t cut_amt, int32_t cutb_amt, uint32_t src2, double *peak)
{
    double sf = 0;
    int inb;
    uint32_t k;
    static int32_t o[CTL * 2];
    host_tracks_init();
    host_preset(&trk[0], ENGI_MARE, 0);
    plain(&trk[0]);
    clipper_proj_neutral();
    trk[0].p[P_AMODE] = 0, trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_E4] = 80, trk[0].p[P_ATK] = 0, trk[0].p[P_SUS] = 127;
    for (k = 0; k < MX_SLOTS; k++)
        trk[0].p[mx_pid(k)] = trk[0].p[mx_pid(k) + 1u] = trk[0].p[mx_pid(k) + 2u] = 0;
    trk[0].p[mx_pid(0)] = MS_DC, trk[0].p[mx_pid(0) + 1u] = MT_CUT, trk[0].p[mx_pid(0) + 2u] = (int16_t)cut_amt;
    trk[0].p[mx_pid(1)] = (int16_t)src2, trk[0].p[mx_pid(1) + 1u] = MT_CUTB, trk[0].p[mx_pid(1) + 2u] = (int16_t)cutb_amt;
    for (k = 0; k < 4u; k++)
        mix_block(o, CTL);
    trk_note_on(&trk[0], 45, 110);
    run_mix(400u, peak, &inb, &sf);
    return inb ? sf : -1.0;
}
static void t_patch(void)
{
    double pa, pb, pc, sa, sb, sc, sn;
    char w[200];
    sa = patch_side(0, 0, MS_DC, &pa);                /* nothing patched */
    sb = patch_side(-50, 0, MS_DC, &pb);              /* CUT1 down: both filters */
    sc = patch_side(-50, 1, MS_DC, &pc);              /* .. and a cable in CUT2: filter 2 its own */
    snprintf(w, sizeof w, "MATRIX: CUT1 moves both filters (sides %.3f, %.3f; level %.0f -> %.0f); a cable in CUT2 breaks it (side %.3f)",
             sa, sb, pa, pb, sc);
    ok(sa >= 0 && sb >= 0 && pb < pa * 0.5 && sc > 0.02 && sc > 4.0 * sb, w);   /* (a darker sound has a little side of
                                                      * its own: the cutoff knob turned down as far, no cable, too) */
    sn = patch_side(0, 40, MS_NOISE, &pa);            /* NOISE into CUT2: bounded, the sides apart */
    snprintf(w, sizeof w, "MATRIX: NOISE and S&H into CUT2 bounded, moving filter 2 (sides %.3f, %.3f)", sn,
             (sc = patch_side(0, 40, MS_SH, &pb)));
    ok(sn > 0.01 && sc > 0.01, w);
    ok(!strcmp(N_MSRC[MS_SH], "S&H") && !strcmp(N_MSRC[MS_NOISE], "NOISE") && !strcmp(N_MSRC[MS_DC], "+V") &&
           !strcmp(N_MDST[MT_CUTB], "CUTB") && MS_NSRC <= 32 && MT_COUNT <= 64,
       "MATRIX: the new jacks named, sources within the stores' 5 bits, destinations 6");
}

/* MARE's normal VCO1 PITCH -> VCO2 PITCH: a cable into PIT1 moves VCO 2 too until a
 * cable goes into PIT2. Per block, VCO 1's and VCO 2's phase steps (ph[0], ph[1]); the beat between them in Hz. And
 * PALOMINO (two VCOs too) unchanged */
#define PN_NB 300u
static double pn_d0[PN_NB], pn_d1[PN_NB];
static int32_t pn_ldpit, pn_ptgt;                      /* LFO > PIT and P>VCO for the next run */
static void pit_run(uint32_t eng, uint32_t s1, uint32_t d1, int32_t a1, uint32_t s2, uint32_t d2, int32_t a2)
{
    static int32_t o[CTL * 2];
    uint32_t b, i, k, vi = NVOICE, p0 = 0, p1 = 0;
    host_tracks_init();
    for (i = 0; i < NPART; i++)                        /* (each run from the same state: compared bit for bit) */
        memset(trk[i].v, 0, sizeof trk[i].v);
    rng_state = 0x1234567u;
    host_preset(&trk[0], eng, 0);
    if (eng == ENGI_MARE)
        plain(&trk[0]);
    clipper_proj_neutral();
    trk[0].p[P_AMODE] = 0, trk[0].p[P_VOICE] = V_POLY, trk[0].p[P_VINT] = 0, trk[0].p[P_GLIDE] = 0;
    trk[0].p[P_ATK] = 0, trk[0].p[P_SUS] = 127, trk[0].p[P_LRATE] = 90, trk[0].p[P_LD_PIT] = trk[0].p[P_ED_PIT] = 0;
    trk[0].p[P_LD_PIT] = (int16_t)pn_ldpit, trk[0].p[P_PTGT] = (int16_t)pn_ptgt;
    for (k = 0; k < MX_SLOTS; k++)
        trk[0].p[mx_pid(k)] = trk[0].p[mx_pid(k) + 1u] = trk[0].p[mx_pid(k) + 2u] = 0;
    trk[0].p[mx_pid(0)] = (int16_t)s1, trk[0].p[mx_pid(0) + 1u] = (int16_t)d1, trk[0].p[mx_pid(0) + 2u] = (int16_t)a1;
    trk[0].p[mx_pid(1)] = (int16_t)s2, trk[0].p[mx_pid(1) + 1u] = (int16_t)d2, trk[0].p[mx_pid(1) + 2u] = (int16_t)a2;
    for (k = 0; k < 4u; k++)
        mix_block(o, CTL);
    trk_note_on(&trk[0], 57, 110);
    for (b = 0; b <= PN_NB; b++) {
        mix_block(o, CTL);
        if (vi == NVOICE)
            for (i = 0; i < NVOICE; i++)
                if (trk[0].v[i].active)
                    vi = i;
        if (vi == NVOICE)
            continue;
        if (b)
            pn_d0[b - 1] = (double)(uint32_t)(trk[0].v[vi].ph[0] - p0) / CTL * FS / 4294967296.0,
            pn_d1[b - 1] = (double)(uint32_t)(trk[0].v[vi].ph[1] - p1) / CTL * FS / 4294967296.0;
        p0 = trk[0].v[vi].ph[0], p1 = trk[0].v[vi].ph[1];
    }
}
static double pn_span(const double *x)                 /* max - min over the blocks */
{
    double lo = 1e30, hi = -1e30;
    uint32_t b;
    for (b = 0; b < PN_NB; b++)
        lo = x[b] < lo ? x[b] : lo, hi = x[b] > hi ? x[b] : hi;
    return hi - lo;
}
static void t_pit_normal(void)
{
    double beat = 0, v1, v2;
    uint32_t b;
    char w[240];
    pit_run(ENGI_MARE, MS_LFO1, MT_PIT1, 40, MS_OFF, MT_OFF, 0);
    v1 = pn_span(pn_d0);
    for (b = 0; b < PN_NB; b++)
        beat = fabs(pn_d1[b] - pn_d0[b]) > beat ? fabs(pn_d1[b] - pn_d0[b]) : beat;
    snprintf(w, sizeof w, "PIT1 normal: LFO1 -> PIT1 +40 moves both VCOs (VCO 1 swings %.0f Hz, the beat at most %.3f Hz)", v1, beat);
    ok(v1 > 50.0 && beat < 0.05, w);
    pit_run(ENGI_MARE, MS_LFO1, MT_PIT1, 40, MS_RAND, MT_PIT2, 30);
    v1 = pn_span(pn_d0), v2 = pn_span(pn_d1);
    snprintf(w, sizeof w, "PIT1 normal: a cable into PIT2 breaks it (VCO 1 swings %.0f Hz, VCO 2 %.4f Hz: RAND alone)", v1, v2);
    ok(v1 > 50.0 && v2 < 0.01, w);
    pit_run(ENGI_PALOMINO, MS_LFO1, MT_PIT1, 40, MS_OFF, MT_OFF, 0);   /* (PALOMINO: ph[1] its VCO-2 too) */
    v1 = pn_span(pn_d0), v2 = pn_span(pn_d1);
    snprintf(w, sizeof w, "PIT1 normal: MARE's alone (PALOMINO: LFO1 -> PIT1 swings VCO-1 %.0f Hz, VCO-2 %.4f Hz)", v1, v2);
    ok(v1 > 50.0 && v2 < 0.01, w);
    {   /* P>VCO, the instrument's PITCH MOD ASSIGN: LFO > PIT on both VCOs, VCO 1 or VCO 2 alone */
        double s[3][2];
        uint32_t k;
        pn_ldpit = 40;
        for (k = 0; k < 3u; k++) {
            pn_ptgt = (int32_t)k;
            pit_run(ENGI_MARE, MS_OFF, MT_OFF, 0, MS_OFF, MT_OFF, 0);
            s[k][0] = pn_span(pn_d0), s[k][1] = pn_span(pn_d1);
        }
        pn_ldpit = pn_ptgt = 0;
        snprintf(w, sizeof w, "P>VCO: LFO > PIT on BOTH (VCO 1 %.0f, VCO 2 %.0f Hz), VCO1 (%.0f, %.3f), VCO2 (%.3f, %.0f)",
                 s[0][0], s[0][1], s[1][0], s[1][1], s[2][0], s[2][1]);
        ok(s[0][0] > 20 && s[0][1] > 20 && s[1][0] > 20 && s[1][1] < 0.5 && s[2][0] < 0.5 && s[2][1] > 20, w);   /* (the rest: the pitch's fraction in the fine tune, < 1 cent) */
    }
}

/* a factory preset's render, a chord held and released (as tests/filter2_test.c's): its
 * FNV-1a 64 hash, in a child process (every render from the power-on state); ids / vals: values set over the preset */
static const uint8_t GH_NOTES[6] = {48, 55, 60, 64, 67, 72};
static uint64_t gh_render(uint32_t e, uint32_t pi, uint32_t nset, const uint32_t *ids, const int16_t *vals)
{
    static int32_t o[CTL * 2];
    uint64_t h = 1469598103934665603ull;
    uint32_t b, i;
    host_tracks_init();
    for (i = 0; i < NPART; i++)
        host_preset(&trk[i], i ? 1u : e, i ? 0u : pi);
    clipper_proj_neutral();
    trk[1].p[P_LYMODE] = LY_OFF;
    for (i = 0; i < nset; i++)
        trk[0].p[ids[i]] = vals[i];
    for (b = 0; b < 4u; b++)
        mix_block(o, CTL);
    for (i = 0; i < 6u; i++)
        trk_note_on(&trk[0], GH_NOTES[i], 40u + 15u * i);
    for (b = 0; b < 300u; b++) {
        if (b == 180u)
            for (i = 0; i < 6u; i++)
                trk_note_off(&trk[0], GH_NOTES[i]);
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
    }
    return h;
}
static uint64_t gh_hash(uint32_t e, uint32_t pi, uint32_t nset, const uint32_t *ids, const int16_t *vals)
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    pid = fork();
    if (pid == 0) {
        close(fd[0]);
        h = gh_render(e, pi, nset, ids, vals);
        _exit(write(fd[1], &h, sizeof h) == (ssize_t)sizeof h ? 0 : 2);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != (ssize_t)sizeof h)
        h = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}
/* every MARE factory preset (MODE 0 .. 5) bit for bit as tests/mare_golden.txt holds it (written from the tree before
 * SPLT, FILTER FM and RING; GOLDEN_UPDATE=1 writes it) */
static void t_golden(void)
{
    static const char *path = "tests/mare_golden.txt";
    uint64_t want[64];
    uint32_t n = 0, pi, diff = 0;
    char line[96], w[200];
    FILE *f;
    if (getenv("GOLDEN_UPDATE")) {
        f = fopen(path, "w");
        if (!f)
            return;
        fprintf(f, "# MARE's factory presets (tests/mare_test.c gh_render): FNV-1a 64 of the output, before SPLT, FILTER FM, RING\n");
        for (pi = 0; pi < ENG_MARE.npresets; pi++)
            fprintf(f, "MARE/%02u %016llx\n", pi, (unsigned long long)gh_hash(ENGI_MARE, pi, 0, 0, 0));
        fclose(f);
        return;
    }
    if ((f = fopen(path, "r"))) {
        while (n < 64u && fgets(line, sizeof line, f)) {
            unsigned long long x;
            unsigned k;
            if (line[0] != '#' && sscanf(line, "MARE/%u %llx", &k, &x) == 2 && k == n)
                want[n++] = x;
        }
        fclose(f);
    }
    for (pi = 0; pi < ENG_MARE.npresets && pi < n; pi++)
        if (gh_hash(ENGI_MARE, pi, 0, 0, 0) != want[pi])
            printf("mare:   MARE/%02u differs\n", pi), diff++;
    snprintf(w, sizeof w, "every MARE factory preset (%u, MODE 0 .. 5) bit for bit as %s (%u differ)", (unsigned)n, path, diff);
    ok(n == ENG_MARE.npresets && !diff, w);
}
/* SPLT: VCO 1 alone into the left ladder, VCO 2 alone into the right (both low-pass, as STER). WAVE SQR (both VCOs:
 * odd harmonics only), VCO 2 an octave up: VCO 2's fundamental is no harmonic of VCO 1, VCO 1's none of VCO 2 */
static void t_split(void)
{
    track_t *t = &trk[0];
    double l1, l2, r1, r2, ms, mp;
    uint32_t i;
    char w[260];
    static const uint32_t ID7[1] = {P_E7};
    static const int16_t V6[1] = {6}, V5[1] = {5}, V3[1] = {3}, V127[1] = {127};
    ok(ENG_MARE.edit[7].max == 8 && !strcmp(N_MTM[MTM_SPLT], "SPLT") && !strcmp(N_MTM[MTM_SPLN], "SPL+N") &&
           MTM_SPLT == 6 && MTM_SPLN == 7 && MTM_STSP == 8 && !strcmp(N_MTM[MTM_STSP], "ST/SP"),
       "MODE: SPLT (6), SPL+N (7) and ST/SP (8) appended, its max 8");
    plain(t);
    t->p[P_E0] = MTW_SQR, t->p[P_E1] = 12, t->p[P_E7] = MTM_SPLT;
    mar_direct(t, 220.0, p16_of(220.0), 64 << 8);
    l1 = magc(220, N / 2, N / 2, 1), l2 = magc(440, N / 2, N / 2, 1);
    r1 = magc(220, N / 2, N / 2, 2), r2 = magc(440, N / 2, N / 2, 2);
    snprintf(w, sizeof w, "SPLT: VCO 1 left (440 Hz %.1f dB under 220), VCO 2 right (220 Hz %.1f dB under 440)", db(l2 / l1),
             db(r1 / r2));
    ok(db(l2 / l1) < -40.0 && db(r1 / r2) < -40.0 && l1 > 100 && r2 > 100, w);
    ms = 0;
    for (i = N / 2; i < N; i++)
        ms += (double)buf[i] * buf[i];
    t->p[P_E7] = MTM_ST;
    mar_direct(t, 220.0, p16_of(220.0), 64 << 8);
    mp = 0;
    for (i = N / 2; i < N; i++)
        mp += (double)buf[i] * buf[i];
    snprintf(w, sizeof w, "SPLT: its middle's level %+.1f dB against STER's", 10 * log10(ms / mp));
    ok(fabs(10 * log10(ms / mp)) < 4.0, w);
    t->p[P_E7] = MTM_SPLN;
    mar_direct(t, 220.0, p16_of(220.0), 64 << 8);
    l2 = magc(330, N / 2, N / 2, 1), r2 = magc(330, N / 2, N / 2, 2);
    t->p[P_E7] = MTM_SPLT;
    mar_direct(t, 220.0, p16_of(220.0), 64 << 8);
    l1 = magc(330, N / 2, N / 2, 1), r1 = magc(330, N / 2, N / 2, 2);
    snprintf(w, sizeof w, "SPL+N: noise in both sides (330 Hz left %+.1f dB, right %+.1f dB against SPLT)", db(l2 / l1), db(r2 / r1));
    ok(db(l2 / l1) > 20.0 && db(r2 / r1) > 20.0, w);
    snprintf(w, sizeof w, "MODE 6 on another engine its own value: RODEO's MODE 6 as its 5, PALOMINO's as its 3, ANALOG (0)'s KTR 6 not 127");
    ok(gh_hash(ENGI_RODEO, 0, 1, ID7, V6) == gh_hash(ENGI_RODEO, 0, 1, ID7, V5) &&
           gh_hash(ENGI_PALOMINO, 0, 1, ID7, V6) == gh_hash(ENGI_PALOMINO, 0, 1, ID7, V3) &&
           gh_hash(0u, 0, 1, ID7, V6) != gh_hash(0u, 0, 1, ID7, V127), w);
}

/* FILTER FM: VCO 2 on both cutoffs at audio rate; RING: VCO 1 x VCO 2 in the mixer, the plain VCOs down
 * as it rises, the level kept. 0 / 0 is the sound without them (t_golden: the factory presets have both at 0) */
static double band_energy(double lo, double hi, double f0)   /* the harmonics of f0 in [lo, hi), the middle */
{
    double e = 0, f;
    for (f = f0 * ceil(lo / f0); f < hi; f += f0) {
        double a = mag(f, N / 2, N / 2);
        e += a * a;
    }
    return e;
}
static double mid_rms(void)
{
    double e = 0;
    uint32_t i;
    for (i = N / 2; i < N; i++)
        e += (double)buf[i] * buf[i];
    return sqrt(e / (N / 2));
}
static void t_ffm_ring(void)
{
    track_t *t = &trk[0];
    double e0, e1, a0, b0, a1, b1, r0, r1, f1 = 220.0, f2 = 220.0 * pow(2, 7 / 12.0), c2 = 65.406;
    char w[260];
    static const uint32_t ID2[2] = {P_MFFM, P_MRING}, IDM[6] = {P_M1SRC, P_M1DST, P_M1AMT, P_M2SRC, P_M2DST, P_M2AMT};
    static const int16_t V0[2] = {MAR_VOFF, MAR_VOFF}, V127[2] = {24, 24}, VF[6] = {MS_DC, MT_FFM, 63, MS_OFF, MT_OFF, 0},
                         VR[6] = {MS_DC, MT_RING, 63, MS_OFF, MT_OFF, 0};
    host_preset(t, ENGI_MARE, 0);
    ok(P_COUNT == 184 && P_MFFM == P_BTGT + 1 && P_MRING == P_MFFM + 1 && P_M13SRC == P_MRING + 1 && P_MW34 == P_M24AMT + 1 &&
           !strcmp(TP[P_MFFM].label, "VCO3") && !strcmp(TP[P_MRING].label, "VCO4") && TP[P_MFFM].min == MAR_VOFF &&
           TP[P_MFFM].max == 24 && TP[P_MFFM].def == MAR_VOFF && TP[P_MRING].def == MAR_VOFF && TP[P_MW34].def == MTW_SAW,
       "VCO3, VCO4 at P_MFFM, P_MRING (after the BEND page), OFF .. +24; WAV34 before P_E0 "
       "(P_COUNT 184, PONY's EDIT 3 and RODEO's KYBD PW0 after it); FILTER FM and RING the matrix's alone");
    plain(t);
    t->p[P_E4] = 50;
    mar_direct(t, c2, p16_of(c2), 64 << 8);
    e0 = band_energy(4000, 12000, c2);
    md_ffm = 100 * 258;
    mar_direct(t, c2, p16_of(c2), 64 << 8);
    e1 = band_energy(4000, 12000, c2);
    snprintf(w, sizeof w, "FILTER FM 100 (the matrix's FLTFM): a C2 saw at CUT 50 brighter above 4 kHz (%+.1f dB)", 10 * log10(e1 / e0));
    ok(10 * log10(e1 / e0) > 6.0, w);
    plain(t);
    t->p[P_E1] = 7;
    mar_direct(t, f1, p16_of(f1), 64 << 8);
    a0 = mag(f2 - f1, N / 2, N / 2), b0 = mag(f2 + f1, N / 2, N / 2), r0 = mid_rms();
    md_ring = 127 * 258;
    mar_direct(t, f1, p16_of(f1), 64 << 8);
    a1 = mag(f2 - f1, N / 2, N / 2), b1 = mag(f2 + f1, N / 2, N / 2), r1 = mid_rms();
    snprintf(w, sizeof w, "RING 127, VCO2 +7: VCO 1 x VCO 2's difference (%+.1f dB) and sum (%+.1f dB) against the plain mix",
             db(a1 / a0), db(b1 / b0));
    ok(db(a1 / a0) > 10.0 && db(b1 / b0) > 10.0, w);
    snprintf(w, sizeof w, "RING 127: the level kept (RMS %+.2f dB against RING 0; SAW + SAW, DTUNE 0, VCO2 +7)", db(r1 / r0));
    ok(fabs(db(r1 / r0)) < 1.0, w);
    ok(gh_hash(ENGI_MARE, 0, 6, IDM, VF) != gh_hash(ENGI_MARE, 0, 0, 0, 0) &&
           gh_hash(ENGI_MARE, 0, 6, IDM, VR) != gh_hash(ENGI_MARE, 0, 0, 0, 0),
       "the matrix's FLTFM and RING move MARE's");
    ok(gh_hash(0u, 0, 2, ID2, V127) == gh_hash(0u, 0, 2, ID2, V0) &&
           gh_hash(ENGI_PALOMINO, 0, 2, ID2, V127) == gh_hash(ENGI_PALOMINO, 0, 2, ID2, V0) &&
           gh_hash(0u, 0, 6, IDM, VF) == gh_hash(0u, 0, 0, 0, 0) && gh_hash(ENGI_PALOMINO, 0, 6, IDM, VR) == gh_hash(ENGI_PALOMINO, 0, 0, 0, 0),
       "another engine: FILTER FM, RING 127 (and their matrix cables) bit for bit as at 0 (ANALOG, PALOMINO)");
    {                                                  /* bounded: both at the top, every mode, the resonance up */
        int32_t mx = 0;
        uint32_t k, i;
        for (k = 0; k < 16u; k++) {
            plain(t);
            md_ffm = 127 * 258, md_ring = (k & 1u ? 127 : 64) * 258, t->p[P_E5] = 127, t->p[P_E7] = (int16_t)(k % 9u);
            t->p[P_MFFM] = 12, t->p[P_MRING] = k & 1u ? -12 : MAR_VOFF;   /* (VCO 3 / 4 on too) */
            t->p[P_E1] = 7, t->p[P_E3] = (int16_t)(k & 8u ? 40 : -40), t->p[P_E4] = k & 2u ? 20 : 110, t->p[P_E6] = k & 4u ? 40 : 0;
            mar_direct(t, 27.5 * (k + 1), p16_of(27.5 * (k + 1)), 64 << 8);
            for (i = 0; i < N; i++)
                mx = abs(buf[i]) + abs(side[i]) > mx ? abs(buf[i]) + abs(side[i]) : mx;
        }
        snprintf(w, sizeof w, "FILTER FM, RING at the top, every MODE, RES 127, SYNC / FM: bounded (peak %d)", mx);
        ok(mx < (1 << 21), w);
    }
}

int main(int argc, char **argv)
{
    host_tracks_init();
    if (argc > 1 && !strcmp(argv[1], "balance")) {   /* (the balance sweep alone) */
        t_space_balance();
        return bad;
    }
    t_golden();                                       /* (first: its renders fork from a process that has played
                                                       * nothing yet, as regress.c's; after the other checks the FX
                                                       * and mix state they left reached every hash, so a trim of
                                                       * one MARE preset moved all 23) */
    t_engine();
    t_vco();
    t_wave();
    t_detune();
    t_sync_fm();
    t_vcf();
    t_noise();
    t_clean();
    t_bounded();
    t_unison();
    t_presets();
    t_stores();
    t_patch();
    t_pit_normal();
    t_split();
    t_ffm_ring();
    t_rebuild();
    t_para_assign();
    t_para_keep();
    t_space_balance();
    if (!(argc > 1 && !strcmp(argv[1], "nocost")))
        t_cost();
    printf(bad ? "mare: %d FAILED\n" : "mare: all passed\n", bad);
    return bad;
}
