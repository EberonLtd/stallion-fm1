/* SPDX-License-Identifier: GPL-3.0-only */
/* Real recovery control flow with hardware hooks mocked. No claim of USB
 * enumeration on hardware: the separate OTA suite exercises the real protocol. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "../firmware/src/bootguard.h"
static bootguard_t bootguard;
static uint32_t ticks, fm1_ms, polls, feeds, retries, services, sessions, rom, detached, scans;
static uint32_t jedec = 0x856014u, jedec_reads, lcd_calls, font;
static uint8_t recovery_active, flash_ok;
static struct { uint8_t ota_req, uboot_req; } usb;
static struct { uint32_t buttons; } fm1_in;
static uint32_t input_buttons, stop_at_retry;
static jmp_buf exit_loop;
#define FM1_TICKS_PER_US 24u
#define FL_FAR(x) x
#include "../firmware/hal/fm1_flash_id.h"
#define C_BLACK 0
#define C_WHITE 65535
#define FONT_S font
#define FM1_CRASH_MAGIC 0x43525348u
static struct { uint8_t p3_rst; } fm1_boot;
static struct { uint32_t magic, pc; } fm1_crash;
static char shown[8][32];
static uint32_t nshown;
static uint32_t fm1_ticks(void) { return ticks; }
static void fm1_wdt_feed(void) { feeds++; }
static void usb_poll(void) { polls++; }
static void usb_retry(uint32_t t)
{
    (void)t; retries++;
    if (stop_at_retry && retries >= stop_at_retry) longjmp(exit_loop, 1);
}
static void usb_detach(void) { detached++; }
static void fm1_delay_ms(uint32_t n) { ticks += n * 24000u; }
static void fm1_enter_uboot(void) { rom++; }
static void ota_service(void) { services++; }
static void recovery_poll(void);
static int ota_session(void)
{
    unsigned i;
    sessions++;
    for (i = 0; i < 200; i++) { ticks += 12000u; recovery_poll(); }
    return -13;  /* failed update must leave rescue available for another try */
}
static void fm1_input_init(void) { fm1_in.buttons = 0; }
static void fm1_input_scan(void) { scans++; ticks += 24000u; fm1_in.buttons = input_buttons; }
static uint32_t fl_jedec_ram(void) { jedec_reads++; return jedec; }
static void lcd_init(void) { lcd_calls++; }
static void lcd_fill(int x, int y, int w, int h, int c) { (void)x;(void)y;(void)w;(void)h;(void)c; }
static void draw_text_box(int x, int y, int w, const void *f, const char *s, int c, int a)
{
    (void)x;(void)y;(void)w;(void)f;(void)c;(void)a;
    if (nshown < 8u) { snprintf(shown[nshown], sizeof shown[0], "%s", s); nshown++; }
}
static void lcd_sync(void) {}
static void usb_start(void) {}
#include "../firmware/src/recovery.c"

