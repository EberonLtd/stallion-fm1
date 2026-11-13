/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* Host test of the SMK-37 Pro's LCD pin set-up for the update loader (hal/fm1_lcd_hw.h, built with -DBOARD_SMK37=1),
 * on words standing in for the registers (FM1_LCD_REG):
 *   - fm1_lcd_hw_init_keep_pads (the loader's, ldr_screen.c): SPI1 on group A, CS high, D/C, CLK, DO low outputs,
 *     SPI1 as the app sets it; the pad LEDs' line PC10 neither driven nor turned to an output, from any state,
 *     and every other pin as it was;
 *   - otherwise the same as the app's fm1_lcd_hw_init, which does drive PC10 (so the difference is the pads only). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t reg[0x60000u / 4u];                /* 0x00000..0x5FFFF: every address the header names */
#define FM1_LCD_REG(a) (*(volatile uint32_t *)&reg[(a) / 4u])
#include "fm1_lcd_hw.h"

#if !BOARD_SMK37
#error "build with -DBOARD_SMK37=1"
#endif

static int check(const char *what, int ok) { printf("%-72s %s\n", what, ok ? "ok" : "FAIL"); return ok ? 0 : 1; }

static void start(uint32_t fill)                   /* every register at one pattern, SPI1 left on group B */
{
    uint32_t i;
    for (i = 0; i < sizeof reg / sizeof reg[0]; i++)
        reg[i] = fill;
    FM1_LCD_IOMAP_CON1 |= FM1_LCD_GROUP_B;
}

int main(void)
{
    static const uint32_t FILL[] = {0x00000000u, 0xFFFFFFFFu, 0x5A5A5A5Au, 0xA5A5A5A5u};
    static uint32_t app[sizeof reg / sizeof reg[0]];
    const uint32_t lcd_pb = FM1_LCD_DC | FM1_LCD_CLK | FM1_LCD_DO;
    uint32_t f, i;
    int bad = 0, pads_kept = 1, lcd_ok = 1, others = 1, same_else = 1, app_drives = 0;

    for (f = 0; f < sizeof FILL / sizeof FILL[0]; f++) {
        uint32_t pc_out, pc_dir, pa_out, pa_dir, pb_out, pb_dir, iomap;
        start(FILL[f]);
        pc_out = FM1_LCD_PC_OUT, pc_dir = FM1_LCD_PC_DIR, pa_out = FM1_LCD_PA_OUT, pa_dir = FM1_LCD_PA_DIR;
        pb_out = FM1_LCD_PB_OUT, pb_dir = FM1_LCD_PB_DIR, iomap = FM1_LCD_IOMAP_CON1;
        fm1_lcd_hw_init_keep_pads();
        pads_kept &= (FM1_LCD_PC_OUT & FM1_PADLED_DO) == (pc_out & FM1_PADLED_DO) &&
                     (FM1_LCD_PC_DIR & FM1_PADLED_DO) == (pc_dir & FM1_PADLED_DO);
        lcd_ok &= !(FM1_LCD_IOMAP_CON1 & FM1_LCD_GROUP_B) && (FM1_LCD_PC_OUT & FM1_LCD_CS) &&
                  !(FM1_LCD_PC_DIR & FM1_LCD_CS) && !(FM1_LCD_PB_OUT & lcd_pb) && !(FM1_LCD_PB_DIR & lcd_pb) &&
                  FM1_LCD_SPI_CON == 0x4021u && FM1_LCD_SPI_BAUD == 4u && fm1_lcd_cur_baud == 4u;
        others &= (FM1_LCD_PC_OUT & ~(FM1_LCD_CS | FM1_PADLED_DO)) == (pc_out & ~(FM1_LCD_CS | FM1_PADLED_DO)) &&
                  (FM1_LCD_PC_DIR & ~(FM1_LCD_CS | FM1_PADLED_DO)) == (pc_dir & ~(FM1_LCD_CS | FM1_PADLED_DO)) &&
                  (FM1_LCD_PB_OUT & ~lcd_pb) == (pb_out & ~lcd_pb) && (FM1_LCD_PB_DIR & ~lcd_pb) == (pb_dir & ~lcd_pb) &&
                  FM1_LCD_PA_OUT == pa_out && FM1_LCD_PA_DIR == pa_dir &&
                  (FM1_LCD_IOMAP_CON1 | FM1_LCD_GROUP_B) == (iomap | FM1_LCD_GROUP_B);
        memcpy(app, reg, sizeof reg);              /* the loader's result; now the app's from the same start */
        start(FILL[f]);
        fm1_lcd_hw_init();
        app_drives |= (FM1_LCD_PC_OUT & FM1_PADLED_DO) != (pc_out & FM1_PADLED_DO) ||
                      (FM1_LCD_PC_DIR & FM1_PADLED_DO) != (pc_dir & FM1_PADLED_DO);
        for (i = 0; i < sizeof reg / sizeof reg[0]; i++) {
            uint32_t m = (i == 0x50080u / 4u || i == 0x50088u / 4u) ? ~FM1_PADLED_DO : ~0u;
            if ((reg[i] & m) != (app[i] & m))
                same_else = 0;
        }
    }
    bad += check("loader set-up: PC10 (pad LEDs) neither driven nor made an output, from any state", pads_kept);
    bad += check("loader set-up: SPI1 on group A, CS high, D/C CLK DO low outputs, SPI1 as the app", lcd_ok);
    bad += check("loader set-up: every other pin and register as it was", others);
    bad += check("the app's set-up drives PC10 (the line the loader must leave alone)", app_drives);
    bad += check("loader set-up = the app's on every register but PC10", same_else);
    printf(bad ? "ldr_lcd_pins: %d FAILED\n" : "ldr_lcd_pins: all passed\n", bad);
    return bad ? 1 : 0;
}
