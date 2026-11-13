/* SPDX-License-Identifier: GPL-3.0-only */
/* Stallion: taken from ESTAMPIE (github.com/ocean/estampie, firmware/src/key_touch.c at af5a871, GPL-3.0-only, by Drew
 * Robinson), itself ported from Charles Vestal's Velo and MONK-1 (GPL-3.0-only). Stallion's changes: the settings are
 * GLO > TOUCH's (touch.h touch_cfg: KEY TOUCH, AT SENS in 1/100 octave) and the calibration is kept exact in the settings
 * flash (touch.h touch_cal, project.c; kt_cal_store / kt_cal_take) instead of ESTAMPIE's favourites row; a key that is
 * not measured in time plays at velocity 100 (Stallion has no SELECT VEL); the note waits at most KT_GATE_MS 11 (12 ms
 * from the scan with the keyboard's 0.73 ms block, specs/touch R3); the pressure's smoothing 30 ms (R6). ESTAMPIE's
 * notice follows.
 *
 * ESTAMPIE: KEY TOUCH, velocity and aftertouch from the FM-1's own keys, with no hardware change. Ported from Charles
 * Vestal's Velo (github.com/charlesvestal/fm1-velocity) as MONK-1 (github.com/charlesvestal/fm1-monk) plays it
 * (sense.c and ui.c pressure(); GPL-3.0-only), to integer arithmetic (this firmware has no float) and to this
 * firmware's keyboard: the scan still decides when a key is down (hal/fm1_input.h), this only measures how hard.
 *
 * How: each key is a rubber dome with a carbon pill, whose resistance falls as it is pressed harder. Two of the four
 * matrix rows (PA7, PA8: notes 0..19, F3..C5) are converter inputs too, so the scan tick samples the key of the column
 * it has just latched on one of them, each key every ~2.2 ms (fm1_input.h fm1_an). A key read against the row's
 * pull-up only shows the matrix diode's drop once it seats, so every other sample of a held key is taken with its row
 * driven high by the pin's weak driver (DRIVE 2, Velo's default): then the reading is the diode (KT_DBASE) plus the
 * pill's resistance, spread over a few hundred counts. The top seven keys (PA6) have no converter: they play at
 * velocity 100, as every key does with KEY TOUCH OFF.
 *
 * The state machine (kt_sample, in the scan ISR) runs on the pulled-up samples: OPEN, TOUCH (below KT_TOUCH: the pill
 * conducts), DOWN (seated below KT_PRESS, KT_WAIT samples after the touch at least), LIFT, and OPEN again after two
 * samples above KT_RELEASE (a seated key can read open for one). The dome snaps, so every press lands within a sample
 * or two whatever its speed: what tells a hard hit from a soft one is how deep the key sits just after landing. The
 * velocity is the lowest driven reading since the touch (the scan ISR keeps it, kt_gate maps it), on a log scale
 * between the key's floor (its firmest landing: 127) and its ceiling (its lightest landing that seats: KT_VMIN):
 * t = log(r / r_floor) / log(r_ceil / r_floor) with r the reading less KT_DBASE (conductive rubber's resistance falls
 * as a power of the force, so its log is linear in the force's), velocity = KT_VMIN + (127 - KT_VMIN) (1 - t)^0.7
 * (Velo's curve: free playing gave quartiles 45 / 63 / 80; its 1 at the ceiling is -42 dB, as good as silent).
 *
 * Each key's ends then follow its own landings as running quantiles (MONK-1's sense_learn): the floor near its firmest
 * 1/32 of presses, the ceiling near its lightest 1/32. A landing past an end moves it most of a step (64 counts for
 * an uncalibrated key's first 16 landings, 24 after), one inside creeps it back by 1/8 of a step (the first 16) or
 * 1/32, so one freak press moves an end a step at most. Keys differ (Velo: the lightest landings 742..987 across one
 * FM-1's keys): without this, a key whose light presses read past the default ceiling played them all at the
 * lightest. The learnt ends last until power-off; the calibration (below) is what is kept.
 *
 * The keyboard (seq.c keyboard_block, kt_gate) holds a measured key's note back until its velocity is in (KT_WAIT
 * pulled samples, two driven: ~6.6 ms from the touch), at most KT_GATE_MS after the scan saw it down; then it plays
 * as any key, at its own velocity. A key that never seats in time plays at velocity 100.
 *
 * Aftertouch (VEL+AT, kt_lean every KT_AT_MS from the audio ISR): pressing harder than the key rests. A key goes
 * on settling after it lands, so the zero follows the driven reading down for KT_SETTLE_MS, then creeps after it
 * (seconds), and comes back up at once when the key is eased. The lean is the pill's resistance below that rest in
 * octaves: KT_DEAD is noise, the span (AT SENS: 0.12 .. 0.8 octave) is 127. Smoothed over ~30 ms. Stallion: each key's
 * pressure is the polyphonic aftertouch of the voices playing its notes (seq.c key_pressure), after the AT shaping.
 *
 * Calibration (GLO > TOUCH > CAL, ui_synth.c touch_cal_*): every measured key pressed gently, then firmly, KT_CAL_N
 * times each; the ceiling is the median of its gentle landings, the floor of its firm ones. Kept in the settings
 * (touch.h touch_cal). Until a key is calibrated it uses Velo's defaults for DRIVE 2 (floor 450, ceiling 896).
 *
 * No HAL here: tests/key_touch_test.c runs it on synthetic presses. */

