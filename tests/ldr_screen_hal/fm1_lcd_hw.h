/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* tests/ldr_screen_test.c: firmware/loader/ldr_screen.c includes "fm1_lcd_hw.h"; with -Itests/ldr_screen_hal it gets
 * this one instead of firmware/hal's. The fake panel (fm1_lcd_hw_init ... fm1_lcd_deselect) is defined in the test
 * before it includes ldr_screen.c. */
#pragma once
