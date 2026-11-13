/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* The update loader's screen: the boot logo (the longship and STALLION, where the boot splash puts it) over a thick
 * loading bar; only the bar moves. Included by loader.c (one compilation unit). Writing the flash comes first, so the
 * screen never waits long:
 *   - the panel is set up once, before USB starts (the app's sequence, src/lcd.c LCD_SEQ: after a power loss the
 *     panel starts from its reset, and a warm reset from the app leaves it as the app left it; both work), then the
 *     logo, the ground and the bar's frame and track are drawn (each transfer waited for, with a deadline);
 *   - then ls_tick (from ldr_poll) draws at most one change of the bar every LS_PERIOD ms: one rectangle by DMA that
 *     is NOT waited for; the next tick only checks that it finished;
 *   - every wait has a deadline in timer ticks (fm1_lcd_done, not fm1_lcd_wait's spin count): a transfer that does
 *     not finish switches the screen off for the rest of the session, and the update goes on without it.
 * The logo: tools/gen_logo.py (build/gen/ldr_logo.h, the boot splash's header: 16 colours, RLE runs of
 * (run - 1) << 4 | colour), decoded a band at a time. The bar: 30 pixels high in a 4-pixel sail-white frame, black
 * where it is still to go; oak while it writes, rust when the update failed (back to oak on a resumed session).
 * SMK-37 Pro (BOARD_SMK37): its ST7789V is 240 x 240 too, in the same orientation (MADCTL 0x00) and colour format, so
 * the picture and every window are the FM-1's; only the start-up table (the app's BOARD_SMK37 LCD_SEQ: voltages and
 * gamma as well) and the pins differ (hal/fm1_lcd_hw.h: SPI1 group A, no backlight control; the set-up used here
 * leaves the pad LEDs' line, PC10, as it was). */
#include "fm1_lcd_hw.h"
#include "ldr_logo.h"

#define LS_RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define LS_SWAP(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))      /* the panel takes the high byte first */
#define LS_BG SLOOP_SPLASH_BG                                  /* navy, the logo's ground */
#define LS_FG LS_RGB(243, 238, 226)                            /* sail white: the bar's frame */
#define LS_OAK LS_RGB(217, 142, 47)                            /* written */
#define LS_RUST LS_RGB(184, 67, 42)                            /* failed */
#define LS_TRACK LS_RGB(0, 0, 0)                               /* still to go */
#define LS_LY 8u                                               /* the logo's top row (as the boot splash) */
#define LS_FW 4u                                               /* the frame's thickness */
#define LS_BX 16u                                              /* the bar inside the frame */
#define LS_BW 208u
#define LS_BY 202u
#define LS_BH 30u
#define LS_BAND 28u                                            /* logo rows a transfer */
#define LS_PX (240u * LS_BAND)                                 /* the buffer: a band, or the whole bar */
#define LS_PERIOD 60u                                          /* ms between bar updates */
#if SLOOP_SPLASH_W != 240 || LS_LY + SLOOP_SPLASH_H + 6u > LS_BY - LS_FW || LS_BW * LS_BH > LS_PX
#error "ldr_screen.c: the logo, the bar and the buffer do not fit"
#endif

static uint16_t ls_px[LS_PX];
static uint8_t ls_cb[4];
static struct {
    uint8_t on, busy, failed, bar_failed;
    uint32_t bar, bar_want;                                    /* bar pixels drawn, wanted */
    uint32_t t, t_busy;                                        /* ticks: last update, transfer started */
} ls;

static int ls_wait(uint32_t us)          /* the running transfer done within us, else the screen is given up */
{
    uint32_t t0 = fm1_ticks();
    while (!fm1_lcd_done())
        if (fm1_ticks() - t0 > us * FM1_TICKS_PER_US) {
            ls.on = 0;
            return -1;
        }
    fm1_lcd_deselect();
    return 0;
}

static int ls_cmd(uint8_t c, const uint8_t *p, uint32_t n)
{
    fm1_lcd_send_cmd(c);
    if (ls_wait(200u))
        return -1;
    if (!n)
        return 0;
    fm1_lcd_send_data(p, n);
    return ls_wait(1000u);
}

static int ls_window(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h)   /* then RAMWR: pixels follow */
{
    uint32_t x1 = x0 + w - 1u, y1 = y0 + h - 1u;
    ls_cb[0] = 0;
    ls_cb[1] = (uint8_t)x0;
    ls_cb[2] = 0;
    ls_cb[3] = (uint8_t)x1;
    if (ls_cmd(0x2A, ls_cb, 4))
        return -1;
    ls_cb[1] = (uint8_t)y0;
    ls_cb[3] = (uint8_t)y1;
    if (ls_cmd(0x2B, ls_cb, 4))
        return -1;
    return ls_cmd(0x2C, 0, 0);
}

/* a rectangle in one colour, waited for (the start; ls_stop) */
static void ls_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, n, k = LS_PX;
    if (!ls.on || !w || !h || ls_window(x, y, w, h))
        return;
    c = LS_SWAP(c);
    for (i = 0; i < k && i < w * h; i++)
        ls_px[i] = c;
    for (n = w * h; n && ls.on; n -= k) {
        if (k > n)
            k = n;
        fm1_lcd_send_data(ls_px, k * 2u);
        fm1_wdt_feed();
        ls_wait(40000u);                                       /* 13 KB: ~10 ms at 12 MHz */
    }
}

