/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 input HAL: key/button/encoder matrix and LEDs.
 *
 * One 11-column x 6-row diode matrix behind a 2x74HC595 chain (PA4 SER, PA3
 * SRCLK, PA1 RCLK), bit-banged and polled; rows PA0, PA5..PA8, PB7 with
 * pull-ups (low = closed). LED lines PH6/PH9/PA9/PA10 light the LED on the
 * same column, row PA7/PA8/PA5/PA6 respectively.
 *
 *   fm1_input_init();
 *   polled:  for (;;) { fm1_input_scan(); ... }
 *   IRQ:     call fm1_input_tick() from a ~10 kHz timer ISR; it advances one
 *            column per call (rows are sampled one tick after the column was
 *            latched, LEDs stay lit in between) and processes a frame every
 *            FM1_NCOL ticks. The main loop reads fm1_in.notes / buttons and
 *            takes edges/steps with fm1_input_edges() / fm1_enc_take().
 *
 * fm1_input_scan() runs one full frame (11 columns, ~0.6 ms) and calls
 * FM1_INPUT_IDLE() while it waits.
 * Keys/buttons (as Felucca 1.0): debounced as soon as their column is read. A press counts after
 * FM1_DEB_PRESS frames closed in a row (1.1-2.2 ms: the matrix has diodes and no ghosting, so a closed
 * sample is a closed key; two in a row keep one stray sample from playing a note), a release after
 * FM1_DEB_RELEASE frames open in a row (~9 ms): a contact bouncing open on the way down, or chattering
 * on the way up, never ends a note early or plays it twice.
 * Encoders: quadrature decoder (+ = clockwise; 2.7.2: no 2-sample filter, below) with detent counting, as
 * Felucca 1.0 reworked it (its #23, "knobs skipping or jumping"): an FM-1 detent is one full
 * quadrature cycle (4 transitions) and the knob rests in one state, the one seen at power-on
 * (relearned only after FM1_REST_FRAMES, ~1 s, parked elsewhere). Steps are emitted on arriving
 * back at it, the net transitions rounded to whole cycles (>= 2 counts one: a lost transition or
 * two is forgiven; a two-state jump counts on in the direction of travel). One click = one step
 * at any speed; bounce and back-and-forth cancel out. Never a second rest state: a knob held
 * mid-click taught the complement as a rest and every click counted twice, and short mid-click
 * pauses of a slow turn taught the mid states and the knob went dead (the 0.9 learner).
 * fm1_enc_take() returns the steps.
 * LEDs: set fm1_led[col] (packed row bits, bit1 PA5..bit4 PA8); they are lit while that column is
 * selected (one tick, ~95 us a frame). Two dim layers (fm1_input_tick only), after Felucca 1.0.1 (its
 * #35: the eye is logarithmic, an LED lit 1/4 or 1/6 of the time reads as nearly lit): a short pulse on
 * every frame (~910 Hz, no flicker) at the start of the column's next tick, riding on the 595 shift of
 * the next column (its outputs change only at the latch, so the pulse stays on column p and the key
 * read before it is unchanged). fm1_led_dim[col]: the glow (landmarks, notes under tiles), FM1_GLOW_NS
 * (~1/24 of a lit LED); fm1_led_bg[col]: the backlight (menu LIGHTS), fm1_led_bg_ns, shorter. TIMER4
 * times the pulses bit by bit; only a pulse longer than the whole shift waits for the rest. An LED in
 * several layers takes the brightest. fm1_led_key/btn helpers address them by id.
 */
#pragma once
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_gpio.h"
#include "fm1_cc.h"
#include "fm1_adc.h"

/* Stallion: KEY TOUCH (src/key_touch.c), from ESTAMPIE (github.com/ocean/estampie hal/fm1_input.h at af5a871,
 * GPL-3.0-only), after Charles Vestal's Velo and MONK-1 (github.com/charlesvestal/fm1-velocity, fm1-monk,
 * GPL-3.0-only). With fm1_an.cfg set (fm1_an_set, the main loop) the tick also samples one converter input between two
 * ticks: right after it latches column n, the key of that column on row PA7 (even frames) or PA8 (odd frames), so
 * each of the 20 keys on those rows (notes 0..19) is read every two frames (~2.2 ms); in the slot of (odd frame,
 * column 0: PA8 there is OCT-, a button) the MASTER pot instead (the battery, behind its high-impedance divider, is
 * read the slow way by the main loop, the scan's sampling paused: main.c). The result is taken at the next tick's
 * start, with the lines dark, before the rows are read and before column p's glow pulse (a lit LED's current goes
 * through its column and lifts its keys' readings). FM1_INPUT_ANALOG(note, v, driven) gets each key's reading (1023
 * open, falling as the key's carbon pill is pressed harder); FM1_INPUT_DRIVE(note) asks whether to take it with the
 * row driven high by the pin's weak driver (HD0, Velo's DRIVE 2: the pill's resistance above the matrix diode, where
 * the 10k pull-up only shows the diode), restored before the rows are read; FM1_INPUT_LED_OK(note) 0 (a held key)
 * keeps its column's LEDs dark on the tick that samples it. Off (cfg 0) the tick is as before and the main loop owns
 * the converter (fm1_adc_read). */
#ifndef FM1_INPUT_ANALOG
#define FM1_INPUT_ANALOG(note, v, driven) ((void)0)
#endif
#ifndef FM1_INPUT_DRIVE
#define FM1_INPUT_DRIVE(note) 0
#endif
#ifndef FM1_INPUT_LED_OK
#define FM1_INPUT_LED_OK(note) 1
#endif

#ifndef FM1_INPUT_IDLE
#define FM1_INPUT_IDLE() ((void)0)
#endif
#ifndef FM1_LED_US
#define FM1_LED_US 40u           /* LED on-time per column (brightness vs scan rate) */
#endif
#define FM1_DEB_PRESS 2u          /* frames closed in a row: a press (a frame = 11 ticks, ~1.1 ms) */
#define FM1_DEB_RELEASE 8u        /* frames open in a row: a release (~9 ms) */
#define FM1_SETTLE_US 10u
#define FM1_REST_FRAMES 900u      /* ~1 s still off the detent state: that is the detent (power-on) */
#define FM1_NCOL 11u
#define FM1_NKEY 41u              /* ids: 0..13 buttons, 14..40 note keys */
#define FM1_NENC 7u
#define FM1_NBTN 14u              /* buttons: ids 0..13 */


/* key id at (physical column, packed row bit), -1 = none */
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PA0: encoders */
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},          /* PA5 */
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},          /* PA6 */
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},          /* PA7 */
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},          /* PA8 */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PB7: encoder 6 */
};
/* encoder i: A at (col, row bit), B at (col, row bit) */
static const uint8_t FM1_ENC[FM1_NENC][4] = {
    {0, 0, 1, 0}, {2, 0, 3, 0}, {8, 1, 9, 1}, {8, 0, 9, 0}, {6, 0, 7, 0}, {4, 0, 5, 0}, {0, 5, 1, 5},
};
enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };
#define FM1_OCT_BOTH (1u << FM1_BTN_OCT_DOWN | 1u << FM1_BTN_OCT_UP)

