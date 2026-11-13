/* SPDX-License-Identifier: GPL-3.0-only */
/* Stallion: KEY TOUCH (firmware/src/key_touch.c) on simulated converter readings. Checks 1-8 are ESTAMPIE's
 * tests/key_touch_test.c (github.com/ocean/estampie at af5a871, GPL-3.0-only, by Drew Robinson), ported to Stallion's
 * settings (touch.h touch_cfg, touch_cal); 9-15 check specs/touch R3-R7 and R9 by their numbers. The presses are shaped
 * as Velo measured them (notes/01-lab.md in github.com/charlesvestal/fm1-velocity): a pulled-up reading falls from
 * 1023 through the touch to the diode's ~190 once seated; a driven one reads the diode (185) plus the pill, lower the
 * harder the key is pressed.
 * 1. the log2 approximation within 0.015 octave;
 * 2. velocity: 127 at the floor and below, KT_VMIN at the ceiling and above, the log middle where the 0.7 curve puts it;
 * 3. the state machine: a landing after KT_WAIT pulled samples, its velocity from the lowest driven reading; a graze
 *    never lands; a release after two samples above RELEASE; samples alternate driven once touched; a held key's column
 *    LEDs stay dark;
 * 4. the gate: a measured key waits for its landing; one that never lands plays at 100 after KT_GATE_MS; a top-row key
 *    never waits; OFF, or not live (the emulator, the host), never waits;
 * 5. pressure: 0 resting, up leaning past the rest, back to 0 eased off; AT SENS more sensitive gives more;
 * 6. the calibration kept (kt_cal_store / kt_cal_take): every key's ends back exactly; none: the defaults;
 * 7. calibration: the medians of three gentle (the ceiling) and three firm (the floor) landings per key;
 * 8. learning: a key light past its ceiling learns it; a calibrated key moves less; calibration's landings teach nothing;
 * 9. R3: a measured key's note starts within 12 ms of the scan seeing it, checked every keyboard block (0.73 ms);
 * 10. R4: velocity monotonic in the landing reading on every key (default, calibrated, learnt ends): floor 127, ceiling 10;
 * 11. R5: one landing outside a key's range moves that end by 64 counts at most;
 * 12. R6: a fall under 0.04 octave reads 0, one of AT SENS or more 127 (0.12, 0.40, 0.80); every 5 ms, 30 ms (+-5);
 * 13. R7: a key held at a steady force (with the converter's noise and the dome's settling) for 10 s reads 4 or less
 *     from 0.5 s after landing; easing it resets its rest at once;
 * 14. R9: an uncalibrated key uses floor 450 and ceiling 896. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "../firmware/src/touch.h"
#include "../firmware/src/key_touch.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("key touch: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* one press of key k: the pulled-up curve (touch at 700, seated 190 from the second sample), every other sample
 * driven at `force` (a driven reading); n pulled samples, then released over `up` samples */
static uint32_t now;
static void press(uint32_t k, uint32_t force, uint32_t n)
{
    static const uint16_t fall[] = {700, 300, 190};
    uint32_t i;
    for (i = 0; i < n; i++) {
        kt_sample(k, i < 3u ? fall[i] : 190u, 0);
        if (kt_want_drive(k))
            kt_sample(k, force, 1);
        now += 2;
    }
}
static void lift(uint32_t k)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        kt_sample(k, i ? 1023u : 650u, 0);
}
/* the driven reading of a pill of resistance r (counts above the diode) fallen by oct octaves */
static uint32_t fallen(uint32_t r, double oct) { return KT_DBASE + (uint32_t)lround(r * pow(2.0, -oct)); }
static uint32_t t_at;
static uint8_t hold(uint32_t k, uint32_t d, uint32_t ms);
/* a fresh landing of key k at the rest r (the pill's counts), 400 ms resting, then 200 ms fallen by oct octaves: its
 * pressure then */
static uint8_t lean_after(uint32_t k, uint32_t r, double oct)
{
    lift(k);
    (void)kt_lean(k, t_at += KT_AT_MS);            /* (up: its pressure's state at rest) */
    press(k, KT_DBASE + r, 6);
    (void)hold(k, KT_DBASE + r, 400);
    return hold(k, fallen(r, oct), 200);
}
/* key k held, its driven reading d: the pressure after ms milliseconds of it (every KT_AT_MS) */
static uint8_t hold(uint32_t k, uint32_t d, uint32_t ms)
{
    uint8_t p = 0;
    uint32_t i;
    for (i = 0; i < ms / KT_AT_MS; i++) {
        kt_key[k].d = (uint16_t)d;
        p = kt_lean(k, t_at += KT_AT_MS);
    }
    return p;
}

