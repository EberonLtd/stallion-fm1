/* SPDX-License-Identifier: GPL-3.0-only */
/* Warm-reset guard, retained in RAM only. Never writes the bootloader/flash.
 * This cannot rescue an image that fails before reaching the application. */
#ifndef FELUCCA_BOOTGUARD_H
#define FELUCCA_BOOTGUARD_H
#include <stdint.h>
#define BOOTGUARD_MAGIC 0x42475232u
typedef struct { uint32_t magic, failed, pending; } bootguard_t;
enum { BOOT_NORMAL, BOOT_RECOVERY, BOOT_ROM };
static void bootguard_clear(bootguard_t *b)
{
    b->magic = BOOTGUARD_MAGIC;
    b->failed = b->pending = 0;
}
static uint32_t bootguard_begin(bootguard_t *b)
{
    if (b->magic != BOOTGUARD_MAGIC || b->failed > 2u || b->pending > 2u)
        bootguard_clear(b);
    if (b->pending == 2u) {           /* recovery itself reset: keep ROM fallback */
        bootguard_clear(b);
        return BOOT_ROM;
    }
    if (b->pending && b->failed < 2u) b->failed++;
    b->pending = b->failed >= 2u ? 2u : 1u;
    return b->pending == 2u ? BOOT_RECOVERY : BOOT_NORMAL;
}
/* P3_RST_SRC (hal/fm1_sys.h fm1_boot.p3_rst): bit0 power-on, 1 VDDIO low, 2 WDT, 5 1.2 V low, 6 soft (P33).
 * Only a watchdog or a software reset (fm1_fault's reboot) can be a failed boot of this image. A power-on or a
 * supply-low reset (a battery or a power-path dip, e.g. the USB cable pulled while running) is not the firmware's:
 * it clears the count instead of adding to it. */
#define BG_RST_POR 0x01u
#define BG_RST_VDDIO 0x02u
#define BG_RST_WDT 0x04u
#define BG_RST_V12 0x20u
#define BG_RST_SOFT 0x40u
static int bootguard_power_reset(uint32_t p3)
{
    return (p3 & (BG_RST_POR | BG_RST_VDDIO | BG_RST_V12)) && !(p3 & (BG_RST_WDT | BG_RST_SOFT));
}
/* the rescue screen's evidence line: the reset that led there and the last crash record (a photo of it is the
 * report): "RST 04 PC 0200ABCD" (crash record valid) or "RST 04 NO CRASH"; b holds 24 */
static void bootguard_why(char *b, uint32_t p3, int crashed, uint32_t pc)
{
    static const char H[] = "0123456789ABCDEF";
    uint32_t i, n = 0;
    const char *t = crashed ? " PC " : " NO CRASH";
    b[n++] = 'R'; b[n++] = 'S'; b[n++] = 'T'; b[n++] = ' ';
    b[n++] = H[(p3 >> 4) & 15u];
    b[n++] = H[p3 & 15u];
    while (*t)
        b[n++] = *t++;
    if (crashed)
        for (i = 0; i < 8u; i++)
            b[n++] = H[(pc >> (28u - 4u * i)) & 15u];
    b[n] = 0;
}
#ifndef FM1_OCT_BOTH                  /* (host test without a HAL: the FM-1's OCT- = 0, OCT+ = 1) */
#define FM1_BTN_OCT_DOWN 0
#define FM1_OCT_BOTH 3u
#endif
static int bootguard_manual(uint32_t buttons)
{
    return (buttons & FM1_OCT_BOTH) == 1u << FM1_BTN_OCT_DOWN;     /* physical OCT- alone; both keep calibration */
}
#endif
