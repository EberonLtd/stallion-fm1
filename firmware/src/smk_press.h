/* SPDX-License-Identifier: GPL-3.0-only */
/* The SMK-37's pad pressure, as 0..127 of what a hand does. The sensor is steep at first and flat later: on a unit a
 * touch passes 500 within 100 ms, a squeeze moves about 600..920, and the hardest press stops near 980 (rest near 0).
 * So the range is the top of the scale, straight, and never moves: a range that grew to the hardest reading went numb
 * after every strike, and one of 150 sat at 127 from the first touch. No HAL here: tests/smk_press_test.c runs it. */
#ifndef SMK_PRESS_H
#define SMK_PRESS_H
#include <stdint.h>
#include "pat_cfg.h"
#define SMK_PRESS_LO 350u                     /* readings above rest: below this, no pressure */
#define SMK_PRESS_HI 950u                     /* from here, 127 */
static inline uint32_t smk_press_of(uint32_t raw, uint32_t base)
{
    uint32_t h = raw > base ? raw - base : 0u;
    if (h <= SMK_PRESS_LO)
        return 0;
    if (h >= SMK_PRESS_HI)
        return 127u;
    return (h - SMK_PRESS_LO) * 127u / (SMK_PRESS_HI - SMK_PRESS_LO);
}
/* toward v, each call (5 ms): up and down about 120 ms. The force falls before a pad's note-off, so a fast fall shut
 * the filter on the note's tail (-30 dB in 100 ms); a fast rise was a puff on every touch, and (v3) at 60 ms a squeeze
 * stepped the filter audibly */
static inline uint32_t smk_press_glide2(uint32_t l, uint32_t v, uint32_t rise_ms, uint32_t fall_ms)
{
    uint32_t ru = rise_ms / 5u ? rise_ms / 5u : 1u, fd = fall_ms / 5u ? fall_ms / 5u : 1u;   /* (PAD AT 2 RISE, FALL) */
    return v > l ? l + (v - l + ru - 1u) / ru : l - (l - v + fd - 1u) / fd;
}
static inline uint32_t smk_press_glide(uint32_t l, uint32_t v) { return smk_press_glide2(l, v, 120u, 120u); }
/* Squeeze pressure. A pad bottoms out on any press: a fast hit reads ~880 within 30 ms, a gentle one rises over
 * 100..200 ms, and both hold at ~850..930 of a top of ~990. So a note's pressure counts from where its press settles:
 * nothing until the rise stops (under SMK_REL_STILL in SMK_REL_STEP ms, or SMK_REL_MAX ms), then what is pressed past
 * that; a normal press plays the sound as saved. A held hand wobbles ~100..150 at 2..3 Hz: the zero stays where it
 * settled, the wobble is under SMK_REL_DEAD, and the squeeze is spread over SMK_REL_SPAN..SMK_REL_SPANX.
 * The pads differ: their firmest held readings run from ~776 to ~896. So each pad learns its own top from its held
 * readings (the strike's spike is over by then) and lets it fall back slowly; 127 is at that top; the squeeze eases in
 * (squared); the zero is never closer than SMK_REL_ROOM under the top, so a firm strike still leaves room; and the
 * rise is glided (smk_press_glide) */
#define SMK_REL_STEP 30u                      /* ms between looks at the rise */
#define SMK_REL_STILL 16u                     /* a rise under this in a step: settled */
#define SMK_REL_MIN 40u                       /* ms: never settled before this */
/* set on GLO > PAD AT, PAD AT 2 (pat_cfg.h; the defaults here): SETTLE (by now settled, however it rises: 250 ms), TOP
 * (a pad's top before it learns one: 800, the weakest pad ~776; it may learn down to 40 under), ROOM (the zero at
 * least this far under the top: 80), DEAD (a held hand's wobble: a third of the room, from 5/12 of DEAD up to DEAD:
 * 25..60), SHAPE (the squeeze's curve: SLOW, x^2) */
