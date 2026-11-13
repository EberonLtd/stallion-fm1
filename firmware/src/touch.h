/* SPDX-License-Identifier: GPL-3.0-only */
/* Stallion: GLO > TOUCH, the settings of the FM-1's own performance controls (specs/touch R1-R28, R36) and the one
 * pressure shaping every pressure source passes (R11-R15). Kept in the settings flash (project.c touch_store_t, the
 * settings object's tail); a settings copy written before Stallion had them loads these defaults.
 *   KEY TOUCH (key_touch.c, seq.c keyboard_block): OFF (every key at velocity 100, as before), VEL, VEL+AT;
 *     AT SENS: the lean below a key's rest that reads 127, in 1/100 octave (0.12 .. 0.80); AT OUT: the keys' pressure
 *     on MIDI out, OFF, CHAN (channel pressure: the most any held key presses) or POLY; TOUCH CAL (an action:
 *     ui_synth.c touch_cal_*, the ends kept here in touch_cal).
 *   AT CURVE (LIN, SOFT p 0.5, HARD p 2), AT OFFSET -4..4, AT ATTACK / AT RELEASE 0..400 ms: at_lut (the curve then the
 *     offset, a table rebuilt by at_build when they change), then at_follow (a one-pole per source, its time constant
 *     the attack rising, the release falling; 0 ms passes the value as it is). At the defaults (LIN, 0, 0, 0) at_lut is
 *     the identity and nothing is smoothed: every pressure reaches the voices bit for bit as before (R15).
 *   LEVER (OCT- / OCT+ bend while a key plays a note; OFF, or how it moves: touch_ext's lever_mode), LEVER TIME; RIB STEPS, RIB SPRING, RIB DELAY, RIB RETURN, RIB RANGE
 *     (SELECT as a sprung ribbon while a key plays a note); MOTION REC (the sequencer's motion lanes): read by the lever,
 *     the ribbon and the sequencer, set only here.
 * The times are kept in ms (the pages step them by 10 ms). */
#pragma once
#include <stdint.h>

enum { KT_OFF, KT_VEL, KT_VELAT, KT_AT, KT_MODES };     /* KEY TOUCH (KT_AT: pressure alone, the notes at 100: appended) */
#ifndef AT_POPUP
#define AT_POPUP 1                                      /* the AT PRESSURE and VOLUME bar (ui_atpop.c), both boards */
#endif
#define KT_HAS_AT(m) ((m) == KT_VELAT || (m) == KT_AT)
enum { AT_OUT_OFF, AT_OUT_CHAN, AT_OUT_POLY, AT_OUT_MODES };
enum { AT_LIN, AT_SOFT, AT_HARD, AT_CURVES };
#if BOARD_SMK37
#define KP_NKEY 27u                                     /* keys with pressure: every pad's note (F3..G5) */
#else
#define KP_NKEY 20u
#endif
#define KT_NKEY 20u                                     /* the measured keys: notes 0..19 (F3..C5, rows PA7 and PA8) */
#if BOARD_SMK37                                         /* (no key converter: its slot holds PAD VEL, smk_press.h) */
#define TOUCH_SENS_MIN 0u
#define TOUCH_SENS_MAX 4u                               /* PV_FLOOR */
#else
#define TOUCH_SENS_MIN 12u                              /* AT SENS, 1/100 octave */
#define TOUCH_SENS_MAX 80u
#endif

