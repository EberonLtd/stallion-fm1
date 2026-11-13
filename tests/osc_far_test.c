/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 John Gorman (Clipper) */
/* (1.1) dsp.c osc_saw_o, osc_pulse_o, osc_tri_bl_o (ANALOG's: the band-limiting out of line) against osc_saw,
 * osc_pulse, osc_tri_bl: random phases and increments (0 up to the whole range), and every other phase right at a
 * wrap or a corner, where the band-limiting works. Bit for bit. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint32_t xs = 12345u;
static uint32_t rnd(void)
{
    xs ^= xs << 13, xs ^= xs >> 17, xs ^= xs << 5;
    return xs;
}

int main(void)
{
    uint32_t k, bad = 0, n = 0, edge = 0;
    for (k = 0; k < 2000000u; k++) {
        uint32_t inc = rnd() >> (rnd() & 31u), ph = rnd(), pw = rnd();
        if (k & 1u) {                                   /* right at a wrap or a corner */
            ph = (k & 2u ? 0x80000000u : 0u) + (rnd() % (inc + 1u)) - (k & 4u ? inc : 0u);
            edge++;
        }
        bad += osc_saw_o(ph, inc) != osc_saw(ph, inc);
        bad += osc_pulse_o(ph, inc, pw) != osc_pulse(ph, inc, pw);
        bad += osc_tri_bl_o(ph, inc) != osc_tri_bl(ph, inc);
        n += 3;
    }
    printf("osc_far: %u samples (%u phases at a wrap or a corner), %u different from blep's %s\n", n, edge, bad,
           bad ? "FAIL" : "ok");
    return bad != 0;
}
