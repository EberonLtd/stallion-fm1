/* SPDX-License-Identifier: GPL-3.0-only */
/* FX > FILTER > SLOPE (dsp.c fmod_slope, pole4_*) and FX > FILTER 2 (engines.c f2_*, voice.c vrender):
 *   build/host/filter2_test [GOLDEN]        COST=1: the host cost a voice of each setting (no checks)
 * 1. as before: every preset of every engine (a chord held, then released, through the FX, each in a child: the state
 *    from power-on) renders bit for bit as 1.0 (+ the OTA) did, with SLOPE at 24 and FILTER 2 OFF (its other values
 *    moved, and matrix slots sending to CUT2 / RES2): its hash as tests/filter2_golden.txt holds it (written by the
 *    tree before SLOPE: F2_GOLDEN_ONLY=1, GOLDEN_UPDATE=1); LADDR, OTA, DIODE at 24 sample for sample as 1.0's code (a
 *    copy below) under noise, the cutoff and RES jumping
 * 2. the slopes: sines from 2 to 8 times the cutoff (300 Hz), RES 0: LADDR, OTA, DIODE fit 12, 24, 48 dB an octave; at
 *    the cutoff and below as at 24 (48: its peak at RES 100 within 3 dB of 24's, the bass within 1 dB); the 2-pole
 *    models keep theirs whatever SLOPE says
 * 3. bounded: a minute of full-scale noise, the cutoff and RES jumping, every model at every SLOPE; RES 127 sings
 *    at 12 and 48 as at 24, bounded
 * 4. the engines: ANALOG / WAVE / VECTOR (MODL LADDR) and PONY (its OTA) darker as SLOPE goes 12, 24, 48; a change of
 *    SLOPE under a held note ducks (no click), SLOPE a page of FX > FILTER, projects and user presets keep it, the
 *    editor protocol passes over it (its ids stay 1.0's)
 * 5. FILTER 2's routes (dsp.c fmod_pair, as the engines run it): SER an LP and an HP make a band; PAR an LP and a resonant BP
 *    two peaks with a dip between; SPLIT the first left, the second right; every model x route bounded under noise
 *    beyond full scale at RES 127 (both filters)
 * 6. FILTER 2 in the engines: ANALOG / PONY (beside their filter) and FM6 / DIGITAL / FMX (after the voice) darker
 *    with an LP; SPLIT: the left as without FILTER 2, the right its own; LINK: the first one's CUT moves it, without
 *    LINK not; ENV DEST FLT and the matrix's CUT2 move it, RES2 sharpens it; a change of MODL ducks; bounded at RES 127
 *    on a hot voice; the page, OFS with LINK, projects and user presets (the matrix's RES2 in slots 1..6: the tail's
 *    fifth bit), the editor protocol's ids (FILTER 2's passed over)
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>
#include <sys/wait.h>
#ifndef F2_GOLDEN_ONLY
#define PROJ_HOST 1
static const uint8_t TRK_DEF[NPART][2] = {{ENGI_HYBRID, 0}, {1, 0}, {4, 5}};   /* (ui.c, Clipper) */
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? TRK_DEF[i][0] : 0u; }
#include "../firmware/src/project.c"
#define UP_HOST 1
#include "../firmware/src/upreset.c"
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("filter2: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static double db(double x) { return 20.0 * log10(x > 1e-12 ? x : 1e-12); }

/* ---------------------------------------------------- 1. as before --- */
static const uint8_t NOTES[6] = {48, 55, 60, 64, 67, 72};
static uint64_t render_preset(uint32_t e, uint32_t pi)
{
    static int32_t o[CTL * 2];
    uint64_t h = 1469598103934665603ull;
    uint32_t b, i;
    host_tracks_init();
    for (i = 0; i < NPART; i++)
        host_preset(&trk[i], i ? 1u : e, i ? 0u : pi);
    clipper_proj_neutral();
    trk[1].p[P_LYMODE] = LY_OFF;
#ifndef F2_GOLDEN_ONLY
    trk[0].p[P_F2CUT] = 10, trk[0].p[P_F2RES] = 127, trk[0].p[P_F2ROUTE] = F2_SPLIT, trk[0].p[P_F2LINK] = 0;   /* (OFF: */
    trk[0].p[P_M11SRC] = MS_VEL, trk[0].p[P_M11DST] = MT_CUT2, trk[0].p[P_M11AMT] = 50;   /* nothing of them heard) */
    trk[0].p[P_M12SRC] = MS_VEL, trk[0].p[P_M12DST] = MT_RES2, trk[0].p[P_M12AMT] = -40;
#endif
    for (b = 0; b < 4u; b++)
        mix_block(o, CTL);
    for (i = 0; i < 6u; i++)
        trk_note_on(&trk[0], NOTES[i], 40u + 15u * i);
    for (b = 0; b < 300u; b++) {
        if (b == 180u)
            for (i = 0; i < 6u; i++)
                trk_note_off(&trk[0], NOTES[i]);
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
    }
    return h;
}
static uint64_t child_hash(uint32_t e, uint32_t pi)
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    pid = fork();
    if (pid == 0) {
        close(fd[0]);
        h = render_preset(e, pi);
        _exit(write(fd[1], &h, sizeof h) == (ssize_t)sizeof h ? 0 : 2);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != (ssize_t)sizeof h)
        h = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}
static void t_golden(const char *path)
{
    static char name[NENGINES * 64][32];
    static uint64_t want[NENGINES * 64];
    uint32_t n = 0, e, pi, diff = 0, missing = 0, total = 0;
    char w[200], line[96];
    FILE *f;
    int upd = getenv("GOLDEN_UPDATE") != 0;
    if (!upd && (f = fopen(path, "r"))) {
        while (n < NELEM(want) && fgets(line, sizeof line, f)) {
            unsigned long long x;
            if (line[0] != '#' && sscanf(line, "%31s %llx", name[n], &x) == 2)
                want[n++] = x;
        }
        fclose(f);
    }
    f = upd ? fopen(path, "w") : 0;
    if (f)
        fprintf(f, "# every engine x preset (tests/filter2_test.c): FNV-1a 64 of the output, as 1.0 + the OTA rendered them\n");
    for (e = 0; e < NENGINES; e++)
        for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
            char nm[32];
            uint64_t h = child_hash(e, pi);
            uint32_t k;
            snprintf(nm, sizeof nm, "%s/%02u", ENGINES[e]->name, pi);
            total++;
            if (f) {
                fprintf(f, "%s %016llx\n", nm, (unsigned long long)h);
                continue;
            }
            for (k = 0; k < n && strcmp(name[k], nm); k++)
                ;
            if (k == n)
                missing++;
            else if (want[k] != h) {
                if (diff < 8)
                    printf("filter2:   %s differs\n", nm);
                diff++;
            }
        }
    if (f) {
        fclose(f);
        printf("filter2: %s written (%u renders)\n", path, total);
        return;
    }
    snprintf(w, sizeof w, "every preset of every engine (%u) as before, bit for bit, SLOPE 24, FILTER 2 OFF (%u differ, %u not in %s)",
             total, diff, missing, path);
    check(w, n > 0 && !diff && !missing);
}

