/* SPDX-License-Identifier: GPL-3.0-only */
/* the SMK-37's pad pressure (firmware/src/smk_press.h), against the readings logged on a unit: a held squeeze moves
 * about 600..920 and must stay off both ends, the hardest press (about 980) is 127, a touch is 0, more force never
 * less, and a hard strike changes nothing after it */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BOARD_SMK37 1                                             /* (pat_cfg.h: the SMK-37's defaults) */
#include "../firmware/src/smk_press.h"
int main(void)
{
    uint32_t r, last = 0, i, v, l = 0, lo, hi;
    assert(smk_press_of(0, 0) == 0);
    assert(smk_press_of(200, 0) == 0);                            /* a touch */
    assert(smk_press_of(50, 100) == 0);                           /* below the rest */
    lo = smk_press_of(600, 0);
    hi = smk_press_of(920, 0);
    printf("press: a squeeze from 600 to 920: %u to %u of 127\n", lo, hi);
    assert(lo >= 30u && lo <= 70u);                               /* (the log of the range of 800: 116 to 127) */
    assert(hi >= 105u && hi < 127u);
    assert(smk_press_of(980, 0) == 127u);
    for (r = 0; r <= 1100u; r++) {                                /* no steps back as it rises */
        v = smk_press_of(r, 0);
        assert(v >= last);
        last = v;
    }
    assert(last == 127u);
    assert(smk_press_of(0xFFFFu, 0) == 127u);                     /* no overflow past the top */
    v = smk_press_of(760, 0);
    (void)smk_press_of(1023, 0);
    assert(smk_press_of(760, 0) == v);                            /* a hard strike changes nothing after it */
    for (i = 0; i < 120u; i++)                                    /* the glide reaches both ends exactly */
        l = smk_press_glide(l, 127);
    assert(l == 127u);
    l = smk_press_glide(l, 0);
    assert(l >= 120u);                                            /* falling: a note's tail is not cut */
    for (i = 0; i < 200u; i++)
        l = smk_press_glide(l, 0);
    assert(l == 0u);
    for (i = 0; i < 24u; i++)                                     /* 120 ms: most of the way, not at once */
        l = smk_press_glide(l, 127);
    assert(l > 60u && l < 110u);
    l = smk_press_glide(0, 127);
    assert(l > 0u && l < 20u);                                    /* one reading's spike is not a puff */
    assert(smk_vel_lift(14, PV_NORMAL) == 14u && smk_vel_lift(63, PV_NORMAL) == 63u);
    assert(smk_vel_lift(14, PV_LIGHT) == 42u && smk_vel_lift(63, PV_LIGHT) == 89u);
    assert(smk_vel_lift(14, PV_XLIGHT) >= 66u && smk_vel_lift(14, PV_XLIGHT) <= 70u);
    assert(smk_vel_lift(63, PV_XLIGHT) >= 98u && smk_vel_lift(63, PV_XLIGHT) <= 102u);
    assert(smk_vel_lift(1, PV_FIXED) == 100u && smk_vel_lift(127, PV_FIXED) == 100u);
    last = 0;
    for (i = 1; i <= 127u; i++) {                                 /* a harder hit is never quieter, and tops out at 127 */
        v = smk_vel_lift(i, PV_LIGHT);
        assert(v >= last && v >= i && v <= 127u);
        last = v;
        v = smk_vel_lift(i, PV_XLIGHT);
        assert(v >= 40u && v <= 127u);
    }
    assert(smk_vel_lift(127, PV_LIGHT) == 127u && smk_vel_lift(127, PV_XLIGHT) == 127u);
    /* FLOOR (the SMK-37's default): a gentle press (read 1..40) plays from 80, a hit still 127, never quieter for more */
    assert(smk_vel_lift(1, PV_FLOOR) >= 80u && smk_vel_lift(1, PV_FLOOR) <= 86u);
    assert(smk_vel_lift(30, PV_FLOOR) >= 98u && smk_vel_lift(30, PV_FLOOR) <= 106u);
    assert(smk_vel_lift(127, PV_FLOOR) == 127u);
    for (last = 0, i = 1; i <= 127u; i++) {
        v = smk_vel_lift(i, PV_FLOOR);
        assert(v >= last && v >= 80u && v <= 127u);
        last = v;
    }
    puts("press: velocity lift");
    {   /* the squeeze (v3): a settled press gives nothing, a squeeze past it eases in, each pad reaches 127 at its own top,
         * a firm landing near the top still leaves room, a held hand's wobble stays calm */
        smk_rel_t s;
        smk_top_t tp = {0, 0};
        const pat_cfg_t pc = PAT_CFG_DEFAULTS;
        uint32_t t, a = 0, mx = 0, a1 = 0, a2 = 0;
        smk_rel_start(&s, 1000);                                  /* a press settling at 650 on a fresh pad */
        for (t = 0; t <= 600u; t += 5u) {
            a = smk_rel_press(&s, &tp, &pc, t < 30u ? 600u : 650u, 1000 + t);
            if (a > mx) mx = a;
        }
        printf("press: a press held at 650: %u at most (zero %u, top %u)\n", mx, s.base, tp.top);
        assert(mx == 0u && s.set && s.base == 650u);
        for (t = 600; t <= 700u; t += 5u)                         /* eased in: a little at first ... */
            a1 = smk_rel_press(&s, &tp, &pc, 730u, 1000 + t);
        for (t = 700; t <= 800u; t += 5u)                         /* ... most at the top (the default 800) */
            a2 = smk_rel_press(&s, &tp, &pc, 800u, 1000 + t);
        printf("press: squeezed to 730: %u, to 800: %u\n", a1, a2);
        assert(a1 > 0u && a1 <= 20u && a2 == 127u);
        smk_rel_start(&s, 3000);                                  /* a stiff pad: learns a top of 890 from a held squeeze */
        for (t = 0; t <= 1500u; t += 5u)
            a = smk_rel_press(&s, &tp, &pc, t < 300u ? 700u : 890u, 3000 + t);
        printf("press: a pad held at 890: top %u, %u\n", tp.top, a);
        assert(tp.top >= 880u && tp.top <= 890u && a >= 120u);
        smk_rel_start(&s, 5000);                                  /* a firm landing near that top: room to squeeze */
        for (mx = 0, t = 0; t <= 500u; t += 5u) {
            a = smk_rel_press(&s, &tp, &pc, 870u, 5000 + t);
            if (a > mx) mx = a;
        }
        printf("press: landing at 870 under a top of %u: zero %u, %u held\n", tp.top, s.base, mx);
        for (t = 500; t <= 700u; t += 5u)
            a = smk_rel_press(&s, &tp, &pc, 890u, 5000 + t);
        printf("press: then squeezed to 890: %u\n", a);
        assert(a >= 120u);
        smk_rel_start(&s, 9000);                                  /* a held hand's wobble (720..820 at 2.5 Hz) on a 770 zero */
        for (t = 0; t <= 300u; t += 5u)
            (void)smk_rel_press(&s, &tp, &pc, 770u, 9000 + t);
        for (mx = 0, t = 300; t <= 2300u; t += 5u) {
            uint32_t ph = t % 400u, h = 720u + (ph < 200u ? ph : 400u - ph) / 2u;
            a = smk_rel_press(&s, &tp, &pc, h, 9000 + t);
            if (a > mx) mx = a;
        }
        printf("press: a hand wobbling 720..820 on a 770 zero: %u at most\n", mx);
        assert(mx <= 16u);
        for (t = 0; t < 600000u; t += 100u)                       /* not pressed for a while: back to the default top */
            (void)smk_top_of(&tp, &pc, 0, 0, 20000u + t);
        assert(tp.top == 800u);
        assert(smk_rel_press(&s, &tp, &pc, 0xFFFFFu, 700000u) == 127u);   /* no overflow */
        for (l = 0, i = 0; i < 12u; i++)                          /* the slower rise: 60 ms about half way */
            l = smk_press_glide(l, 127);
        assert(l > 40u && l < 80u);
        puts("press: squeeze from the settled press, each pad's own top, eased in PASS");
    }
    {   /* KEY AT (smk_kat): a hit lifts to HIT x velocity, eased in (no jump), settles to HOLD %, holding swells to CEIL;
         * a harder hit is never less; the defaults 50 70 40 90, 120 400 3000 ms */
        uint32_t t, a, last, mx = 0, step = 0;
        assert(smk_kat(50, 70, 40, 90, 120, 400, 3000, 0, SH_EASE, 127, 0) == 0u);           /* nothing at the note-on */
        for (last = 0, t = 0; t <= 6000u; t += 5u) {                           /* a full hit, held 6 s */
            a = smk_kat(50, 70, 40, 90, 120, 400, 3000, 0, SH_EASE, 127, t);
            if (t <= 120u && a > mx) mx = a;
            step = a > last ? (a - last > step ? a - last : step) : (last - a > step ? last - a : step);
            last = a;
        }
        printf("key at: a full hit: %u at 120 ms, %u at 520 ms, %u at 3 s; the biggest 5 ms step %u\n",
               smk_kat(50, 70, 40, 90, 120, 400, 3000, 0, SH_EASE, 127, 120), smk_kat(50, 70, 40, 90, 120, 400, 3000, 0, SH_EASE, 127, 520),
               smk_kat(50, 70, 40, 90, 120, 400, 3000, 0, SH_EASE, 127, 3000), step);
        assert(mx == 50u && step <= 8u);                                       /* the lift arrives eased, no jump */
        assert(smk_kat(50, 70, 40, 90, 120, 400, 3000, 0, SH_EASE, 127, 3000) == 90u);     /* swelled to CEIL */
        assert(smk_kat(50, 70, 40, 90, 120, 400, 3000, 0, SH_EASE, 30, 120) < 50u);         /* a soft hit lifts less ... */
        assert(smk_kat(50, 70, 40, 90, 120, 400, 3000, 0, SH_EASE, 30, 3000) < 90u);       /* ... and swells slower */
        for (t = 0; t <= 4000u; t += 50u)
            for (a = 1; a < 127u; a++)                                         /* a harder hit is never less */
                assert(smk_kat(50, 70, 40, 90, 120, 400, 3000, 0, SH_EASE, a + 1u, t) + 1u >= smk_kat(50, 70, 40, 90, 120, 400, 3000, 0, SH_EASE, a, t));   /* (1: rounding) */
        assert(smk_kat(50, 0, 40, 90, 40, 400, 0, 0, SH_EASE, 10, 40) == 50u);           /* VEL 0: every hit the same; SWELL 0 off */
        assert(smk_kat(50, 0, 40, 90, 40, 400, 0, 0, SH_EASE, 10, 60000u) == 20u);       /* settled to HOLD 40 % */
        assert(smk_kat(127, 100, 100, 127, 0, 10, 0, 0, SH_EASE, 127, 0) == 127u);       /* RISE 0: at once (no divide by 0) */
        assert(smk_kat(127, 100, 100, 127, 1000, 3000, 10000, 0, SH_EASE, 127, 0xFFFFFFFFu) <= 127u);
        puts("key at: lift, settle, swell, eased PASS");
    }
    {   /* (PAD AT, KEY AT 3) the settings: the defaults as tuned, out-of-range values back to them, each one heard */
        pat_cfg_t c = PAT_CFG_DEFAULTS, d = PAT_CFG_DEFAULTS;
        smk_rel_t s;
        smk_top_t tp = {0, 0};
        uint32_t t, a = 0;
        assert(c.dead == 60u && c.room == 80u && c.top == 800u && c.settle == 250u && c.rise == 120u && c.fall == 120u &&
               c.shape == SH_SLOW && c.kshape == SH_EASE && c.kdelay == 0u);
        c.dead = 200; c.room = 5; c.top = 5000; c.settle = 1; c.rise = 0; c.fall = 9999; c.shape = 9; c.kshape = 9; c.kdelay = 60000u;
        pat_cfg_fix(&c);
        assert(!memcmp(&c, &d, sizeof c));
        for (t = 0; t <= 1024u; t += 64u)                         /* the four curves: from 0 to 1, never back */
            assert(smk_shape(t, SH_LIN) == t && smk_shape(t, SH_SLOW) <= t && smk_shape(t, SH_FAST) >= t);
        assert(smk_shape(1024, SH_EASE) == 1024u && smk_shape(0, SH_FAST) == 0u);
        c.dead = 0;                                               /* DEAD 0: the first count past the zero counts */
        smk_rel_start(&s, 0);
        for (t = 0; t <= 400u; t += 5u)
            (void)smk_rel_press(&s, &tp, &c, 650u, t);
        a = smk_rel_press(&s, &tp, &c, 700u, 405u);
        assert(a > 0u);
        c = d;
        c.shape = SH_LIN;                                         /* LIN gives more halfway than SLOW */
        tp.top = 0;
        smk_rel_start(&s, 0);
        for (t = 0; t <= 400u; t += 5u)
            (void)smk_rel_press(&s, &tp, &c, 650u, t);
        a = smk_rel_press(&s, &tp, &c, 744u, 405u);
        c.shape = SH_SLOW;
        assert(a > smk_rel_press(&s, &tp, &c, 744u, 410u));
        assert(smk_kat(50, 70, 40, 90, 120, 400, 3000, 1000, SH_EASE, 127, 900) == 20u);   /* DELAY: no swell yet */
        assert(smk_kat(50, 70, 40, 90, 120, 400, 3000, 1000, SH_EASE, 127, 4000) == 90u);  /* then it comes */
        assert(smk_press_glide2(0, 127, 20u, 120u) > smk_press_glide(0, 127));             /* RISE 20 ms: faster */
        puts("press: PAD AT / KEY AT 3 settings, defaults as tuned PASS");
    }
    puts("press: the measured range, 127 at the top, no numbing, glide PASS");
    return 0;
}