/* the boot logo at LS_LY, a band at a time, each transfer waited for */
static void ls_logo(void)
{
    uint32_t i = 0, run = 0, c = 0, y, k, n;
    for (y = 0; y < SLOOP_SPLASH_H && ls.on; y += LS_BAND) {
        n = (SLOOP_SPLASH_H - y < LS_BAND ? SLOOP_SPLASH_H - y : LS_BAND) * 240u;
        for (k = 0; k < n; k++) {
            if (!run) {
                if (i < sizeof SLOOP_SPLASH_RLE) {
                    run = (SLOOP_SPLASH_RLE[i] >> 4) + 1u;
                    c = SLOOP_SPLASH_RLE[i++] & 15u;
                } else {
                    run = n;                                   /* (past the runs: the ground) */
                    c = 0;
                }
            }
            ls_px[k] = LS_SWAP(SLOOP_SPLASH_PAL[c]);
            run--;
        }
        if (ls_window(0, LS_LY + y, 240u, n / 240u))
            return;
        fm1_lcd_send_data(ls_px, n * 2u);
        fm1_wdt_feed();
        ls_wait(40000u);
    }
}

/* the panel set up as the app does it (src/lcd.c LCD_SEQ), the logo on navy, the bar's frame and track, display on */
static void ls_init(void)
{
#if BOARD_SMK37
    static const uint8_t SEQ[] = {                             /* src/lcd.c LCD_SEQ, BOARD_SMK37, without its waits */
        0x01, 0, 0x11, 0, 0x36, 1, 0x00, 0x3A, 1, 0x55,
        0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33, 0xB7, 1, 0x35, 0xBB, 1, 0x32, 0xC2, 1, 0x01, 0xC3, 1, 0x15,
        0xC4, 1, 0x20, 0xC6, 1, 0x0F, 0xD0, 2, 0xA4, 0xA1,
        0xE0, 14, 0xD0, 0x08, 0x0E, 0x09, 0x09, 0x05, 0x31, 0x33, 0x48, 0x17, 0x14, 0x15, 0x31, 0x34,
        0xE1, 14, 0xD0, 0x08, 0x0E, 0x09, 0x09, 0x05, 0x31, 0x33, 0x48, 0x17, 0x14, 0x15, 0x31, 0x34,
        0x21, 0, 0x13, 0};
    uint32_t r;
    fm1_lcd_hw_init_keep_pads();                               /* pins (not the pads'), SPI1 at the app's clock */
#else
    static const uint8_t SEQ[] = {0x01, 0, 0x11, 0, 0x3A, 1, 0x55, 0x36, 1, 0x00, 0x21, 0, 0x13, 0};
    uint32_t r;
    fm1_lcd_hw_init();                                         /* pins, backlight on, SPI1 at the app's clock */
#endif
    fm1_delay_ms(20);
    ls.on = 1;
    for (r = 0; r < sizeof SEQ && ls.on; r += 2u + SEQ[r + 1u]) {
        ls_cmd(SEQ[r], SEQ + r + 2u, SEQ[r + 1u]);
        if (SEQ[r] == 0x01 || SEQ[r] == 0x11)
            fm1_delay_ms(SEQ[r] == 0x01 ? 150u : 120u);        /* after SWRESET, after SLPOUT */
        fm1_wdt_feed();
    }
    ls_fill(0, 0, 240, LS_LY, LS_BG);                          /* every pixel once: ground, logo, ground */
    ls_logo();
    ls_fill(0, LS_LY + SLOOP_SPLASH_H, 240, 240u - LS_LY - SLOOP_SPLASH_H, LS_BG);
    ls_fill(LS_BX - LS_FW, LS_BY - LS_FW, LS_BW + 2u * LS_FW, LS_BH + 2u * LS_FW, LS_FG);
    ls_fill(LS_BX, LS_BY, LS_BW, LS_BH, LS_TRACK);
    if (ls.on)
        ls_cmd(0x29, 0, 0);                                    /* DISPON */
    ls.t = fm1_ticks();
}