#ifndef F2_GOLDEN_ONLY
/* 1.0's LADDR, OTA and DIODE (dsp.c before SLOPE), verbatim: the reference for SLOPE 24 */
static int32_t old_ladder(const ladder_t *c, int32_t x, int32_t *s)
{
    int64_t in = (int64_t)clamp(x, -131072, 131072) << 12, S, y, u;
    int32_t G = c->G, uq, v, j;
    in += (in * c->k) >> 13;
    S = ((int64_t)G * s[0] >> 16) + s[1];
    S = ((G * S) >> 16) + s[2];
    S = ((G * S) >> 16) + s[3];
    S = (c->H * S) >> 16;
    y = ((((int64_t)c->G4 * in) >> 16) + S) * c->inv >> 16;
    u = in - ((c->k * y) >> 12);
    {
        int64_t h = u >> 13;
        if (h <= -65536 || h >= 65536)
            uq = softclip(h < 0 ? -65536 : 65536) << 13;
        else
            uq = (softclip((int32_t)h) << 13) + (int32_t)(u - (h << 13));
    }
    for (j = 0; j < 4; j++) {
        v = (int32_t)(((int64_t)G * (uq - s[j]) + 32768) >> 16);
        uq = v + s[j];
        s[j] = uq + v;
    }
    return (uq + 2048) >> 12;
}
static int32_t old_diode(const zdf_t *z, int32_t x, int32_t *ls)
{
    int64_t in = (int64_t)clamp(x, -131072, 131072) << 12, S, T, y;
    int32_t u, v, j;
    in += (in * z->k) >> 13;
    T = (((int64_t)z->G * ls[1]) >> 16) + ls[2];
    T = ((z->G * T) >> 16) + ls[3];
    T = (z->H * T) >> 16;
    S = mul_sh(mul_sh(mul_sh(z->G, z->G, 16), z->G, 16), z->H1, 16);
    S = ((S * (int64_t)ls[0]) >> 16) + T;
    y = ((((int64_t)z->PG * in) >> 16) + S) * z->inv >> 16;
    u = zdf_sat(in - ((z->k * y) >> 12));
    v = (int32_t)(((int64_t)z->G1 * (u - ls[0]) + 32768) >> 16);
    u = v + ls[0], ls[0] = u + v;
    for (j = 1; j < 4; j++) {
        v = (int32_t)(((int64_t)z->G * (u - ls[j]) + 32768) >> 16);
        u = v + ls[j], ls[j] = u + v;
    }
    return (u + 4096) >> 13;
}
static void t_same24(void)
{
    static const int MODELS[3] = {FM_LADDER, FM_OTA, FM_DIODE};
    uint32_t m, b, i, r = 7, diff = 0;
    char w[160];
    for (m = 0; m < 3u; m++) {
        int32_t sv[2] = {0, 0}, ls[8] = {0}, os[4] = {0}, osv[2] = {0, 0};
        for (b = 0; b < 20u * FS / CTL; b++) {
            fmod_t f;
            int32_t cut, res;
            r = r * 1664525u + 1013904223u;
            cut = (int32_t)((r >> 8) % (128u << 8));
            res = (b & 3u) == 0 ? 127 : (int32_t)((r >> 20) % 128u);
            fmod_coef(&f, MODELS[m], cut, res);
            if (b & 1u)
                fmod_slope(&f, FS_24);                   /* (called or not: the same) */
            for (i = 0; i < CTL; i++) {
                int32_t x, y, yo;
                r = r * 1664525u + 1013904223u;
                x = ((int32_t)(r >> 15) - 65536) * (b & 4u ? 2 : 1) / ((b & 8u) ? 8 : 1);
                y = fmod_tick(&f, x, sv, ls);
                yo = MODELS[m] == FM_LADDER ? old_ladder(&f.lad, x, os) >> 1 : MODELS[m] == FM_OTA ?
                     ota_tick(&f.lad, x, os, 0) >> 1 : old_diode(&f.z, x, os);
                (void)osv;
                diff += y != yo;
            }
        }
    }
    snprintf(w, sizeof w, "LADDR, OTA, DIODE at SLOPE 24: 20 s of noise each, sample for sample as 1.0's code (%u differ)", diff);
    check(w, !diff);
}

/* ------------------------------------------------------ 2. slopes --- */
static int cut_index(double hz)
{
    int i, best = 0;
    for (i = 0; i < 128; i++)
        if (fabs(log(CUTOFF_HZ[i] / hz)) < fabs(log(CUTOFF_HZ[best] / hz)))
            best = i;
    return best;
}
/* the gain (dB) of a model at a slope for a sine of hz at amp: the output at hz (a DFT bin: the OTA's rounding leaves a
 * DC of a unit or two, and the floor of the integer signal is near -80 dB) */
static double fgain(int model, int slope, int32_t cut, int32_t res, double hz, double amp)
{
    fmod_t f;
    int32_t sv[2] = {0, 0}, ls[8] = {0};
    double re = 0, im = 0;
    int i, n = FS;
    fmod_coef(&f, model, cut, res);
    fmod_slope(&f, slope);
    for (i = 0; i < n; i++) {
        double ph = 2 * M_PI * hz * i / FS;
        int32_t x = (int32_t)lrint(amp * sin(ph)), y = fmod_tick(&f, x, sv, ls);
        if (i >= n / 2) {
            re += 2.0 * y * cos(ph);                     /* (fmod_tick: half scale) */
            im += 2.0 * y * sin(ph);
        }
    }
    return db(2.0 * sqrt(re * re + im * im) / (n / 2) / amp);
}
/* dB an octave from 1.4 to 5.7 times the cutoff: a least-squares line through the points above the floor (-75 dB) */
static double fslope(int model, int slope, int ci)
{
    double fc = CUTOFF_HZ[ci], sx = 0, sy = 0, sxx = 0, sxy = 0;
    int k, n = 0;
    for (k = 0; k <= 8; k++) {
        double oct = 0.5 + k * 0.25, g = fgain(model, slope, ci << 8, 0, fc * pow(2.0, oct), 16000);
        if (g < -75.0)
            continue;
        sx += oct, sy += g, sxx += oct * oct, sxy += oct * g, n++;
    }
    return n >= 3 ? (n * sxy - sx * sy) / (n * sxx - sx * sx) : 0;
}
static void t_slopes(void)
{
    static const int MODELS[3] = {FM_LADDER, FM_OTA, FM_DIODE};
    static const char *const NM[3] = {"LADDR", "OTA", "DIODE"};
    static const double LO[3] = {-14.5, -27.5, -55.0}, HI[3] = {-9.5, -19.5, -36.0};
    int ci = cut_index(300), m, s;
    char w[200];
    for (m = 0; m < 3; m++) {
        double sl[3], bass[3], at[3];
        for (s = 0; s < 3; s++) {
            sl[s] = fslope(MODELS[m], s, ci);
            bass[s] = fgain(MODELS[m], s, ci << 8, 0, CUTOFF_HZ[ci] / 8, 8000);
            at[s] = fgain(MODELS[m], s, ci << 8, 0, CUTOFF_HZ[ci], 8000);
        }
        snprintf(w, sizeof w, "%-5s above the cutoff: 12 %5.1f, 24 %5.1f, 48 %5.1f dB/oct; at fc/8 %+.1f %+.1f %+.1f; at fc %+.1f %+.1f %+.1f",
                 NM[m], sl[0], sl[1], sl[2], bass[0], bass[1], bass[2], at[0], at[1], at[2]);
        check(w, sl[0] > LO[0] && sl[0] < HI[0] && sl[1] > LO[1] && sl[1] < HI[1] && sl[2] > LO[2] && sl[2] < HI[2] &&
                 fabs(bass[0] - bass[1]) < 1.0 && fabs(bass[2] - bass[1]) < 1.0);
    }
    for (m = 0; m < 3; m++) {                            /* 48: the resonance's peak as high as 24's */
        double pk[2] = {-99, -99}, lo[2];
        int k;
        for (s = 0; s < 2; s++) {
            for (k = -6; k <= 6; k++) {
                double g = fgain(MODELS[m], s ? FS_48 : FS_24, ci << 8, 100, CUTOFF_HZ[ci] * pow(2.0, k / 24.0), 4000);
                if (g > pk[s])
                    pk[s] = g;
            }
            lo[s] = fgain(MODELS[m], s ? FS_48 : FS_24, ci << 8, 100, CUTOFF_HZ[ci] / 8, 4000);
        }
        snprintf(w, sizeof w, "%-5s RES 100: the peak at 24 %+.1f dB, at 48 %+.1f; the bass %+.1f, %+.1f", NM[m], pk[0], pk[1],
                 lo[0], lo[1]);
        check(w, fabs(pk[1] - pk[0]) < 3.0 && fabs(lo[1] - lo[0]) < 1.0);
    }
    {   /* the 2-pole models: SLOPE does nothing */
        static const int TWO[6] = {FM_SVF, FM_SK, FM_WARM, FM_NOTCH, FM_BP, FM_HP};
        int same = 1;
        for (m = 0; m < 6; m++)
            for (s = 0; s < 3; s += 2) {
                fmod_t a, b;
                int32_t sa[2] = {0, 0}, la[8] = {0}, sb[2] = {0, 0}, lb[8] = {0}, i;
                uint32_t r = 3;
                fmod_coef(&a, TWO[m], 60 << 8, 90);
                fmod_coef(&b, TWO[m], 60 << 8, 90);
                fmod_slope(&b, s);
                for (i = 0; i < 4000; i++) {
                    r = r * 1664525u + 1013904223u;
                    same &= fmod_tick(&a, (int32_t)(r >> 16) - 32768, sa, la) == fmod_tick(&b, (int32_t)(r >> 16) - 32768, sb, lb);
                }
            }
        check("SVF SK WARM NOTCH BP HP: SLOPE 12 and 48 as 24 (their own slope)", same);
    }
}