int main(void)
{
    uint32_t x, k;
    double err = 0;
    for (x = 1; x < 5000u; x++) {
        double e = fabs(kt_log2q8(x) / 256.0 - log2((double)x));
        if (e > err)
            err = e;
    }
    check("1. log2 in Q8 within 0.015 octave", err < 0.015);

    kt_init();
    {
        uint8_t a = kt_velocity(450, 450, 896), b = kt_velocity(896, 450, 896), c = kt_velocity(300, 450, 896);
        uint8_t m = kt_velocity(185 + (uint32_t)sqrt(265.0 * 711.0), 450, 896), up = 1;
        uint32_t d;
        for (d = 460; d < 896; d += 10)
            up &= kt_velocity(d, 450, 896) <= kt_velocity(d - 10, 450, 896);
        check("2. velocity: 127 at the floor and harder, KT_VMIN at the ceiling", a == 127 && c == 127 && b == KT_VMIN);
        check("   softer landings never louder; the log middle 82 (the 0.7 curve)", up && m >= 80 && m <= 84);
    }

    kt_init();
    kt_mode = KT_VEL;
    press(3, 500, 6);
    check("3. a press lands, its velocity from the driven reading",
          kt_key[3].st == KT_S_DOWN && kt_key[3].land == 1 && kt_key[3].dland == 500);
    kt_sample(3, 190, 0);
    check("   held: after a pulled sample the next driven, its column's LEDs dark", kt_want_drive(3) && !kt_led_ok(3) && kt_led_ok(4));
    kt_sample(3, 500, 1);
    kt_sample(3, 1023, 0);
    check("   one sample open is not a release", kt_key[3].st != KT_S_OPEN);
    lift(3);
    check("   two open: released, nothing driven, LEDs again", kt_key[3].st == KT_S_OPEN && !kt_want_drive(3) && kt_led_ok(3));
    kt_sample(5, 700, 0);
    kt_sample(5, 1023, 0);
    check("   a graze (touched, gone before seating) never lands", kt_key[5].st == KT_S_OPEN && kt_key[5].land == 0);
    press(6, 300, 6);
    press(7, 800, 6);
    check("   a firmer landing plays louder",
          kt_velocity(kt_key[6].dland, kt_key[6].floor_, kt_key[6].ceil) > kt_velocity(kt_key[7].dland, kt_key[7].floor_, kt_key[7].ceil));
    kt_mode = KT_OFF;
    check("   OFF (and not calibrating): no key driven", !kt_want_drive(6));

    kt_init();
    kt_mode = KT_VEL;
    kt_live = 1;
    memset(kt_seen, 0, sizeof kt_seen);
    {
        uint32_t cur, prev = 0, t = 1000;
        cur = kt_gate(1u << 2 | 1u << 22, prev, t);
        check("4. a measured key waits for its landing; a top-row key plays at once (at 100)",
              !((cur >> 2) & 1u) && ((cur >> 22) & 1u) && kt_kvel[22] == 0);
        prev = cur;
        press(2, 400, 6);
        cur = kt_gate(1u << 2 | 1u << 22, prev, t + 4);
        check("   landed: it plays, with its velocity", ((cur >> 2) & 1u) && kt_kvel[2] == kt_velocity(400, 450, 896));
        prev = cur;
        cur = kt_gate(prev | 1u << 9, prev, t + 10);
        check("   one that never lands waits ...", !((cur >> 9) & 1u));
        cur = kt_gate(prev | 1u << 9, prev, t + 10 + KT_GATE_MS);
        check("   ... then plays at 100 after KT_GATE_MS", ((cur >> 9) & 1u) && kt_kvel[9] == 0);
        kt_live = 0;
        kt_kvel[11] = 77;
        check("   not live (the emulator, the host): never waits, at 100", kt_gate(1u << 11, 0, t) == 1u << 11 && !kt_kvel[11]);
        kt_live = 1;
        kt_mode = KT_OFF;
        check("   OFF: never waits", kt_gate(1u << 12, 0, t) == 1u << 12);
    }

    kt_init();
    kt_mode = KT_VELAT;
    touch_cfg.at_sens = 40;
    {
        uint32_t t = 0, i;
        uint8_t rest = 0, firm = 0, eased = 0, firm9 = 0;
        press(4, 600, 6);
        for (i = 0; i < 80u; i++) {                /* resting at 600 for 400 ms */
            kt_key[4].d = 600;
            rest = kt_lean(4, t += 5);
        }
        for (i = 0; i < 20u; i++) {                /* leaning: 400 */
            kt_key[4].d = 400;
            firm = kt_lean(4, t += 5);
        }
        for (i = 0; i < 24u; i++) {                /* eased back: 120 ms (Stallion: 30 ms smoothing, ESTAMPIE's 26: 100) */
            kt_key[4].d = 640;
            eased = kt_lean(4, t += 5);
        }
        check("5. pressure: 0 resting, up leaning past the rest, nearly gone 120 ms eased off",
              rest == 0 && firm > 60 && eased <= 4);
        for (i = 0; i < 36u; i++) {
            kt_key[4].d = 640;
            eased = kt_lean(4, t += 5);
        }
        check("   and gone 300 ms after", eased == 0);
        touch_cfg.at_sens = 12;                    /* the most sensitive */
        for (i = 0; i < 20u; i++) {
            kt_key[4].d = 560;
            firm9 = kt_lean(4, t += 5);
        }
        touch_cfg.at_sens = 80;
        for (i = 0; i < 20u; i++) {
            kt_key[4].d = 640;
            (void)kt_lean(4, t += 5);
        }
        for (i = 0; i < 20u; i++) {
            kt_key[4].d = 560;
            firm = kt_lean(4, t += 5);
        }
        check("   AT SENS 0.12 gives more than 0.80 for the same lean", firm9 > firm);
        touch_cfg.at_sens = 40;
    }

    kt_init();
    {
        touch_cal_t c, none, after;
        uint32_t ok = 1;
        memset(&c, 0, sizeof c);
        for (k = 0; k < KT_NKEY; k++)
            c.floor_[k] = (uint16_t)(300 + 13 * k), c.ceil[k] = (uint16_t)(700 + 11 * k);
        c.cal = 1;
        kt_cal_take(&c);
        for (k = 0; k < KT_NKEY; k++)
            ok &= kt_key[k].floor_ == 300 + 13 * k && kt_key[k].ceil == 700 + 11 * k && kt_nland[k] == KT_LEARN_N;
        kt_cal_store(&after);
        check("6. the calibration kept: every key's ends back exactly, learnt slowly", ok && kt_calibrated &&
              !memcmp(&after, &c, sizeof c));
        kt_learn(3, 1010);                         /* (played since: its ends learnt) */
        kt_cal_store(&after);
        check("   the ends learnt since are not what is kept (until power-off only)", kt_key[3].ceil != c.ceil[3] &&
              !memcmp(&after, &c, sizeof c));
        memset(&none, 0, sizeof none);
        kt_cal_take(&none);
        check("   none: not calibrated, the defaults", !none.cal && !kt_calibrated && kt_key[0].floor_ == KT_FLOOR0 &&
                                                         kt_key[19].ceil == KT_CEIL0);
        c.floor_[5] = 2000;                        /* (a damaged value: that key's defaults) */
        kt_cal_take(&c);
        check("   a value no landing gives: that key's defaults, the others kept",
              kt_key[5].floor_ == KT_FLOOR0 && kt_key[5].ceil == KT_CEIL0 && kt_key[6].floor_ == 300 + 13 * 6);
    }

    kt_init();
    kt_mode = KT_VEL;
    kt_cal_begin();
    for (k = 0; k < KT_NKEY; k++) {
        press(k, 800 - k, 6), lift(k);
        press(k, 850 - k, 6), lift(k);
        press(k, 820 - k, 6), lift(k);
    }
    check("7. calibration: three gentle presses of every key taken", kt_cal_left(1, 0) == 0 && kt_cal_left(1, 19) == 0);
    kt_cal_phase = 2;
    for (k = 0; k < KT_NKEY; k++) {
        press(k, 320 + k, 6), lift(k);
        press(k, 360 + k, 6), lift(k);
        press(k, 340 + k, 6), lift(k);
    }
    kt_cal_finish();
    check("   each key's ceiling the gentle median, its floor the firm median, kept",
          kt_key[0].ceil == 820 && kt_key[0].floor_ == 340 && kt_key[19].ceil == 801 && kt_key[19].floor_ == 359 &&
          kt_calibrated && !kt_cal_phase && touch_cal.cal && touch_cal.ceil[0] == 820 && touch_cal.floor_[19] == 359);
    kt_mode = KT_OFF;
    kt_cal_phase = 1;
    kt_sample(8, 700, 0);
    check("   KEY TOUCH OFF: calibrating drives the keys all the same", kt_want_drive(8));
    kt_cal_phase = 0;

    kt_init();
    kt_mode = KT_VEL;
    kt_live = 1;
    memset(kt_seen, 0, sizeof kt_seen);
    {
        uint32_t i, prev = 0, t = 5000, first = 0, last = 0, c0;
        for (i = 0; i < 6u; i++) {                 /* key 16 (A4): light presses at 960, past the ceiling (896) */
            press(16, 960, 6);
            prev = kt_gate(1u << 16, 0, t);
            if (!i)
                first = kt_kvel[16];
            last = kt_kvel[16];
            lift(16);
            prev = kt_gate(0, prev, t += 50);
        }
        check("8. a key light past its ceiling: KT_VMIN, not silent; its ceiling learnt to them",
              first == KT_VMIN && last == KT_VMIN && kt_key[16].ceil >= 955);
        check("   so a medium press of it no longer plays at the lightest",
              kt_velocity(700, kt_key[16].floor_, kt_key[16].ceil) > 40u);
        for (i = 0; i < 6u; i++) {
            press(16, 380, 6);
            prev = kt_gate(1u << 16, 0, t);
            lift(16);
            prev = kt_gate(0, prev, t += 50);
        }
        check("   its floor comes down to its firm presses", kt_key[16].floor_ < 420 && kt_velocity(380, kt_key[16].floor_, kt_key[16].ceil) > 115);
        kt_ends(&kt_key[17], KT_FLOOR0, KT_CEIL0);
        kt_nland[17] = KT_LEARN_N;
        c0 = kt_key[17].ceil;
        press(17, 960, 6);
        (void)kt_gate(1u << 17, 0, t);
        lift(17);
        (void)kt_gate(0, 1u << 17, t += 50);
        check("   a calibrated key's ceiling moves a smaller step", kt_key[17].ceil > c0 && kt_key[17].ceil - c0 <= 24u);
        kt_cal_begin();
        c0 = kt_key[18].ceil;
        press(18, 990, 6);
        (void)kt_gate(1u << 18, 0, t);
        lift(18);
        (void)kt_gate(0, 1u << 18, t += 50);
        kt_cal_phase = 0;
        check("   calibration's landings teach nothing", kt_key[18].ceil == c0);
        (void)prev;
    }

    /* 9. R3: the keyboard asks every block (0.73 ms); the scan's ms count. A key that never lands: its note within
     * 12 ms of the first block that saw it down; one that lands at 7 ms: then, with its velocity */
    kt_init();
    kt_mode = KT_VEL;
    kt_live = 1;
    memset(kt_seen, 0, sizeof kt_seen);
    {
        double t;
        uint32_t cur, played = 0, t0 = 20000, worst = 0, ok = 1, ph;
        for (ph = 0; ph < 8u; ph++) {              /* the press at any point of a ms */
            double first = 0;
            int seen = 0;
            played = 0;
            for (t = t0 + ph / 8.0; !played; t += 32.0 / 44.1) {
                uint32_t ms = (uint32_t)t;
                if (!seen)
                    first = t, seen = 1;
                cur = kt_gate(1u << 13, 0, ms);
                if (cur & (1u << 13)) {
                    played = 1;
                    if ((uint32_t)ceil(t - first) > worst)
                        worst = (uint32_t)ceil(t - first);
                    ok &= kt_kvel[13] == 0;        /* (not measured: 100) */
                }
                if (t - first > 50)
                    break;
            }
            ok &= played;
            (void)kt_gate(0, 1u << 13, (uint32_t)t + 20);   /* let go */
            t0 += 100;
        }
        check("9. R3: a key never measured plays at 100 within 12 ms of the keyboard seeing it", ok && worst <= 12u);
        {
            uint32_t ms, at = 0;
            for (ms = 30000; ms < 30020; ms++) {
                if (ms == 30007)
                    press(13, 420, 6);             /* lands 7 ms in */
                cur = kt_gate(1u << 13, 0, ms);
                if (cur & (1u << 13)) {
                    at = ms;
                    break;
                }
            }
            check("   one measured by then plays when it lands, at its velocity",
                  at == 30007 && kt_kvel[13] == kt_velocity(420, KT_FLOOR0, KT_CEIL0) && kt_kvel[13] != 0);
        }
    }

    /* 10. R4 */
    {
        uint32_t ok = 1, d, set;
        static const uint16_t ENDS[4][2] = {{KT_FLOOR0, KT_CEIL0}, {260, 520}, {340, 1000}, {600, 690}};
        for (set = 0; set < 4u; set++) {
            uint32_t fl = ENDS[set][0], cl = ENDS[set][1];
            kt_init();
            for (k = 0; k < KT_NKEY; k++)
                kt_ends(&kt_key[k], fl, cl);
            for (k = 0; k < KT_NKEY; k++) {
                uint32_t f = kt_key[k].floor_, c = kt_key[k].ceil;
                ok &= kt_velocity(f, f, c) == 127 && kt_velocity(c, f, c) == 10;
                for (d = 186; d < 1023u; d++)
                    ok &= kt_velocity(d, f, c) >= kt_velocity(d + 1, f, c);
            }
        }
        check("10. R4: on every key, a firmer (lower) landing never plays softer; floor 127, ceiling 10", ok && KT_VMIN == 10);
    }

    /* 11. R5 */
    {
        uint32_t ok = 1, i;
        kt_init();
        for (i = 0; i < 40u; i++) {                /* each landing far past an end: the end moves 64 at most */
            uint32_t f = kt_key[0].floor_, c = kt_key[0].ceil;
            kt_learn(0, 1020);
            ok &= kt_key[0].ceil <= c + 64u;
            kt_learn(1, 200);
            (void)f;
        }
        kt_init();
        {
            uint32_t f = kt_key[1].floor_;
            kt_learn(1, 190);
            ok &= f - kt_key[1].floor_ <= 64u && kt_key[1].floor_ < f;
        }
        kt_init();
        kt_nland[2] = KT_LEARN_N;                  /* (and once learnt: less) */
        {
            uint32_t c = kt_key[2].ceil;
            kt_learn(2, 1020);
            ok &= kt_key[2].ceil - c <= 24u && kt_key[2].ceil > c;
        }
        check("11. R5: one landing outside a key's range moves that end by 64 counts at most (24 once learnt)", ok);
    }

    /* 12. R6: lean after the rest settled (250 ms) */
    {
        static const uint8_t SENS[3] = {12, 40, 80};
        uint32_t s, ok = 1;
        for (s = 0; s < 3u; s++) {
            uint8_t p0, p1, p2;
            touch_cfg.at_sens = SENS[s];
            kt_init();
            kt_mode = KT_VELAT;
            p0 = lean_after(5, 400, 0.035);                 /* under the dead zone */
            p1 = lean_after(5, 400, SENS[s] / 100.0 + 0.06);   /* AT SENS and the dead zone past the rest */
            p2 = lean_after(5, 400, 0.04 + SENS[s] / 200.0);   /* half way */
            ok &= p0 == 0 && p1 == 127 && p2 > 40 && p2 < 90;
            if (p0 || p1 != 127 || p2 <= 40 || p2 >= 90)
                printf("   (AT SENS %u: under the dead zone %u, AT SENS past %u, half %u)\n", SENS[s], p0, p1, p2);
        }
        check("12. R6: a fall under 0.04 octave reads 0, of AT SENS or more 127 (0.12, 0.40, 0.80)", ok);
        touch_cfg.at_sens = 40;
        kt_init();
        kt_mode = KT_VELAT;
        (void)lean_after(6, 400, 0);
        {
            uint8_t p = hold(6, fallen(400, 1.0), 50);      /* a step to 127: 50 ms of the smoothing */
            double tau = -50.0 / log(1.0 - p / 127.0);
            check("    every 5 ms, smoothed with a time constant of 30 ms (within 5 ms)", tau > 25.0 && tau < 35.0);
            if (!(tau > 25.0 && tau < 35.0))
                printf("   (tau %.1f ms)\n", tau);
        }
    }

    /* 13. R7: a key held at a steady force for 10 s: the dome settling 6 % over the first 300 ms and 2 % more over
     * the next 10 s, the converter's +-2 counts of noise */
    {
        uint32_t ms, worst = 0, d;
        uint8_t p;
        srand(7);
        kt_init();
        kt_mode = KT_VELAT;
        touch_cfg.at_sens = 12;                    /* (the most sensitive: the hardest case) */
        lift(7);
        (void)kt_lean(7, t_at += KT_AT_MS);
        press(7, 600, 6);
        for (ms = 0; ms < 10000u; ms += KT_AT_MS) {
            double r = 415.0 * (1.0 - 0.06 * (ms < 300u ? ms / 300.0 : 1.0) - 0.02 * (ms > 300u ? (ms - 300) / 9700.0 : 0));
            d = KT_DBASE + (uint32_t)lround(r) + (uint32_t)(rand() % 5) - 2u;
            kt_key[7].d = (uint16_t)d;
            p = kt_lean(7, t_at += KT_AT_MS);
            if (ms >= 500u && p > worst)
                worst = p;
        }
        check("13. R7: a key held at a steady force 10 s reads 4 or less from 0.5 s after landing", worst <= 4u);
        if (worst > 4u)
            printf("   (worst %u)\n", worst);
        touch_cfg.at_sens = 40;
        (void)hold(7, fallen(415, 0.6), 200);      /* leaning */
        kt_key[7].d = 640;
        (void)kt_lean(7, t_at += KT_AT_MS);
        check("    easing the key resets its rest at once", kt_base8[7] == (int32_t)((640u - KT_DBASE) << 8));
    }

    kt_init();
    check("14. R9: an uncalibrated key: floor 450, ceiling 896",
          kt_key[0].floor_ == 450 && kt_key[0].ceil == 896 && kt_key[19].floor_ == 450 && kt_key[19].ceil == 896 &&
          !kt_calibrated);

    /* 17. KEY TOUCH AT (velocity from these keys is not consistent enough to play): pressure alone; a
     * measured key never waits for a landing, every note at 100; the pressure as VEL+AT's */
    kt_init();
    kt_mode = KT_AT;
    kt_live = 1;
    memset(kt_seen, 0, sizeof kt_seen);
    {
        uint32_t i, t = 9000;
        uint8_t p_at = 0, p_va = 0;
        uint32_t cur = kt_gate(1u << 3, 0, t);
        check("17. AT: a measured key plays at once (no wait), at 100", ((cur >> 3) & 1u) && kt_kvel[3] == 0);
        check("    its row is still driven (the pressure needs the readings)", (kt_sample(3, 700, 0), kt_want_drive(3)));
        lift(3);
        press(4, 600, 6);
        for (i = 0; i < 80u; i++)
            kt_key[4].d = 600, (void)kt_lean(4, t += 5);
        for (i = 0; i < 20u; i++)
            kt_key[4].d = 400, p_at = kt_lean(4, t += 5);
        lift(4);
        (void)kt_lean(4, t += 5);
        kt_mode = KT_VELAT;
        press(4, 600, 6);
        for (i = 0; i < 80u; i++)
            kt_key[4].d = 600, (void)kt_lean(4, t += 5);
        for (i = 0; i < 20u; i++)
            kt_key[4].d = 400, p_va = kt_lean(4, t += 5);
        check("    the pressure of a lean is VEL+AT's (within 2: the smoothing's rest)", p_at > 60 && abs((int)p_at - (int)p_va) <= 2);
        kt_mode = KT_VEL;
        lift(4);
        (void)kt_lean(4, t += 5);
        press(4, 600, 6);
        for (i = 0; i < 20u; i++)
            kt_key[4].d = 400, p_va = kt_lean(4, t += 5);
        check("    VEL: no pressure", p_va == 0);
        check("    a setting from before AT (0..2) is kept; past the last mode, the default", (touch_cfg.key_touch = KT_AT,
              touch_cfg_fix(&touch_cfg), touch_cfg.key_touch == KT_AT) && (touch_cfg.key_touch = KT_MODES,
              touch_cfg_fix(&touch_cfg), touch_cfg.key_touch == KT_OFF));
    }

    /* 16. TOUCH CAL's feedback (two presses gave no sign the difference was caught): the meter rises with the
     * force on one scale for every key, the last landing is shown with its key and whether it counted, and keys whose
     * gentle and firm presses read alike are named */
    kt_init();
    kt_mode = KT_VEL;
    {
        uint32_t d, up = 1, held_d = 0;
        for (d = 270; d < 1000u; d += 10)
            up &= kt_cal_meter(d) <= kt_cal_meter(d - 10);
        check("16. TOUCH CAL's meter: firmer reads higher; 0 open, 127 at the hardest",
              up && kt_cal_meter(1023) == 0 && kt_cal_meter(KT_CAL_HARD) == 127 && kt_cal_meter(700) > 0 && kt_cal_meter(700) < 127);
        press(5, 520, 3);                          /* held, not calibrating: the meter follows it */
        check("    the held key and its driven reading", kt_held(&held_d) == 5 && held_d == 520);
        lift(5);
        kt_cal_begin();
        check("    a new calibration shows no last press", kt_cal_lastk == 0xFFu);
        press(6, 850, 6), lift(6);
        check("    a gentle landing: its key, its reading, counted", kt_cal_lastk == 6 && kt_cal_lastv == 850 && kt_cal_lastok);
        press(6, 840, 6), lift(6);
        press(6, 860, 6), lift(6);
        press(6, 855, 6), lift(6);
        check("    a fourth: shown, not counted", kt_cal_lastk == 6 && kt_cal_lastv == 855 && !kt_cal_lastok);
        for (k = 0; k < KT_NKEY; k++)              /* every other key gentle 850, key 6 above */
            if (k != 6u)
                press(k, 850, 6), lift(k), press(k, 850, 6), lift(k), press(k, 850, 6), lift(k);
        kt_cal_phase = 2;
        for (k = 0; k < KT_NKEY; k++)              /* firm: 350, but key 9 pressed as gently as before */
            press(k, k == 9u ? 840u : 350u, 6), lift(k), press(k, k == 9u ? 845u : 350u, 6), lift(k),
                press(k, k == 9u ? 850u : 350u, 6), lift(k);
        kt_cal_finish();
        check("    a key whose firm presses read as its gentle ones is named, the others not", kt_cal_close == 1u << 9);
    }

    /* 15. a landing never plays another press's note: a soft press seating after KT_GATE_MS (seen on an FM-1, after
     * TOUCH CAL: soft hits played hard, then the next hard hit played the soft one's velocity, one press behind) */
    kt_init();
    kt_mode = KT_VEL;
    kt_live = 1;
    memset(kt_seen, 0, sizeof kt_seen);
    {
        uint32_t t = 5000, prev = 0, cur = 0, ms, soft, hard = kt_velocity(300, 450, 896);
        uint8_t v_soft = 0, v_hard;
        kt_sample(8, 700, 0);                      /* touched: a slow soft press, driven at 850, seated after 20 ms */
        for (ms = 0; ms <= 22u; ms += 2) {
            if (kt_want_drive(8))
                kt_sample(8, 850, 1);
            else
                kt_sample(8, ms < 18u ? 600u : 190u, 0);
            cur = kt_gate(1u << 8, prev, t + ms);
            if ((cur >> 8) & 1u && !((prev >> 8) & 1u))
                v_soft = kt_kvel[8];
            prev = cur;
        }
        soft = kt_velocity(850, 450, 896);
        check("15. a soft press seating after KT_GATE_MS plays soft (its lowest driven reading so far), not 100",
              v_soft == soft && v_soft < hard);
        lift(8);
        prev = kt_gate(0, prev, t + 60);
        cur = kt_gate(1u << 8, prev, t + 100);     /* a hard press: the scan sees it down first (as on the FM-1) */
        {
            static const uint16_t fall[] = {700, 300, 190};
            for (ms = 0; !((cur >> 8) & 1u) && ms <= KT_GATE_MS; ms += 2) {
                kt_sample(8, ms / 2u < 3u ? fall[ms / 2u] : 190u, 0);
                if (kt_want_drive(8))
                    kt_sample(8, 300, 1);
                cur = kt_gate(1u << 8, prev, t + 100 + ms);
            }
        }
        v_hard = kt_kvel[8];
        check("    the next, hard press plays its own velocity, never the soft one's left behind", v_hard == hard);
        lift(8);
        prev = kt_gate(0, cur, t + 160);
        kt_sample(8, 700, 0);                      /* the gate sees the key down before its touch: no stale landing */
        cur = kt_gate(1u << 8, prev, t + 200);
        check("    a key down before its touch waits for its own landing", !((cur >> 8) & 1u));
    }

    printf(bad ? "KEY TOUCH TEST FAILED (%d)\n" : "key touch test passed\n", bad);
    return bad ? 1 : 0;
}