#define KT_ANALOG 0xFFFFFu
#define KT_TOUCH 900u                  /* pulled-up readings (1023 open) */
#define KT_PRESS 400u
#define KT_RELEASE 600u
#define KT_WAIT 2u                     /* pulled samples after the touch before the velocity (two driven between) */
#define KT_DBASE 185u                  /* a driven reading with no pill resistance: the diode and the 595 */
#define KT_FLOOR0 450u                 /* the defaults (Velo, DRIVE 2) */
#define KT_CEIL0 896u
#define KT_SPAN 80u                    /* the least a ceiling sits above its floor */
#define KT_VMIN 10u                    /* the lightest velocity (at the ceiling) */
#define KT_LEARN_N 16u                 /* landings learnt quickly (an uncalibrated key's first) */
#define KT_GATE_MS 11u                 /* the longest a measured key's note waits for its velocity (Stallion: 12 ms
                                        * from the scan, with the keyboard's 0.73 ms block) */
#define KT_AT_MS 5u
#define KT_SETTLE_MS 250u
#define KT_DEAD 10                     /* aftertouch: Q8 octaves of noise (0.04) */
#define KT_CAL_N 3u                    /* calibration: presses per key and kind */
enum { KT_S_OPEN, KT_S_TOUCH, KT_S_DOWN, KT_S_LIFT };

typedef struct {
    uint16_t v;                        /* the last pulled-up reading */
    uint16_t d, dmin;                  /* driven: the last reading, the lowest since the touch */
    uint16_t floor_, ceil;             /* the key's ends (calibration) */
    uint8_t st, since, up, dn, dnext;
    uint16_t dland;                    /* the last landing's lowest driven reading (0xFFFF: none driven) */
    volatile uint8_t land;             /* bumps at each landing (kt_gate: its velocity from dland) */
    volatile uint8_t touch, ltouch;    /* (Stallion) bumps at each touch; the touch the last landing belongs to */
} kt_key_t;

static kt_key_t kt_key[KT_NKEY];
static uint16_t kt_f16[KT_NKEY], kt_c16[KT_NKEY];   /* the ends in Q4 (kt_learn's creep is under a count) */
static uint8_t kt_nland[KT_NKEY];                   /* landings learnt (to KT_LEARN_N) */
#define kt_mode touch_cfg.key_touch     /* (Stallion) GLO > TOUCH > KEY TOUCH: OFF, VEL, VEL+AT, AT */
static uint8_t kt_calibrated;          /* the ends came from a TOUCH CAL (else the defaults) */
static volatile uint8_t kt_live;       /* the scan is measuring the keys (main.c: KEY TOUCH on, on the hardware); the
                                        * emulator and the host never are, and their keys never wait */

