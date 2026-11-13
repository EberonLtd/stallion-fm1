/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* The audio render's hot code and tables in RAM, as the stock M-VAVE firmware and Melodee run theirs: the flash (XIP)
 * feeds the cores through a cache that both of them and the UI share, and every miss there is a line fill over SPI.
 *
 *   code          tools/build.py HOT: the functions are renamed after the compile (their -ffunction-sections
 *                 sections) into .ram_hot, the RAM code area after the flash driver and core 1's loop. The compiled
 *                 code is the same as in the flash: a section attribute in the source would stop clang from
 *                 inlining across it.
 *   TABLE_HOT     a lookup table read every sample (felucca_tables.h): section .ram_hot_tab, in the POOL region.
 *   HOT_FAR(fn)   a call between RAM (0x01C0xxxx) and the flash (0x02xxxxxx) is beyond the 23-bit call range: the
 *                 linker refuses it, and tools/build.py hot_check checks the listing. Calls across go through a
 *                 pointer the compiler cannot fold back (an engine's render, cpu1_run's job, and this).
 *
 * main.c fm1_cstart copies both from their load addresses at boot, with .ram_text and .data; the guard's RAM code
 * window (fm1_guard.h, app.ld _rt_end) runs from .ram_text to the end of .ram_hot, code only. Unlike .ram_text (the flash
 * driver), nothing here runs while the flash is off: it may read .rodata in the flash; it is placed for speed alone.
 * On the host HOT_FAR is a plain call (core.h) and TABLE_HOT nothing (felucca_tables.h). */
#pragma once

#define TABLE_HOT __attribute__((section(".ram_hot_tab")))
static inline void *fm1_hot_far(void *p) { void *volatile q = p; return q; }
#define HOT_FAR(fn) ((__typeof__(&fn))fm1_hot_far((void *)&fn))
