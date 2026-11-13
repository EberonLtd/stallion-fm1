/* SPDX-License-Identifier: GPL-3.0-only */
/* Stallion: the LEVER (OCT- / OCT+) and the RIBBON (SELECT) while a key plays a note (specs/touch R16-R25, R34), the
 * control-rate half: the UI (ui_input.c) says which OCT button the lever follows (lv_want) and counts SELECT's detents
 * (det_n); tc_tick, once an audio control block (seq.c touch_block, events_block), moves both and leaves them in lever /
 * ribbon (Q15 bipolar, 0 at rest: part_ctl's lever and ribbon), and tc_midi what goes out on MIDI. Integer only, our
 * own (from the behaviour the spec describes, no outside code).
 *   LEVER: a phase (Q24, signed) runs to an end in LEVER TIME and back to rest in 4/3 of it (3/4 of the speed); the
 *     lever is the phase through the S-curve 3u^2 - 2u^3 (half the travel at half the time, R17). The end it runs to
 *     is latched (lv_dir): a press shorter than LEVER TIME still reaches full before it returns; the most recent button
 *     sets it (the UI: both held, R18). That is LEVER SPRNG (lv_mode, seq.c: also with LEVER OFF, so a bent lever goes
 *     back); the others never return by themselves, the notes let go or not (only a project load / NEW resets it):
 *     HOLD: the button held (lv_want) moves it toward its end at the same speed, let go it stays; the other button
 *       moves it back, through 0 to its own end if held;
 *     LATCH (the default): a press (lv_press, the UI's count) of the button on the side the lever is at, or with it at
 *       rest, moves it toward that end while held, let go it stays; a press of the other button springs it back to 0
 *       at the return's speed (lv_back) and it stops there, that button held or not: a fresh press bends the other way.
 *   RIBBON: rib_t, where the detents put it (Q24, 1 / RIB STEPS a detent, within -1..+1; HOLD: one across 0 stops at
 *     0 and holds it there until the turn pauses TC_STOP_MS or turns back, R23), and rib_o, what the voices get: each detent's change as a ramp of TC_RAMP_MS (R25). RIB SPRING ON:
 *     RIB DELAY after the last detent, or with no note key down, rib_t falls back exponentially (time constant RIB
 *     RETURN / 3), the last 1/128 of full at the slope it has there, to 0 exactly (R22). RIB SPRING LATCH (rib_latch,
 *     seq.c: touch_ext's, the default): no return by itself, the notes let go or not; a detent away from 0 (or from 0)
 *     moves it, a detent back toward 0 springs it back to 0 (the same return) and the rest of that turn is let go by
 *     (rib_stop, as HOLD's): a fresh turn, after a pause of TC_STOP_MS, moves it again.
 * Nothing touched: lever and ribbon stay 0 and nothing is sent (R29). */
#pragma once
#include <stdint.h>

#define TC_FULL (1 << 24)               /* the phase's and the ribbon's full travel */
#define TC_RAMP_MS 20u                  /* a detent's ramp (R25: 10 .. 30 ms) */
#define TC_RATE 44100u
#define TC_STOP_MS 200u                 /* HOLD: a turn that reached 0 stays there until it pauses this long */
#define TC_MIDI_GAP (TC_RATE / 100u)    /* 10 ms between two messages of a kind (R34) */

