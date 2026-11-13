/* SPDX-License-Identifier: GPL-3.0-only */
/* STAMPEDE (firmware/src/eng_stampede.c, after the dual-channel 1970s polyphonic's voice; CLIPPER_SYNTH 1):
 *   build/host/stampede_test
 *   engine     the 25th engine, after PALOMINO (the stores' numbers kept), named STAMPEDE (and in the ENG list)
 *   presets    every one bounded, audible, its voices free after the release (a chord through the whole mix)
 *   VCOs       channel I in tune (a saw at A4: 440 Hz); FEET +12 with BAL +63: channel II alone, 880 Hz
 *   VCF        the high-pass then the low-pass: a band (the harmonics inside it louder than below and above), each
 *              filter doing its part (HPF 0: the bass back; LPF 127: the top back)
 *   DETUNE     the channels beat: two sines DTUNE 127 apart (40 cents at A4, ~10 Hz), the level swings ~10 times a
 *              second; DTUNE 0: steady
 *   RING       WAVE RING, channel II alone (BAL +63): the sum and the difference of the two channels, not their own
 *              pitches
 *   IL         the filter envelope: IL -> AL (ATK) -> 0 (DEC) -> IL (REL)
 *   bounded    RES at the top, the filters at their ends, every wave, a loud chord
 *   cost       host instructions per sample and voice (8 voices held), every preset, against PONY's and PALOMINO's
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define STAMPEDE_COST_MAX 260.0      /* host instructions per sample and voice, the heaviest preset (~235 on an Apple M
                                  * host; PONY and PALOMINO, measured alongside, ~280 and ~265) */

static int bad;
static void ok(int c, const char *what)
{
    printf("stampede: %-96s %s\n", what, c ? "ok" : "FAIL");
    bad += !c;
}
static double db(double x) { return 20.0 * log10(x > 1e-12 ? x : 1e-12); }

#define N 16384
static int32_t buf[N];

/* one voice straight through stampede_render, at hz (pitch16: its note, for the keyboard tracking and channel II) */
static void sta_direct(track_t *t, double hz, int32_t p16)
{
    voice_t v;
    vmod_t m;
    uint32_t b;
    memset(&v, 0, sizeof v);
    memset(&m, 0, sizeof m);
    v.ph[2] = 0x2545F491u;
    v.pitch16 = p16;
    v.stage = 2;
    m.inc = (uint32_t)(hz / FS * 4294967296.0);
    m.pitch16 = p16;
    m.amp0 = m.amp1 = 32767;
    m.shape = 64 << 8;
    stampede_note_on(t, &v);
    memset(buf, 0, sizeof buf);
    for (b = 0; b + CTL <= N; b += CTL)
        stampede_render(t, &v, buf + b, CTL, &m);
}
static int32_t p16_of(double hz) { return (int32_t)lrint((69.0 + 12.0 * log2(hz / 440.0)) * 16.0); }
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
/* a plain sound: a saw, channel I alone, both filters open, no resonance, no filter envelope */
static void plain(track_t *t)
{
    host_preset(t, ENGI_STAMPEDE, 0);
    t->p[P_E0] = CSW_SAW, t->p[P_E1] = 0, t->p[P_E2] = 0, t->p[P_E3] = -64;
    t->p[P_E4] = 0, t->p[P_E5] = 127, t->p[P_E6] = 0, t->p[P_E7] = 0;
    t->p[P_ED_FLT] = 0;
}