/* calibration in progress (ui_touch.c): 1 gentle, 2 firm; each landing of a measured key is kept */
static volatile uint8_t kt_cal_phase;
static uint16_t kt_cal_v[2][KT_NKEY][KT_CAL_N];
static uint8_t kt_cal_n[2][KT_NKEY];

/* (i / 32)^0.7 in Q12, i = 0..32: the velocity's curve */
static const uint16_t KT_POW07[33] = {0, 362, 588, 781, 955, 1117, 1269, 1414, 1552, 1685, 1814, 1940, 2061, 2180,
                                      2296, 2410, 2521, 2631, 2738, 2844, 2948, 3050, 3151, 3251, 3349, 3446, 3542,
                                      3637, 3730, 3823, 3915, 4006, 4096};

/* log2(x) in Q8 (x >= 1): the exponent (a binary search: no loop, the audio ISR's cost budget), and the mantissa's
 * log by f + 0.3466 f (1 - f) (within 0.008 octave); 32-bit arithmetic only */
static int32_t kt_log2q8(uint32_t x)
{
    uint32_t e = 0, m;
    int32_t f;
    if (!x)
        x = 1;
    if (x >> 16)
        e = 16;
    if (x >> (e + 8u))
        e += 8;
    if (x >> (e + 4u))
        e += 4;
    if (x >> (e + 2u))
        e += 2;
    if (x >> (e + 1u))
        e += 1;
    m = e <= 15u ? x << (15u - e) : x >> (e - 15u);
    f = (int32_t)(m - 32768u);                     /* Q15, 0..1 */
    f += (((f * (32768 - f)) >> 15) * 11358) >> 15;   /* (0.3466 in Q15) */
    return (int32_t)(e << 8) + (f >> 7);
}

/* key i's ends in Q4 (f16 floor, c16 ceiling), kept apart and within the readings */
static void kt_ends16(uint32_t i, uint32_t f16, uint32_t c16)
{
    if (f16 < (KT_DBASE + 16u) << 4)
        f16 = (KT_DBASE + 16u) << 4;
    if (c16 > 1020u << 4)
        c16 = 1020u << 4;
    if (c16 < f16 + (KT_SPAN << 4)) {              /* too close: part them about the middle */
        uint32_t mid = (f16 + c16) / 2u;
        f16 = mid - (KT_SPAN << 3);
        c16 = mid + (KT_SPAN << 3);
    }
    kt_f16[i] = (uint16_t)f16;
    kt_c16[i] = (uint16_t)c16;
    kt_key[i].floor_ = (uint16_t)((f16 + 8u) >> 4);
    kt_key[i].ceil = (uint16_t)((c16 + 8u) >> 4);
}
static void kt_ends(kt_key_t *k, uint32_t fl, uint32_t cl) { kt_ends16((uint32_t)(k - kt_key), fl << 4, cl << 4); }

/* a landing of key i (m: its lowest driven reading) moves its ends (the top) */
static void kt_learn(uint32_t i, uint32_t m)
{
    uint32_t early = kt_nland[i] < KT_LEARN_N, step = early ? 64u << 4 : 24u << 4, m16 = m << 4;
    uint32_t creep = early ? step / 8u : step / 32u, jump = step - creep, f = kt_f16[i], c = kt_c16[i];
    if (m16 < f)
        f -= f - m16 < jump ? f - m16 : jump;
    else
        f += creep;
    if (m16 > c)
        c += m16 - c < jump ? m16 - c : jump;
    else
        c -= creep;
    kt_ends16(i, f, c);
    if (kt_nland[i] < KT_LEARN_N)
        kt_nland[i]++;
}

static void kt_defaults(void)
{
    uint32_t i;
    for (i = 0; i < KT_NKEY; i++) {
        kt_ends(&kt_key[i], KT_FLOOR0, KT_CEIL0);
        kt_nland[i] = 0;
    }
    kt_calibrated = 0;
}