static const int8_t FM1_LED_PORT[6] = {-1, FM1_PA, FM1_PA, FM1_PH, FM1_PH, -1};
static const uint8_t FM1_LED_BIT[6] = {0, 9, 10, 6, 9, 0};

static volatile struct {
    uint32_t notes;              /* debounced: bit n = note key n (0 = F3 .. 26 = G5) */
    uint32_t buttons;            /* debounced: bit i = button i (0..13) */
    uint32_t pressed, released;  /* button edges since the last fm1_input_edges() */
    uint32_t notes_pressed;      /* note-key press edges since the last fm1_input_note_edges() */
    uint8_t raw[FM1_NCOL];       /* last frame, packed rows, 1 = closed */
    uint8_t cnt[FM1_NKEY];
    uint8_t enc_prev[FM1_NENC], enc_last[FM1_NENC];
    uint8_t enc_rest[FM1_NENC];  /* the detent state (0..3) */
    uint16_t enc_still[FM1_NENC]; /* frames since the last state change */
    int8_t enc_sub[FM1_NENC];    /* net transitions since the last rest state */
    int16_t enc_steps[FM1_NENC]; /* + = clockwise */
    uint32_t frames;
} fm1_in;
/* SLOOP 2.7: buttons held over MIDI (GLO > MIDI 3 > REMOTE: src/ui_input.c remote_poll), bit i = button i: read as
 * closed by the debounce, so a remote press is a press like any other (taps, holds, layers) */
