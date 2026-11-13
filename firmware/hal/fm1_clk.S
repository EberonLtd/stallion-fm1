/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* (Stallion) the clock and flash measurements' loops (fm1_clk.h): written out so their instructions are known.
 *   fm1_clk_add8_*(n), fm1_clk_add24_*(n)   n turns of 8 / 24 single adds and a counted branch: the difference
 *                                            between the two is 16 n adds (one cycle each on an in-order core)
 *   fm1_clk_lines(addr, n, stride)          n words read stride bytes apart, each read's address depending on
 *                                            the one before (no overlap): one line fill each from the flash
 * The _ram ones and fm1_clk_lines live in .ram_text (no calls, no data: the build checks), the _xip ones in the
 * flash. Arguments r0..r2, scratch r0..r3 only, as the C calling convention allows. */

	.macro add8
	r1 += 1
	r1 += 1
	r1 += 1
	r1 += 1
	r1 += 1
	r1 += 1
	r1 += 1
	r1 += 1
	.endm

	.section .ram_text.clk,"ax",@progbits
	.balign 4
	.globl fm1_clk_add8_ram
fm1_clk_add8_ram:
1:	add8
	if (--r0 != 0) goto 1b
	rts

	.balign 4
	.globl fm1_clk_add24_ram
fm1_clk_add24_ram:
1:	add8
	add8
	add8
	if (--r0 != 0) goto 1b
	rts

	.balign 4
	.globl fm1_clk_lines
fm1_clk_lines:
1:	r3 = [r0+0]
	r3 = r3 & 0
	r0 = r0 + r3
	r0 = r0 + r2
	if (--r1 != 0) goto 1b
	rts

	.section .text.clk,"ax",@progbits
	.balign 4
	.globl fm1_clk_add8_xip
fm1_clk_add8_xip:
1:	add8
	if (--r0 != 0) goto 1b
	rts

	.balign 4
	.globl fm1_clk_add24_xip
fm1_clk_add24_xip:
1:	add8
	add8
	add8
	if (--r0 != 0) goto 1b
	rts