static void kt_init(void)
{
    uint32_t i;
    for (i = 0; i < KT_NKEY; i++) {
        kt_key[i].v = kt_key[i].d = kt_key[i].dmin = 1023;
        kt_key[i].st = KT_S_OPEN;
        kt_key[i].dland = 0xFFFFu;
    }
    kt_defaults();
}

/* the velocity of driven reading d on a key with ends fl (127) .. cl (KT_VMIN) */
static uint8_t kt_velocity(uint32_t d, uint32_t fl, uint32_t cl)
{
    int32_t l, l0, l1, t, i, fr, p;
    if (d <= fl)
        return 127;
    if (d >= cl)
        return KT_VMIN;
    l = kt_log2q8(d - KT_DBASE);
    l0 = kt_log2q8(fl > KT_DBASE ? fl - KT_DBASE : 1u);
    l1 = kt_log2q8(cl - KT_DBASE);
    if (l1 <= l0)
        return 64;
    t = 4096 - ((l - l0) << 12) / (l1 - l0);       /* 1 - t, Q12 */
    t = t < 0 ? 0 : t > 4096 ? 4096 : t;
    i = t >> 7;                                    /* the curve: 32 steps, interpolated */
    fr = t & 127;
    p = KT_POW07[i] + (i < 32 ? ((KT_POW07[i + 1] - KT_POW07[i]) * fr) >> 7 : 0);
    return (uint8_t)(KT_VMIN + (((127 - KT_VMIN) * (uint32_t)p + 2048u) >> 12));
}

/* the scan asks before each sample of key k: drive its row? (the first after the touch, then every other one) */
static int kt_want_drive(uint32_t k) { return (kt_mode || kt_cal_phase) && k < KT_NKEY && kt_key[k].dnext; }
/* the key's column LEDs may be lit while it is read (fm1_input.h): an open key. A lit LED's current goes through the
 * column and lifts its keys' readings (Velo measured +25): a held key's would jump as an LED changed */
static int kt_led_ok(uint32_t k) { return k >= KT_NKEY || kt_key[k].st == KT_S_OPEN; }
/* no measured key is touched (main.c: the battery's slow reading pauses the scan's sampling only then) */
static int kt_idle(void)
{
    uint32_t k;
    for (k = 0; k < KT_NKEY; k++)
        if (kt_key[k].st != KT_S_OPEN)
            return 0;
    return 1;
}

/* (Stallion) what TOUCH CAL's screen shows: the last landing (its key, reading, whether it counted) */
static volatile uint8_t kt_cal_lastk = 0xFFu, kt_cal_lastok;
static volatile uint16_t kt_cal_lastv;
static void kt_cal_keep(uint32_t key, uint32_t m)
{
    uint32_t ph = kt_cal_phase - 1u, n = kt_cal_n[ph][key];
    kt_cal_lastk = (uint8_t)key;
    kt_cal_lastv = (uint16_t)m;
    kt_cal_lastok = (uint8_t)(n < KT_CAL_N);
    if (n < KT_CAL_N) {
        kt_cal_v[ph][key][n] = (uint16_t)m;
        kt_cal_n[ph][key] = (uint8_t)(n + 1u);
    }
}

