/* SPDX-License-Identifier: GPL-3.0-only */
/* Stallion: the LEVER and the RIBBON's control-rate half (firmware/src/touch_ctl.c, specs/touch R17, R18, R21-R23, R25,
 * R34), a control block (32 samples) at a time:
 *   R17  the S-curve: full in LEVER TIME and half of it at half the time (within 5 %, 10 .. 1000 ms), back in 4/3 of it;
 *        a press shorter than LEVER TIME still reaches full first;
 *   R18  both buttons: the most recent's way; let go, the other's;
 *   R21  the ribbon: 1 / RIB STEPS a detent, clockwise up, within -1..+1;
 *   R22  RIB SPRING: RIB DELAY after the last detent, back with a time constant of RIB RETURN / 3 (within 10 %), to 0
 *        exactly; at once with no note key down;
 *   R23  HOLD: stays; a detent across 0 stops at 0;
 *   R25  a detent's change as a ramp of 10 .. 30 ms, no step over a quarter of it;
 *   R34  MIDI out: pitch bend 8191 / -8192 at full, CC 16 64 at rest, 0 and 127 at the ends; one message of a kind
 *        every 10 ms at most, on change only; nothing touched, nothing sent and both 0 (R29);
 *   LEVER's modes (touch.h LV_*; the checks above are SPRNG's, lv_mode 0): HOLD follows the button held and stays where
 *        let go, the other button through 0 to its end; LATCH (the default) stays where let go, the same button again
 *        further, the other button's press back to 0 at the return's speed and stopped there while held, a fresh press
 *        the other way; both keep their place with the keys let go and on MIDI out, until a project load;
 *   RIB SPRING LATCH (rib_latch, touch_ext rib_sprng 0: the default): stays where turned, the keys let go too; a detent
 *        back toward 0 springs it there (RIB RETURN), the rest of that turn let go by; a fresh turn after a pause moves
 *        it again; the way it was bent, during the return: the return stops, on from there; HOLD as before. */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/touch.h"
#include "../firmware/src/touch_ctl.c"