/* ----------------------------------------------------- 3. bounded --- */
static void t_bounded(void)
{
    int m, s, wm = 0, ws = 0;
    uint32_t r = 1, i, b;
    int32_t mxo = 0, mxs = 0;
    char w[200];
    for (m = FM_SVF; m <= FM_LAST; m++)
        for (s = 0; s < 3; s++) {
            int32_t sv[2] = {0, 0}, ls[8] = {0}, o0 = mxo;
            if (m != FM_LADDER && m != FM_OTA && m != FM_DIODE)
                continue;                                /* (the others: SLOPE is not theirs; synth9 bounds them) */
            for (b = 0; b < 20u * FS / CTL; b++) {
                fmod_t f;
                int32_t cut, res, j;
                r = r * 1664525u + 1013904223u;
                cut = (b & 7u) == 0 ? 127 << 8 : (b & 7u) == 1 ? 0 : (int32_t)((r >> 8) % (128u << 8));
                res = (b & 3u) == 0 ? 127 : (int32_t)((r >> 20) % 128u);
                fmod_coef(&f, m, cut, res);
                fmod_slope(&f, s);
                for (i = 0; i < CTL; i++) {
                    int32_t x, y;
                    r = r * 1664525u + 1013904223u;
                    x = ((int32_t)(r >> 15) - 65536) * 2;   /* full scale and beyond (the clamp) */
                    y = fmod_tick(&f, x, sv, ls);
                    if (abs(y) > mxo)
                        mxo = abs(y);
                    for (j = 0; j < 8; j++)
                        if (abs(ls[j]) > mxs)
                            mxs = abs(ls[j]);
                }
            }
            if (mxo > o0)
                wm = m, ws = s;
        }
    snprintf(w, sizeof w, "LADDR OTA DIODE at every SLOPE, 20 s of noise beyond full scale, cutoff and RES jumping: out %d (%s %d), states %d",
             mxo, N_FMODEL[wm - 1], ws == 0 ? 12 : ws == 1 ? 24 : 48, mxs);
    check(w, mxo < 400000 && mxs < 2000000000);
    for (m = 0; m < 3; m++) {                            /* RES 127: sings at 12 and 48 too, bounded */
        static const int MODELS[3] = {FM_LADDER, FM_OTA, FM_DIODE};
        for (s = 0; s < 3; s += 2) {
            fmod_t f;
            int32_t sv[2] = {0, 0}, ls[8] = {0}, mx = 0, tail = 0, n;
            fmod_coef(&f, MODELS[m], cut_index(440) << 8, 127);
            fmod_slope(&f, s);
            for (n = 0; n < 2 * FS; n++) {
                int32_t y = fmod_tick(&f, n < 32 ? 20000 : 0, sv, ls);
                if (abs(y) > mx)
                    mx = abs(y);
                if (n > FS && abs(y) > tail)
                    tail = abs(y);
            }
            snprintf(w, sizeof w, "%s SLOPE %d at RES 127: rings on (a second later %d), bounded (peak %d)",
                     m == 0 ? "LADDR" : m == 1 ? "OTA" : "DIODE", s ? 48 : 12, tail, mx);
            check(w, tail > 500 && mx < 200000);
        }
    }
}

/* ----------------------------------------------------- 4. engines --- */
/* a function of the DSP state from power-on: in a child (a double back through a pipe) */
static double in_child(double (*fn)(const void *), const void *arg)
{
    int fd[2];
    double r = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    pid = fork();
    if (pid == 0) {
        close(fd[0]);
        r = fn(arg);
        _exit(write(fd[1], &r, sizeof r) == (ssize_t)sizeof r ? 0 : 2);
    }
    close(fd[1]);
    if (read(fd[0], &r, sizeof r) != (ssize_t)sizeof r)
        r = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return r;
}
typedef struct { uint32_t e, pi, cut_id; int cut, model, s; } bright_t;
/* the power of x[0..n) from lo to hi Hz (DFT bins every 10 Hz, a Hann window: no leakage of the bass) */
static double band(const double *x, uint32_t n, double lo, double hi)
{
    double f, p = 0;
    uint32_t k;
    for (f = lo; f <= hi; f += 10.0) {
        double re = 0, im = 0;
        for (k = 0; k < n; k++) {
            double w = x[k] * (0.5 - 0.5 * cos(2 * M_PI * k / n));
            re += w * cos(2 * M_PI * f * k / FS), im += w * sin(2 * M_PI * f * k / FS);
        }
        p += re * re + im * im;
    }
    return p;
}
/* a held low note (C2) of an engine at a SLOPE, its cutoff low (~260 Hz), no drive, envelope or LFO on it: 1 to 2 octaves up
 * against 40 to 300 Hz, dB */
