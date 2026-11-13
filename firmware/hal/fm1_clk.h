/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* (Stallion) what the clocks and the flash really do, measured on the unit: read-only diagnostics for the console's
 * `clk` (console.c) and GLO's CPU column (ui_draw.c), after the stock M-VAVE firmware and Melodee were found to run
 * their audio from RAM. Nothing here changes a clock or the flash; TIMER3 (unused by Stallion, the ROM and the SPL)
 * counts for a moment and is put back as it was.
 *
 *   fm1_clk_regs(v)            the clock, PLL, flash controller and cache registers below (FM1_CLK_REGS), as found
 *   fm1_clk_timer_hz(src)      TIMER3 counting clock source src (CON bits 3:2: 0 the LSB clock, 2 the 24 MHz crystal
 *                              as TIMER4 runs) over 10 ms of TIMER4: its rate in Hz. The caller keeps the interrupts on
 *                              (each end is read with them off, the wait between has them on)
 *   fm1_clk_loop_ticks(...)    TIMER4 ticks of n turns of fm1_clk.S's add loops, from RAM or the flash
 *   fm1_clk_fill_ticks(...)    TIMER4 ticks of n reads stride bytes apart at XIP address a (fm1_clk.S, run from RAM)
 * The two timed ones run with the interrupts off (the caller's: fm1_irq_off / fm1_irq_on, or before they are on). */
#pragma once
#include <stdint.h>
#include "fm1_time.h"

#define FM1_CLK_NREG 12u
static const uint32_t FM1_CLK_REGS[FM1_CLK_NREG] = {0x10008u, 0x1000Cu, 0x10010u, 0x10014u, 0x119A0u, 0x119A4u, 0x119A8u,
                                                    0x119ACu, 0x40200u, 0x40204u, 0x1EEE008u, 0u};
#define FM1_T3_CON (*(volatile uint32_t *)0x10700u)
#define FM1_T3_CNT (*(volatile uint32_t *)0x10704u)
#define FM1_T3_PRD (*(volatile uint32_t *)0x10708u)

uint32_t fm1_clk_add8_ram(uint32_t n), fm1_clk_add24_ram(uint32_t n), fm1_clk_add8_xip(uint32_t n),
    fm1_clk_add24_xip(uint32_t n), fm1_clk_lines(uint32_t a, uint32_t n, uint32_t stride);

static inline void *fm1_clk_far(void *p) { void *volatile q = p; return q; }   /* RAM code: beyond the call range */

static void fm1_clk_regs(uint32_t *v)
{
    uint32_t i;
    for (i = 0; i < FM1_CLK_NREG; i++)
        v[i] = FM1_CLK_REGS[i] ? *(volatile uint32_t *)FM1_CLK_REGS[i] : 0u;
}

static uint32_t fm1_clk_timer_hz(uint32_t src)
{
    uint32_t con = FM1_T3_CON, prd = FM1_T3_PRD, cnt = FM1_T3_CNT, a3, a4, b3, b4;
    FM1_T3_CON = 0;
    FM1_T3_PRD = 0xFFFFFFFFu;
    FM1_T3_CNT = 0;
    FM1_T3_CON = ((src & 3u) << 2) | 1u | (1u << 14);
    __asm__ volatile("cli" ::: "memory");
    a3 = FM1_T3_CNT, a4 = FM1_T4_CNT;
    __asm__ volatile("csync\n\tsti" ::: "memory");
    while (FM1_T4_CNT - a4 < 240000u)                   /* 10 ms */
        ;
    __asm__ volatile("cli" ::: "memory");
    b3 = FM1_T3_CNT, b4 = FM1_T4_CNT;
    __asm__ volatile("csync\n\tsti" ::: "memory");
    FM1_T3_CON = 0;
    FM1_T3_PRD = prd;
    FM1_T3_CNT = cnt;
    FM1_T3_CON = con | (1u << 14);
    return (uint32_t)(((uint64_t)(b3 - a3) * 24000000u) / (b4 - a4));
}

/* ram: 1 from RAM, 0 the flash (run once first, untimed: the loop in the cache); adds: 8 or 24 */
static uint32_t fm1_clk_loop_ticks(int ram, uint32_t adds, uint32_t n)
{
    uint32_t (*f)(uint32_t) = ram ? (adds == 24u ? fm1_clk_add24_ram : fm1_clk_add8_ram)
                                  : (adds == 24u ? fm1_clk_add24_xip : fm1_clk_add8_xip), t;
    f = (uint32_t (*)(uint32_t))fm1_clk_far((void *)f);
    if (!ram)
        f(16u);
    t = FM1_T4_CNT;
    f(n);
    return FM1_T4_CNT - t;
}

static uint32_t fm1_clk_fill_ticks(uint32_t a, uint32_t n, uint32_t stride)
{
    uint32_t (*f)(uint32_t, uint32_t, uint32_t) =
        (uint32_t (*)(uint32_t, uint32_t, uint32_t))fm1_clk_far((void *)fm1_clk_lines), t;
    t = FM1_T4_CNT;
    f(a, n, stride);
    return FM1_T4_CNT - t;
}
