/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Changes: Copyright (C) 2026 Eberon Limited (John Gorman) */
/* Felucca update loader (device side). Runs from RAM, started by the SPL from
 * an update record, with interrupts off: USB is polled from the main loop.
 * It is a USB-MIDI device "Felucca Update" (1209:0002) that answers the
 * M-UPGRADE update protocol as "ota-" + the package's identity, "ota-FM-1_104210" for Stallion 1.0
 * (SMK: "ota-SMK-37 Pro_104210", or the SMK_MODEL build's name; build.py sets it; hosts look for "ota-"), and
 * writes the app area only (ldr_core.c). The UBOOT soft key works here too.
 * The screen (ldr_screen.c): the boot logo over a thick loading bar; it never
 * holds up the write (deadlines, off for good when the panel does not answer).
 * Single compilation unit. */
#include <stdint.h>
#define FELUCCA_LOADER 1
#define FELUCCA_CDC 0
#define FELUCCA_OTA 1
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_USB_PID 0x0002
#include "fm1_cc.h"                             /* BOARD_SMK37 */
#ifndef FELUCCA_ID                             /* build.py: "ota-" + the package identity */
#if BOARD_SMK37
#define FELUCCA_ID "ota-" SMK_MODEL "_104200"   /* Stallion's own number (fm1pkg_make.py STALLION_NUM), a dev build */
#else
#define FELUCCA_ID "ota-FM-1_104200"
#endif
#endif
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "fm1_time.h"
#include "fm1_sys.h"
/* the loader may write the app area and erase update records; never the head */
#if BOARD_SMK37
/* SMK: app area 0x4000..0x9CFFF, records in the VM 0x9D000..0xBFFFF only: never BTIF / USRTRIM
 * 0xC0000..0xC1FFF, USR 0xF4000.., key_mac 0xFF000 (FL_APP / FL_REC, asserted in fm1_flash.h; the RAM
 * driver also refuses every protected range on its own, FL_GUARD) */
#define FL_RANGE_OK(off, n) FL_LDR_RANGE_OK(off, n)
#else
#define FL_RANGE_OK(off, n) (FL_IN(off, n, 0x4000u, 0x93000u) || FL_IN(off, n, 0x93000u, 0xFC000u))
#endif
#include "fm1_flash.h"
#include "../src/libc.c"
#include "../src/usb.c"
#include "ldr_screen.c"                        /* (SMK-37 Pro: its own ST7789V start-up table, the pads left alone) */

static uint32_t ldr_ms(void)   /* monotonic ms; survives the TIMER4 wrap (178.9 s) */
{
    static uint32_t last, acc, ms;
    uint32_t now = fm1_ticks();
    acc += now - last;
    last = now;
    ms += acc / (1000u * FM1_TICKS_PER_US);
    acc %= 1000u * FM1_TICKS_PER_US;
    return ms;
}

static void ldr_poll(void)                          /* USB at ~2 kHz, watchdog, the bar when due */
{
    static uint32_t last;
    uint32_t now = fm1_ticks();
    fm1_wdt_feed();
    if (now - last >= 500u * FM1_TICKS_PER_US) {
        last = now;
        usb_poll();
    }
    ls_tick();                                      /* returns at once unless a bar update is due */
}

/* ---- ota.c hooks (the SysEx frame hooks are in usb.c) ---- */
static uint32_t ota_now_ms(void) { return ldr_ms(); }
static void ota_idle(void) { ldr_poll(); }
/* step-1 hooks of ota.c: not used by the loader */
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { return fl_read_ram(off, p, n); }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../src/ota.c"

/* ---- ldr_core.c hooks: interrupts are off for the whole loader ---- */
static int ldr_fread(uint32_t off, void *p, uint32_t n) { return fl_read_ram(off, p, n); }
static int ldr_erase(uint32_t off)
{
    uint32_t took;
    return fl_erase4k_ram(off, &took);
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    uint32_t took;
    return fl_prog_ram(off, p, n, &took);
}
static void ldr_record_clear(void)
{
    fm1_updata_parm_clear();                        /* CRC 0: the SPL ignores it on a warm reset */
}
static int ldr_flash_known(void) { return FL_JEDEC_OK(fl_jedec_ram()); }   /* as the app (project.c) */
static void ldr_progress(uint32_t done, uint32_t total)
{
    ls_progress(done, total);                       /* only stores it: ls_tick draws */
    ldr_poll();
}
#include "ldr_core.c"

extern uint32_t _bss_start[], _bss_end[];

void ldr_main(void)
{
    fm1_wdt_arm(0x0D);                              /* ~8 s, fed by ldr_poll */
    fm1_time_init();
    ls_init();                                      /* before USB: ~0.4 s, bounded; a dead panel: screen off */
    usb_start();
    for (;;) {
        ldr_poll();
        usb_retry(ldr_ms());
        if (usb.uboot_req) {                        /* soft key: mask-ROM UBOOT, as in Felucca */
            ls_stop();
            usb_detach();
            fm1_delay_ms(30);
            fm1_enter_uboot();
        }
        ota_service();                              /* handshake */
        if (usb.ota_req) {
            int rc;
            usb.ota_req = 0;
            ls_failed(0);                           /* (a resumed session: oak again) */
            rc = ldr_session();
            ls_failed(rc != 0);                     /* the bar in rust: the record stays, the host can resume */
            if (rc == 0 || rc == LDR_EFLASH) {      /* done, or another flash chip: the old firmware again */
                ls_stop();                          /* the bar finished (bounded), the panel not mid-write */
                fm1_delay_ms(200);                  /* the "success" reply leaves */
                usb_detach();
                fm1_delay_ms(30);
                fm1_core_reset();                   /* the SPL boots the new app */
            }
        }
    }
}

void ldr_cstart(void)
{
    uint32_t *p;
    ldr_record_clear();                             /* first thing: a crash below cannot loop through a
                                                     * warm reset; resume after power loss uses the flash record */
    for (p = _bss_start; p < _bss_end; p++)
        *p = 0;
    ldr_main();
}