typedef struct {
    uint8_t key_touch;          /* KT_OFF (default), KT_VEL, KT_VELAT, KT_AT */
    uint8_t at_sens;            /* AT SENS, 1/100 octave 12..80 (40) */
    uint8_t at_out;             /* AT_OUT_OFF (default), AT_OUT_CHAN, AT_OUT_POLY */
    uint8_t at_curve;           /* AT_LIN (default), AT_SOFT, AT_HARD */
    int8_t at_offset;           /* AT OFFSET -4..4 (0) */
    uint8_t lever;              /* LEVER: 1 ON (default, A1) */
    uint8_t rib_spring;         /* RIB SPRING: 1 ON (default: touch_ext's rib_sprng says SPRNG or LATCH), 0 HOLD */
    uint8_t motion_rec;         /* MOTION REC: 1 ON (default) */
    uint8_t rib_steps;          /* RIB STEPS 6..96 (24): detents from the centre to an end */
    uint8_t rib_range;          /* RIB RANGE 0..24 semitones (12) */
    uint16_t at_attack;         /* AT ATTACK 0..400 ms (0) */
    uint16_t at_release;        /* AT RELEASE 0..400 ms (0) */
    uint16_t lever_time;        /* LEVER TIME 10..1000 ms (120) */
    uint16_t rib_delay;         /* RIB DELAY 0..3000 ms (150) */
    uint16_t rib_return;        /* RIB RETURN 50..5000 ms (400) */
} touch_cfg_t;
#if !BOARD_SMK37
#define TOUCH_CFG_DEFAULTS {KT_OFF, 40, AT_OUT_OFF, AT_LIN, 0, 1, 1, 1, 24, 12, 0, 0, 120, 150, 400}
#else                                                   /* (the slot holds PAD VEL: PV_FLOOR) */
#define TOUCH_CFG_DEFAULTS {KT_OFF, 4, AT_OUT_OFF, AT_LIN, 0, 1, 1, 1, 24, 12, 0, 0, 120, 150, 400}
#endif
static touch_cfg_t touch_cfg = TOUCH_CFG_DEFAULTS;

/* (appended after TOUCH CAL's ends in the stored copy, project.c touch_store_t: touch_cfg_t keeps its 20 bytes) the
 * LEVER's way of moving when it is ON (touch_ctl.c): SPRNG back to rest when let go (as before), HOLD stays where
 * let go (the other button moves it back, through 0 on), LATCH (default) stays where let go, a press of the other
 * button springs it back to 0. A copy saved before it had this byte loads LATCH. rib_sprng (RIB SPRING ON): 1 SPRNG
 * (back after RIB DELAY, as before), 0 LATCH (the default: stays, a turn back toward 0 springs it there); a copy
 * saved before it had this byte (0) loads LATCH */
enum { LV_SPRNG, LV_HOLD, LV_LATCH, LV_MODES };
typedef struct {
    uint8_t lever_mode;         /* LV_SPRNG, LV_HOLD, LV_LATCH (default) */
    uint8_t rib_sprng;          /* RIB SPRING ON: 1 SPRNG, 0 LATCH (default) */
    uint8_t rsv[2];
} touch_ext_t;
#define TOUCH_EXT_DEFAULTS {LV_LATCH, 0, {0, 0}}
static touch_ext_t touch_ext = TOUCH_EXT_DEFAULTS;
/* KEY AT: the SMK-37's keys have no pressure, so a held key's is made up (smk_press.h smk_kat): a hit
 * lifts it by the velocity, it settles, and holding swells it. Kept with the touch settings (project.c), on every
 * board (the layout); the FM-1 has real pressure and ignores it */
