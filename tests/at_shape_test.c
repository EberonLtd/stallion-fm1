/* SPDX-License-Identifier: GPL-3.0-only */
/* Stallion: the pressure shaping (firmware/src/touch.h at_build, at_lut, at_follow; specs/touch R11-R13, R15) by its
 * numbers, on its own. (Its path from every source to the voices: tests/touch_test.c.)
 * R11 AT CURVE: out = 127 (in / 127)^p, p 1 (LIN), 0.5 (SOFT), 2 (HARD), within 1 step, monotonic, 0 -> 0, 127 -> 127;
 * R12 AT OFFSET -4..4: above 0, inputs below 8 x OFFSET read 0 and the rest is rescaled to reach 127 (within 1 step);
 *     below 0, inputs of 127 + 8 x OFFSET or more read 127; applied after the curve;
 * R13 AT ATTACK / AT RELEASE 0..400 ms: a rising step follows with the attack time constant, a falling one with the
 *     release's, each within 10 % (the time to 63 % of the step, at the control rate of 32 samples), reaching the
 *     target exactly; 0 ms: at once;
 * R15 the defaults (LIN, 0, 0 ms, 0 ms): the table is the identity and nothing is smoothed. */
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "../firmware/src/touch.h"

static int bad;
static void check(const char *what, int ok)
{
    printf("at shape: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* the ms a step from a to b takes to cover 63.2 % of it, following at_follow a block (32 samples) at a time; *end:
 * the value after 8 time constants (+ 1 s) */
static double step_ms(uint32_t a, uint32_t b, uint32_t tau, uint32_t *end)
{
    const double blk = 32.0 * 1000.0 / 44100.0;
    double goal = a + (double)((int)b - (int)a) * (1.0 - exp(-1.0)), t = 0, hit = -1;
    uint32_t s = a << 16, n, v = a, prev = a;
    for (n = 0; n < (uint32_t)((8.0 * tau + 1000.0) / blk); n++) {
        v = at_follow(&s, b);
        t += blk;
        if (hit < 0 && (b > a ? (double)v >= goal : (double)v <= goal)) {   /* (linear between the blocks' values) */
            double f = (goal - (double)prev) / ((double)v - (double)prev);
            hit = t - blk + f * blk;
        }
        prev = v;
    }
    *end = v;
    return hit;
}

int main(void)
{
    uint32_t i, ok;
    const touch_cfg_t d = TOUCH_CFG_DEFAULTS;
    touch_cfg = d;
    at_build();
    ok = !at_smooth;
    for (i = 0; i < 128u; i++)
        ok &= at_lut[i] == i;
    check("R15: the defaults (LIN, OFFSET 0, ATTACK 0, RELEASE 0): the identity, nothing smoothed", ok && d.at_curve == AT_LIN &&
          d.at_offset == 0 && d.at_attack == 0 && d.at_release == 0);
    {
        uint32_t s = 0, e = 1;
        touch_cfg.at_release = 50;                 /* (attack 0: a rise at once; release 50 ms) */
        at_build();
        e &= at_follow(&s, 127) == 127 && s == 127u << 16;
        touch_cfg.at_release = 0;
        at_build();
        check("R13: ATTACK 0: a rising value at once", e);
    }

    {
        static const double P[AT_CURVES] = {1.0, 0.5, 2.0};
        static const char *const N[AT_CURVES] = {"LIN", "SOFT", "HARD"};
        uint32_t c;
        for (c = 0; c < AT_CURVES; c++) {
            double worst = 0;
            uint32_t mono = 1;
            char m[96];
            touch_cfg.at_curve = (uint8_t)c;
            touch_cfg.at_offset = 0;
            at_build();
            for (i = 0; i < 128u; i++) {
                double want = 127.0 * pow(i / 127.0, P[c]), e = fabs(at_lut[i] - want);
                if (e > worst)
                    worst = e;
                if (i && at_lut[i] < at_lut[i - 1])
                    mono = 0;
            }
            snprintf(m, sizeof m, "R11: AT CURVE %s: 127 (in / 127)^%.1f within 1 step (worst %.2f), monotonic, 0 and 127 kept", N[c],
                     P[c], worst);
            check(m, worst <= 1.0 && mono && at_lut[0] == 0 && at_lut[127] == 127);
        }
    }

    {
        int32_t o;
        uint32_t c;
        ok = 1;
        for (c = 0; c < AT_CURVES; c++)
            for (o = -4; o <= 4; o++) {
                static const double P[AT_CURVES] = {1.0, 0.5, 2.0};
                touch_cfg.at_curve = (uint8_t)c;
                touch_cfg.at_offset = (int8_t)o;
                at_build();
                for (i = 0; i < 128u; i++) {
                    double y = 127.0 * pow(i / 127.0, P[c]), want;
                    double yr = (double)at_shape1(i, c, 0);   /* (the curve's own step, then the offset) */
                    if (o > 0)
                        want = yr < 8 * o ? 0 : (yr - 8 * o) * 127.0 / (127.0 - 8 * o);
                    else if (o < 0)
                        want = yr >= 127 + 8 * o ? 127 : yr * 127.0 / (127.0 + 8 * o);
                    else
                        want = y;
                    if (fabs(at_lut[i] - want) > 1.0 || (i && at_lut[i] < at_lut[i - 1]))
                        ok = 0;
                    if (o > 0 && yr < 8 * o && at_lut[i] != 0)
                        ok = 0;
                    if (o < 0 && yr >= 127 + 8 * o && at_lut[i] != 127)
                        ok = 0;
                }
                if (at_lut[127] != 127 || at_lut[0] != 0)
                    ok = 0;
            }
        check("R12: AT OFFSET -4..4 after each curve: above 0 under 8 x OFFSET reads 0, the rest reaches 127; below 0 from "
              "127 + 8 x OFFSET reads 127", ok);
        touch_cfg.at_curve = AT_LIN;
        touch_cfg.at_offset = 2;
        at_build();
        check("     e.g. LIN, OFFSET +2: 15 -> 0, 16 -> 0, 17 -> 1, 127 -> 127; OFFSET -2: 111 -> 127, 55 -> 63",
              at_lut[15] == 0 && at_lut[16] == 0 && at_lut[17] == 1 && at_lut[127] == 127 &&
              (touch_cfg.at_offset = -2, at_build(), at_lut[111] == 127 && at_lut[110] == 126 && at_lut[55] == 63));
    }

    {
        static const uint16_t TAU[] = {10, 50, 100, 200, 400};
        uint32_t k;
        ok = 1;
        touch_cfg.at_curve = AT_LIN;
        touch_cfg.at_offset = 0;
        for (k = 0; k < sizeof TAU / sizeof TAU[0]; k++) {
            uint32_t ea, er;
            double ta, tr;
            touch_cfg.at_attack = TAU[k];
            touch_cfg.at_release = (uint16_t)(TAU[sizeof TAU / sizeof TAU[0] - 1u - k]);
            at_build();
            ta = step_ms(0, 127, touch_cfg.at_attack, &ea);
            tr = step_ms(127, 0, touch_cfg.at_release, &er);
            printf("   ATTACK %3u ms: 63 %% at %6.1f ms; RELEASE %3u ms: at %6.1f ms\n", touch_cfg.at_attack, ta,
                   touch_cfg.at_release, tr);
            ok &= fabs(ta - touch_cfg.at_attack) <= 0.1 * touch_cfg.at_attack && ea == 127;
            ok &= fabs(tr - touch_cfg.at_release) <= 0.1 * touch_cfg.at_release && er == 0;
            {   /* a partial move: 40 -> 90 and back, the same time constants */
                uint32_t e1, e2;
                double t1 = step_ms(40, 90, touch_cfg.at_attack, &e1), t2 = step_ms(90, 40, touch_cfg.at_release, &e2);
                ok &= fabs(t1 - touch_cfg.at_attack) <= 0.1 * touch_cfg.at_attack && e1 == 90;
                ok &= fabs(t2 - touch_cfg.at_release) <= 0.1 * touch_cfg.at_release && e2 == 40;
            }
        }
        check("R13: ATTACK / RELEASE 10..400 ms: rising and falling time constants within 10 %, the target reached exactly", ok);
        touch_cfg.at_attack = 400;
        touch_cfg.at_release = 400;
        at_build();
        check("     the settings' range: 400 ms and 0 ms are the ends (touch_cfg_fix)",
              (touch_cfg.at_attack = 401, touch_cfg_fix(&touch_cfg), touch_cfg.at_attack == 0));
    }

    printf(bad ? "AT SHAPE TEST FAILED (%d)\n" : "at shape test passed\n", bad);
    return bad ? 1 : 0;
}