static void t_engine(void)
{
    ok(NENGINES >= 25u && ENGI_STAMPEDE == 24u && ENGINES[ENGI_STAMPEDE] == &ENG_STAMPEDE && !strcmp(ENG_STAMPEDE.name, "STAMPEDE") &&
           ENGINES[ENGI_PALOMINO] == &ENG_PALOMINO, "the 25th engine, after PALOMINO, named STAMPEDE");
    ok(NELEM(N_ENGNAME) == NENGINES && !strcmp(N_ENGNAME[ENGI_STAMPEDE], "STAMPEDE") && !strcmp(N_ENGNAME[ENGI_PALOMINO], "PALOMINO"),
       "ENG: every engine named (PALOMINO, STAMPEDE)");
    ok(vs_ok(&ENG_STAMPEDE), "its voices on both cores (vs_ok)");
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
    for (pi = 0; pi < ENG_STAMPEDE.npresets; pi++) {
        double r;
        int inb, inb2;
        for (i = 0; i < NPART; i++)
            trk_all_off(&trk[i]);
        run_mix(600, &r, &inb);
        host_preset(&trk[0], ENGI_STAMPEDE, pi);
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
            good = 0, printf("  %s: out of bounds\n", ENG_STAMPEDE.presets[pi].name);
        if (r < 300.0)
            loud = 0, printf("  %s: quiet (%.0f)\n", ENG_STAMPEDE.presets[pi].name, r);
        if (sounding(&trk[0]))
            freed = 0, printf("  %s: %u voices left\n", ENG_STAMPEDE.presets[pi].name, (unsigned)sounding(&trk[0]));
    }
    snprintf(w, sizeof w, "all %u presets bounded, audible, their voices free after the release",
             (unsigned)ENG_STAMPEDE.npresets);
    ok(good && loud && freed && ENG_STAMPEDE.npresets >= 16u && ENG_STAMPEDE.npresets <= 32u, w);
}

static void t_vco(void)
{
    track_t *t = &trk[0];
    double a, b, c;
    char w[160];
    plain(t);
    sta_direct(t, 440.0, p16_of(440.0));
    a = mag(440, N / 2, N / 2), b = mag(415.3, N / 2, N / 2), c = mag(466.2, N / 2, N / 2);
    ok(a > 8 * b && a > 8 * c, "channel I: in tune (a saw at A4: 440 Hz, not beside it)");
    t->p[P_E1] = 12, t->p[P_E3] = 63;                  /* channel II alone, an octave up */
    sta_direct(t, 440.0, p16_of(440.0));
    a = mag(880, N / 2, N / 2), b = mag(440, N / 2, N / 2);
    snprintf(w, sizeof w, "FEET +12, BAL +63: channel II alone at 880 Hz, no 440 (%.0f vs %.0f)", a, b);
    ok(a > 20 * b, w);
    t->p[P_E3] = 0, t->p[P_E1] = 7;                    /* both, a fifth apart */
    sta_direct(t, 440.0, p16_of(440.0));
    snprintf(w, sizeof w, "BAL 0: both channels (660 Hz %.1f dB against 440)", db(mag(660, N / 2, N / 2) / mag(440, N / 2, N / 2)));
    ok(db(mag(660, N / 2, N / 2) / mag(440, N / 2, N / 2)) > -6.0, w);
}

/* the HPF -> LPF band, on a 55 Hz saw (harmonics every 55 Hz), the keyboard tracking at 0 (C4's pitch16) */
static void t_band(void)
{
    track_t *t = &trk[0];
    double lo, mid, hi, lo0, hi0, olo, omid, ohi;
    char w[200];
    plain(t);                                          /* open: the saw's own harmonics */
    sta_direct(t, 55.0, 60 * 16);
    olo = mag(110, N / 2, N / 2), omid = mag(440, N / 2, N / 2), ohi = mag(4400, N / 2, N / 2);
    t->p[P_E4] = 40, t->p[P_E5] = 70;                  /* ~216 Hz .. ~950 Hz */
    sta_direct(t, 55.0, 60 * 16);
    lo = mag(110, N / 2, N / 2), mid = mag(440, N / 2, N / 2), hi = mag(4400, N / 2, N / 2);
    snprintf(w, sizeof w, "HPF 40, LPF 70: a band (440 Hz %+.1f dB, 110 Hz %+.1f dB, 4.4 kHz %+.1f dB against open)",
             db(mid / omid), db(lo / olo), db(hi / ohi));
    ok(db(mid / omid) > -6.0 &&db(lo / olo) < -9.0 && db(hi / ohi) < -24.0, w);
    t->p[P_E4] = 0;
    sta_direct(t, 55.0, 60 * 16);
    lo0 = mag(110, N / 2, N / 2);
    t->p[P_E4] = 40, t->p[P_E5] = 127;
    sta_direct(t, 55.0, 60 * 16);
    hi0 = mag(4400, N / 2, N / 2);
    snprintf(w, sizeof w, "each filter its part: HPF 0 brings 110 Hz back (%+.1f dB), LPF 127 4.4 kHz (%+.1f dB)", db(lo0 / lo), db(hi0 / hi));
    ok(db(lo0 / lo) > 10.0 && db(hi0 / hi) > 20.0, w);
    t->p[P_E4] = 40, t->p[P_E5] = 70, t->p[P_E6] = 110;   /* RES: the low-pass's peak */
    sta_direct(t, 55.0, 60 * 16);
    {
        double r1 = mag(935, N / 2, N / 2), r0;
        t->p[P_E6] = 0;
        sta_direct(t, 55.0, 60 * 16);
        r0 = mag(935, N / 2, N / 2);
        snprintf(w, sizeof w, "RES 110: the low-pass rings at its cutoff (~950 Hz %+.1f dB against RES 0)", db(r1 / r0));
        ok(db(r1 / r0) > 10.0, w);
    }
}