/* how far the write is: the bar fills from the left */
static void ls_progress(uint32_t done, uint32_t total)
{
    if (!total)
        return;
    if (done > total)
        done = total;
    ls.bar_want = done / 64u * LS_BW / (total / 64u ? total / 64u : 1u);
}

static void ls_failed(int f) { ls.failed = (uint8_t)(f != 0); }

/* the bar's next change, one rectangle (all of the bar so far on a colour change): sent and, with wait, waited for;
 * 0 when there is nothing left to draw */
static int ls_bar(int wait)
{
    uint16_t c = ls.failed ? LS_RUST : LS_OAK;
    uint32_t x, w, i;
    if (ls.bar_failed != ls.failed) {
        ls.bar_failed = ls.failed;
        x = 0, w = ls.bar;
    } else if (ls.bar_want > ls.bar) {
        x = ls.bar, w = ls.bar_want - ls.bar;
        ls.bar = ls.bar_want;
    } else if (ls.bar_want < ls.bar) {                         /* another pass: from the start again */
        x = ls.bar_want, w = ls.bar - ls.bar_want, c = LS_TRACK;
        ls.bar = ls.bar_want;
    } else
        return 0;
    if (!w)
        return 1;
    c = LS_SWAP(c);
    for (i = 0; i < w * LS_BH; i++)
        ls_px[i] = c;
    if (ls_window(LS_BX + x, LS_BY, w, LS_BH))
        return 0;
    fm1_lcd_send_data(ls_px, w * LS_BH * 2u);
    if (wait)
        return ls_wait(40000u) == 0;
    ls.busy = 1;
    ls.t_busy = fm1_ticks();
    return 1;
}

/* from ldr_poll: returns at once unless an update is due (then short command waits; the pixels are not waited for) */
static void ls_tick(void)
{
    uint32_t now;
    if (!ls.on)
        return;
    now = fm1_ticks();
    if (ls.busy) {
        if (!fm1_lcd_done()) {
            if (now - ls.t_busy > 100u * 1000u * FM1_TICKS_PER_US)
                ls.on = 0;                                     /* the transfer never finished: no more screen */
            return;
        }
        fm1_lcd_deselect();
        ls.busy = 0;
    }
    if (now - ls.t < LS_PERIOD * 1000u * FM1_TICKS_PER_US)
        return;
    ls.t = now;
    ls_bar(0);
}

/* before a reset: the transfer in flight finished, then the bar brought up to date (bounded: at most three
 * rectangles, each with its deadline), so the panel is not left mid-write and shows where the update ended */
static void ls_stop(void)
{
    uint32_t i;
    if (ls.on && ls.busy)
        ls_wait(40000u);
    ls.busy = 0;
    for (i = 0; i < 3u && ls.on && ls_bar(1); i++)
        ;
}
