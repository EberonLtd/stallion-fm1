/* SPDX-License-Identifier: GPL-3.0-only */
/* AT PRESSURE (firmware/src/ui_atpop.c): a thin bar in the gap under the four boxes: it never touches a row of the page,
 * waits AT_POP_MS, sends only what moved, follows the pressure and leaves the gap black.
 *   cc -O2 -w -DCLIPPER_SYNTH=1 -Ibuild/gen -Ifirmware/src -Ifirmware/hal tests/at_pop_test.c -lm */
#define UI_PAGES_HARNESS_ONLY 1
#include <stdint.h>
static void rec_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c);
#define AT_POP_FILL rec_fill                           /* (the harness includes ui_atpop.c with its fills recorded here;
                                                        * at_pop_held, _show, _t0: seq.c's, both boards) */
#include "ui_pages_test.c"
static int fills, outside;
static uint16_t row[240];
static void rec_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t i;
    fills++;
    outside += y < Y_SEP_END || y + h > Y_GRAPH || x < 0 || x + w > 240;
    for (i = x; i < x + w && i < 240; i++)
        row[i] = c;
}
static int bad;
static void pop_check(const char *w, int ok)
{
    printf("at pop: %-58s %s\n", w, ok ? "ok" : "FAIL");
    bad |= !ok;
}
static void one_frame(uint32_t page) { at_strip_page = (uint8_t)page; at_strip_draw(); }
static int lit(void)
{
    int i, n = 0;
    for (i = 0; i < 240; i++)
        n += row[i] == C_HI;
    return n;
}
int main(void)
{
    int i, f;
    fm1_ms = 1000u;
    pal[4] = 0xFFFFu;                                  /* (the sound's colour: the harness has no palette) */
    one_frame(1);
    pop_check("nothing held: nothing drawn", !fills);
    at_pop_held = 1, at_pop_t0 = 1000u, at_pop_show = 127u;
    fm1_ms = 1000u + AT_POP_MS - 1u;
    one_frame(1);
    pop_check("just pressed: not yet", !fills);
    fm1_ms = 1000u + AT_POP_MS;
    one_frame(0);
    pop_check("another screen up: not on it", !fills);
    one_frame(1);
    pop_check("held on: up", fills >= 1 && at_strip_on);
    pop_check("127: the whole width", lit() == 240);
    f = fills;
    one_frame(1);
    pop_check("the same value: nothing sent", fills == f);
    at_pop_show = 64u;
    one_frame(1);
    pop_check("64: about half, one fill", lit() >= 118 && lit() <= 124 && fills == f + 1);
    f = fills;
    at_pop_show = 100u;
    one_frame(1);
    pop_check("up: one fill", lit() == 188 && fills == f + 1);
    at_pop_show = 0u;
    one_frame(1);
    pop_check("0: no bar, the track left", !lit() && at_strip_on && row[10] == TE_G1);
    at_pop_show = 64u;
    one_frame(2);
    pop_check("the page drew: the bar whole again", lit() >= 118 && lit() <= 124);
    at_pop_held = 0;
    one_frame(1);
    for (i = 0, f = 1; i < 240; i++)
        f &= row[i] == C_BLACK;
    pop_check("released: the gap black again", f && !at_strip_on);
    f = fills;
    one_frame(1);
    pop_check("released: once", fills == f);
    pop_check("never a row of the page", !outside);
    return bad;
}