#define SMK_TOP_MAX 1000u
#define SMK_TOP_FALL 100u                     /* ms a count, back toward TOP: ~10 a second */
typedef struct {
    uint32_t t0, tm;                          /* the note's start; the last look */
    uint16_t peak, mark, base;                /* the most so far; the most at the last look; the zero */
    uint8_t on, set;
} smk_rel_t;
typedef struct {
    uint16_t top;                             /* a pad's firmest held reading lately (0: not learned) */
    uint32_t tf;                              /* its last fall */
} smk_top_t;
static inline void smk_rel_start(smk_rel_t *s, uint32_t now)
{
    s->t0 = s->tm = now;
    s->peak = s->mark = s->base = 0;
    s->on = 1;
    s->set = 0;
}
static inline uint32_t smk_top_of(smk_top_t *tp, const pat_cfg_t *c, uint32_t h, int held, uint32_t now)
{
    uint32_t top = tp->top ? tp->top : c->top, lo = c->top > 40u ? c->top - 40u : 0u;
    if (held && h > top)
        top += (h - top + 7u) / 8u;           /* toward a firmer hold, over ~40 ms (one reading's spike: an eighth) */
    else if (now - tp->tf >= SMK_TOP_FALL) {
        tp->tf = now;
        if (top > c->top)
            top--;
    }
    if (top > SMK_TOP_MAX)
        top = SMK_TOP_MAX;
    if (top < lo)
        top = lo;
    tp->top = (uint16_t)top;
    return top;
}
static inline uint32_t smk_rel_press(smk_rel_t *s, smk_top_t *tp, const pat_cfg_t *c, uint32_t h, uint32_t now)
{                                                       /* (h: the reading above rest) */
    uint32_t d, span, dead, base, top, x, dmin = c->dead * 5u / 12u;
    if (h > 0xFFFFu)
        h = 0xFFFFu;
    if (!s->set) {
        if (h > s->peak)
            s->peak = (uint16_t)h;
        if (now - s->tm >= SMK_REL_STEP) {
            if ((s->peak <= s->mark + SMK_REL_STILL && now - s->t0 >= SMK_REL_MIN) || now - s->t0 >= c->settle) {
                s->set = 1;
                s->base = s->peak;
            } else {
                s->mark = s->peak;
                s->tm = now;
            }
        }
        (void)smk_top_of(tp, c, h, 0, now);
        if (!s->set)
            return 0;
    }
    top = smk_top_of(tp, c, h, 1, now);
    base = s->base + c->room > top ? (top > c->room ? top - c->room : 0u) : s->base;
    dead = (top - base) / 3u;
    dead = dead > c->dead ? c->dead : dead < dmin ? dmin : dead;
    if (h <= base + dead)
        return 0;
    d = h - base - dead;
    span = top > base + dead ? top - base - dead : 1u;
    x = d >= span ? 1024u : d * 1024u / span;
    return (smk_shape(x, c->shape) * 127u + 512u) >> 10;   /* (SLOW: a quarter of the way, 8; half, 32) */
}
/* KEY AT: a held key's made-up pressure, ms after its note-on at velocity vel. A hit lifts it to
 * hit x (the velocity, scaled by velp %) over rise ms, it settles to hold % of that over fall ms, and holding swells
 * it to ceil over swell ms (a full-velocity hit; a soft one up to twice as long). The larger of the two; both
 * eased (smoothstep), so it never jumps. Its shaping after (AT CURVE, OFFS) is the pads' */
static inline uint32_t smk_ease(uint32_t x)            /* 0..1024 -> 0..1024, 3x^2 - 2x^3 */
{
    uint32_t x2 = x * x >> 10;
    return x >= 1024u ? 1024u : (3u * x2 - (2u * x2 * x >> 10));
}
static inline uint32_t smk_kat(uint32_t hit, uint32_t velp, uint32_t hold, uint32_t ceil, uint32_t rise, uint32_t fall,
                               uint32_t swell, uint32_t kdelay, uint32_t kshape, uint32_t vel, uint32_t ms)
{                                                       /* (KEY AT 3: the swell starts kdelay ms in, kshape its curve) */
    uint32_t v = ((100u - velp) * 127u + velp * (vel > 127u ? 127u : vel)) / 100u;   /* 0..127 */
    uint32_t pk = hit * v / 127u, lo = pk * hold / 100u, a, b = 0, x;
    if (ms < rise)
        a = pk * smk_ease(ms * 1024u / rise) >> 10;
    else if (ms - rise < fall)
        a = pk - ((pk - lo) * smk_ease((ms - rise) * 1024u / fall) >> 10);
    else
        a = lo;
    if (swell && ms > kdelay) {
        uint32_t t = swell * (254u - v) / 127u, m = ms - kdelay;   /* (velocity 127: swell; 0: twice) */
        x = m >= t ? 1024u : (uint32_t)((uint64_t)m * 1024u / t);
        b = ceil * smk_shape(x, kshape) >> 10;
    }
    a = a > b ? a : b;
    return a > 127u ? 127u : a;
}
/* PAD VEL: the pads are stiff and a hit's force is read in its first milliseconds, so a note that plays loud has used the
 * pressure up. The velocity a hit gets, lifted: LIGHT on 127 (v / 127)^0.5, XLIGHT that from 40 up, FIXED one level,
 * FLOOR (the default) that from 80 up: a gentle press reads 11..40, and FLOOR plays it a few dB under a hit */
enum { PV_NORMAL, PV_LIGHT, PV_XLIGHT, PV_FIXED, PV_FLOOR, PV_MODES };
static inline uint32_t smk_vel_lift(uint32_t v, uint32_t mode)
{
    uint32_t x = v * 127u, r = 0, b;
    if (mode == PV_NORMAL || mode >= PV_MODES)
        return v;
    if (mode == PV_FIXED)
        return 100u;
    for (b = 1u << 7; b; b >>= 1)
        if ((r + b) * (r + b) <= x)
            r += b;
    return mode == PV_LIGHT ? r : mode == PV_FLOOR ? 80u + r * 47u / 127u : 40u + r * 87u / 127u;
}
#endif