static double eng_bright1(const void *arg)
{
    const bright_t *c = arg;
    static int32_t o[CTL * 2];
    static double x[200 * CTL];
    uint32_t b, i, n = 0;
    int s = c->s, model = c->model;
    host_tracks_init();
    host_preset(&trk[0], c->e, c->pi);
    trk[0].p[c->cut_id] = (int16_t)c->cut;
    trk[0].p[P_E6] = 0;                                  /* (DRV; VECTOR's MOVE, PONY's RES) */
    trk[0].p[P_LD_PIT] = 0, trk[0].p[P_VINT] = 0;
    clipper_proj_neutral();
    trk[1].p[P_LYMODE] = LY_OFF;
    trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_CHOR] = trk[0].p[P_DIST] = 0;
    trk[0].p[P_SPREAD] = 0;
    trk[0].p[P_LD_FLT] = 0, trk[0].p[P_ED_FLT] = 0;
    trk[0].p[P_SUS] = 127;
    if (model)
        trk[0].p[P_FMODEL] = (int16_t)model;
    trk[0].p[P_FSLOPE] = (int16_t)s;
    trk[0].fm_eff = trk[0].fx_eff = 0;
    for (b = 0; b < 4u; b++)
        mix_block(o, CTL);
    trk_note_on(&trk[0], 36, 100);
    for (b = 0; b < 400u; b++) {
        mix_block(o, CTL);
        if (b >= 200u)
            for (i = 0; i < CTL; i++)
                x[n++] = o[2 * i];
    }
    return 10 * log10((band(x, n, 520, 1040) + 1) / (band(x, n, 40, 300) + 1));
}
static double eng_bright(uint32_t e, uint32_t pi, uint32_t cut_id, int s, int model)
{
    bright_t c = {e, pi, cut_id, 44, model, s};
    return in_child(eng_bright1, &c);
}
static void t_engines(void)
{
    static const struct { uint32_t e, pi, cut_id; int model; const char *nm; } C[] = {
        {0, 6, P_E4, FM_LADDER, "ANALOG REESE, MODL LADDR"}, {ENGI_WAVE, 0, P_E4, FM_LADDER, "WAVE WT BASIC, MODL LADDR"},
        {ENGI_VECTOR, 0, P_E7, FM_DIODE, "VECTOR VS PAD, MODL DIODE"}, {ENGI_PONY, 1, P_E5, 0, "PONY BRASS (its OTA)"},
        {0, 6, P_E4, FM_OTA, "ANALOG REESE, MODL OTA"},
    };
    uint32_t k;
    char w[200];
    for (k = 0; k < NELEM(C); k++) {
        double b12 = eng_bright(C[k].e, C[k].pi, C[k].cut_id, FS_12, C[k].model);
        double b24 = eng_bright(C[k].e, C[k].pi, C[k].cut_id, FS_24, C[k].model);
        double b48 = eng_bright(C[k].e, C[k].pi, C[k].cut_id, FS_48, C[k].model);
        snprintf(w, sizeof w, "%s (CUT 44): 520..1040 Hz at SLOPE 12 %.1f, 24 %.1f, 48 %.1f dB (darker each time)", C[k].nm,
                 b12, b24, b48);
        check(w, b12 > b24 + 3.0 && b24 > b48 + 3.0);
    }
}
/* a change of SLOPE under a held note: the part ducks, no step */
static void t_duck(void)
{
    static int32_t o[CTL * 2];
    int32_t prev = 0, jump = 0, pk = 0;
    uint32_t b, i;
    char w[160];
    host_tracks_init();
    host_preset(&trk[0], 0, 6);
    clipper_proj_neutral();
    trk[1].p[P_LYMODE] = LY_OFF;
    trk[0].p[P_FMODEL] = FM_LADDER;
    trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_CHOR] = 0;
    trk[0].p[P_E5] = 110;
    trk[0].fm_eff = trk[0].fx_eff = 0;
    for (b = 0; b < 4u; b++)
        mix_block(o, CTL);
    trk_note_on(&trk[0], 48, 110);
    for (b = 0; b < 600u; b++) {
        if (b == 200u)
            trk[0].p[P_FSLOPE] = FS_48;
        if (b == 400u)
            trk[0].p[P_FSLOPE] = FS_12;
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t x = o[2 * i];
            if (b > 100u && abs(x - prev) > jump)
                jump = abs(x - prev);
            if (abs(x) > pk)
                pk = abs(x);
            prev = x;
        }
    }
    snprintf(w, sizeof w, "SLOPE 24 -> 48 -> 12 under a held note: ducked (the largest step %d, peak %d), it plays it", jump, pk);
    check(w, jump < pk / 3 && trk[0].fx_eff == 1 + FS_12);
}
static void t_stores(void)
{
    char w[200];
    int pg = 0;
    uint32_t i, k;
    for (i = 0; i < NPAGES; i++)
        if (!strcmp(PAGES[i].title, "FILTER"))
            for (k = 0; k < 4u; k++)
                pg |= PAGES[i].id[k] == P_FSLOPE;
    snprintf(w, sizeof w, "SLOPE on FX > FILTER (%s, %s .. %s dB, default 24), not lockable", TP[P_FSLOPE].label,
             TP[P_FSLOPE].names[0], TP[P_FSLOPE].names[2]);
    check(w, pg && TP[P_FSLOPE].def == FS_24 && !p_lockable(P_FSLOPE) && strlen(TP[P_FSLOPE].label) <= 5);
    check("the editor protocol: 127 values (1.0's and P>VCO, P_E0 119 there), SLOPE and after passed over (core.h p_of_ed)",
          P_EDN == 127u && p_of_ed(117) == P_M12AMT && p_of_ed(118) == P_PTGT && p_of_ed(119) == P_E0 &&
          p_of_ed(126) == P_E7 && ed_of_p(P_E0) == 119u);
    {   /* a project: both sounds' SLOPE back */
        static project_t pj;
        host_tracks_init();
        trk[0].p[P_FSLOPE] = FS_48, trk[1].p[P_FSLOPE] = FS_12;
        proj_capture(&pj);
        trk[0].p[P_FSLOPE] = trk[1].p[P_FSLOPE] = FS_24;
        proj_apply(&pj, 1);
        check("a project keeps both sounds' SLOPE (track 4's nudges), the drum track's nudges none",
              trk[0].p[P_FSLOPE] == FS_48 && trk[1].p[P_FSLOPE] == FS_12 && !trk[3].micro[0] && !trk[3].micro[5]);
        memset(pj.t[3].micro, 0, sizeof pj.t[3].micro);  /* (as 1.0 wrote it) */
        memset(&proj_xss[proj_x10_at(&pj)], 0, sizeof proj_xss[0]);   /* (nor the block beside it: Stallion's since 1.3) */
        proj_apply(&pj, 1);
        check("a 1.0 project: SLOPE at its default (24)", trk[0].p[P_FSLOPE] == FS_24 && trk[1].p[P_FSLOPE] == FS_24);
    }
    {   /* a user preset: SLOPE (preset format 2: every value a byte) */
        up_rec_t r;
        int16_t v[P_COUNT], out[P_COUNT], def[P_COUNT];
        for (i = 0; i < P_COUNT; i++)
            def[i] = i >= P_E0 ? 0 : TP[i].def, v[i] = def[i];
        v[P_FSLOPE] = FS_12;
        v[P_SPREAD] = 77;
        memset(&r, 0, sizeof r);
        r.used = UP_USED, r.name[0] = 'T';
        up_rec_set(&r, v);
        up_params(&r, out, def);
        check("a user preset keeps SLOPE", out[P_FSLOPE] == FS_12 && out[P_SPREAD] == 77);
    }
}

