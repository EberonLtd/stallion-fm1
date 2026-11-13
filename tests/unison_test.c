/* SPDX-License-Identifier: GPL-3.0-only */
/* UNISON / UNI2 / UNI4 against POLY (CLIPPER_SYNTH 1; run_tests.sh builds it with NVOICE=16, as the FM-1 runs, and 8):
 *   build/host/unison_test
 *   PHASES   an extra voice from silence starts every oscillator at a random phase, not only the first: a TRIO bass
 *            and an ANALOG saw bass (their second and third oscillators started alike on every voice: the saw edges of
 *            the stack as one, a bang at each note) no harder at the onset than POLY's note; the PONY's noise of a
 *            unison note no louder than a POLY note's (one seed on every voice: the noise of the stack added up in
 *            phase); the STRINGS' taps still start together (set apart they cancel: the unison note no quieter at its
 *            start than later)
 *   STACK    UNISON on at most 8 voices in any build (16: the CPU of a 16-note chord on one key, ~3 dB louder), the
 *            detune as wide as UNI4's, a held note's level within 2 dB of POLY's (the median of five engines)
 *   SHED     the overload shedder's voices of a UNISON note stay out on a legato note (they came back with a fresh
 *            attack, and the load), back on the next note
 *   POLY     the voices of POLY / UNI2 / UNI4 as before (counted)
 * The master at 600 / 4096: the limiter off, what the voices sum to is measured. Exit status: the failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

static int bad;
static void ok(int c, const char *what)
{
    printf("unison: %-100s %s\n", what, c ? "ok" : "FAIL");
    bad += !c;
}

#define NB 2800u
static int16_t L[NB * CTL];
static uint32_t held;                                   /* voices sounding at the end of the note */

static uint32_t sounding(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += trk[0].v[i].active && trk[0].v[i].gate;
    return n;
}

/* engine e, preset pi, voice mode, note held for nb blocks from silence (fresh voices, as at power-on) into L */
static void render(uint32_t e, uint32_t pi, uint32_t mode, uint32_t note, uint32_t nb, void (*tweak)(track_t *))
{
    static int32_t o[2 * CTL];
    track_t *t = &trk[0];
    uint32_t b, i, p;
    for (p = 0; p < NPART; p++) {
        trk_all_off(&trk[p]);
        for (i = 0; i < NVOICE; i++) {
            memset(&trk[p].v[i], 0, sizeof trk[p].v[i]);
        }
    }
    for (b = 0; b < 400u; b++)
        mix_block(o, CTL);
    host_preset(t, e, pi);
    t->p[P_AMODE] = 0;
    t->p[P_VOICE] = (int16_t)mode;
    if (tweak)
        tweak(t);
    for (b = 0; b < 100u; b++)
        mix_block(o, CTL);
    trk_note_on(t, note, 100);
    for (b = 0; b < nb && b < NB; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            L[b * CTL + i] = (int16_t)((o[2 * i] + o[2 * i + 1]) / 2);
    }
    held = sounding();
    trk_note_off(t, note);
}
static int32_t max_step(uint32_t a, uint32_t n)        /* the biggest sample-to-sample step of L[a .. a + n) */
{
    int32_t m = 0;
    uint32_t i;
    for (i = a + 1u; i < a + n; i++)
        if (abs(L[i] - L[i - 1]) > m)
            m = abs(L[i] - L[i - 1]);
    return m;
}
static double rms(uint32_t a, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = a; i < a + n; i++)
        s += (double)L[i] * L[i];
    return sqrt(s / n);
}
static void t_onset(uint32_t e, uint32_t pi)
{
    char w[200];
    int32_t sp, su;
    render(e, pi, V_POLY, 48, 140, 0);
    sp = max_step(0, 100u * CTL);
    render(e, pi, V_UNISON, 48, 140, 0);
    su = max_step(0, 100u * CTL);
    snprintf(w, sizeof w, "PHASES %s %s: UNISON's onset no harder than POLY's (biggest step %d, POLY %d)", ENGINES[e]->name,
             ENGINES[e]->presets[pi].name, su, sp);
    ok(su <= 2 * sp, w);
}
static void noise_only(track_t *t)
{
    t->p[P_E0] = JW_PLS, t->p[P_E1] = 127;             /* (the narrowest pulse, nearly silent; the noise up) */
    t->p[P_E2] = 0, t->p[P_E3] = 127, t->p[P_DETUNE] = 0;
}
static void t_noise(void)
{
    char w[200];
    double rp, ru;
    render(ENGI_PONY, 0, V_POLY, 60, 700, noise_only);
    rp = rms(300u * CTL, 300u * CTL);
    render(ENGI_PONY, 0, V_UNISON, 60, 700, noise_only);
    ru = rms(300u * CTL, 300u * CTL);
    snprintf(w, sizeof w, "PHASES PONY noise: a unison note's no louder than a POLY note's (%+.1f dB)", 20 * log10(ru / rp));
    ok(ru < rp * 1.6, w);
}
static void t_strings(void)
{
    char w[200];
    double a, b, c, d;
    render(ENGI_STRINGS, 0, V_POLY, 60, 2000, 0);
    a = rms(500u * CTL, 300u * CTL), b = rms(1600u * CTL, 300u * CTL);
    render(ENGI_STRINGS, 0, V_UNISON, 60, 2000, 0);
    c = rms(500u * CTL, 300u * CTL), d = rms(1600u * CTL, 300u * CTL);
    a = 20 * log10(a / b), c = 20 * log10(c / d);
    snprintf(w, sizeof w, "PHASES STRINGS: the taps start together (0.4 s against 1.2 s: UNISON %+.1f dB, POLY %+.1f dB)", c, a);
    ok(fabs(c - a) < 3.0, w);
}

