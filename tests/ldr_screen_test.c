/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* Host test of the update loader's screen (firmware/loader/ldr_screen.c, build/gen/ldr_logo.h) on a fake panel:
 * a simulated clock (one timer tick per fm1_ticks() call, plus fm1_delay_ms), an SPI that finishes a transfer after
 * its bytes at ~9.6 MHz (or never: a dead panel), and a 240 x 240 frame buffer fed by CASET / RASET / RAMWR.
 *   - the logo: the boot splash's runs (240 wide, every pixel of it);
 *   - a healthy panel: the logo where the boot splash puts it, on navy, the bar's thick sail-white frame and black
 *     track; the bar fills from the left, never back, to full, in oak (rust on a failure, back to the track on a
 *     resumed session); nothing but the bar ever changes; nothing sent when nothing changes; no transfer started
 *     while one runs; short ls_tick calls; ls_stop brings the bar up to date;
 *   - a dead panel at the start, one that dies in its start-up table, or one that dies mid-update: the screen
 *     switches itself off within its deadlines and sends nothing more; nothing waits longer than those deadlines;
 *   - the start-up table is the app's (src/lcd.c LCD_SEQ, included here): the same commands and data in the same
 *     order, each wait at least the app's, then straight to the first full-width window, and every pixel of the
 *     panel written.
 * Built twice (tests/run_tests.sh): as the FM-1 and with -DBOARD_SMK37=1, the SMK-37 Pro (its own ST7789V table;
 * the loader must leave the pad LEDs' line alone: tests/ldr_lcd_pins_test.c checks that on the registers).
 * LDR_SCREEN_DIR=DIR: the panel as DIR/ldr-000.ppm, ldr-050.ppm, ldr-100.ppm and ldr-failed.ppm. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef BOARD_SMK37
#define BOARD_SMK37 0
#endif

/* ---- the clock, the watchdog (loader.c gets them from fm1_time.h, fm1_sys.h) */
#define FM1_TICKS_PER_US 24u
static uint64_t clk;
static uint32_t fm1_ticks(void) { return (uint32_t)clk++; }
static void fm1_delay_ms(uint32_t ms) { clk += (uint64_t)ms * 1000u * FM1_TICKS_PER_US; }
static void fm1_wdt_feed(void) {}

/* ---- the fake panel */
#define NEVER (~(uint64_t)0)
#define TICKS_PER_BYTE 20u                         /* 8 bits at ~9.6 MHz */
static uint16_t fb[240][240];
static uint64_t xfer_end;
static int dead, cs_low;
static uint8_t cur_cmd;
static uint32_t wx0, wx1, wy0, wy1, px, py;
static uint32_t sends, overlaps, dispon, bad_window;
static uint32_t die_after;                         /* the panel stops answering at this send (0: never) */
static uint32_t hw_init_app;                       /* which pin set-up ran (SMK: and anything sent to the pads) */
#if BOARD_SMK37
static uint32_t hw_init_keep, pads;
#endif
static uint8_t slog[512];                          /* the start-up as sent: cmd, n, n data bytes (logging set) */
static uint64_t slog_t[64];                        /* when each command went out */
static uint32_t slog_n, slog_cmds, slog_at, logging;

static void xfer(uint32_t bytes)
{
    if (clk < xfer_end && xfer_end != NEVER)
        overlaps++;                                /* a transfer started over a running one */
    sends++;
    if (die_after && sends >= die_after)
        dead = 1;
    cs_low = 1;
    xfer_end = dead ? NEVER : clk + (uint64_t)bytes * TICKS_PER_BYTE;
}

#if BOARD_SMK37
/* the app's set-up drives the pad LEDs' line (PC10) as well: the loader must use the one that leaves it alone */
static void fm1_lcd_hw_init(void) { xfer_end = 0; hw_init_app++; pads++; }
static void fm1_lcd_hw_init_keep_pads(void) { xfer_end = 0; hw_init_keep++; }
static void fm1_padled_send(const void *p, uint32_t n) { (void)p; (void)n; pads++; }
static void fm1_padled_done(void) { pads++; }
#else
static void fm1_lcd_hw_init(void) { xfer_end = 0; hw_init_app++; }
#endif
static void fm1_lcd_wait(void) {}                  /* (src/lcd.c, included for its LCD_SEQ only) */
static void fm1_lcd_baud(uint32_t b) { (void)b; }static int fm1_lcd_done(void) { return xfer_end != NEVER && clk >= xfer_end; }
static void fm1_lcd_deselect(void) { cs_low = 0; }
static void fm1_lcd_send_cmd(uint8_t c)
{
    xfer(1);
    cur_cmd = c;
    if (logging && slog_n + 2u <= sizeof slog && slog_cmds < 64u) {
        slog_t[slog_cmds++] = clk;
        slog_at = slog_n;
        slog[slog_n++] = c;
        slog[slog_n++] = 0;
    }
    if (c == 0x29)
        dispon++;
    if (c == 0x2C)
        px = wx0, py = wy0;
}
static void fm1_lcd_send_data(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    uint32_t i;
    xfer(n);
    if (logging && cur_cmd != 0x2C)
        for (i = 0; i < n && slog_n < sizeof slog; i++) {
            slog[slog_n++] = b[i];
            slog[slog_at + 1u]++;
        }
    if (dead)
        return;
    if ((cur_cmd == 0x2A || cur_cmd == 0x2B) && n == 4) {
        uint32_t a = (uint32_t)b[0] << 8 | b[1], e = (uint32_t)b[2] << 8 | b[3];
        if (a > e || e > 239u)
            bad_window++;
        if (cur_cmd == 0x2A)
            wx0 = a, wx1 = e;
        else
            wy0 = a, wy1 = e;
    } else if (cur_cmd == 0x2C) {
        for (i = 0; i + 1u < n; i += 2u) {         /* the panel takes the high byte first */
            if (py <= wy1)
                fb[py][px] = (uint16_t)(b[i] << 8 | b[i + 1u]);
            if (++px > wx1) {
                px = wx0;
                py++;
            }
        }
    }
}

#include "../firmware/loader/ldr_screen.c"
#include "../firmware/src/lcd.c"                   /* the app's LCD_SEQ, for this board */

static int check(const char *what, int ok) { printf("%-72s %s\n", what, ok ? "ok" : "FAIL"); return ok ? 0 : 1; }
static uint32_t ms(uint64_t t) { return (uint32_t)(t / (1000u * FM1_TICKS_PER_US)); }

static uint64_t tick_max;                          /* the longest ls_tick call, in ticks */
static void run(uint32_t total_ms)                 /* ldr_poll's cadence: ls_tick every ~100 us */
{
    uint64_t end = clk + (uint64_t)total_ms * 1000u * FM1_TICKS_PER_US;
    while (clk < end) {
        uint64_t t0 = clk;
        ls_tick();
        if (clk - t0 > tick_max)
            tick_max = clk - t0;
        clk += 100u * FM1_TICKS_PER_US;
    }
}

static int bar_is(uint16_t c, uint32_t from, uint32_t to)
{
    uint32_t x, y;
    for (y = LS_BY; y < LS_BY + LS_BH; y++)
        for (x = LS_BX + from; x < LS_BX + to; x++)
            if (fb[y][x] != c)
                return 0;
    return 1;
}

/* how much of the bar is filled: oak (or c) from the left, then the track to the end, in every row; -1 if not so */
static int32_t bar_len(uint16_t c)
{
    uint32_t x, y, n = 0;
    while (n < LS_BW && fb[LS_BY][LS_BX + n] == c)
        n++;
    for (y = LS_BY; y < LS_BY + LS_BH; y++)
        for (x = 0; x < LS_BW; x++)
            if (fb[y][LS_BX + x] != (x < n ? c : LS_TRACK))
                return -1;
    return (int32_t)n;
}

static int in_bar(uint32_t x, uint32_t y)
{
    return x >= LS_BX && x < LS_BX + LS_BW && y >= LS_BY && y < LS_BY + LS_BH;
}

static uint16_t snap[240][240];                    /* the panel after the start */
static uint32_t changed_outside_bar(void)
{
    uint32_t x, y, n = 0;
    for (y = 0; y < 240u; y++)
        for (x = 0; x < 240u; x++)
            n += !in_bar(x, y) && fb[y][x] != snap[y][x];
    return n;
}

static uint16_t logo[SLOOP_SPLASH_H * SLOOP_SPLASH_W];
static uint32_t logo_px;                           /* pixels the runs make */
static void logo_decode(void)
{
    uint32_t i, r;
    for (i = 0; i < sizeof SLOOP_SPLASH_RLE; i++)
        for (r = 0; r <= (SLOOP_SPLASH_RLE[i] >> 4); r++, logo_px++)
            if (logo_px < SLOOP_SPLASH_H * SLOOP_SPLASH_W)
                logo[logo_px] = SLOOP_SPLASH_PAL[SLOOP_SPLASH_RLE[i] & 15u];
}

static void reset(int panel_dead)
{
    memset(&ls, 0, sizeof ls);
    memset(fb, 0xA5, sizeof fb);                   /* (0xA5A5: no colour the loader draws) */
    dead = panel_dead;
    xfer_end = 0;
    sends = overlaps = dispon = bad_window = die_after = 0;
    slog_n = slog_cmds = 0;
    tick_max = 0;
}

/* (screenshots) LDR_SCREEN_DIR=DIR: the panel as DIR/NAME.ppm at a few moments of the healthy update, for the site */
static void dump(const char *name)
{
    const char *dir = getenv("LDR_SCREEN_DIR");
    char path[512];
    FILE *f;
    uint32_t x, y;
    if (!dir)
        return;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    fprintf(f, "P6\n240 240\n255\n");
    for (y = 0; y < 240u; y++)
        for (x = 0; x < 240u; x++) {
            uint16_t p = fb[y][x];
            uint8_t rgb[3] = {(uint8_t)((p >> 11) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63),
                              (uint8_t)((p & 31) * 255 / 31)};
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

int main(void)
{
    const uint32_t total = 0x93000u - 0x4000u;
    uint32_t i, done, n_before;
    int32_t len, last = 0;
    uint64_t t0;
    int bad = 0, mono = 1, half_dumped = 0;

    /* the logo */
    logo_decode();
    printf("  logo: %ux%u, %u B of runs, %u colours\n", SLOOP_SPLASH_W, SLOOP_SPLASH_H,
           (unsigned)sizeof SLOOP_SPLASH_RLE, 16u);
    bad += check("ldr_logo.h: the boot splash, 240 wide, the runs make every pixel of it, no more",
                 SLOOP_SPLASH_W == 240u && logo_px == SLOOP_SPLASH_H * SLOOP_SPLASH_W);
    bad += check("the logo, the bar's frame and the bar on the panel, not overlapping",
                 LS_LY + SLOOP_SPLASH_H < LS_BY - LS_FW && LS_BY + LS_BH + LS_FW <= 240u &&
                 LS_BX >= LS_FW && LS_BX + LS_BW + LS_FW <= 240u);
    bad += check("the bar thick: at least 24 pixels high, a frame of at least 4, 200 or more wide",
                 LS_BH >= 24u && LS_FW >= 4u && LS_BW >= 200u);

    /* a healthy panel */
    reset(0);
    t0 = clk;
    logging = 1;
    ls_init();
    logging = 0;
    printf("  init: %u ms, %u transfers\n", ms(clk - t0), sends);
    {                                              /* the app's table, waits (cmd 0x00) taken out, = what was sent */
        uint8_t want[sizeof LCD_SEQ];
        uint32_t r, k = 0, c = 0, waits_ok = 1, wait = 0;
        for (r = 0; r < sizeof LCD_SEQ; r += 2u + LCD_SEQ[r + 1u]) {
            if (LCD_SEQ[r] == 0x00u) {             /* the next command at least this long after the last one */
                wait = LCD_SEQ[r + 2u];
                continue;
            }
            if (wait && c && c < slog_cmds && ms(slog_t[c] - slog_t[c - 1u]) < wait)
                waits_ok = 0;
            wait = 0;
            memcpy(want + k, LCD_SEQ + r, 2u + LCD_SEQ[r + 1u]);
            k += 2u + LCD_SEQ[r + 1u];
            c++;
        }
        printf("  start-up: %u commands, %u bytes (the app's table: %u commands)\n", slog_cmds, slog_n, c);
        bad += check("start-up: the app's LCD_SEQ for this board, every command and byte, in order",
                     slog_n > k && !memcmp(slog, want, k));
        bad += check("start-up: each of the app's waits kept (after SWRESET, after SLPOUT)", waits_ok);
        bad += check("start-up: then a full-width window (CASET 0..239), nothing else in between",
                     slog_n >= k + 6u && slog[k] == 0x2A && slog[k + 1u] == 4u && slog[k + 2u] == 0 &&
                     slog[k + 3u] == 0 && slog[k + 4u] == 0 && slog[k + 5u] == 239u);
    }
#if BOARD_SMK37
    bad += check("SMK: the pin set-up that leaves the pad LEDs' line alone, nothing sent to the pads",
                 hw_init_keep == 1 && hw_init_app == 0 && pads == 0);
#else
    bad += check("FM-1: the app's pin set-up", hw_init_app == 1);
#endif
    {
        uint32_t x, y, left = 0, logo_bad = 0, frame_bad = 0, ground_bad = 0;
        for (y = 0; y < 240u; y++)
            for (x = 0; x < 240u; x++) {
                uint16_t p = fb[y][x];
                int in_logo = y >= LS_LY && y < LS_LY + SLOOP_SPLASH_H;
                int in_frame = x + LS_FW >= LS_BX && x < LS_BX + LS_BW + LS_FW && y + LS_FW >= LS_BY &&
                               y < LS_BY + LS_BH + LS_FW && !in_bar(x, y);
                left += p == 0xA5A5u;
                if (in_logo)
                    logo_bad += p != logo[(y - LS_LY) * 240u + x];
                else if (in_frame)
                    frame_bad += p != LS_FG;
                else if (!in_bar(x, y))
                    ground_bad += p != LS_BG;
            }
        bad += check("init: every pixel of the 240 x 240 panel written (the windows match the panel)", left == 0);
        bad += check("init: the boot logo, every pixel, where the boot splash puts it", logo_bad == 0);
        bad += check("init: the bar's frame sail white all round, the rest of the panel navy",
                     frame_bad == 0 && ground_bad == 0);
    }
    bad += check("init: screen on, display on (DISPON), under 600 ms", ls.on && dispon == 1 && ms(clk - t0) < 600u);
    bad += check("init: the bar empty (black track)", bar_is(LS_TRACK, 0, LS_BW));
    memcpy(snap, fb, sizeof fb);
    dump("ldr-000");
    for (done = 0x1000u; done <= total; done += 0x1000u) {      /* 143 sectors, ~20 ms each */
        ls_progress(done, total);
        run(20);
        len = bar_len(LS_OAK);
        if (len < last)                            /* (-1: not a clean fill from the left) */
            mono = 0;
        else
            last = len;
        if (!half_dumped && done * 2u >= total) {
            run(LS_PERIOD + 5u);
            dump("ldr-050");
            half_dumped = 1;
        }
    }
    run(2000);
    dump("ldr-100");
    printf("  update: longest ls_tick %u us, %u transfers\n", (unsigned)(tick_max / FM1_TICKS_PER_US), sends);
    bad += check("update: the bar fills from the left, oak then track, never going back", mono);
    bad += check("update: the bar full, in oak", ls.on && bar_is(LS_OAK, 0, LS_BW));
    bad += check("update: nothing but the bar changed (the logo, the frame and the ground as drawn)",
                 changed_outside_bar() == 0);
    bad += check("no transfer started over a running one, windows on the panel", overlaps == 0 && bad_window == 0);
    bad += check("ls_tick never takes more than 3 ms", tick_max < 3000u * FM1_TICKS_PER_US);
    n_before = sends;
    run(LS_PERIOD * 10u + 5u);
    bad += check("idle: nothing sent while nothing changes (no animation)", sends == n_before);
    ls_failed(1);
    run(200);
    dump("ldr-failed");
    bad += check("a failed update: the bar in rust", bar_is(LS_RUST, 0, LS_BW));
    ls_failed(0);                                  /* the host resumes: a new pass from the start */
    ls_progress(0, total);
    run(3000);
    bad += check("a resumed session: back to the empty track", bar_is(LS_TRACK, 0, LS_BW));
    ls_progress(total / 2u, total);
    run(1000);
    bad += check("half way: the bar half oak, half track", bar_is(LS_OAK, 0, LS_BW / 2u - 1u) &&
                 bar_is(LS_TRACK, LS_BW / 2u + 1u, LS_BW));
    ls_progress(total * 3u / 4u, total);
    for (i = 0; i < 2000u && !ls.busy; i++) {      /* a bar update in flight, then the reset */
        ls_tick();
        clk += 100u * FM1_TICKS_PER_US;
    }
    ls_progress(total, total);                     /* the last sectors written, not drawn yet */
    t0 = clk;
    ls_stop();
    bad += check("ls_stop: the transfer in flight finished, the bar brought to full, under 130 ms",
                 i < 2000u && !ls.busy && fm1_lcd_done() && !cs_low && bar_is(LS_OAK, 0, LS_BW) &&
                 ms(clk - t0) <= 130u);
    bad += check("still nothing but the bar changed, no overlapping transfer, ls_tick still short",
                 changed_outside_bar() == 0 && overlaps == 0 && tick_max < 3000u * FM1_TICKS_PER_US);

    /* a dead panel from the start */
    reset(1);
    t0 = clk;
    ls_init();
    printf("  dead panel: init %u ms, %u transfers\n", ms(clk - t0), sends);
    bad += check("dead panel: init gives up (screen off) within 200 ms", !ls.on && ms(clk - t0) < 200u);
    n_before = sends;
    ls_progress(total / 2u, total);
    run(1000);
    ls_failed(1);
    run(200);
    t0 = clk;
    ls_stop();
    bad += check("dead panel: ls_tick, ls_progress, ls_failed, ls_stop send nothing, never wait",
                 sends == n_before && tick_max < 10u * FM1_TICKS_PER_US && clk - t0 < 10u * FM1_TICKS_PER_US);

    /* a panel that dies in the start-up table (after SLPOUT and a few settings): no more than the waits */
    reset(0);
    die_after = 8u;
    t0 = clk;
    ls_init();
    printf("  dies in start-up: init %u ms, %u transfers\n", ms(clk - t0), sends);
    bad += check("dies in start-up: the screen off, init under 600 ms, no DISPON", !ls.on && dispon == 0 &&
                 ms(clk - t0) < 600u);
    n_before = sends;
    ls_progress(total / 2u, total);
    run(1000);
    ls_stop();
    bad += check("dies in start-up: nothing sent after", sends == n_before);

    /* a panel that dies mid-update */
    reset(0);
    ls_init();
    ls_progress(total / 4u, total);
    run(500);
    dead = 1;
    xfer_end = NEVER;                              /* the transfer running (or the next one) never finishes */
    t0 = clk;
    for (done = total / 4u; done <= total && ls.on; done += 0x1000u) {
        ls_progress(done, total);
        run(20);
    }
    printf("  dies mid-update: off after %u ms, longest ls_tick %u us\n", ms(clk - t0),
           (unsigned)(tick_max / FM1_TICKS_PER_US));
    bad += check("dies mid-update: the screen off within 200 ms", !ls.on && ms(clk - t0) <= 200u);
    bad += check("dies mid-update: no ls_tick call waits past the bar's 40 ms deadline",
                 tick_max <= 41000u * FM1_TICKS_PER_US);
    n_before = sends;
    ls_progress(total, total);
    run(1000);
    t0 = clk;
    ls_stop();
    bad += check("dies mid-update: nothing sent after, ls_stop returns at once",
                 sends == n_before && clk - t0 < 10u * FM1_TICKS_PER_US);

#if BOARD_SMK37
    bad += check("SMK: nothing ever sent to the pad LEDs, their line never set up", pads == 0);
#endif
    printf(bad ? "ldr_screen%s: %d FAILED\n" : "ldr_screen%s: all passed\n", BOARD_SMK37 ? " (SMK-37 Pro)" : "", bad);
    return bad ? 1 : 0;
}
