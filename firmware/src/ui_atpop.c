/* SPDX-License-Identifier: GPL-3.0-only */
/* AT PRESSURE: a thin bar in the gap under the four boxes while pressure is applied (SMK-37: a pad held; FM-1: a key
 * leaned on, seq.c AT_POP_LEAN): full width at 127, the touch itself (before AT CURVE and OFFS, which shape what the
 * sound does with it, not where the finger is). Nothing lives in the gap, so nothing is covered and nothing has to be redrawn:
 * only the bar's changed pixels are sent. It waits AT_POP_MS, so quick playing does not blink it */
#define AT_POP_MS 120u
#define AT_STRIP_Y Y_SEP_END
#define AT_STRIP_H 3
_Static_assert(Y_SEP_END + AT_STRIP_H < Y_GRAPH, "AT PRESSURE: the bar stays in the gap");
static uint8_t at_strip_page;                      /* ui_draw_screen: the four boxes this frame (2: all of it drawn) */
static uint8_t at_strip_on;
/* VOLUME: the same bar, white, the MASTER knob's position
 * (0..1023, main.c) for VOL_POP_MS after it moves, over the pressure's */
#define VOL_POP_MS 2000u
static volatile uint16_t ui_master_k10 = 0xFFFFu;  /* (main.c, after this in felucca.c: the knob as it is read;
                                                    * 0xFFFF none yet) */
static void at_strip_draw(void)
{
    static uint32_t shown, vt;
    static uint16_t vk = 0xFFFFu;
    uint32_t k = ui_master_k10, x = (uint32_t)at_pop_show * 240u / 127u, c = C_HI;
    if (vk == 0xFFFFu || k + 12u < vk || k > vk + 12u)   /* (moved more than ~1 %: the knob's own noise stays under) */
        vt = vk == 0xFFFFu ? 0u : fm1_ms, vk = (uint16_t)k;
    if (vt && fm1_ms - vt < VOL_POP_MS && k <= 1023u) {
        x = k * 240u / 1023u, c = C_WHITE;
        if (at_strip_page)                         /* (shown at once, drawn whole: the colour may have changed) */
            at_strip_on = 0;
    } else if (!at_pop_held || fm1_ms - at_pop_t0 < AT_POP_MS || !at_strip_page) {
        if (at_strip_on && at_strip_page)
            lcd_fill(0, AT_STRIP_Y, 240, AT_STRIP_H, C_BLACK);
        at_strip_on = 0;
        return;
    }
    if (!at_strip_on || at_strip_page == 2u) {     /* the whole bar */
        if (x)
            lcd_fill(0, AT_STRIP_Y, x, AT_STRIP_H, c);
        if (x < 240u)
            lcd_fill(x, AT_STRIP_Y, 240u - x, AT_STRIP_H, TE_G1);
    } else if (x > shown) {                        /* only what moved */
        lcd_fill(shown, AT_STRIP_Y, x - shown, AT_STRIP_H, c);
    } else if (x < shown) {
        lcd_fill(x, AT_STRIP_Y, shown - x, AT_STRIP_H, TE_G1);
    }
    at_strip_on = 1;
    shown = x;
}