/* the level's swing: RMS over 5 ms windows of buf[a .. a + n): max / min, and the dips counted */
static double swing(uint32_t a, uint32_t n, uint32_t *dips)
{
    double r[400], mx = 0, mn = 1e30, mean = 0;
    uint32_t k, nk = n / 220u, i, below = 0;
    nk = nk > 400u ? 400u : nk;
    for (k = 0; k < nk; k++) {
        double e = 0;
        for (i = 0; i < 220u; i++)
            e += (double)buf[a + k * 220u + i] * buf[a + k * 220u + i];
        r[k] = sqrt(e / 220);
        mx = r[k] > mx ? r[k] : mx;
        mn = r[k] < mn ? r[k] : mn;
        mean += r[k] / nk;
    }
    *dips = 0;
    for (k = 0; k < nk; k++) {                         /* a dip: under half the mean, after being over it */
        if (!below && r[k] < mean * 0.5)
            below = 1, (*dips)++;
        else if (below && r[k] > mean)
            below = 0;
    }
    return mx / (mn > 1 ? mn : 1);
}
static void t_detune(void)
{
    track_t *t = &trk[0];
    double s0, s1;
    uint32_t d0, d1;
    char w[200];
    plain(t);
    t->p[P_E0] = CSW_SINE, t->p[P_E3] = 0;             /* two sines, equal, the same pitch */
    sta_direct(t, 440.0, p16_of(440.0));
    s0 = swing(N / 4, N * 3 / 4, &d0);
    t->p[P_E2] = 127;
    sta_direct(t, 440.0, p16_of(440.0));
    s1 = swing(N / 4, N * 3 / 4, &d1);                 /* 12288 samples: 0.28 s, ~2.8 beats at ~10 Hz */
    snprintf(w, sizeof w, "DTUNE 127: the channels beat (level swing x%.0f, %u dips in 0.28 s; DTUNE 0: x%.2f)", s1, d1, s0);
    ok(s1 > 8.0 && d1 >= 2u && d1 <= 4u && s0 < 1.2, w);
    {
        double a = mag(440.0 * pow(2, -20.0 / 1200), 0, N), b = mag(440.0 * pow(2, 20.0 / 1200), 0, N), c = mag(440, 0, N);
        snprintf(w, sizeof w, "DTUNE 127: 20 cents down and up (434.9 / 445.1 Hz), not 440 (%.0f %.0f vs %.0f)", a, b, c);
        ok(a > 2 * c && b > 2 * c, w);
    }
}

static void t_ring(void)
{
    track_t *t = &trk[0];
    double s, d, f1, f2;
    char w[200];
    plain(t);
    t->p[P_E0] = CSW_RING, t->p[P_E1] = 7, t->p[P_E3] = 63;   /* the ring alone, channel II a fifth up */
    sta_direct(t, 220.0, p16_of(220.0));
    s = mag(220.0 + 220.0 * pow(2, 7 / 12.0), N / 2, N / 2), d = mag(220.0 * pow(2, 7 / 12.0) - 220.0, N / 2, N / 2);
    f1 = mag(220.0, N / 2, N / 2), f2 = mag(220.0 * pow(2, 7 / 12.0), N / 2, N / 2);
    snprintf(w, sizeof w, "RING: the sum (549.6 Hz) and the difference (109.6 Hz), not 220 / 329.6 (%.0f %.0f vs %.0f %.0f)", s, d, f1, f2);
    ok(s > 10 * f1 && s > 10 * f2 && d > 10 * f1 && d > 10 * f2, w);
}

