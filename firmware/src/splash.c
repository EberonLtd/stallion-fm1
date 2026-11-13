/* SPDX-License-Identifier: GPL-3.0-only */
/* The boot splash (SLOOP's code, Stallion's logo: stallion R1): the logo (tools/gen_logo.py -> sloop_logo.h: 16
 * colours, RLE runs of (run - 1) << 4 | colour, 0 the ground), drawn into the canvas in bands of at most 120 rows. */
#include "sloop_logo.h"
static void sloop_logo_draw(uint32_t y0)
{
    uint32_t r0;
    for (r0 = 0; r0 < SLOOP_SPLASH_H; r0 += 120u) {
        uint32_t rows = SLOOP_SPLASH_H - r0 > 120u ? 120u : SLOOP_SPLASH_H - r0;
        uint32_t lo = r0 * SLOOP_SPLASH_W, hi = (r0 + rows) * SLOOP_SPLASH_W, pos = 0, i;
        cv_begin(SLOOP_SPLASH_W, rows, SLOOP_SPLASH_BG);
        for (i = 0; i < sizeof SLOOP_SPLASH_RLE && pos < hi; i++) {
            uint32_t run = (SLOOP_SPLASH_RLE[i] >> 4) + 1u, c = SLOOP_SPLASH_RLE[i] & 15u, p;
            if (c && pos + run > lo)
                for (p = pos < lo ? lo : pos; p < pos + run && p < hi; p++)
                    cv_px[p - lo] = swap16(SLOOP_SPLASH_PAL[c]);
            pos += run;
        }
        cv_blit((240u - SLOOP_SPLASH_W) / 2u, y0 + r0);
    }
}

/* the boot screen: the logo (the longship and STALLION), where it comes from; splash_ver adds the version line */
static void sloop_splash(void)
{
    lcd_fill(0, 0, 240, 240, SLOOP_SPLASH_BG);
    sloop_logo_draw(8);
    cv_begin(240, 16, SLOOP_SPLASH_BG);
    cv_text(120 - text_w(&FONT_S, "from Clipper, SLOOP + Felucca") / 2, 0, &FONT_S, "from Clipper, SLOOP + Felucca",
            RGB(120, 140, 166));
    cv_blit(0, 220);
}

/* (stallion R1) under STALLION: "1.0 8V 2 CORES" (ui_menu.c ver_line); main.c calls it once the second core has
 * answered, so the cores are the ones Stallion runs on */
static void splash_ver(void)
{
    char v[32];
    ver_line(v, sizeof v);
    cv_begin(240, 16, SLOOP_SPLASH_BG);
    cv_text(120 - text_w(&FONT_S, v) / 2, 0, &FONT_S, v, RGB(243, 238, 226));
    cv_blit(0, 198);
}