/* ----------------------------------------------- 5. FILTER 2's routes --- */
/* (dsp.c FM_PAIR, the MODEL filter and FILTER 2 together in the engines' loops; engines.c f2_pair) */
/* the pair as the engines run it (dsp.c FM_PAIR): filter 1 (m1 at cutoff index c1, RES r1), FILTER 2 (m2, c2, r2) */
static void pair_set(fmod_t *pr, fmod_t *f1, fmod_t *f2, f2st_t *st, int m1, int c1, int r1, int m2, int c2, int r2, int route)
{
    fmod_coef(f1, m1, c1, r1);
    fmod_coef(f2, m2, c2, r2);
    memset(st, 0, sizeof *st);
    *pr = *f1;
    pr->model = FM_PAIR, pr->route = (uint8_t)route, pr->p1 = f1, pr->p2 = f2, pr->st = st;
}
/* the gain (dB) at hz of the pair (filter 1 at c1 Hz, FILTER 2 at c2 Hz); SPLIT: the left (mid + side) or the right
 * (mid - side) */
static double pair_gain(int m1, int c1, int r1, int m2, int c2, int r2, int route, int right, double hz)
{
    fmod_t pr, f1, f2;
    f2st_t st;
    int32_t sv[2] = {0, 0}, ls[8] = {0};
    double re = 0, im = 0, amp = 8000;
    int i, n = FS;
    pair_set(&pr, &f1, &f2, &st, m1, cut_index(c1) << 8, r1, m2, cut_index(c2) << 8, r2, route);
    for (i = 0; i < n; i++) {
        double ph = 2 * M_PI * hz * i / FS;
        int32_t x = (int32_t)lrint(amp * sin(ph)), y, d = 0;
        st.sd = &d;
        y = fmod_tick(&pr, x, sv, ls);
        if (route == F2_SPLIT)
            y = right ? y - d : y + d;                   /* (mid - side, mid + side) */
        if (i >= n / 2)
            re += 2.0 * y * cos(ph), im += 2.0 * y * sin(ph);  /* (half scale) */
    }
    return db(2.0 * sqrt(re * re + im * im) / (n / 2) / amp);
}
static void t_routes(void)
{
    char w[220];
    double a, b, c, d;
    int32_t i, same = 1;
    a = pair_gain(FM_SVF, 3000, 0, FM_HP, 300, 0, F2_SER, 0, 60);
    b = pair_gain(FM_SVF, 3000, 0, FM_HP, 300, 0, F2_SER, 0, 950);
    c = pair_gain(FM_SVF, 3000, 0, FM_HP, 300, 0, F2_SER, 0, 12000);
    snprintf(w, sizeof w, "SER: LP 3 kHz then HP 300 Hz, a band: 60 Hz %+.1f, 950 Hz %+.1f, 12 kHz %+.1f dB", a, b, c);
    check(w, b > -4.0 && a < b - 12.0 && c < b - 12.0);
    a = pair_gain(FM_SVF, 300, 0, FM_BP, 3000, 110, F2_PAR, 0, 150);
    b = pair_gain(FM_SVF, 300, 0, FM_BP, 3000, 110, F2_PAR, 0, 950);
    c = pair_gain(FM_SVF, 300, 0, FM_BP, 3000, 110, F2_PAR, 0, 3000);
    snprintf(w, sizeof w, "PAR: LP 300 Hz and BP 3 kHz, two peaks: 150 Hz %+.1f, a dip at 950 Hz %+.1f, 3 kHz %+.1f dB", a, b, c);
    check(w, a > b + 6.0 && c > b + 6.0);
    a = pair_gain(FM_SVF, 300, 0, FM_HP, 3000, 0, F2_SPLIT, 0, 3000);
    b = pair_gain(FM_SVF, 300, 0, FM_HP, 3000, 0, F2_SPLIT, 1, 3000);
    c = pair_gain(FM_SVF, 300, 0, FM_HP, 3000, 0, F2_SPLIT, 1, 100);
    d = pair_gain(FM_SVF, 300, 0, FM_HP, 3000, 0, F2_SPLIT, 0, 100);
    {   /* the left (mid + side) is filter 1 alone, to a unit of rounding */
        fmod_t pr, f1, f2, g;
        f2st_t st;
        int32_t sv[2] = {0, 0}, ls[8] = {0}, s2[2] = {0, 0}, l2[8] = {0};
        uint32_t r = 5;
        pair_set(&pr, &f1, &f2, &st, FM_SVF, cut_index(300) << 8, 0, FM_HP, cut_index(3000) << 8, 0, F2_SPLIT);
        g = f1;
        for (i = 0; i < 20000; i++) {
            int32_t x, y, y0, dd = 0;
            r = r * 1664525u + 1013904223u;
            x = (int32_t)(r >> 17) - 16384;
            y0 = fmod_tick(&g, x, s2, l2);
            st.sd = &dd;
            y = fmod_tick(&pr, x, sv, ls);
            same &= abs(y + dd - y0) <= 1;
        }
    }
    snprintf(w, sizeof w, "SPLIT: LP 300 Hz left, HP 3 kHz right: at 3 kHz left %+.1f, right %+.1f; at 100 Hz left %+.1f, right %+.1f dB; "
             "the left the LP's", a, b, d, c);
    check(w, same && b > a + 20.0 && d > c + 20.0);
}
static void t_routes_bounded(void)
{
    int m, route;
    uint32_t r = 9, b, i;
    int32_t mxo = 0, mxs = 0;
    char w[220];
    for (m = FM_SVF; m <= FM_LAST; m++)
        for (route = F2_SER; route <= F2_SPLIT; route++) {
            fmod_t pr, f1, f2;
            f2st_t st, keep;
            int32_t sv[2] = {0, 0}, ls[8] = {0};
            memset(&keep, 0, sizeof keep);
            for (b = 0; b < 5u * FS / CTL; b++) {
                int32_t c1, c2, j;
                r = r * 1664525u + 1013904223u;
                c1 = (b & 7u) == 0 ? 127 << 8 : (int32_t)((r >> 8) % (128u << 8));
                c2 = (b & 7u) == 1 ? 0 : (int32_t)((r >> 3) % (128u << 8));
                pair_set(&pr, &f1, &f2, &st, FM_LADDER, c1, 127, m, c2, 127, route);
                st = keep;                               /* (FILTER 2's state goes on) */
                for (i = 0; i < CTL; i++) {
                    int32_t x, y, d = 0;
                    r = r * 1664525u + 1013904223u;
                    x = ((int32_t)(r >> 15) - 65536) * 2;
                    st.sd = &d;
                    y = fmod_tick(&pr, x, sv, ls);
                    if (abs(y) > mxo)
                        mxo = abs(y);
                    if (abs(d) > mxo)
                        mxo = abs(d);
                    for (j = 0; j < 4; j++)
                        if (abs(st.s[j]) > mxs)
                            mxs = abs(st.s[j]);
                }
                keep = st;
            }
        }
    snprintf(w, sizeof w, "FILTER 2: every model x route after LADDR, both at RES 127, 5 s of noise beyond full scale, the cutoffs "
             "jumping: out %d, states %d", mxo, mxs);
    check(w, mxo < 600000 && mxs < 2000000000);
}