typedef struct {
    /* the UI's (ui_input.c), read here */
    volatile int8_t lv_want;            /* -1 OCT-, +1 OCT+, 0 none: the button the lever follows */
    volatile int32_t det_n;             /* SELECT's detents the ribbon took, a running count */
    volatile uint8_t reset_req;         /* a project load / NEW: everything back to rest at once */
    volatile uint8_t lv_mode;           /* LV_SPRNG, LV_HOLD, LV_LATCH (seq.c: touch_ext's, SPRNG with LEVER OFF) */
    volatile uint8_t lv_press;          /* the OCT presses the lever took, a running count x 2, bit 0 the way (1 OCT+) */
    volatile uint8_t rib_latch;         /* RIB SPRING LATCH (seq.c: touch_ext's; 0: cfg rib_spring's HOLD or SPRNG) */
    /* the outputs, Q15 */
    volatile int16_t lever, ribbon;
    /* the state */
    int32_t lv_pos;                     /* the phase, -TC_FULL .. TC_FULL */
    int8_t lv_dir;                      /* the end it runs to (latched), 0: back to rest (HOLD, LATCH: stays) */
    uint8_t lv_pseen;                   /* lv_press as last taken */
    uint8_t lv_back;                    /* LATCH: springing back to 0 (the other button pressed) */
    int32_t det_seen;
    int32_t rib_t, rib_o, rib_d;        /* where the detents put it; the voices'; its step a block while ramping */
    uint8_t rib_left;                   /* the ramp's blocks left */
    uint8_t rib_ret;                    /* returning (RIB SPRING) */
    int8_t rib_stop;                    /* HOLD: the way a turn reached 0 (it stays there while that turn goes on) */
    uint32_t rib_idle;                  /* samples since the last detent */
    /* the steps, from the settings (recomputed when they change) */
    uint16_t c_lt, c_ret, c_n;
    int32_t lv_step;                    /* the phase a block, out */
    uint32_t rib_k;                     /* the return's one-pole step a block, Q16 */
    /* MIDI out (R34): what was sent last, samples since */
    int16_t pb_sent;                    /* -8192 .. 8191 */
    uint8_t cc_sent;
    uint32_t pb_age, cc_age;
} tc_t;

static void tc_init(tc_t *c)
{
    int32_t dn = c->det_n;
    uint8_t md = c->lv_mode, pr = c->lv_press, rl = c->rib_latch;
    uint32_t i;
    for (i = 0; i < sizeof *c; i++)
        ((uint8_t *)c)[i] = 0;
    c->det_n = c->det_seen = dn;
    c->lv_mode = md;                                    /* (the UI's, kept) */
    c->rib_latch = rl;
    c->lv_press = c->lv_pseen = pr;
    c->cc_sent = 64;
    c->pb_age = c->cc_age = TC_MIDI_GAP;
}

/* 3u^2 - 2u^3 of u (0 .. TC_FULL), Q15 (0 .. 32768) */
static int32_t tc_scurve(int32_t u)
{
    uint32_t x = (uint32_t)u >> 9, x2 = (x * x) >> 15;  /* (Q15) */
    return (int32_t)((x2 * (3u * 32768u - 2u * x)) >> 15);
}

static void tc_steps(tc_t *c, const touch_cfg_t *cfg, uint32_t n)
{
    uint32_t tau_us, dt_us;
    if (c->c_lt == cfg->lever_time && c->c_ret == cfg->rib_return && c->c_n == n)
        return;
    c->c_lt = cfg->lever_time;
    c->c_ret = cfg->rib_return;
    c->c_n = (uint16_t)n;
    {   /* TC_FULL n / (LEVER TIME in samples), 32-bit (no 64-bit divide on the target): 2^16 n 1000 / (ms 44100 / 2^8),
         * within 0.1 % (n <= 64) */
        uint32_t den = ((uint32_t)(cfg->lever_time ? cfg->lever_time : 1u) * TC_RATE + 128u) >> 8;
        c->lv_step = (int32_t)(((n * 1000u) << 16) / den);
    }
    if (c->lv_step < 1)
        c->lv_step = 1;
    dt_us = n * 1000000u / TC_RATE;                     /* (us; dt / (tau + dt / 2), as at_kq16) */
    tau_us = cfg->rib_return * 1000u / 3u;
    c->rib_k = (dt_us << 16) / (tau_us + dt_us / 2u);   /* (32-bit: dt < 65536 us) */
    if (c->rib_k > 65536u)
        c->rib_k = 65536u;
}

static int32_t tc_toward(int32_t p, int32_t to, int32_t st)   /* p a step st on toward to, to at most */
{
    return p < to ? (to - p > st ? p + st : to) : (p - to > st ? p - st : to);
}

