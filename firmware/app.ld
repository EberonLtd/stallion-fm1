/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Changes: Copyright (C) 2026 Eberon Limited (John Gorman) */
/* Felucca memory map.
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * 0x01C00000..0x01C07F00 holds the code that runs from RAM, and nothing else (the guard's PC window over it is
 * one range, fm1_guard.h: no data may sit inside it): .ram_text (code that must run while the flash is busy: the
 * flash driver), .c1_text (the second core's waiting loop, fm1_cpu1.h) and .ram_hot (Stallion: the audio render's
 * hot code, hal/fm1_hot.h). crt0.S writes the SPL's state at _status + 0x20 (0x01C07F20 before Stallion's .ram_hot
 * grew the area by 6 KB, taken from the start of the RAM region: now 0x01C09720): the region stops at _status.
 * .lowram (sloop-lab: the reverb lines, the second core's stacks and the render split's scratch) sat after them
 * until Stallion's .ram_hot took that room: it is in the RAM region now, between .data and .bss. main.c proves and
 * zeroes it at boot.
 * RAM  : .data, .lowram, .bss (96 KiB less those 6 KB)
 * POOL : big zero-initialised buffers (FX lines, canvases), section .pool
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x7F00 + 0x1980   /* (Stallion: + 6.4 KB of the RAM region for .ram_hot; 6 KB until the SMK-37 build of voice-pressure overflowed it, 6.5 KB until RODEO's EDIT 3 did by 8 B, 6.6 KB until PONY's CHAR moved part_pending and pch_mode to the flash and its voice state overflowed the SMK-37's RAM by 168 B) */
    RAM    (rwx) : ORIGIN = 0x01C08000 + 0x1980, LENGTH = 96K - 0x1980
    POOL   (rw)  : ORIGIN = 0x01C20000, LENGTH = 0x54000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_tend = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .c1_text : { _c1_start = .; KEEP(*(.c1_text*)) . = ALIGN(4); _c1_end = .; } > RAMTEXT AT > XIP
    _c1_load = LOADADDR(.c1_text);
    /* (Stallion) the audio render's hot code (hal/fm1_hot.h; tools/build.py HOT renames it .rhot.*): run from RAM
     * for speed, copied at boot as the others (main.c) */
    .ram_hot : { _rh_start = .; *(.rhot.*) . = ALIGN(4); _rh_end = .; } > RAMTEXT AT > XIP
    _rh_load = LOADADDR(.ram_hot);
    _rt_end = _rh_end;                  /* the guard's RAM code window: the three, code only (fm1_guard.h, fm1_cpu1.h;
                                         * tools/build.py hot_check) */
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    /* section(".lowram") buffers and the second core's stacks (fm1_cpu1.S .bss.c1stack; before .bss, which would take
     * them); zeroed at boot as .pool is (main.c lowram_test) */
    .lowram (NOLOAD) : { . = ALIGN(8); _lr_start = .; *(.bss.c1stack) *(.lowram*) . = ALIGN(4); _lr_end = .; } > RAM
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    /* (Stallion) the hot lookup tables (hal/fm1_hot.h TABLE_HOT) at the start of the POOL region, copied at boot */
    .ram_hot_tab : { _rht_start = .; *(.ram_hot_tab*) . = ALIGN(4); _rht_end = .; } > POOL AT > XIP
    _rht_load = LOADADDR(.ram_hot_tab);
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    /* (SLOOP 2.8.3) .noinit.c1bar (fm1_cpu1.h, one core until power-off) after the others: none of them moves;
     * (Stallion) .noinit.freset (storage.c: FACTORY RESET's hold on the saves) after it, for the same reason */
    .noinit (NOLOAD) : { *(.noinit) *(.noinit.c1bar) *(.noinit.*) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    _status = ORIGIN(RAMTEXT) + LENGTH(RAMTEXT);   /* (Stallion) crt0.S's STATUS words (0x100 B), between the RAM code
                                         * area and the RAM region (it was 0x01C07F00, before .ram_hot took 6 KB more) */
    _ram_lo = ORIGIN(RAM); _ram_hi = ORIGIN(RAM) + LENGTH(RAM);
    /DISCARD/ : { *(.comment) *(.note*) }
}