/* ------------------------------------------- 6. FILTER 2 in the engines --- */
typedef struct {
    uint32_t e, pi;
    int16_t set[16][2];                                  /* parameter, value; (0, 0) ends */
    uint32_t note, right;
    double lo, hi;                                       /* the band measured */
} f2case_t;
/* a held note with the settings, no effects: the power from lo to hi Hz against 40..300 Hz (dB), the left or the right */
static double f2_band1(const void *arg)
{
    const f2case_t *c = arg;
    static int32_t o[CTL * 2];
    static double x[200 * CTL];
    uint32_t b, i, n = 0, k;
    host_tracks_init();
    host_preset(&trk[0], c->e, c->pi);
    clipper_proj_neutral();
    trk[1].p[P_LYMODE] = LY_OFF;
    trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_CHOR] = trk[0].p[P_DIST] = 0;
    trk[0].p[P_SPREAD] = 0, trk[0].p[P_LD_FLT] = 0, trk[0].p[P_LD_PIT] = 0, trk[0].p[P_VINT] = 0;
    trk[0].p[P_ED_FLT] = 0, trk[0].p[P_SUS] = 127, trk[0].p[P_ATK] = 0, trk[0].p[P_VOICE] = V_POLY, trk[0].p[P_AMODE] = 0;
    for (k = 0; k < 16u && (c->set[k][0] || c->set[k][1]); k++)
        trk[0].p[c->set[k][0]] = c->set[k][1];
    trk[0].fm_eff = trk[0].fx_eff = 0;
    for (b = 0; b < 4u; b++)
        mix_block(o, CTL);
    trk_note_on(&trk[0], c->note ? c->note : 36, 120);
    for (b = 0; b < 400u; b++) {
        mix_block(o, CTL);
        if (b >= 200u)
            for (i = 0; i < CTL; i++)
                x[n++] = o[2 * i + (c->right ? 1 : 0)];
    }
    return 10 * log10((band(x, n, c->lo, c->hi) + 1) / (band(x, n, 40, 300) + 1));
}
static double f2_band(const f2case_t *c) { return in_child(f2_band1, c); }
static double f2_level1(const void *arg)               /* the peak of the output over a loud chord */
{
    const f2case_t *c = arg;
    static int32_t o[CTL * 2];
    uint32_t b, i, k;
    int32_t pk = 0;
    host_tracks_init();
    host_preset(&trk[0], c->e, c->pi);
    clipper_proj_neutral();
    trk[1].p[P_LYMODE] = LY_OFF;
    for (k = 0; k < 16u && (c->set[k][0] || c->set[k][1]); k++)
        trk[0].p[c->set[k][0]] = c->set[k][1];
    trk[0].fm_eff = trk[0].fx_eff = 0;
    for (i = 0; i < 6u; i++)
        trk_note_on(&trk[0], NOTES[i], 127);
    for (b = 0; b < 600u; b++) {
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            if (abs(o[i]) > pk)
                pk = abs(o[i]);
    }
    return pk;
}
static void t_f2_engines(void)
{
    char w[240];
    double a, b, c, d;
    uint32_t k;
    {   /* an LP at CUT 40 darkens: beside the filter (ANALOG, PONY) and after the voice (DIGITAL, TRIO, HYBRID) */
        static const struct { uint32_t e, pi; const char *nm; } E[] = {
            {0, 6, "ANALOG REESE"}, {ENGI_PONY, 1, "PONY BRASS"}, {1, 0, "DIGITAL"}, {ENGI_TRIO, 0, "TRIO"}, {ENGI_HYBRID, 0, "HYBRID"},
        };
        for (k = 0; k < NELEM(E); k++) {
            f2case_t off = {E[k].e, E[k].pi, {{0, 0}}, 36, 0, 1000, 3000};
            f2case_t on = {E[k].e, E[k].pi, {{P_F2MODEL, FM_LADDER}, {P_F2LINK, 0}, {P_F2CUT, 40}, {0, 0}}, 36, 0, 1000, 3000};
            a = f2_band(&off), b = f2_band(&on);
            snprintf(w, sizeof w, "%s: FILTER 2 LADDR at CUT 40 (%s): 1..3 kHz against the bass %.1f -> %.1f dB", E[k].nm,
                     f2_own(ENGINES[E[k].e]) ? "beside its filter" : "after the voice", a, b);
            check(w, b < a - 15.0);
        }
    }
    {   /* SPLIT: the left as without FILTER 2, the right through the HP */
        f2case_t off = {0, 6, {{P_E4, 60}, {0, 0}}, 36, 0, 1000, 3000};
        f2case_t l = {0, 6, {{P_E4, 60}, {P_F2MODEL, FM_HP}, {P_F2ROUTE, F2_SPLIT}, {P_F2LINK, 0}, {P_F2CUT, 100}, {0, 0}}, 36, 0,
                      1000, 3000};
        f2case_t r = l;
        r.right = 1;
        a = f2_band(&off), b = f2_band(&l), c = f2_band(&r);
        r.e = l.e = 1, r.pi = l.pi = 0;
        d = f2_band(&r) - f2_band(&l);
        snprintf(w, sizeof w, "SPLIT (ANALOG, HP 9 kHz): 1..3 kHz against the bass: without %.1f, left %.1f, right %.1f dB; "
                 "DIGITAL (after the voice) right - left %+.1f", a, b, c, d);
        check(w, fabs(a - b) < 1.0 && c > b + 20.0 && d > 10.0);
    }
    {   /* LINK: FILTER 2 (an LP at offset 0) follows the first's CUT; without LINK it holds */
        f2case_t lo = {0, 6, {{P_E4, 40}, {P_F2MODEL, FM_SVF}, {P_F2LINK, 1}, {P_F2CUT, 64}, {0, 0}}, 36, 0, 600, 1200};
        f2case_t hi = lo, ulo, uhi;
        hi.set[0][1] = 80;
        ulo = lo, uhi = hi;
        ulo.set[2][1] = 0, uhi.set[2][1] = 0;            /* (no LINK: its own CUT 40) */
        ulo.set[3][1] = 40, uhi.set[3][1] = 40;
        a = f2_band(&lo), b = f2_band(&hi), c = f2_band(&ulo), d = f2_band(&uhi);
        snprintf(w, sizeof w, "LINK: CUT 40 -> 80 opens both (600..1200 Hz %.1f -> %.1f dB); without LINK FILTER 2 holds at its "
                 "CUT 40 (%.1f -> %.1f)", a, b, c, d);
        check(w, b > a + 15.0 && d < b - 10.0);
    }
    {   /* ENV DEST FLT and the matrix's CUT2 move it, RES2 sharpens it (no LINK, on DIGITAL: after the voice) */
        f2case_t base = {1, 0, {{P_F2MODEL, FM_LADDER}, {P_F2LINK, 0}, {P_F2CUT, 40}, {0, 0}}, 36, 0, 1000, 3000};
        f2case_t env = base, mx = base, res = base, r0 = base;
        env.set[3][0] = P_ED_FLT, env.set[3][1] = 60;
        mx.set[3][0] = P_M1SRC, mx.set[3][1] = MS_VEL, mx.set[4][0] = P_M1DST, mx.set[4][1] = MT_CUT2;
        mx.set[5][0] = P_M1AMT, mx.set[5][1] = 63;
        a = f2_band(&base), b = f2_band(&env), c = f2_band(&mx);
        res.set[3][0] = P_M1SRC, res.set[3][1] = MS_VEL, res.set[4][0] = P_M1DST, res.set[4][1] = MT_RES2;
        res.set[5][0] = P_M1AMT, res.set[5][1] = 63;
        res.lo = r0.lo = 230, res.hi = r0.hi = 330;      /* (the cutoff, CUT 40 + the keyboard half: ~270 Hz) */
        d = f2_band(&res) - f2_band(&r0);
        snprintf(w, sizeof w, "DIGITAL, FILTER 2 at CUT 40: ENV DEST FLT +60 opens it (%.1f -> %.1f dB), VEL > CUT2 too (%.1f), "
                 "VEL > RES2 peaks it (%+.1f dB)", a, b, c, d);
        check(w, b > a + 6.0 && c > a + 6.0 && d > 3.0);
    }
    {   /* bounded: a hot ANALOG (DRV 127, RES 127) into LADDR RES 127 at every route */
        int route;
        double mx = 0;
        for (route = F2_SER; route <= F2_SPLIT; route++) {
            f2case_t c0 = {0, 6, {{P_E6, 127}, {P_E5, 127}, {P_F2MODEL, FM_LADDER}, {P_F2RES, 127}, {P_F2ROUTE, 0}, {0, 0}}, 0, 0, 0, 0};
            c0.set[4][1] = (int16_t)route;
            a = in_child(f2_level1, &c0);
            if (a > mx)
                mx = a;
        }
        snprintf(w, sizeof w, "ANALOG DRV 127 RES 127, FILTER 2 LADDR RES 127, a loud chord, every route: the peak %.0f (bounded)", mx);
        check(w, mx > 1000 && mx < 32767);
    }
}
static void t_f2_duck(void)
{
    static int32_t o[CTL * 2];
    int32_t prev = 0, jump = 0, pk = 0;
    uint32_t b, i;
    char w[200];
    host_tracks_init();
    host_preset(&trk[0], 0, 6);
    clipper_proj_neutral();
    trk[1].p[P_LYMODE] = LY_OFF;
    trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_CHOR] = 0;
    trk[0].fm_eff = trk[0].fx_eff = 0;
    for (b = 0; b < 4u; b++)
        mix_block(o, CTL);
    trk_note_on(&trk[0], 48, 110);
    for (b = 0; b < 800u; b++) {
        if (b == 200u)
            trk[0].p[P_F2MODEL] = FM_LADDER, trk[0].p[P_F2RES] = 100;
        if (b == 400u)
            trk[0].p[P_F2ROUTE] = F2_SPLIT;
        if (b == 600u)
            trk[0].p[P_F2MODEL] = 0;
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t x = o[2 * i];
            if (b > 100u && abs(x - prev) > jump)
                jump = abs(x - prev);
            if (abs(x) > pk)
                pk = abs(x);
            prev = x;
        }
    }
    snprintf(w, sizeof w, "FILTER 2 MODL OFF -> LADDR, ROUTE SER -> SPLIT, MODL -> OFF under a held note: ducked (the largest step "
             "%d, peak %d)", jump, pk);
    check(w, jump < pk / 3 && trk[0].f2v == 0);
}
static void t_f2_ui(void)
{
    char w[220], val[12];
    const char *unit;
    int pg = 0, lk = 0, lab = 1;
    uint32_t i, k;
    for (i = 0; i < NPAGES; i++) {
        if (!strcmp(PAGES[i].title, "FILTER 2") && PAGES[i].fam == FAM_FX)
            pg = PAGES[i].id[0] == P_F2MODEL && PAGES[i].id[1] == P_F2CUT && PAGES[i].id[2] == P_F2RES &&
                 PAGES[i].id[3] == P_F2ROUTE && !strcmp(PAGES[i - 1].title, "FILTER");
        if (!strcmp(PAGES[i].title, "FILTER"))
            lk = PAGES[i].id[3] == P_F2LINK;
    }
    for (k = P_FSLOPE; k < P_E0; k++)
        lab &= strlen(TP[k].label) <= 5u && !p_lockable(k);
    host_tracks_init();
    trk[0].eng_req = 0, trk[0].p[P_F2LINK] = 1, trk[0].p[P_F2CUT] = 78;
    param_format(track_desc(&trk[0], P_F2CUT), trk[0].p[P_F2CUT], val, &unit);
    snprintf(w, sizeof w, "FX > FILTER 2 (MODL CUT RES ROUTE) after FILTER (+ LINK), labels <= 5, not lockable; linked on ANALOG: "
             "%s %s %s", track_desc(&trk[0], P_F2CUT)->label, val, unit);
    check(w, pg && lk && lab && !strcmp(track_desc(&trk[0], P_F2CUT)->label, "OFS") && !strcmp(val, "+12") &&
             TP[P_F2MODEL].def == 0 && TP[P_F2LINK].def == 1);
    trk[0].eng_req = ENGI_FM6;
    k = !strcmp(track_desc(&trk[0], P_F2CUT)->label, "CUT");
    trk[0].eng_req = 0, trk[0].p[P_F2LINK] = 0;
    check("... CUT on an engine it filters after (FM6), and without LINK; the matrix's destinations CUT2 RES2",
          k && !strcmp(track_desc(&trk[0], P_F2CUT)->label, "CUT") && !strcmp(track_desc(&trk[0], P_M1DST)->names[MT_CUT2], "CUT2") &&
          !strcmp(track_desc(&trk[0], P_M1DST)->names[MT_RES2], "RES2") && TP[P_M1DST].max == MT_COUNT - 1);
    check("the editor protocol: still 127 values, FILTER 2's passed over with SLOPE (P_EDH0 .. P_E0)",
          P_EDN == 127u && P_EDHN == 57u && p_of_ed(119) == P_E0 && ed_of_p(P_E7) == 126u);   /* (+ Stallion's BEND page: 6; MARE's VCO3 VCO4: 2; the matrix's slots 13..24: 36; MARE's WAV34: 1; PONY's EDIT 3: 4; RODEO's KYBD PW0: 2) */
    {   /* a project keeps both sounds' FILTER 2 */
        static project_t pj;
        static const int16_t A[5] = {FM_BP, 20, 99, F2_SPLIT, 0}, B[5] = {FM_OTA, 100, 5, F2_PAR, 1};
        int same = 1;
        host_tracks_init();
        for (k = 0; k < 5u; k++)
            trk[0].p[P_F2MODEL + k] = A[k], trk[1].p[P_F2MODEL + k] = B[k];
        proj_capture(&pj);
        for (k = 0; k < 5u; k++)
            trk[0].p[P_F2MODEL + k] = trk[1].p[P_F2MODEL + k] = TP[P_F2MODEL + k].def;
        proj_apply(&pj, 1);
        for (k = 0; k < 5u; k++)
            same &= trk[0].p[P_F2MODEL + k] == A[k] && trk[1].p[P_F2MODEL + k] == B[k];
        check("a project keeps both sounds' FILTER 2 (track 4's nudges)", same);
    }
    {   /* a user preset: FILTER 2, a slot sending to RES2 */
        up_rec_t r;
        int16_t v[P_COUNT], out[P_COUNT], def[P_COUNT];
        int same = 1;
        for (i = 0; i < P_COUNT; i++)
            def[i] = i >= P_E0 ? 0 : TP[i].def, v[i] = def[i];
        v[P_F2MODEL] = FM_DIODE, v[P_F2CUT] = 33, v[P_F2RES] = 77, v[P_F2ROUTE] = F2_PAR, v[P_F2LINK] = 0;
        v[P_M2SRC] = MS_LFO2, v[P_M2DST] = MT_RES2, v[P_M2AMT] = 30, v[P_M3SRC] = MS_ENV2, v[P_M3DST] = MT_CUT2, v[P_M3AMT] = -9;
        memset(&r, 0, sizeof r);
        r.used = UP_USED, r.name[0] = 'F';
        up_rec_set(&r, v);
        up_params(&r, out, def);
        for (i = 0; i < P_COUNT; i++)
            same &= out[i] == v[i];
        check("a user preset keeps FILTER 2 and the matrix's CUT2 / RES2", same);
    }
}