static uint32_t tc_ramp_blocks(uint32_t n)
{
    uint32_t b = (TC_RAMP_MS * TC_RATE / 1000u + n / 2u) / (n ? n : 1u);
    return b < 1u ? 1u : b > 255u ? 255u : b;
}

/* a block of n samples on; notes: a key plays a note (the spring returns at once without one) */
static void tc_tick(tc_t *c, const touch_cfg_t *cfg, uint32_t n, int notes)
{
    int32_t d, want;
    if (c->reset_req) {
        int16_t pb = c->pb_sent;
        uint8_t cs = c->cc_sent;
        c->reset_req = 0;
        tc_init(c);
        c->pb_sent = pb, c->cc_sent = cs;               /* (what went out last: the rest goes out, the next block) */
    }
    tc_steps(c, cfg, n);
    /* LEVER */
    {
        uint8_t pr = c->lv_press;                       /* (before lv_want: the UI writes it after, a press seen
                                                         * comes with its button) */
        int32_t st = c->lv_step - c->lv_step / 4;       /* (the return: 4/3 of LEVER TIME) */
        int32_t p = pr != c->lv_pseen ? (pr & 1u ? 1 : -1) : 0;   /* (a press, its way) */
        c->lv_pseen = pr;
        want = c->lv_want;
        want = want > 0 ? 1 : want < 0 ? -1 : 0;
        if (st < 1)
            st = 1;
        if (c->lv_mode == LV_HOLD) {
            c->lv_back = 0;
            c->lv_dir = (int8_t)want;
            if (want)
                c->lv_pos = tc_toward(c->lv_pos, want * TC_FULL, c->lv_step);
        } else if (c->lv_mode == LV_LATCH) {
            if (p) {                                    /* (the other side's button: back to 0; else on its way) */
                c->lv_back = (uint8_t)((c->lv_pos > 0 && p < 0) || (c->lv_pos < 0 && p > 0));
                c->lv_dir = (int8_t)(c->lv_back ? 0 : p);
            }
            if (c->lv_dir != want)
                c->lv_dir = 0;                          /* (let go: it stays) */
            if (c->lv_back) {
                c->lv_pos = tc_toward(c->lv_pos, 0, st);
                c->lv_back = (uint8_t)(c->lv_pos != 0);
            } else if (c->lv_dir) {
                c->lv_pos = tc_toward(c->lv_pos, c->lv_dir * TC_FULL, c->lv_step);
            }
        } else {                                        /* LV_SPRNG */
            c->lv_back = 0;
            if (want)
                c->lv_dir = (int8_t)want;
            else if (c->lv_dir && c->lv_pos == c->lv_dir * TC_FULL)
                c->lv_dir = 0;                          /* (let go, and full reached: back) */
            if (c->lv_dir)
                c->lv_pos = tc_toward(c->lv_pos, c->lv_dir * TC_FULL, c->lv_step);
            else if (c->lv_pos)
                c->lv_pos = tc_toward(c->lv_pos, 0, st);
        }
    }
    {
        int32_t s = tc_scurve(c->lv_pos < 0 ? -c->lv_pos : c->lv_pos);
        c->lever = (int16_t)(c->lv_pos < 0 ? -s : s > 32767 ? 32767 : s);
    }
    /* RIBBON: the detents */
    d = c->det_n - c->det_seen;
    c->det_seen += d;
    if (d) {
        int32_t step = TC_FULL / (cfg->rib_steps ? cfg->rib_steps : 24), t = c->rib_t;
        int recent = c->rib_idle < TC_STOP_MS * (TC_RATE / 1000u), latch = cfg->rib_spring && c->rib_latch, moved = 0, back = 0;
        for (; d; d += d > 0 ? -1 : 1) {
            int32_t s = d > 0 ? 1 : -1, nt = t + s * step;
            if ((!cfg->rib_spring || latch) && c->rib_stop == s && recent)
                continue;                               /* (HOLD: stopped at 0; LATCH: sent back; the turn going on) */
            c->rib_stop = 0;
            if (latch && ((t > 0 && s < 0) || (t < 0 && s > 0))) {
                back = 1;                               /* (LATCH: back toward 0: the spring, the turn let go by) */
                c->rib_stop = (int8_t)s, recent = 1;
                continue;
            }
            moved = 1;
            if (!cfg->rib_spring && ((t > 0 && nt < 0) || (t < 0 && nt > 0)))
                nt = 0;                                 /* (HOLD: across 0 it stops there) */
            if (nt > -step / 2 && nt < step / 2)
                nt = 0;
            if (!cfg->rib_spring && t && !nt)
                c->rib_stop = (int8_t)s, recent = 1;
            if (nt > TC_FULL - step / 2)
                nt = TC_FULL;
            if (nt < -TC_FULL + step / 2)
                nt = -TC_FULL;
            t = nt;
        }
        c->rib_idle = 0;
        if (!latch || moved) {
            c->rib_t = t;
            c->rib_left = (uint8_t)tc_ramp_blocks(n);
            c->rib_ret = 0;
        }
        if (back)
            c->rib_ret = 1;
    } else if (c->rib_idle < 0x40000000u) {
        c->rib_idle += n;
    }
    /* .. the spring */
    if (cfg->rib_spring && !c->rib_latch && c->rib_t && !c->rib_ret &&
        (!notes || c->rib_idle >= (uint32_t)cfg->rib_delay * (TC_RATE / 100u) / 10u))
        c->rib_ret = 1;
    if (!cfg->rib_spring)
        c->rib_ret = 0;
    if (c->rib_ret) {
        int32_t a = c->rib_t < 0 ? -c->rib_t : c->rib_t, st;
        st = (int32_t)(((int64_t)(a > TC_FULL / 128 ? a : TC_FULL / 128) * c->rib_k) >> 16);
        if (st < 1)
            st = 1;
        a = a > st ? a - st : 0;
        c->rib_t = c->rib_t < 0 ? -a : a;
        if (!a)
            c->rib_ret = 0;
    }
    /* .. what the voices get: the ramp to rib_t, else rib_t */
    if (c->rib_left) {
        c->rib_o += (c->rib_t - c->rib_o) / c->rib_left;
        if (!--c->rib_left)
            c->rib_o = c->rib_t;
    } else {
        c->rib_o = c->rib_t;
    }
    {
        int32_t r = (c->rib_o + (c->rib_o < 0 ? -256 : 256)) / 512;   /* (to the nearest) */
        c->ribbon = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    }
}