typedef struct {
    uint8_t on;                 /* KEY AT: 1 ON (the SMK-37's default) */
    uint8_t hit;                /* HIT 0..127 (50): a full-velocity hit's lift */
    uint8_t vel;                /* VEL 0..100 % (70): how much the velocity scales it (0: every hit the same) */
    uint8_t hold;               /* HOLD 0..100 % (40): what the lift settles to */
    uint8_t ceil;               /* CEIL 0..127 (127): where holding swells to */
    uint8_t rsv;
    uint16_t rise;              /* RISE 0..1000 ms (120): the lift's arrival (40 stepped 10 a reading) */
    uint16_t fall;              /* FALL 10..3000 ms (400): its settling */
    uint16_t swell;             /* SWELL 0..10000 ms (3000; 0 off): holding to CEIL, at a full-velocity hit */
} kat_cfg_t;
#define KAT_CFG_DEFAULTS {1, 50, 70, 40, 127, 0, 120, 400, 3000}
static kat_cfg_t kat_cfg = KAT_CFG_DEFAULTS;
#include "pat_cfg.h"                                    /* (PAD AT, PAD AT 2, KEY AT 3: the pads' squeeze, the keys' swell) */
static pat_cfg_t pat_cfg = PAT_CFG_DEFAULTS;
static int key_at_on(void)                              /* (the head's "AT", GLOBE + F3: ui_layers.c key_at_toggle) */
{
#if BOARD_SMK37
    return kat_cfg.on != 0;                             /* KEY AT: the SMK-37's keys' made-up pressure */
#else
    return KT_HAS_AT(touch_cfg.key_touch);              /* KEY TOUCH's AT: the FM-1's keys' real pressure */
#endif
}
static void kat_cfg_fix(kat_cfg_t *c)
{
    const kat_cfg_t d = KAT_CFG_DEFAULTS;
    if (c->on > 1u) c->on = d.on;
    if (c->hit > 127u) c->hit = d.hit;
    if (c->vel > 100u) c->vel = d.vel;
    if (c->hold > 100u) c->hold = d.hold;
    if (c->ceil > 127u) c->ceil = d.ceil;
    if (c->rise > 1000u) c->rise = d.rise;
    if (c->fall < 10u || c->fall > 3000u) c->fall = d.fall;
    if (c->swell > 10000u) c->swell = d.swell;
    c->rsv = 0;
}

/* TOUCH CAL's result (R9): each measured key's ends in converter counts, cal 1 once a calibration ran (else the
 * uncalibrated defaults, floor 450, ceiling 896) */
typedef struct {
    uint16_t floor_[KT_NKEY], ceil[KT_NKEY];
    uint8_t cal;
} touch_cal_t;
static touch_cal_t touch_cal;

/* every value within its range (a settings copy read back, a knob) */
static void touch_cfg_fix(touch_cfg_t *c)
{
    const touch_cfg_t d = TOUCH_CFG_DEFAULTS;
    if (c->key_touch >= KT_MODES) c->key_touch = d.key_touch;
    if (c->at_sens < TOUCH_SENS_MIN || c->at_sens > TOUCH_SENS_MAX) c->at_sens = d.at_sens;
    if (c->at_out >= AT_OUT_MODES) c->at_out = d.at_out;
    if (c->at_curve >= AT_CURVES) c->at_curve = d.at_curve;
    if (c->at_offset < -4 || c->at_offset > 4) c->at_offset = d.at_offset;
    c->lever = c->lever != 0;
    c->rib_spring = c->rib_spring != 0;
    c->motion_rec = c->motion_rec != 0;
    if (c->rib_steps < 6u || c->rib_steps > 96u) c->rib_steps = d.rib_steps;
    if (c->rib_range > 24u) c->rib_range = d.rib_range;
    if (c->at_attack > 400u) c->at_attack = d.at_attack;
    if (c->at_release > 400u) c->at_release = d.at_release;
    if (c->lever_time < 10u || c->lever_time > 1000u) c->lever_time = d.lever_time;
    if (c->rib_delay > 3000u) c->rib_delay = d.rib_delay;
    if (c->rib_return < 50u || c->rib_return > 5000u) c->rib_return = d.rib_return;
}

static void touch_ext_fix(touch_ext_t *x)
{
    if (x->lever_mode >= LV_MODES) x->lever_mode = LV_LATCH;
    x->rib_sprng = x->rib_sprng != 0;
}

/* ------------------------------------------------------------------ the pressure shaping (R11-R15) */
static uint8_t at_lut[128] = {
    0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,
    22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,
    44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  65,
    66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,
    88,  89,  90,  91,  92,  93,  94,  95,  96,  97,  98,  99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 109,
    110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127};   /* (LIN, 0: as it is) */
static uint8_t at_smooth;                               /* AT ATTACK or AT RELEASE not 0: at_follow runs (seq.c at_block) */
static uint32_t at_k_att, at_k_rel;                     /* their one-pole steps a control block (CTL samples), Q16 */
static uint32_t at_k_min;                               /* AT_MIN_MS's: the keys' own pressure's fastest */