static volatile uint32_t fm1_remote_btn;
static uint8_t fm1_led[FM1_NCOL];
#ifndef FM1_GLOW_NS
#define FM1_GLOW_NS 4000u        /* the glow pulse a frame (ns); a lit LED ~95 us: ~1/24 the brightness */
#endif
#define FM1__NS_T(ns) (((uint32_t)(ns) * FM1_TICKS_PER_US + 500u) / 1000u)   /* ns -> TIMER4 ticks */
static uint8_t fm1_led_dim[FM1_NCOL];   /* same layout as fm1_led: the glow */
static uint8_t fm1_led_bg[FM1_NCOL];    /* same layout: the backlight (labels readable in the dark) */
static volatile uint16_t fm1_led_bg_ns; /* the backlight pulse a frame (ns), 0 = off (menu LIGHTS) */

static void fm1__led_lines(uint32_t rowmask)
{
    uint32_t r;
    for (r = 1; r < 5u; r++) {
        if (rowmask & (1u << r))
            FM1_PR(FM1_LED_PORT[r], FM1_OUT) |= 1u << FM1_LED_BIT[r];
        else
            FM1_PR(FM1_LED_PORT[r], FM1_OUT) &= ~(1u << FM1_LED_BIT[r]);
    }
}

static void fm1__sr_bit(uint32_t w, uint32_t i)  /* bit i of w (msb first) into the 595 */
{
    if (w & (0x8000u >> i))
        FM1_PR(FM1_PA, FM1_OUT) |= 1u << 4;
    else
        FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 4);
    FM1_PR(FM1_PA, FM1_OUT) |= 1u << 3;         /* read-modify-write per edge: each SFR write is */
    FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 3);      /* far slower than the 595 needs */
}
static void fm1__sr_latch(void)                 /* the outputs change here only */
{
    FM1_PR(FM1_PA, FM1_OUT) |= 1u << 1;
    FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 1);
}
static void fm1__sr_word(uint32_t w)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        fm1__sr_bit(w, i);
    fm1__sr_latch();
}

static uint32_t fm1__rows(void)
{
    uint32_t a = FM1_PR(FM1_PA, FM1_IN), b = FM1_PR(FM1_PB, FM1_IN);
    return (~((a & 1u) | ((a >> 4) & 0x1Eu) | ((b >> 2) & 0x20u))) & 0x3Fu;
}

/* KEY TOUCH (see top) */
static struct {
    volatile uint16_t cfg;       /* FM1_ADC_CFG; 0: off, the main loop owns the converter */
    uint8_t pend, col, rowbit;   /* a conversion runs for (col, rowbit); rowbit 0: the pot */
    uint8_t ch, driven, phase;
    volatile uint16_t master;    /* the pot's last reading (0: none yet) */
    uint32_t timeouts;           /* conversions not done by the next tick */
} fm1_an;
/* row PA7 (rowbit 3) / PA8 (4) driven high by its weak driver, and back to an input pulled up */
static void fm1__row_drive(uint32_t rowbit)
{
    uint32_t m = 1u << (rowbit == 3u ? 7 : 8);
    FM1_PR(FM1_PA, FM1_HD0) |= m;
    FM1_PR(FM1_PA, FM1_HD) &= ~m;
    FM1_PR(FM1_PA, FM1_OUT) |= m;
    FM1_PR(FM1_PA, FM1_DIR) &= ~m;
}
static void fm1__row_release(uint32_t rowbit)
{
    uint32_t m = 1u << (rowbit == 3u ? 7 : 8);
    FM1_PR(FM1_PA, FM1_DIR) |= m;
    FM1_PR(FM1_PA, FM1_OUT) &= ~m;
    FM1_PR(FM1_PA, FM1_HD0) &= ~m;
    FM1_PR(FM1_PA, FM1_HD) &= ~m;
}
/* the conversion started last tick: its row released, its result to its owner */
static __attribute__((noinline)) void fm1__an_take(void)   /* (out of the ISR's body: tests/target_budget.py) */
{
    uint32_t driven = fm1_an.driven, v;
    int id;
    fm1_an.pend = fm1_an.driven = 0;
    if (driven)
        fm1__row_release(fm1_an.rowbit);
    if (!fm1_adc_done()) {
        fm1_adc_abort();
        fm1_an.timeouts++;
        return;
    }
    v = fm1_adc_take();
    if (!fm1_an.rowbit) {
        fm1_an.master = (uint16_t)(v ? v : 1u);    /* (0 means none yet) */
    } else if ((id = FM1_KEYMAP[fm1_an.rowbit][fm1_an.col]) >= 14) {
        FM1_INPUT_ANALOG((uint32_t)id - 14u, v, (int)driven);
    }
}
/* column n is latched and lit: start its sample (see top) */
static __attribute__((noinline)) void fm1__an_start(uint32_t n)
{
    uint32_t rb, ch;
    int id;
    if (n == 0u)
        fm1_an.phase ^= 1u;
    if (fm1_an.phase && n == 0u) {
        rb = 0;
        ch = FM1_ADC_MASTER;
    } else {
        rb = fm1_an.phase ? 4u : 3u;
        ch = fm1_an.phase ? FM1_ADC_ROW_PA8 : FM1_ADC_ROW_PA7;
    }
    fm1_an.col = (uint8_t)n;
    fm1_an.rowbit = (uint8_t)rb;
    fm1_an.ch = (uint8_t)ch;
    id = rb ? FM1_KEYMAP[rb][n] : -1;
    if (id >= 14) {
        if (!FM1_INPUT_LED_OK((uint32_t)id - 14u))
            fm1__led_lines(0);                     /* a held key: no LED current in its column while it is read */
        if (FM1_INPUT_DRIVE((uint32_t)id - 14u)) {
            fm1__row_drive(rb);
            fm1_an.driven = 1;
        }
    }
    fm1_adc_start(ch, fm1_an.cfg);
    fm1_an.pend = 1;
}