/* the lever as MIDI pitch bend (full: 8191 up, -8192 down) and the ribbon as CC 16 (64 at rest, 0 and 127 the ends),
 * a block of n samples on: bit 0 *pb to send (-8192 .. 8191), bit 1 *cc; one of a kind every 10 ms at most, on change
 * only (R34) */
static uint32_t tc_midi(tc_t *c, uint32_t n, int32_t *pb, uint32_t *cc)
{
    uint32_t out = 0;
    int32_t l = c->lever, r = c->ribbon;
    int32_t b = l >= 0 ? (l * 8191 + 16383) / 32767 : l / 4;
    int32_t v = 64 + ((r * 64 + (r < 0 ? -16384 : 16384)) / 32768);
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    if (c->pb_age < TC_MIDI_GAP)
        c->pb_age += n;
    if (c->cc_age < TC_MIDI_GAP)
        c->cc_age += n;
    if (b != c->pb_sent && c->pb_age >= TC_MIDI_GAP) {
        c->pb_sent = (int16_t)b;
        c->pb_age = 0;
        *pb = b;
        out |= 1u;
    }
    if ((uint32_t)v != c->cc_sent && c->cc_age >= TC_MIDI_GAP) {
        c->cc_sent = (uint8_t)v;
        c->cc_age = 0;
        *cc = (uint32_t)v;
        out |= 2u;
    }
    return out;
}