/* ------------------------------------------------------------ cost --- */
#ifdef __APPLE__                                       /* the kernel's instruction count (macOS only) */
#include <libproc.h>
#include <sys/resource.h>
#endif
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;                                          /* elsewhere: no counter, t_cost says so */
}
typedef struct { uint32_t e, pi, nv; int16_t set[8][2]; } cost_t;
static double cost1(const void *arg)                   /* host instructions a sample of the mix, nv voices held */
{
    const cost_t *c = arg;
    static int32_t o[CTL * 2];
    uint32_t b, i, k;
    uint64_t i0;
    host_tracks_init();
    host_preset(&trk[0], c->e, c->pi);
    clipper_proj_neutral();
    trk[1].p[P_LYMODE] = LY_OFF;
    trk[0].p[P_VOICE] = V_POLY, trk[0].p[P_SUS] = 127, trk[0].p[P_REL] = 127, trk[0].p[P_AMODE] = 0;
    trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_CHOR] = trk[0].p[P_SPREAD] = 0;
    for (k = 0; k < 8u && (c->set[k][0] || c->set[k][1]); k++)
        trk[0].p[c->set[k][0]] = c->set[k][1];
    trk[0].fm_eff = trk[0].fx_eff = 0;
    cpu1_units = 0;
    for (b = 0; b < 8u; b++)
        mix_block(o, CTL);
    for (i = 0; i < c->nv; i++)
        trk_note_on(&trk[0], 40 + 3 * i, 100);
    for (b = 0; b < 100u; b++)
        mix_block(o, CTL);
    i0 = instr_now();
    for (b = 0; b < 3000u; b++)
        mix_block(o, CTL);
    return (double)(instr_now() - i0) / (3000.0 * CTL);
}
/* a voice's cost (host instructions a sample: 8 voices less none, / 8) and, at hybrid_test's 1.7 % of the FM-1 a
 * hundred, how many fit two cores at 90 % (core 0 also the mix: the idle part's cost; core 1 the bigger half) */