static void det80(track_t *t) { t->p[P_DETUNE] = 80; }
static void t_stack(void)
{
    static const uint8_t E[5] = {0, ENGI_WAVE, ENGI_PONY, ENGI_VPS, 6};   /* ANALOG WAVE PONY VPS TRIO */
    double d[5], tmp;
    uint32_t k, j, nu;
    int32_t fu = 0, f4 = 0;
    char w[200];
    render(0, 13, V_UNISON, 60, 60, det80);
    nu = held;
    for (k = 0; k < NVOICE; k++)
        if (abs(trk[0].v[k].fine) > fu && k < nu)
            fu = abs(trk[0].v[k].fine);
    render(0, 13, V_PUNI4, 60, 60, det80);
    for (k = 0; k < NVOICE; k++)
        if (abs(trk[0].v[k].fine) > f4 && k < 4u)
            f4 = abs(trk[0].v[k].fine);
    snprintf(w, sizeof w, "STACK UNISON on %u voices (NVOICE %u), detune out to %d (UNI4 %d)", nu, NVOICE, fu, f4);
    ok(nu == (NVOICE < 8 ? NVOICE : 8u) && f4 > 0 && fu <= f4 + f4 / 8, w);
    for (k = 0; k < 5u; k++) {
        double rp, ru;
        render(E[k], 0, V_POLY, 48, 1200, 0);
        rp = rms(600u * CTL, 600u * CTL);
        render(E[k], 0, V_UNISON, 48, 1200, 0);
        ru = rms(600u * CTL, 600u * CTL);
        d[k] = 20 * log10(ru / (rp > 1 ? rp : 1));
    }
    for (k = 0; k < 5u; k++)                            /* (the median) */
        for (j = k + 1u; j < 5u; j++)
            if (d[j] < d[k])
                tmp = d[k], d[k] = d[j], d[j] = tmp;
    snprintf(w, sizeof w, "STACK a held UNISON note's level against POLY's: median %+.1f dB (%+.1f .. %+.1f)", d[2], d[0], d[4]);
    ok(fabs(d[2]) < 2.0, w);
}

/* the overload shedder (audio.c: a voice a half while two halves in a row take > 85 %) on a UNISON note, then a legato
 * note: the voices it gave up stay out (back, they came with a fresh attack in the line and the load the shedder had
 * just cut); the next note played after a let-go has them all again */
static uint32_t active(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += trk[0].v[i].active;
    return n;
}
static void t_shed(void)
{
    static int32_t o[2 * CTL];
    uint32_t b, k, n0, n1, n2, n3;
    char w[200];
    render(0, 13, V_UNISON, 60, 200, 0);
    trk_note_on(&trk[0], 60, 100);
    for (b = 0; b < 200u; b++)
        mix_block(o, CTL);
    n0 = active();
    for (k = 0; k < 4u; k++) {
        shed_voice(0);
        for (b = 0; b < 8u; b++)
            mix_block(o, CTL);
    }
    n1 = active();
    trk_note_on(&trk[0], 62, 100);
    mix_block(o, CTL);
    n2 = active();
    trk_note_off(&trk[0], 62), trk_note_off(&trk[0], 60);
    for (b = 0; b < 4u; b++)
        mix_block(o, CTL);
    trk_note_on(&trk[0], 64, 100);
    mix_block(o, CTL);
    n3 = active();
    trk_note_off(&trk[0], 64);
    snprintf(w, sizeof w, "SHED UNISON on %u voices, 4 shed: %u; a legato note: %u (no fresh attacks); the next note: %u", n0, n1,
             n2, n3);
    ok(n1 + 4u == n0 && n2 == n1 && n3 == n0, w);
}

static void t_poly(void)
{
    static const uint8_t ch[4] = {48, 55, 60, 64};
    uint32_t m, i, n[3];
    static const uint32_t MODE[3] = {V_POLY, V_PUNI2, V_PUNI4};
    char w[160];
    for (m = 0; m < 3u; m++) {
        static int32_t o[2 * CTL];
        render(0, 13, MODE[m], ch[0], 20, 0);
        for (i = 0; i < 4u; i++)
            trk_note_on(&trk[0], ch[i], 100);
        mix_block(o, CTL);
        n[m] = sounding();
        for (i = 0; i < 4u; i++)
            trk_note_off(&trk[0], ch[i]);
    }
    snprintf(w, sizeof w, "POLY a 4-note chord on %u voices, UNI2 on %u, UNI4 on %u", n[0], n[1], n[2]);
    ok(n[0] == 4u && n[1] == 8u && n[2] == (NVOICE < 16 ? NVOICE : 16u), w);
}

int main(void)
{
    host_tracks_init();
    song.master_q12 = 600;
    trk[1].p[P_LYMODE] = LY_OFF;
    t_onset(6, 0);                                      /* TRIO FAT BASS */
    t_onset(0, 20);                                     /* ANALOG SAW BASS */
    t_noise();
    t_strings();
    t_stack();
    t_shed();
    t_poly();
    printf(bad ? "unison: %d FAILED\n" : "unison: all passed\n", bad);
    return bad;
}