static void t_eg(void)
{
    track_t *t = &trk[0];
    voice_t v;
    int32_t at0, top = 0, low, rel;
    uint32_t k;
    char w[200];
    plain(t);
    t->p[P_ATK] = 40, t->p[P_DEC] = 40, t->p[P_REL] = 40, t->p[P_E7] = 64;   /* IL half */
    memset(&v, 0, sizeof v);
    v.stage = 1;
    stampede_note_on(t, &v);
    at0 = v.s[4];
    for (k = 0; k < 2000u; k++) {
        int32_t l = stampede_eg(t->p, &t->p[P_E0], &v);
        top = l > top ? l : top;
    }
    low = v.s[4];
    v.stage = 3;
    for (k = 0; k < 4000u; k++)
        stampede_eg(t->p, &t->p[P_E0], &v);
    rel = v.s[4];
    snprintf(w, sizeof w, "the filter envelope: from IL (%.2f) up to AL (%.2f), down to %.3f, back to IL in REL (%.2f)",
             at0 / 16777216.0, top / 16777216.0, low / 16777216.0, rel / 16777216.0);
    ok(at0 == 64 << 17 && top == 1 << 24 && low < (1 << 24) / 100 && abs(rel - (64 << 17)) < (1 << 24) / 100, w);
}

static void t_bounded(void)
{
    track_t *t = &trk[0];
    uint32_t i, k;
    int32_t mx = 0;
    char w[160];
    static const int8_t WV[5] = {CSW_NOIS, 16, CSW_SAW, CSW_PLS, CSW_RING};
    for (k = 0; k < 10u; k++) {
        plain(t);
        t->p[P_E0] = WV[k % 5u], t->p[P_E1] = k & 1u ? 24 : -24, t->p[P_E2] = 127, t->p[P_E3] = 0;
        t->p[P_E4] = k < 5u ? 0 : 127, t->p[P_E5] = k < 5u ? 127 : 40, t->p[P_E6] = 127, t->p[P_E7] = 127;
        t->p[P_ED_FLT] = k & 1u ? 63 : -64;
        sta_direct(t, 27.5 * (k + 1), p16_of(27.5 * (k + 1)));
        for (i = 0; i < N; i++)
            if (abs(buf[i]) > mx)
                mx = abs(buf[i]);
    }
    snprintf(w, sizeof w, "RES at the top, the filters at their ends, every wave, the envelope both ways: bounded (peak %d)", mx);
    ok(mx < (1 << 21), w);
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
static double engine_cost(uint32_t e, int print, uint32_t *worst_p)
{
    uint32_t pi;
    double worst = 0;
    for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
        double c = (mix_cost(e, pi, 8) - mix_cost(e, pi, 0)) / 8;
        if (print)
            printf("stampede:   %-14s %5.0f\n", ENGINES[e]->presets[pi].name, c);
        if (c > worst)
            worst = c, *worst_p = pi;
    }
    return worst;
}
static void t_cost(void)
{
    double cs, ju, jp;
    uint32_t pc = 0, pu = 0, pp = 0;
    char w[260];
    if (!instr_now()) {
        printf("stampede: cost: no instruction counter on this host (proc_pid_rusage); not measured\n");
        return;
    }
    printf("stampede: cost, host instructions per sample and voice (8 voices held, one core):\n");
    cs = engine_cost(ENGI_STAMPEDE, 1, &pc);
    ju = engine_cost(ENGI_PONY, 0, &pu);
    jp = engine_cost(ENGI_PALOMINO, 0, &pp);
    printf("stampede: heaviest: STAMPEDE %s %.0f, PONY %s %.0f, PALOMINO %s %.0f\n", ENG_STAMPEDE.presets[pc].name, cs,
           ENG_PONY.presets[pu].name, ju, ENG_PALOMINO.presets[pp].name, jp);
    snprintf(w, sizeof w, "cost: at most %.0f instructions a sample and voice (PONY %.0f, PALOMINO %.0f; limit %.0f)", cs, ju, jp,
             STAMPEDE_COST_MAX);
    ok(cs <= STAMPEDE_COST_MAX, w);
}

int main(int argc, char **argv)
{
    host_tracks_init();
    t_engine();
    t_vco();
    t_band();
    t_detune();
    t_ring();
    t_eg();
    t_bounded();
    t_presets();
    if (!(argc > 1 && !strcmp(argv[1], "nocost")))
        t_cost();
    printf(bad ? "stampede: %d FAILED\n" : "stampede: all passed\n", bad);
    return bad;
}