int main(void)
{
    bootguard_t b = {0};
    assert(bootguard_begin(&b) == BOOT_NORMAL);
    assert(bootguard_begin(&b) == BOOT_NORMAL && b.failed == 1);
    assert(bootguard_begin(&b) == BOOT_RECOVERY && b.pending == 2);
    assert(bootguard_begin(&b) == BOOT_ROM && b.pending == 0);
    assert(bootguard_begin(&b) == BOOT_NORMAL);
    bootguard_clear(&b);                     /* healthy boot / intentional update */
    assert(bootguard_begin(&b) == BOOT_NORMAL && b.failed == 0);
    b.pending = 2; bootguard_clear(&b);
    assert(bootguard_begin(&b) == BOOT_NORMAL);
    b.failed = 0xFFFFFFFFu;
    assert(bootguard_begin(&b) == BOOT_NORMAL && b.failed == 0);
    assert(bootguard_manual(1) && !bootguard_manual(0) && !bootguard_manual(2) && !bootguard_manual(3));

    /* which resets count (fm1_cstart): a power-on and a supply-low reset (the USB cable pulled from a unit on its
     * battery, a battery dip) clear the guard; a watchdog or a soft reset (fm1_fault) counts, even beside them */
    assert(bootguard_power_reset(BG_RST_POR));
    assert(bootguard_power_reset(BG_RST_VDDIO));
    assert(bootguard_power_reset(BG_RST_V12));
    assert(bootguard_power_reset(BG_RST_POR | BG_RST_VDDIO));
    assert(!bootguard_power_reset(BG_RST_WDT));
    assert(!bootguard_power_reset(BG_RST_SOFT));
    assert(!bootguard_power_reset(BG_RST_VDDIO | BG_RST_WDT));
    assert(!bootguard_power_reset(BG_RST_POR | BG_RST_SOFT));
    assert(!bootguard_power_reset(0));
    {   /* fm1_cstart's sequence: supply-low resets in a row never reach rescue; watchdog resets still do */
        bootguard_t g = {0};
        uint32_t k, mode = BOOT_NORMAL;
        bootguard_clear(&g);
        for (k = 0; k < 6u; k++) {
            if (bootguard_power_reset(BG_RST_VDDIO)) bootguard_clear(&g);
            mode = bootguard_begin(&g);
            assert(mode == BOOT_NORMAL);
        }
        for (k = 0; k < 3u && mode == BOOT_NORMAL; k++) {
            if (bootguard_power_reset(BG_RST_WDT)) bootguard_clear(&g);
            mode = bootguard_begin(&g);
        }
        assert(mode == BOOT_RECOVERY);
    }
    {
        char w[24];
        bootguard_why(w, 0x04u, 1, 0x0200ABCDu); assert(!strcmp(w, "RST 04 PC 0200ABCD"));
        bootguard_why(w, 0x42u, 0, 0); assert(!strcmp(w, "RST 42 NO CRASH"));
    }
    input_buttons = 1; assert(recovery_key());
    input_buttons = 3; assert(!recovery_key());
    input_buttons = 0; assert(!recovery_key());
    assert(scans >= 150);

    fm1_ms = 0; recovery_fraction = 0;
    recovery_last = recovery_usb_last = ticks = 0xFFFFF000u;
    ticks += 12000u; recovery_poll();         /* wrap + half a millisecond */
    assert(fm1_ms == 0 && polls == 1);
    ticks += 12000u; recovery_poll();
    assert(fm1_ms == 1 && polls == 2);
    recovery_poll(); assert(polls == 2);     /* never busy-poll USB faster than 2 kHz */

    flash_ok = 0; usb.ota_req = 1; recovery_step();
    assert(sessions == 0 && usb.ota_req == 0);
    flash_ok = 1; usb.ota_req = 1; recovery_step();
    assert(sessions == 1 && fm1_ms == 101 && polls == 202);
    usb.ota_req = 1; recovery_step();
    assert(sessions == 2 && fm1_ms == 201 && polls == 402);
    assert(feeds > 400 && services == 3);
    usb.uboot_req = 1; bootguard.pending = 2; recovery_step();
    assert(rom == 1 && detached == 1 && bootguard.pending == 0);
    usb.uboot_req = 0;

    retries = 0; stop_at_retry = 5;
    if (!setjmp(exit_loop)) recovery_main();
    assert(recovery_active && bootguard.pending == 2 && flash_ok);
    assert(jedec_reads == 1 && lcd_calls == 1);
    assert(nshown == 5u && !strcmp(shown[0], "STALLION USB RESCUE") && !strcmp(shown[4], "RST 00 NO CRASH"));
    nshown = 0; fm1_boot.p3_rst = 0x04u; fm1_crash.magic = FM1_CRASH_MAGIC; fm1_crash.pc = 0x02012340u;
    assert(services == 7 && sessions == 2);  /* idle startup starts no update */
    assert(bootguard_begin(&bootguard) == BOOT_ROM);
    /* the flash parts: the Puya P25Q80 and the UC25HQ80 known, anything else not (and recovery_main reads it) */
    assert(FL_JEDEC_OK(0x856014u) && FL_JEDEC_OK(0xB36014u) && !FL_JEDEC_OK(0xEF4014u) && !FL_JEDEC_OK(0u) && !FL_JEDEC_OK(0xFFFFFFu));
    jedec = 0xB36014u; flash_ok = 0; recovery_active = 0; retries = 0; stop_at_retry = 3;
    if (!setjmp(exit_loop)) recovery_main();
    assert(flash_ok);
    assert(nshown == 5u && !strcmp(shown[4], "RST 04 PC 02012340"));   /* the reset and the crash on the screen */
    jedec = 0xEF4014u; flash_ok = 1; retries = 0; stop_at_retry = 3;
    if (!setjmp(exit_loop)) recovery_main();
    assert(!flash_ok);
    puts("recovery: boot failures, manual key, timer wrap, polled USB during OTA, retry, flash refusal and ROM fallback, the UC25HQ80 flash known, supply-low resets not counted, the reset and crash on the rescue screen passed");
    return 0;
}