/* a sample of key k (0..19): v the reading, driven: taken with the row driven high. The scan ISR */
static void kt_sample(uint32_t key, uint32_t v, int driven)
{
    kt_key_t *k;
    if (key >= KT_NKEY)
        return;
    k = &kt_key[key];
    if (driven) {                                  /* the force: no state change (a driven graze reads open too) */
        k->dnext = 0;
        if (k->st == KT_S_OPEN)
            return;
        k->d = (uint16_t)v;
        if (k->st == KT_S_TOUCH)
            k->dn++;
        if (k->st != KT_S_LIFT && v < k->dmin)
            k->dmin = (uint16_t)v;
        return;
    }
    k->v = (uint16_t)v;
    if (k->st == KT_S_OPEN && v < KT_TOUCH) {
        k->st = KT_S_TOUCH;
        k->since = 0;
        k->dn = 0;
        k->dmin = 1023;
        k->touch++;
    }
    if (k->st == KT_S_TOUCH) {
        if (v >= KT_TOUCH) {                       /* gone before it seated: a graze */
            k->st = KT_S_OPEN;
        } else if (k->since >= KT_WAIT && v < KT_PRESS) {   /* seated: the landing (its velocity: kt_gate) */
            uint32_t m = k->dn ? k->dmin : 0xFFFFu;
            k->st = KT_S_DOWN;
            k->dland = (uint16_t)m;
            k->ltouch = k->touch;
            if (kt_cal_phase && m != 0xFFFFu)
                kt_cal_keep(key, m);
            k->land++;
        } else if (k->since < 255u) {
            k->since++;
        }
    } else if (k->st == KT_S_DOWN && v >= KT_PRESS) {
        k->st = KT_S_LIFT;
        k->up = 0;
    }
    if (k->st == KT_S_LIFT) {
        if (v < KT_PRESS)
            k->st = KT_S_DOWN;
        else if (v < KT_RELEASE)
            k->up = 0;
        else if (++k->up >= 2u)
            k->st = KT_S_OPEN;
    }
    k->dnext = (uint8_t)(k->st != KT_S_OPEN);      /* a held key: the next sample driven */
}

/* ------------------------------------------------------------------ the keyboard (seq.c keyboard_block, audio ISR) */
static uint8_t kt_seen[KT_NKEY];        /* the landing the keyboard last took */
static uint32_t kt_wait;                /* keys down whose velocity is not in yet */
static uint32_t kt_wait_ms[KT_NKEY];
static uint8_t kt_kvel[27];             /* the velocity each key played with (0: 100, Stallion) */

/* the keys the keyboard may play now: cur (the scan's) less those still waiting for their velocity. prev: the keys it
 * played last time. A measured key newly down waits until its landing is in, at most KT_GATE_MS */
static uint32_t kt_gate(uint32_t cur, uint32_t prev, uint32_t now_ms)
{
    uint32_t k, newd = cur & ~prev & KT_ANALOG;
    if (!kt_mode || !kt_live || kt_mode == KT_AT) {  /* (Stallion: AT, pressure alone: no wait, every note at 100) */
        kt_wait = 0;
        for (k = 0; k < 27u; k++)                  /* (Stallion: none measured, 100) */
            if ((cur & ~prev) >> k & 1u)
                kt_kvel[k] = 0;
        return cur;
    }
    kt_wait &= cur;                                /* (let go while waiting: nothing to play) */
    for (k = 0; k < KT_NKEY; k++) {
        if (!((newd >> k) & 1u))
            continue;
        if (!((kt_wait >> k) & 1u)) {              /* newly down: from now */
            kt_wait |= 1u << k;
            kt_wait_ms[k] = now_ms;
            /* (Stallion) a landing not of this touch is an earlier press's, seated after its note gave up waiting:
             * dropped, or this press would play it (seen on an FM-1: every velocity one press behind) */
            if (kt_key[k].land != kt_seen[k] && (kt_key[k].st == KT_S_OPEN || kt_key[k].ltouch != kt_key[k].touch))
                kt_seen[k] = kt_key[k].land;
        }
        if (kt_key[k].land != kt_seen[k]) {        /* landed: its velocity */
            uint32_t m = kt_key[k].dland;
            kt_seen[k] = kt_key[k].land;
            kt_kvel[k] = m == 0xFFFFu ? 0u : kt_velocity(m, kt_key[k].floor_, kt_key[k].ceil);
            if (m != 0xFFFFu && !kt_cal_phase)     /* (then its ends follow it: MONK-1's order) */
                kt_learn(k, m);
            kt_wait &= ~(1u << k);
        } else if (now_ms - kt_wait_ms[k] >= KT_GATE_MS) {   /* never seated in time: (Stallion) its lowest driven
                                                     * reading so far if it has one (a slow soft press: soft), else 100 */
            const kt_key_t *x = &kt_key[k];
            kt_kvel[k] = x->st != KT_S_OPEN && x->dn ? kt_velocity(x->dmin, x->floor_, x->ceil) : 0u;
            kt_wait &= ~(1u << k);
        }
    }
    for (k = KT_NKEY; k < 27u; k++)                /* the top row: 100 */
        if ((cur & ~prev) >> k & 1u)
            kt_kvel[k] = 0;
    return cur & ~kt_wait;
}