/* a time constant of tau ms as a one-pole's step over a block of n samples, Q16 (dt / (tau + dt / 2): within 1 % of
 * 1 - e^(-dt / tau) for any tau >= the block; 0 ms: 65536, the value at once) */
static uint32_t at_kq16(uint32_t tau_ms, uint32_t n)
{
    uint32_t dt = n * 1000000u / 44100u, den = tau_ms * 1000u + dt / 2u;   /* (us) */
    uint32_t k = (dt << 16) / (den ? den : 1u);        /* (32-bit: dt < 65536 us) */
    return k > 65536u ? 65536u : k;
}
/* curve then offset, rounded (R11, R12) */
static uint32_t at_shape1(uint32_t i, uint32_t curve, int32_t off)
{
    uint32_t c = i;
    if (curve == AT_SOFT) {                             /* 127 (i / 127)^0.5 = sqrt(127 i), to the nearest */
#if BOARD_SMK37
        /* the pads: 127 (1 - (1 - i / 127)^2), soft but with a slope of 2 at 0; with the squeeze's ease-in
         * (smk_press.h) an S, gentle at first */
        c = (254u * i - i * i + 63u) / 127u;
#else
        uint32_t x = i * 127u, r = 0, b;
        for (b = 1u << 7; b; b >>= 1)
            if ((r + b) * (r + b) <= x)
                r += b;
        c = r * r + r < x ? r + 1u : r;
#endif
    } else if (curve == AT_HARD) {                      /* 127 (i / 127)^2 */
        c = (i * i + 63u) / 127u;
    }
    if (off > 0) {                                      /* below 8 x OFFSET: 0, the rest stretched to 127 */
        uint32_t lo = 8u * (uint32_t)off;
        c = c < lo ? 0u : ((c - lo) * 127u + (127u - lo) / 2u) / (127u - lo);
    } else if (off < 0) {                               /* from 127 + 8 x OFFSET: 127 */
        uint32_t hi = (uint32_t)(127 + 8 * off);
        c = c >= hi ? 127u : (c * 127u + hi / 2u) / hi;
    }
    return c > 127u ? 127u : c;
}
#define AT_MIN_MS 10u                                  /* the shortest AT ATTACK / RELEASE (0: this): no steps heard */
static void at_build(void)                              /* after any AT setting changed (and at power-on) */
{
    uint32_t i;
    for (i = 0; i < 128u; i++)
        at_lut[i] = (uint8_t)at_shape1(i, touch_cfg.at_curve, touch_cfg.at_offset);
    at_k_att = at_kq16(touch_cfg.at_attack, 32u);       /* (CTL: events_block's block) */
    at_k_rel = at_kq16(touch_cfg.at_release, 32u);
    at_k_min = at_kq16(AT_MIN_MS, 32u);                 /* the keys' own pressure: never faster (seq.c) */
    at_smooth = (uint8_t)(touch_cfg.at_attack || touch_cfg.at_release);   /* (the keys' own pressure glides
                                                         * AT_MIN_MS at the least whatever these, seq.c key_at; MIDI's
                                                         * at once at 0, R13 / R15) */
}
/* a source's state (s: Q16 of 0..127) one block on toward its target t (0..127): the value it shows. Steps of at
 * least 1/65536, so it reaches t exactly */
static uint8_t at_floor;                               /* (seq.c at_block: this voice's pressure is the keys') */
static uint32_t at_follow(uint32_t *s, uint32_t t)
{
    int32_t d = (int32_t)(t << 16) - (int32_t)*s, st;
    uint32_t k;
    if (!d)
        return t;
    k = d > 0 ? at_k_att : at_k_rel;
    if (at_floor && k > at_k_min)                       /* (the keys' own: AT_MIN_MS at the fastest; both boards) */
        k = at_k_min;
    st = (int32_t)(((int64_t)d * (int32_t)k) >> 16);
    if (!st)
        st = d > 0 ? 1 : -1;
    *s = (uint32_t)((int32_t)*s + st);
    return (*s + 32768u) >> 16;
}