static void fm1__wait(uint32_t us)
{
    uint32_t t0 = fm1_ticks(), span = us * FM1_TICKS_PER_US;
    while ((uint32_t)(fm1_ticks() - t0) < span)
        FM1_INPUT_IDLE();
}

static void fm1_input_init(void)
{
    static const uint8_t LEDP[4][2] = {{FM1_PH, 6}, {FM1_PH, 9}, {FM1_PA, 9}, {FM1_PA, 10}};
    const uint32_t rows_a = (1u << 0) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8);
    const uint32_t sr = (1u << 1) | (1u << 3) | (1u << 4), row_b = 1u << 7;
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        uint32_t p = LEDP[i][0], m = 1u << LEDP[i][1];
        FM1_PR(p, FM1_DIE) |= m;
        FM1_PR(p, FM1_OUT) &= ~m;
        FM1_PR(p, FM1_DIR) &= ~m;
        FM1_PR(p, FM1_HD0) |= m;
        FM1_PR(p, FM1_HD) |= m;
    }
    FM1_PR(FM1_PA, FM1_DIE) |= rows_a;
    FM1_PR(FM1_PA, FM1_DIR) |= rows_a;
    FM1_PR(FM1_PA, FM1_PD) &= ~rows_a;
    FM1_PR(FM1_PA, FM1_PU) |= rows_a;
    FM1_PR(FM1_PB, FM1_DIE) |= row_b;
    FM1_PR(FM1_PB, FM1_DIR) |= row_b;
    FM1_PR(FM1_PB, FM1_PD) &= ~row_b;
    FM1_PR(FM1_PB, FM1_PU) |= row_b;
    FM1_PR(FM1_PA, FM1_DIE) |= sr;
    FM1_PR(FM1_PA, FM1_PU) &= ~sr;
    FM1_PR(FM1_PA, FM1_PD) &= ~sr;
    FM1_PR(FM1_PA, FM1_OUT) &= ~sr;
    FM1_PR(FM1_PA, FM1_DIR) &= ~sr;
    fm1__sr_word(0xFFFFu);
    for (i = 0; i < FM1_NENC; i++)
        fm1_in.enc_prev[i] = fm1_in.enc_last[i] = 0xFF;   /* seeded by the first frame */
}

static void fm1__key(uint32_t id, uint32_t closed)
{
    volatile uint8_t *c = &fm1_in.cnt[id];
    uint32_t note = id >= 14u, bit = note ? 1u << (id - 14u) : 1u << id;
    uint32_t on = ((note ? fm1_in.notes : fm1_in.buttons) & bit) != 0u;
    if (!note && (fm1_remote_btn & bit))
        closed = 1;                                /* (2.7) held over MIDI */
    if (closed == on) {                            /* agrees with the state: start over */
        *c = 0;
        return;
    }
    if (++*c < (on ? FM1_DEB_RELEASE : FM1_DEB_PRESS))
        return;
    *c = 0;
    if (note) {
        if (!on)
            fm1_in.notes_pressed |= bit;
        fm1_in.notes ^= bit;
    } else {
        fm1_in.buttons ^= bit;
        if (!on)
            fm1_in.pressed |= bit;
        else
            fm1_in.released |= bit;
    }
}