/* ------------------------------------------------------------------ aftertouch (VEL+AT) */
static int32_t kt_base8[KT_NKEY];        /* the rest, Q8 of the driven reading less KT_DBASE */
static uint32_t kt_t0[KT_NKEY];
static int32_t kt_ps[KT_NKEY];           /* the smoothed pressure, Q8 of 0..127 */
static uint8_t kt_on[KT_NKEY], kt_p[KP_NKEY];

/* key k's pressure now (0..127), from its driven readings; called every KT_AT_MS for every measured key */
static uint8_t kt_lean(uint32_t k, uint32_t now_ms)
{
    kt_key_t *x = &kt_key[k];
    int32_t d8, lean, span, xq;
    if (x->st != KT_S_DOWN || !x->dn || !KT_HAS_AT(kt_mode)) {
        kt_on[k] = 0;
        kt_ps[k] = 0;
        return 0;
    }
    d8 = (int32_t)((x->d > KT_DBASE + 1u ? x->d - KT_DBASE : 1u) << 8);
    if (!kt_on[k]) {
        kt_on[k] = 1;
        kt_base8[k] = d8;
        kt_t0[k] = now_ms;
    }
    if (d8 > kt_base8[k])                          /* eased off: that is the rest now */
        kt_base8[k] = d8;
    else if (now_ms - kt_t0[k] < pat_cfg.settle)   /* settling: follow it down (GLO > TOUCH 5 SETTL: 250 ms) */
        kt_base8[k] += (d8 - kt_base8[k]) / 4;
    else                                           /* then creep (~1/1250 a step) */
        kt_base8[k] += (d8 - kt_base8[k]) / 1024;
    lean = kt_log2q8((uint32_t)kt_base8[k]) - kt_log2q8((uint32_t)d8) - (int32_t)pat_cfg.dead;   /* (TOUCH 5 DEAD: 10) */
    span = (int32_t)touch_cfg.at_sens * 256 / 100;  /* (Stallion) AT SENS 0.12 .. 0.8 octave, in Q8 */
    if (span < 8)
        span = 8;
    xq = lean <= 0 ? 0 : lean >= span ? 127 << 8 : (int32_t)(smk_shape((uint32_t)((lean << 10) / span), pat_cfg.shape) * 127u >> 2);
    {   /* smoothed: TOUCH 6 RISE / FALL (30 ms: 39 / 256 a 5 ms step, -5 / ln(1 - 39 / 256)) */
        int32_t tau = xq > kt_ps[k] ? (int32_t)pat_cfg.rise : (int32_t)pat_cfg.fall, g = 1280 * 2 / (2 * tau + 5);
        kt_ps[k] += (xq - kt_ps[k]) * (g < 1 ? 1 : g > 256 ? 256 : g) / 256;
    }
    return (uint8_t)((kt_ps[k] + 128) >> 8);
}

/* ------------------------------------------------------------------ the settings (Stallion: touch.h touch_cal) */
/* the calibration kept (touch.h touch_cal: what TOUCH CAL found, never the ends learnt since, which last until power-off
 * as in ESTAMPIE; a settings save does not follow the learning) into c */