#define N 32u
static int bad;
static void check(const char *what, int ok)
{
    printf("touch_ctl: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static double ms_of(uint32_t blocks) { return blocks * (double)N * 1000.0 / 44100.0; }
static tc_t c;
static touch_cfg_t cfg;
static void fresh(void)
{
    memset(&c, 0, sizeof c);
    tc_init(&c);
    cfg = (touch_cfg_t)TOUCH_CFG_DEFAULTS;
}
/* an OCT press the lever takes, as ui_input.c lever_take: lv_want, then the press count with its way */
static void oct_press(int w)
{
    c.lv_want = (int8_t)w;
    c.lv_press = (uint8_t)(((c.lv_press + 2u) & ~1u) | (w > 0));
}
/* MIDI out, as seq.c touch_block takes it */
static int32_t pb_last;
static uint32_t cc_last, n_pb, n_cc, gap_bad, blk, pb_blk, cc_blk;
static void tick(int notes)
{
    int32_t pb = 0;
    uint32_t cc = 0, o;
    tc_tick(&c, &cfg, N, notes);
    o = tc_midi(&c, N, &pb, &cc);
    blk++;
    if (o & 1u) {
        if (n_pb && (blk - pb_blk) * N < TC_MIDI_GAP)
            gap_bad++;
        pb_blk = blk, pb_last = pb, n_pb++;
    }
    if (o & 2u) {
        if (n_cc && (blk - cc_blk) * N < TC_MIDI_GAP)
            gap_bad++;
        cc_blk = blk, cc_last = cc, n_cc++;
    }
}
static int near(double got, double want, double tol) { return got >= want * (1 - tol) && got <= want * (1 + tol); }

int main(void)
{
    uint32_t i, b, ok;
    /* --- R17 the S-curve, several LEVER TIMEs */
    {
        static const uint16_t LT[] = {10, 50, 120, 400, 1000};
        uint32_t k;
        ok = 1;
        for (k = 0; k < 5u; k++) {
            uint32_t half = 0, full = 0, back = 0;
            fresh();
            cfg.lever_time = LT[k];
            c.lv_want = 1;
            for (b = 1; b < 100000u && !full; b++) {
                tick(1);
                if (!half && c.lever >= 16384)
                    half = b;
                if (c.lever == 32767)
                    full = b;
            }
            c.lv_want = 0;
            for (b = 1; b < 100000u && !back; b++) {
                tick(1);
                if (c.lever == 0)
                    back = b;
            }
            /* (a block is 0.73 ms: 10 ms is 14 blocks, so its 5 % is a block) */
            ok &= (near(ms_of(full), LT[k], 0.05) || ms_of(full) - LT[k] < 0.73) &&
                  (near(ms_of(half), LT[k] / 2.0, 0.05) || ms_of(half) - LT[k] / 2.0 < 0.73) &&
                  (near(ms_of(back), LT[k] * 4.0 / 3.0, 0.05) || ms_of(back) - LT[k] * 4.0 / 3.0 < 0.73);
            if (LT[k] == 120)
                printf("touch_ctl: LEVER TIME 120 ms: half %.1f ms, full %.1f ms, back %.1f ms\n", ms_of(half), ms_of(full),
                       ms_of(back));
        }
        check("R17: full in LEVER TIME, half at half the time, back in 4/3 of it (10, 50, 120, 400, 1000 ms; 5 %)", ok);
        fresh();
        c.lv_want = 1;
        ok = 1;
        for (i = 0, b = 0; b < 400u; b++) {             /* (S: slow, fast, slow) */
            int32_t was = c.lever;
            tick(1);
            if (c.lever < was)
                ok = 0;
        }
        check("     the travel monotonic, held at full while held", ok && c.lever == 32767);
        fresh();
        c.lv_want = -1;
        for (b = 0; b < 400u; b++)
            tick(1);
        check("     OCT-: down to -32768", c.lever == -32768);
    }
    /* --- R17 a short press */
    {
        uint32_t full = 0, back = 0;
        fresh();
        c.lv_want = 1;
        tick(1);
        tick(1);
        c.lv_want = 0;                                  /* (1.5 ms) */
        for (b = 3; b < 2000u && !back; b++) {
            tick(1);
            if (c.lever == 32767 && !full)
                full = b;
            if (full && c.lever == 0)
                back = b;
        }
        check("R17: a 1.5 ms press still reaches full (in LEVER TIME), then returns", full && near(ms_of(full), 120, 0.05) &&
              back && near(ms_of(back - full), 160, 0.05));
    }
    /* --- R18 both buttons */
    {
        fresh();
        c.lv_want = 1;
        for (b = 0; b < 300u; b++)
            tick(1);
        ok = c.lever == 32767;
        c.lv_want = -1;                                 /* OCT- pressed too: the most recent */
        for (b = 0; b < 600u; b++)
            tick(1);
        ok &= c.lever == -32768;
        c.lv_want = 1;                                  /* OCT- let go: OCT+ still held has it */
        for (b = 0; b < 600u; b++)
            tick(1);
        check("R18: both held: the most recent's way; let go, the other's", ok && c.lever == 32767);
    }
    /* --- R21 the ribbon's steps, R23 HOLD */
    {
        fresh();
        cfg.rib_spring = 0;
        c.det_n += 1;
        for (b = 0; b < 100u; b++)
            tick(1);
        ok = c.ribbon == 1365;
        c.det_n += 23;
        for (b = 0; b < 100u; b++)
            tick(1);
        ok &= c.ribbon == 32767;
        c.det_n += 10;
        for (b = 0; b < 100u; b++)
            tick(1);
        ok &= c.ribbon == 32767;
        check("R21: a detent 1 / RIB STEPS (24), clockwise up, 24 to the end, within +1", ok);
        c.det_n -= 24;
        for (b = 0; b < 100u; b++)
            tick(1);
        check("R23: HOLD: 24 back: 0 exactly; it stays (5 s)", c.ribbon == 0 && c.rib_t == 0);
        c.det_n += 2;
        for (b = 0; b < 100u; b++)
            tick(1);
        c.det_n -= 5;                                   /* (across 0 in one pass) */
        for (b = 0; b < 100u; b++)
            tick(1);
        ok = c.ribbon == 0;
        for (b = 0; b < 7000u; b++)
            tick(0);
        ok &= c.ribbon == 0;
        c.det_n -= 3;
        for (b = 0; b < 100u; b++)
            tick(0);
        check("     a detent across 0 stops at 0; from 0 on down", ok && c.ribbon == -4096);
        c.det_n += 4;                                   /* (-1: the turn up, a detent every 20 ms, meets 0 and stops) */
        for (i = 0; i < 6u; i++)
            for (b = 0, c.det_n += 1; b < 28u; b++)
                tick(0);
        ok = c.ribbon == 0;
        for (b = 0; b < 400u; b++)
            tick(0);
        c.det_n += 1;                                   /* (paused 0.3 s: on up) */
        for (b = 0; b < 100u; b++)
            tick(0);
        check("     a turn meeting 0 stays at 0 while it goes on; paused, it goes on past", ok && c.ribbon == 1365);
        c.det_n -= 101;                                 /* (a fast turn down: stops at 0) */
        for (b = 0; b < 400u; b++)
            tick(0);
        ok = c.ribbon == 0;
        c.det_n -= 100;
        for (b = 0; b < 100u; b++)
            tick(0);
        ok &= c.ribbon == -32768;
        for (b = 0; b < 7000u; b++)
            tick(0);
        check("     within -1; HOLD keeps it there with no key (5 s)", ok && c.ribbon == -32768);
        fresh();
        cfg.rib_spring = 0;
        cfg.rib_steps = 6;
        c.det_n += 3;
        for (b = 0; b < 100u; b++)
            tick(1);
        ok = c.ribbon == 16384;
        fresh();
        cfg.rib_spring = 0;
        cfg.rib_steps = 96;
        c.det_n += 96;
        for (b = 0; b < 100u; b++)
            tick(1);
        check("     RIB STEPS 6: 3 detents half way; 96: 96 to the end", ok && c.ribbon == 32767);
    }
    /* --- R25 the ramp */
    {
        int32_t from, to, worst = 0;
        uint32_t len = 0;
        fresh();
        cfg.rib_spring = 0;
        c.det_n += 1;
        for (b = 0; b < 100u; b++)
            tick(1);
        from = c.ribbon;
        c.det_n += 1;
        for (b = 0; b < 100u; b++) {
            int32_t was = c.ribbon, d;
            tick(1);
            d = c.ribbon - was;
            if (d > worst)
                worst = d;
            if (d)
                len = b + 1u;
        }
        to = c.ribbon;
        check("R25: a detent's change as a ramp of 10 .. 30 ms, no block's step over a quarter of it",
              (to - from == 1365 || to - from == 1366) && ms_of(len) >= 10.0 && ms_of(len) <= 30.0 && worst * 4 <= to - from);
        printf("touch_ctl: a detent: %d over %.1f ms, the largest step %d\n", to - from, ms_of(len), worst);
    }
    /* --- R22 the spring */
    {
        uint32_t t0 = 0, t37 = 0, zero = 0;
        int32_t top;
        fresh();                                        /* (150 ms, 400 ms: a time constant of 133 ms) */
        c.det_n += 12;
        for (b = 1; b < 20000u && !zero; b++) {
            int32_t was = c.ribbon;
            tick(1);
            if (!t0 && c.ribbon < was && b > 40u)
                t0 = b;                                 /* (the return begins) */
            if (b == 40u)
                top = c.ribbon;
            if (t0 && !t37 && c.ribbon <= top * 0.3679)
                t37 = b;
            if (c.ribbon == 0)
                zero = b;
        }
        printf("touch_ctl: SPRING: the return from %.1f ms, 1 / e after %.1f ms more, 0 at %.1f ms\n", ms_of(t0 - 1),
               ms_of(t37 - t0 + 1), ms_of(zero));
        check("R22: RIB SPRING: held RIB DELAY (150 ms) after the last detent, then back (time constant 133 ms, 10 %)",
              top == 16384 && near(ms_of(t0 - 1), 150, 0.05) && near(ms_of(t37 - t0 + 1), 400.0 / 3.0, 0.10));
        for (b = 0, ok = 1; b < 2000u; b++)
            tick(1), ok &= c.ribbon == 0;
        check("     to 0 exactly, and stays there", zero && ok && c.rib_t == 0 && c.rib_o == 0);
        fresh();
        c.det_n += 12;
        for (b = 0; b < 30u; b++)
            tick(1);
        top = c.ribbon;
        for (b = 0, zero = 0; b < 20000u && !zero; b++) {
            tick(0);                                    /* (the last note key let go) */
            if (b == 2u)
                ok = c.ribbon < top || c.rib_t < 16384 * 512;
            if (c.ribbon == 0)
                zero = b;
        }
        check("     at once when the last note key is let go", ok && zero);
        fresh();
        cfg.rib_delay = 0;
        cfg.rib_return = 5000;
        c.det_n -= 24;
        tick(1);
        for (b = 1; b < 100000u && c.ribbon; b++)
            tick(1);
        printf("touch_ctl: RIB RETURN 5 s: 0 after %.0f ms\n", ms_of(b));
        check("     RIB DELAY 0, RIB RETURN 5 s: from -1, back to 0 exactly, in under 3 x RIB RETURN",
              c.ribbon == 0 && ms_of(b) < 15000.0 && ms_of(b) > 5000.0);
    }
    /* --- R34 MIDI out */
    {
        fresh();
        n_pb = n_cc = gap_bad = 0;
        for (b = 0; b < 2000u; b++)
            tick(1);
        check("R34 / R29: nothing touched: lever and ribbon 0, nothing sent", c.lever == 0 && c.ribbon == 0 && !n_pb && !n_cc);
        c.lv_want = 1;
        for (b = 0; b < 300u; b++)
            tick(1);
        ok = pb_last == 8191 && n_pb >= 5u && n_pb <= 15u;   /* (120 ms: 12 at most) */
        c.lv_want = -1;
        for (b = 0; b < 600u; b++)
            tick(1);
        ok &= pb_last == -8192;
        c.lv_want = 0;
        for (b = 0; b < 600u; b++)
            tick(1);
        check("R34: the lever as pitch bend: 8191 at full up, -8192 down, 0 at rest", ok && pb_last == 0 && c.lever == 0);
        c.det_n += 24;
        for (b = 0; b < 60u; b++)
            tick(1);
        ok = cc_last == 127;
        c.det_n -= 48;
        for (b = 0; b < 60u; b++)
            tick(1);
        ok &= cc_last == 0;
        for (b = 0; b < 8000u; b++)
            tick(0);
        check("     the ribbon as CC 16: 127, 0 at the ends, 64 at rest", ok && cc_last == 64 && c.ribbon == 0);
        check("     one message of a kind every 10 ms at most", !gap_bad);
        i = n_pb + n_cc;
        for (b = 0; b < 2000u; b++)
            tick(1);
        check("     on change only: at rest, nothing more", n_pb + n_cc == i);
    }
    /* --- LEVER HOLD */
    {
        int32_t a, b2;
        fresh();
        c.lv_mode = LV_HOLD;
        oct_press(1);
        for (b = 0; b < 60u; b++)
            tick(1);
        a = c.lever;
        c.lv_want = 0;                                  /* (let go part way) */
        for (b = 0, ok = 1; b < 7000u; b++)
            tick(b < 3500u), ok &= c.lever == a;      /* (5 s, the keys let go half way) */
        check("HOLD: OCT+ held part way (60 blocks), let go: it stays there, the keys let go too (5 s)",
              a > 3000 && a < 32767 && ok);
        oct_press(1);
        for (b = 0, ok = 1; b < 300u; b++) {
            int32_t was = c.lever;
            tick(0);
            ok &= c.lever >= was;
        }
        c.lv_want = 0;
        for (b = 0; b < 2000u; b++)
            tick(0);
        check("     OCT+ again: on up to full (held, no key), let go: stays at full", ok && c.lever == 32767);
        oct_press(-1);
        for (b = 0, b2 = 0, i = 0; b < 600u; b++) {
            tick(1);
            if (!b2 && c.lever == 0)
                b2 = b + 1u;                            /* (it passes 0) */
            if (!i && c.lever == -32768)
                i = b + 1u;
        }
        printf("touch_ctl: HOLD: OCT- from full up: 0 after %.1f ms, full down after %.1f ms\n", ms_of(b2), ms_of(i));
        check("     OCT- held from full up: through 0 (in LEVER TIME) to full down (in 2 x), and stays held",
              b2 && near(ms_of(b2), 120, 0.05) && i && near(ms_of(i), 240, 0.05) && c.lever == -32768);
        for (b = 0; b < 60u; b++)
            tick(1);
        oct_press(1);
        for (b = 0; b < 40u; b++)
            tick(1);
        a = c.lever;
        c.lv_want = 0;
        for (b = 0, ok = 1; b < 3000u; b++)
            tick(0), ok &= c.lever == a;
        check("     OCT+ a moment from full down: part way back, stays (never springs)", a > -32768 && a < 0 && ok);
        n_pb = 0;
        for (b = 0; b < 100u; b++)
            tick(0);
        check("     MIDI out: the bend where it stays (sent, then nothing more)", pb_last == (a >= 0 ? (a * 8191 + 16383) / 32767 : a / 4) &&
              n_pb == 0u);
    }
    /* --- LEVER LATCH */
    {
        int32_t a, a2;
        uint32_t t0;
        fresh();
        c.lv_mode = LV_LATCH;
        oct_press(-1);
        tick(1);
        tick(1);
        c.lv_want = 0;                                  /* (1.5 ms: as SPRNG's short press) */
        for (b = 0; b < 400u; b++)
            tick(1);
        a = c.lever;
        check("LATCH: a 1.5 ms OCT- press: it stops where let go (2 blocks' travel, not full), stays",
              a < 0 && a > -100);
        oct_press(-1);
        for (b = 0; b < 80u; b++)
            tick(1);
        a = c.lever;
        c.lv_want = 0;
        for (b = 0, ok = 1; b < 7000u; b++)
            tick(b < 100u), ok &= c.lever == a;      /* (the keys let go: it stays) */
        check("     OCT- held 80 blocks: part way down; let go: it stays (5 s, the keys let go)", a < -8000 && a > -32768 && ok);
        oct_press(-1);
        for (b = 0; b < 30u; b++)
            tick(0);
        c.lv_want = 0;
        tick(0);
        a2 = c.lever;
        for (b = 0, ok = 1; b < 1000u; b++)
            tick(0), ok &= c.lever == a2;
        check("     OCT- again: further down from there, then stays", a2 < a && ok);
        oct_press(-1);
        for (b = 0; b < 300u; b++)
            tick(1);
        c.lv_want = 0;
        for (b = 0; b < 300u; b++)
            tick(1);
        check("     held on: full down, stays", c.lever == -32768);
        oct_press(1);                                   /* (the other button, held on) */
        for (b = 0, t0 = 0, ok = 1; b < 3000u; b++) {
            int32_t was = c.lever;
            tick(1);
            ok &= c.lever >= was && c.lever <= 0;
            if (!t0 && c.lever == 0)
                t0 = b + 1u;
        }
        printf("touch_ctl: LATCH: OCT+ from full down: 0 after %.1f ms, then held there\n", ms_of(t0));
        check("     OCT+ pressed (held on): back to 0 in 4/3 of LEVER TIME, and it stops there while held (2 s)",
              ok && t0 && near(ms_of(t0), 160, 0.05) && c.lever == 0 && c.lv_pos == 0);
        c.lv_want = 0;
        tick(1);
        oct_press(1);                                   /* (a fresh press: up) */
        for (b = 0; b < 50u; b++)
            tick(1);
        c.lv_want = 0;
        a = c.lever;
        for (b = 0, ok = 1; b < 1000u; b++)
            tick(1), ok &= c.lever == a;
        check("     a fresh OCT+ press: up from 0, stays where let go", a > 0 && a < 32767 && ok);
        oct_press(-1);                                  /* (a short OCT- press: back to 0 all the same) */
        tick(1);
        c.lv_want = 0;
        for (b = 0, ok = 1; b < 1000u; b++)
            tick(1);
        check("     a short OCT- press: back to 0 all the way, stops there", c.lever == 0 && c.lv_pos == 0);
        /* both held: OCT+ bending up, OCT- pressed too: back to 0; OCT- let go (OCT+ held on: no fresh press): stays 0 */
        oct_press(1);
        for (b = 0; b < 60u; b++)
            tick(1);
        oct_press(-1);
        for (b = 0; b < 400u; b++)
            tick(1);
        ok = c.lever == 0;
        c.lv_want = 1;                                  /* (lever_follow: OCT- let go, OCT+ still held) */
        for (b = 0; b < 400u; b++)
            tick(1);
        check("     both: OCT+ held, OCT- pressed: back to 0; OCT- let go with OCT+ held: stays at 0", ok && c.lever == 0);
        c.lv_want = 0;
        tick(1);
        /* a press of the bent side's button during the return: on that way again */
        oct_press(1);
        for (b = 0; b < 200u; b++)
            tick(1);
        c.lv_want = 0;
        oct_press(-1);
        for (b = 0; b < 40u; b++)
            tick(1);
        c.lv_want = 0;
        a = c.lever;
        oct_press(1);
        for (b = 0; b < 300u; b++)
            tick(1);
        c.lv_want = 0;
        check("     a press of OCT+ during OCT-'s return: up again, to full", a > 0 && a < 32767 && c.lever == 32767);
        n_pb = 0;
        for (b = 0; b < 2000u; b++)
            tick(0);
        check("     MIDI out: 8191 held there, nothing more sent", pb_last == 8191 && n_pb == 0u && c.lever == 32767);
        /* LEVER OFF (seq.c: SPRNG) with the lever latched: it goes back */
        c.lv_mode = LV_SPRNG;
        for (b = 0; b < 400u; b++)
            tick(0);
        check("     LEVER OFF (or SPRNG) with it latched up: back to rest", c.lever == 0 && pb_last == 0);
        /* a project load: back to 0 at once, the mode kept */
        c.lv_mode = LV_LATCH;
        oct_press(-1);
        for (b = 0; b < 100u; b++)
            tick(1);
        c.lv_want = 0;
        tick(1);
        ok = c.lever < 0;
        c.reset_req = 1;
        tick(1);
        ok &= c.lever == 0 && c.lv_mode == LV_LATCH;
        for (b = 0; b < 400u; b++)
            tick(1);
        check("     a project load: from part way down to 0 at once, stays; the mode kept", ok && c.lever == 0);
        /* SPRNG: the presses counted, nothing changes (its checks above ran with lv_press 0) */
        fresh();
        oct_press(1);
        tick(1);
        tick(1);
        c.lv_want = 0;
        for (b = 3, i = 0; b < 2000u && !(i && c.lever == 0); b++) {
            tick(1);
            if (c.lever == 32767)
                i = 1;
        }
        check("SPRNG with the presses counted: a short press still reaches full, then back to 0", i && c.lever == 0);
    }
    /* --- RIBBON LATCH (RIB SPRING ON, touch_ext rib_sprng 0: the default) */
    {
        int32_t a;
        uint32_t t0;
        fresh();
        c.rib_latch = 1;
        for (i = 0; i < 6u; i++)                        /* (a turn down: a detent every 20 ms) */
            for (b = 0, c.det_n -= 1; b < 28u; b++)
                tick(1);
        a = c.ribbon;
        for (b = 0, ok = 1; b < 7000u; b++)
            tick(b < 100u), ok &= c.ribbon == a;        /* (past RIB DELAY, the keys let go: it stays) */
        check("RIBBON LATCH: 6 detents down: -6 / 24; it stays (5 s, past RIB DELAY, the keys let go)", a == -8192 && ok);
        for (i = 0; i < 3u; i++)                        /* (a fresh turn down, after the pause: further) */
            for (b = 0, c.det_n -= 1; b < 28u; b++)
                tick(1);
        for (b = 0; b < 400u; b++)
            tick(1);
        check("     a fresh turn down: further down from there, stays", c.ribbon == -12288);
        c.det_n += 1;                                   /* (one detent back up: the nudge) */
        for (b = 0, t0 = 0, ok = 1; b < 3000u; b++) {
            int32_t was = c.ribbon;
            if (b < 140u && b % 28u == 27u)
                c.det_n += 1;                           /* (the turn goes on up: let go by) */
            tick(1);
            ok &= c.ribbon >= was && c.ribbon <= 0;
            if (!t0 && c.ribbon == 0)
                t0 = b + 1u;
        }
        printf("touch_ctl: RIBBON LATCH: a detent back from -9 / 24: 0 after %.1f ms\n", ms_of(t0));
        check("     a detent back up: springs to 0 (RIB RETURN), the rest of that turn let go by: it stops at 0",
              ok && t0 && ms_of(t0) < 1000.0 && c.ribbon == 0 && c.rib_t == 0);
        for (i = 0; i < 3u; i++)                        /* (a fresh turn up, after the pause) */
            for (b = 0, c.det_n += 1; b < 28u; b++)
                tick(1);
        for (b = 0; b < 400u; b++)
            tick(1);
        check("     a fresh turn up: up from 0, stays", c.ribbon == 4096);
        c.det_n -= 1;                                   /* (back down: springs to 0, even with no key down) */
        for (b = 0; b < 3000u; b++)
            tick(0);
        check("     a detent back down: to 0, stays", c.ribbon == 0);
        c.det_n -= 3;                                   /* (a fresh turn down, past the pause) */
        for (b = 0; b < 400u; b++)
            tick(1);
        ok = c.ribbon == -4096;
        c.det_n += 1;                                   /* (back up, then the same way down again after a pause: */
        for (b = 0; b < 3u; b++)                        /*  the return stops, on down from where it is) */
            tick(1);
        for (b = 0; b < 300u; b++)
            tick(1);
        a = c.ribbon;
        c.det_n -= 1;
        for (b = 0; b < 400u; b++)
            tick(1);
        check("     the way it was bent again after a pause: the return stops, on from there",
              ok && a < 0 && a > -4096 && c.ribbon >= a - 1366 && c.ribbon <= a - 1364);
        fresh();
        c.rib_latch = 1;
        c.det_n -= 3;
        for (b = 0; b < 30u; b++)
            tick(1);
        c.det_n += 1;                                   /* (back toward 0 in the same block as the turn down) */
        for (b = 0; b < 3000u; b++)
            tick(1);
        check("     a turn down then back within a block: back to 0", c.ribbon == 0);
        fresh();                                        /* (RIB SPRING HOLD: rib_latch is not looked at) */
        c.rib_latch = 1;
        cfg.rib_spring = 0;
        c.det_n -= 3;
        for (b = 0; b < 400u; b++)
            tick(1);
        c.det_n += 1;
        for (b = 0; b < 400u; b++)
            tick(1);
        check("     HOLD as before with rib_latch set: a detent back moves it one step", c.ribbon == -2731);
    }
    /* --- a reset (project load) */
    fresh();
    c.lv_want = 1;
    c.det_n += 5;
    for (b = 0; b < 30u; b++)
        tick(1);
    c.lv_want = 0;
    c.reset_req = 1;
    tick(1);
    check("a project load: both at rest at once", c.lever == 0 && c.ribbon == 0 && !c.reset_req);

    printf(bad ? "TOUCH CTL TEST FAILED (%d)\n" : "touch_ctl test passed\n", bad);
    return bad ? 1 : 0;
}
