/* SPDX-License-Identifier: GPL-3.0-only */
/* GLO > PAD AT, PAD AT 2, KEY AT 3: the pads' squeeze (smk_press.h) and the keys' swell, each adjustable. Kept with
 * the touch settings (project.c), on every board (the layout). No HAL: tests/smk_press_test.c runs it */
#ifndef PAT_CFG_H
#define PAT_CFG_H
#include <stdint.h>
enum { SH_LIN, SH_EASE, SH_SLOW, SH_FAST, SH_N };   /* a curve 0..1: straight, smoothstep, x^2, 1 - (1 - x)^2 */
typedef struct {
    uint8_t dead;               /* DEAD 0..150 (60): a held hand's wobble, at most (from 5/12 of it near the top) */
    uint8_t shape;              /* SHAPE (SLOW): the squeeze's curve */
    uint8_t kshape;             /* KEY AT 3 > SHAPE (EASE): the keys' swell's curve */
    uint8_t rsv;
    uint16_t room;              /* ROOM 20..250 (80): the zero at least this far under the pad's top */
    uint16_t top;               /* TOP 600..1000 (800): a pad's top before it learns one (it may learn 40 under) */
    uint16_t settle;            /* SETTLE 50..500 ms (250): a press counts as settled by now */
    uint16_t rise;              /* RISE 10..400 ms (120): the pressure following a squeeze up ... */
    uint16_t fall;              /* FALL 10..400 ms (120): ... and back down */
    uint16_t kdelay;            /* KEY AT 3 > DELAY 0..3000 ms (0): a held key's swell starts after this */
} pat_cfg_t;
#if BOARD_SMK37
#define PAT_CFG_DEFAULTS {60, SH_SLOW, SH_EASE, 0, 80, 800, 250, 120, 120, 0}
#else   /* the FM-1: its keys' pressure (key_touch.c kt_lean, GLO > TOUCH 5 / 6), as before: DEAD 10 (Q8 octaves, 0.04),
         * a straight squeeze, SETTLE 250 ms, the smoothing 30 ms both ways (ROOM, TOP, KEY AT 3: unused there) */
#define PAT_CFG_DEFAULTS {10, SH_LIN, SH_EASE, 0, 80, 800, 250, 30, 30, 0}
#endif
static inline void pat_cfg_fix(pat_cfg_t *c)
{
    const pat_cfg_t d = PAT_CFG_DEFAULTS;
    if (c->dead > 150u) c->dead = d.dead;
    if (c->shape >= SH_N) c->shape = d.shape;
    if (c->kshape >= SH_N) c->kshape = d.kshape;
    if (c->room < 20u || c->room > 250u) c->room = d.room;
    if (c->top < 600u || c->top > 1000u) c->top = d.top;
    if (c->settle < 50u || c->settle > 500u) c->settle = d.settle;
    if (c->rise < 10u || c->rise > 400u) c->rise = d.rise;
    if (c->fall < 10u || c->fall > 400u) c->fall = d.fall;
    if (c->kdelay > 3000u) c->kdelay = d.kdelay;
    c->rsv = 0;
}
static inline uint32_t smk_shape(uint32_t x, uint32_t sh)   /* 0..1024 -> 0..1024 */
{
    uint32_t x2;
    if (x >= 1024u)
        return 1024u;
    x2 = x * x >> 10;
    switch (sh) {
    case SH_LIN: return x;
    case SH_SLOW: return x2;
    case SH_FAST: return 2u * x - x2;
    default: return 3u * x2 - (2u * x2 * x >> 10);            /* SH_EASE */
    }
}
#endif