static void fm1__keys(uint32_t p)                  /* the keys of column p, just read */
{
    uint32_t r, raw = fm1_in.raw[p];
    for (r = 1; r < 5u; r++)
        if (FM1_KEYMAP[r][p] >= 0)
            fm1__key((uint32_t)FM1_KEYMAP[r][p], (raw >> r) & 1u);
}

static void fm1__frame(void);

static void fm1_input_scan(void)
{
    uint32_t p;
    for (p = 0; p < FM1_NCOL; p++) {
        fm1__led_lines(0);
        fm1__sr_word(0xFFFFu ^ (1u << p) ^ (p < 2u ? 1u << (11u + p) : 0u));
        fm1__wait(FM1_SETTLE_US);
        fm1_in.raw[p] = (uint8_t)fm1__rows();
        fm1__keys(p);
        fm1__led_lines(fm1_led[p]);
        fm1__wait(FM1_LED_US);
    }
    fm1__led_lines(0);
    fm1__frame();
}

static void fm1__frame(void)
{
    uint32_t e;                                    /* (the keys: fm1__keys, as each column is read) */
    for (e = 0; e < FM1_NENC; e++) {               /* quadrature decoder + detents */
        const uint8_t *m = FM1_ENC[e];
        uint32_t cur = ((fm1_in.raw[m[0]] >> m[1]) & 1u) << 1 | ((fm1_in.raw[m[2]] >> m[3]) & 1u);
        uint32_t idx;
        volatile int8_t *sub = &fm1_in.enc_sub[e];
        if (cur != fm1_in.enc_last[e]) {           /* (2.7.2: no 2-frame filter. A state had to be seen on two frames
                                                    * running, ~2.2 ms, to count: in a fast turn the mid states of a
                                                    * click go by quicker, the click was lost and the value slowed down
                                                    * or stopped. A frame (~1.1 ms) outlasts a contact bounce, and a step
                                                    * still needs >= 2 net transitions back on the detent) */
            fm1_in.enc_last[e] = (uint8_t)cur;
            fm1_in.enc_still[e] = 0;
        }
        if (fm1_in.enc_prev[e] == 0xFF) {          /* first frame: the knob rests here */
            fm1_in.enc_prev[e] = (uint8_t)cur;
            fm1_in.enc_rest[e] = (uint8_t)cur;
        }
        if (fm1_in.enc_still[e] < 0xFFFFu && ++fm1_in.enc_still[e] == FM1_REST_FRAMES &&
            cur != fm1_in.enc_rest[e]) {
            fm1_in.enc_rest[e] = (uint8_t)cur;     /* parked ~1 s off the detent state (held at power-on): */
            *sub = 0;                              /* that is the detent. Never a second state (see top) */
        }
        if (cur == fm1_in.enc_prev[e])
            continue;
        idx = (uint32_t)fm1_in.enc_prev[e] << 2 | cur;
        if ((0x4182u >> idx) & 1u)
            (*sub)++;
        else if ((0x2814u >> idx) & 1u)
            (*sub)--;
        else if (*sub > 0)                         /* two states in one sample: a fast turn, */
            *sub = (int8_t)(*sub + 2);             /* the way it was going */
        else if (*sub < 0)
            *sub = (int8_t)(*sub - 2);
        fm1_in.enc_prev[e] = (uint8_t)cur;
        if (*sub > 100 || *sub < -100)
            *sub = 0;                              /* (never off the detent that long) */
        if (cur == fm1_in.enc_rest[e]) {           /* back on the detent: whole cycles, a lost transition */
            int32_t n = *sub < 0 ? -*sub : *sub;   /* or two forgiven (one click = 4 transitions) */
            n = n >= 2 ? (n + 2) / 4 : 0;
            fm1_in.enc_steps[e] = (int16_t)(fm1_in.enc_steps[e] + (*sub < 0 ? -n : n));
            *sub = 0;
        }
    }
    fm1_in.frames++;
}

/* one column per call, from a timer ISR (see top). The dim pulses of column p ride on the shift of
 * column n: the lines go lit | glow | backlight of p, each layer ends when its time is up (TIMER4,
 * checked after every bit), and the latch comes with the lines dark. */
