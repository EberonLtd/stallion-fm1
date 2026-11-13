/* SPDX-License-Identifier: GPL-3.0-only */
/* The engines against their originals (as measured): VCO-2's fine tune, an RC attack, POLY2, the delay's
 * free times, MARE's SPACE and drive, STAMPEDE's plain SINE after the filters.
 *   cc -O2 -w -DCLIPPER_SYNTH=1 -Ibuild/gen -Ifirmware/src -Ifirmware/hal -Itests tests/gaps_test.c -lm -lpthread */
#include <stdio.h>
#include <math.h>
#define main hostsim_main
#include "hostsim.c"
#undef main
static int bad;
static void chk(const char *w, int ok) { printf("gaps: %-62s %s\n", w, ok ? "ok" : "FAIL"); bad |= !ok; }
static int32_t o[2 * CTL];
static double el, er, diff, hl, hr;   /* hl, hr: each side's first-difference energy (its brightness) */
static void setup(uint32_t e, uint32_t pi)
{
    uint32_t i;
    host_tracks_init();
    song.g[G_BPM] = 100;
    for (i = 0; i < P_E0; i++)
        trk[0].p[i] = TP[i].def;
    host_preset(&trk[0], e, pi);
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    song.sel = 0;
}
static void run(double s, int m)
{
    int b, i, n = (int)(s * 44100 / CTL);
    double pl = 0, pr = 0;
    el = er = diff = hl = hr = 0;
    for (b = 0; b < n; b++) {
        mix_block(o, CTL);
        if (m)
            for (i = 0; i < CTL; i++) {
                double l = o[2 * i], r = o[2 * i + 1];
                el += l * l, er += r * r, diff += (l - r) * (l - r);
                hl += (l - pl) * (l - pl), hr += (r - pr) * (r - pr), pl = l, pr = r;
            }
    }
}
int main(void)
{
    uint32_t i, ms[2], c = 0;
    int32_t e1, e2;
    /* the delay's free times */
    song.g[G_BPM] = 100;
    chk("the tempo's 1/8 as before", dly_samples(1) == dly_units(1) / 100u);
    chk("35 MS", dly_samples(DLY_NSYNC) == 1543u);
    chk("780 MS, whatever the tempo", dly_samples(DLY_NSYNC + 11u) == 34398u);
    /* an RC attack: past half way at half the time */
    setup(23, host_pi(23, "INIT"));
    trk[0].p[P_ATK] = 90;
    trk[0].p[P_E5] = 127;
    trk_note_on(&trk[0], 60, 100);
    for (i = 0; i < 40000u && trk[0].v[0].stage == 1u; i++, c++)
        mix_block(o, CTL);
    setup(23, host_pi(23, "INIT"));
    trk[0].p[P_ATK] = 90;
    trk_note_on(&trk[0], 60, 100);
    for (i = 0; i < c / 2u; i++)
        mix_block(o, CTL);
    e1 = trk[0].v[0].env;
    chk("PALOMINO's attack: well past half at half its time", c > 8u && e1 > (int32_t)(0.6 * (1 << 24)));
    setup(0, host_pi(0, "BOOM BASS"));                 /* (the ANALOG sound this was set on, by name) */
    trk[0].p[P_ATK] = 90;
    trk_note_on(&trk[0], 60, 100);
    for (i = 0; i < c / 2u; i++)
        mix_block(o, CTL);
    e2 = trk[0].v[0].env;
    chk("ANALOG's as before: straight", e2 < e1);
    /* POLY2 */
    setup(23, host_pi(23, "INIT"));
    trk[0].p[P_ALLOC] = 2;
    trk[0].p[P_REL] = 110;
    trk_note_on(&trk[0], 60, 100);
    run(0.1, 0);
    trk_note_off(&trk[0], 60);
    run(0.05, 0);
    trk_note_on(&trk[0], 64, 100);
    run(0.1, 0);
    c = 0;
    for (i = 0; i < NVOICE; i++)
        c += trk[0].v[i].active && trk[0].v[i].note == 60;
    chk("POLY2: the new note cuts the tail", !c);
    /* VCO-2's fine tune */
    setup(23, host_pi(23, "INIT"));
    trk_note_on(&trk[0], 60, 100);
    run(1.0, 0);
    run(0.5, 1);
    e1 = (int32_t)(el / 1e6);
    setup(23, host_pi(23, "INIT"));
    trk[0].p[P_DETUNE] = 120;
    trk_note_on(&trk[0], 60, 100);
    run(1.0, 0);
    run(0.5, 1);
    chk("DTUNE moves VCO-2 in POLY", (int32_t)(el / 1e6) != e1);
    /* MARE: SPACE moves the left filter (VCF 1) alone, + above VCF 2 (the manual's sign): SPACE 40, the left brighter.
     * Judged by brightness (first-difference energy over energy), not level: the side balance evens the levels */
    setup(26, host_pi(26, "INIT"));
    trk[0].p[P_E7] = MTM_ST, trk[0].p[P_E6] = 40, trk[0].p[P_E4] = 50;
    trk[0].p[P_VINT] = 0, trk[0].p[P_SPREAD] = 0;
    trk_note_on(&trk[0], 48, 100);
    run(0.5, 0);
    run(0.5, 1);
    chk("MARE: SPACE up, the left filter brighter (VCF 1 above VCF 2)", hl / (el + 1) > 1.2 * hr / (er + 1));
    {   /* (the control: SPACE 0, both sides alike) */
        double bl = hl / (el + 1), br = hr / (er + 1), sl = el, sr = er, shl = hl, shr = hr;
        setup(26, host_pi(26, "INIT"));
        trk[0].p[P_E7] = MTM_ST, trk[0].p[P_E6] = 0, trk[0].p[P_E4] = 50;
        trk[0].p[P_VINT] = 0, trk[0].p[P_SPREAD] = 0;
        trk_note_on(&trk[0], 48, 100);
        run(0.5, 0);
        run(0.5, 1);
        chk("MARE: SPACE 0, the two sides equally bright", hl / (el + 1) < 1.05 * hr / (er + 1) && hr / (er + 1) < 1.05 * hl / (el + 1));
        printf("gaps:   (SPACE 40: L/R brightness %.2f; SPACE 0: %.2f)\n", bl / br, (hl / (el + 1)) / (hr / (er + 1)));
        el = sl, er = sr, hl = shl, hr = shr;   /* (the DIST check below goes on from SPACE 40) */
    }
    e1 = (int32_t)(10 * log10(er + 1));
    setup(26, host_pi(26, "INIT"));
    trk[0].p[P_E7] = MTM_ST, trk[0].p[P_E6] = 40, trk[0].p[P_E4] = 50, trk[0].p[P_DIST] = 100;
    trk[0].p[P_VINT] = 0, trk[0].p[P_SPREAD] = 0;
    trk_note_on(&trk[0], 48, 100);
    run(0.5, 0);
    run(0.5, 1);
    chk("MARE: DIST drives the ladders, in range", er > 0 && 10 * log10(er + 1) >= e1 - 1);
    /* STAMPEDE: the plain SINE ignores the filters */
    setup(24, host_pi(24, "INIT"));
    trk[0].p[P_E0] = CSW_SINE, trk[0].p[P_E5] = 10, trk[0].p[P_E6] = 0;
    trk[0].p[P_E2] = 0, trk[0].p[P_ED_FLT] = 0;
    trk_note_on(&trk[0], 72, 100);
    run(0.5, 0);
    run(0.5, 1);
    e1 = (int32_t)(10 * log10(el + 1));
    setup(24, host_pi(24, "INIT"));
    trk[0].p[P_E0] = CSW_SINE, trk[0].p[P_E5] = 120, trk[0].p[P_E6] = 0;
    trk[0].p[P_E2] = 0, trk[0].p[P_ED_FLT] = 0;
    trk_note_on(&trk[0], 72, 100);
    run(0.5, 0);
    run(0.5, 1);
    chk("STAMPEDE: SINE as loud with the LPF shut", abs((int32_t)(10 * log10(el + 1)) - e1) <= 1);
    return bad;
}