static void kt_cal_store(touch_cal_t *c) { *c = touch_cal; }
/* a calibration read back (the settings at power-on): its ends, learnt slowly from the start; none: the defaults */
static void kt_cal_take(const touch_cal_t *c)
{
    touch_cal_t t = *c;
    uint32_t i;
    kt_defaults();
    memset(&touch_cal, 0, sizeof touch_cal);
    if (!t.cal)
        return;
    for (i = 0; i < KT_NKEY; i++) {
        uint32_t fl = t.floor_[i], cl = t.ceil[i];
        if (fl < KT_DBASE || fl > 1023u || cl > 1023u)
            fl = KT_FLOOR0, cl = KT_CEIL0;             /* (a value no landing gives: that key's defaults) */
        kt_ends(&kt_key[i], fl, cl);
        kt_nland[i] = KT_LEARN_N;
        touch_cal.floor_[i] = kt_key[i].floor_;
        touch_cal.ceil[i] = kt_key[i].ceil;
    }
    touch_cal.cal = 1;
    kt_calibrated = 1;
}

/* ------------------------------------------------------------------ calibration (ui_synth.c touch_cal_*) */
static void kt_cal_begin(void)
{
    memset(kt_cal_n, 0, sizeof kt_cal_n);
    kt_cal_lastk = 0xFFu;
    kt_cal_phase = 1;
}
/* (Stallion) TOUCH CAL's meter: how hard a reading presses, 0 (open, KT_CAL_SOFT) .. 127 (KT_CAL_HARD), on the log
 * scale velocity uses, with fixed ends wider than any key's (Velo: the lightest landings 742..987) so every key reads
 * on the same scale */
#define KT_CAL_SOFT 1000u
#define KT_CAL_HARD 260u
static uint32_t kt_cal_meter(uint32_t d)
{
    return d >= KT_CAL_SOFT ? 0u : (kt_velocity(d, KT_CAL_HARD, KT_CAL_SOFT) - KT_VMIN) * 127u / (127u - KT_VMIN);
}
/* the measured key held now (its driven reading in *d), or -1 */
static int32_t kt_held(uint32_t *d)
{
    uint32_t k;
    for (k = 0; k < KT_NKEY; k++)
        if (kt_key[k].st != KT_S_OPEN && kt_key[k].dn) {
            *d = kt_key[k].d;
            return (int32_t)k;
        }
    return -1;
}
/* the keys whose gentle and firm medians came out closer than KT_SPAN (their range is a guess: kt_ends parts them) */
static uint32_t kt_cal_close;
/* how many presses phase ph (1 gentle, 2 firm) still wants of key k */
static uint32_t kt_cal_left(uint32_t ph, uint32_t k) { return KT_CAL_N - kt_cal_n[ph - 1u][k]; }
static uint32_t kt_cal_median(const uint16_t *v)
{
    uint32_t a = v[0], b = v[1], c = v[2];
    return a > b ? (b > c ? b : a > c ? c : a) : (a > c ? a : b > c ? c : b);
}
/* the presses in: every key's ends from the medians (a key with too few keeps its own), calibrated */
static void kt_cal_finish(void)
{
    uint32_t k;
    kt_cal_phase = 0;
    kt_cal_close = 0;
    for (k = 0; k < KT_NKEY; k++) {
        uint32_t fl = kt_key[k].floor_, cl = kt_key[k].ceil;
        if (kt_cal_n[0][k] == KT_CAL_N)
            cl = kt_cal_median(kt_cal_v[0][k]);
        if (kt_cal_n[1][k] == KT_CAL_N)
            fl = kt_cal_median(kt_cal_v[1][k]);
        if (kt_cal_n[0][k] == KT_CAL_N && kt_cal_n[1][k] == KT_CAL_N && cl < fl + KT_SPAN)
            kt_cal_close |= 1u << k;                   /* (Stallion: TOUCH CAL names it) */
        kt_ends(&kt_key[k], fl, cl);
        kt_nland[k] = KT_LEARN_N;
        touch_cal.floor_[k] = kt_key[k].floor_;     /* (Stallion: what is kept) */
        touch_cal.ceil[k] = kt_key[k].ceil;
    }
    touch_cal.cal = 1;
    kt_calibrated = 1;
}