static uint8_t fm1__tick_col;
static void fm1_input_tick(void)
{
    uint32_t p = fm1__tick_col, n = p + 1u == FM1_NCOL ? 0u : p + 1u, i;
    uint32_t w = 0xFFFFu ^ (1u << n) ^ (n < 2u ? 1u << (11u + n) : 0u);
    uint32_t lit = fm1_led[p], a = fm1_led_dim[p] & ~lit, b = fm1_led_bg[p] & ~lit & ~a;
    uint32_t gns = fm1_led_bg_ns * 3u / 2u > FM1_GLOW_NS ? fm1_led_bg_ns * 3u / 2u : FM1_GLOW_NS;   /* (the glow stays
                                                                 * over a bright backlight: 1.5 x; the wait is the longest) */
    uint32_t ta = a ? FM1__NS_T(gns) : 0u, tb = b ? FM1__NS_T(fm1_led_bg_ns) : 0u;
    uint32_t tmax = ta > tb ? ta : tb;
    fm1__led_lines(0);
    if (fm1_an.pend)                               /* KEY TOUCH: last tick's sample, before the rows and the glow */
        fm1__an_take();
    fm1_in.raw[p] = (uint8_t)fm1__rows();          /* column p has been latched one tick (the lines dark) */
    if (tmax) {
        uint32_t t0 = fm1_ticks(), cur = lit | a | (tb ? b : 0u), on, d;
        fm1__led_lines(cur);                       /* (the 595 still drives column p) */
        for (i = 0; i < 16u || cur; i++) {         /* the shift; then wait if the pulse is longer */
            if (i < 16u)
                fm1__sr_bit(w, i);
            d = fm1_ticks() - t0;
            on = d < tmax ? lit | (d < ta ? a : 0u) | (d < tb ? b : 0u) : 0u;
            if (on != cur) {
                fm1__led_lines(on);
                cur = on;
            }
        }
        fm1__sr_latch();                           /* column n, the lines dark */
    } else {
        fm1__sr_word(w);
    }
    fm1__led_lines(fm1_led[n]);
    fm1__tick_col = (uint8_t)n;
    fm1__keys(p);                                  /* its keys now: no wait for the frame's end */
    if (n == 0u)
        fm1__frame();
    if (fm1_an.cfg)                                /* KEY TOUCH: column n's sample, to the next tick */
        fm1__an_start(n);
}

/* main-loop critical section against fm1_input_tick (main loop only: it
 * re-enables interrupts unconditionally) */
static inline uint32_t fm1__lock(void)
{
    __asm__ volatile("cli" ::: "memory");
    return 0;
}
static inline void fm1__unlock(uint32_t v)
{
    (void)v;
    __asm__ volatile("csync\n\tsti" ::: "memory");
}

/* KEY TOUCH: the tick samples the keys (cfg: FM1_ADC_CFG_FAST) or not (0: the main loop has the converter back). Main
 * loop only; a conversion under way is dropped and its row released */
static void fm1_an_set(uint32_t cfg)
{
    uint32_t k = fm1__lock();
    if (fm1_an.pend) {
        fm1_adc_abort();
        if (fm1_an.driven)
            fm1__row_release(fm1_an.rowbit);
        fm1_an.pend = fm1_an.driven = 0;
    }
    fm1_an.cfg = (uint16_t)cfg;
    if (!cfg)
        fm1_an.master = 0;
    fm1__unlock(k);
}

/* detent steps turned since the last call, + = clockwise */
static int32_t fm1_enc_take(uint32_t e)
{
    uint32_t k = fm1__lock();
    int32_t s = fm1_in.enc_steps[e];
    fm1_in.enc_steps[e] = 0;
    fm1__unlock(k);
    return s;
}

static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.pressed;
    if (released)
        *released = fm1_in.released;
    fm1_in.pressed = fm1_in.released = 0;
    fm1__unlock(k);
    return p;
}

static uint32_t fm1_input_note_edges(void)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.notes_pressed;
    fm1_in.notes_pressed = 0;
    fm1__unlock(k);
    return p;
}

/* LED of key id (button 0..13 or note key 14..40) */
static void fm1_led_key(uint32_t id, int on)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on)
                    fm1_led[p] |= (uint8_t)(1u << r);
                else
                    fm1_led[p] &= (uint8_t)~(1u << r);
            }
}