static void cost_row(const char *nm, cost_t c)
{
    double idle, v8, pv, dev, ovh;
    int n;
    c.nv = 0, idle = in_child(cost1, &c);
    c.nv = 8, v8 = in_child(cost1, &c);
    pv = (v8 - idle) / 8.0, dev = pv * 1.7 / 100.0, ovh = idle * 1.7 / 100.0;
    for (n = 1; n < 99 && ovh + (n / 2) * dev <= 90.0 && ((n + 1) / 2) * dev <= 90.0; n++)
        ;
    printf("filter2: cost %-42s %5.0f a voice (~%.1f %% of a core), the idle mix %4.0f; fits %d voices\n", nm, pv, dev, idle,
           n - 1);
}
static void t_cost(void)
{
    static const int MOD[3] = {FM_LADDER, FM_OTA, FM_DIODE};
    static const char *const MN[] = {"OFF", "SVF", "LADDR", "SK", "DIODE", "WARM", "NOTCH", "BP", "HP", "OTA"};
    static const char *const RN[] = {"SER", "PAR", "SPLIT"};
    char nm[64];
    int m, s, r;
    if (!instr_now()) {
        printf("filter2: cost: no instruction counter on this host\n");
        return;
    }
    for (m = FM_SVF; m <= FM_LAST; m++) {
        cost_t c = {0, 0, 0, {{P_FMODEL, (int16_t)m}, {0, 0}}};
        snprintf(nm, sizeof nm, "ANALOG %s", MN[m]);
        cost_row(nm, c);
    }
    for (m = 0; m < 3; m++)
        for (s = 0; s < 3; s += 2) {
            cost_t c = {0, 0, 0, {{P_FMODEL, (int16_t)MOD[m]}, {P_FSLOPE, (int16_t)s}, {0, 0}}};
            snprintf(nm, sizeof nm, "ANALOG %s SLOPE %d", MN[MOD[m]], s ? 48 : 12);
            cost_row(nm, c);
        }
    for (s = 0; s < 3; s++) {
        cost_t c = {ENGI_PONY, 0, 0, {{P_FSLOPE, (int16_t)s}, {0, 0}}};
        snprintf(nm, sizeof nm, "PONY SLOPE %d", s == 0 ? 12 : s == 1 ? 24 : 48);
        cost_row(nm, c);
    }
    for (m = FM_SVF; m <= FM_LAST; m++) {
        cost_t a = {0, 0, 0, {{P_FMODEL, FM_LADDER}, {P_F2MODEL, (int16_t)m}, {0, 0}}};
        cost_t j = {ENGI_PONY, 0, 0, {{P_F2MODEL, (int16_t)m}, {0, 0}}};
        snprintf(nm, sizeof nm, "ANALOG LADDR + FILTER 2 %s SER", MN[m]);
        cost_row(nm, a);
        snprintf(nm, sizeof nm, "PONY + FILTER 2 %s SER", MN[m]);
        cost_row(nm, j);
    }
    for (r = F2_PAR; r <= F2_SPLIT; r++) {
        cost_t a = {0, 0, 0, {{P_FMODEL, FM_LADDER}, {P_F2MODEL, FM_LADDER}, {P_F2ROUTE, (int16_t)r}, {0, 0}}};
        cost_t j = {ENGI_PONY, 0, 0, {{P_F2MODEL, FM_OTA}, {P_F2ROUTE, (int16_t)r}, {0, 0}}};
        snprintf(nm, sizeof nm, "ANALOG LADDR + FILTER 2 LADDR %s", RN[r]);
        cost_row(nm, a);
        snprintf(nm, sizeof nm, "PONY + FILTER 2 OTA %s", RN[r]);
        cost_row(nm, j);
    }
    for (m = 0; m <= FM_LAST; m += m ? 7 : 1)            /* (OFF, SVF, HP) */
        for (r = F2_SER; r <= (m ? F2_SPLIT : F2_SER); r++) {
            cost_t f = {ENGI_FM6, 0, 0, {{P_F2MODEL, (int16_t)m}, {P_F2ROUTE, (int16_t)r}, {0, 0}}};
            cost_t d = {1, 0, 0, {{P_F2MODEL, (int16_t)m}, {P_F2ROUTE, (int16_t)r}, {0, 0}}};
            snprintf(nm, sizeof nm, "FM6 + FILTER 2 %s %s (after the voice)", MN[m], m ? RN[r] : "");
            cost_row(nm, f);
            snprintf(nm, sizeof nm, "DIGITAL + FILTER 2 %s %s (after)", MN[m], m ? RN[r] : "");
            cost_row(nm, d);
        }
}
#endif

int main(int argc, char **argv)
{
    const char *gold = argc > 1 ? argv[1] : "tests/filter2_golden.txt";
#ifndef F2_GOLDEN_ONLY
    if (getenv("COST")) {
        t_cost();
        return 0;
    }
#endif
    t_golden(gold);
    if (getenv("GOLDEN_UPDATE"))
        return 0;
#ifndef F2_GOLDEN_ONLY
    t_same24();
    t_slopes();
    t_bounded();
    t_engines();
    t_duck();
    t_stores();
    t_routes();
    t_routes_bounded();
    t_f2_engines();
    t_f2_duck();
    t_f2_ui();
#endif
    printf("filter2: %s\n", bad ? "FAILED" : "all passed");
    return bad;
}
