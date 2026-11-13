/* SPDX-License-Identifier: GPL-3.0-only */
/* MENU > SYS > FACTORY RESET, the flash part (firmware/src/storage.c st_wipe_user, st_replace) on a simulated 1 MiB NOR,
 * per board (build with and without -DBOARD_SMK37=1): every byte of the part filled, every object saved three times
 * (both copies valid, an older and a newer record; the current copy A for some objects, B for the others), then:
 *   - st_wipe_user: every object but the settings reads as empty (no copy valid), as on a fresh install; the settings'
 *     two sectors untouched (project.c replaces them with st_replace: the HARDWARE CALIBRATION panel table kept)
 *   - each object's non-current copy erased before its current one, so a power cut between the erases (simulated:
 *     after any number of flash operations nothing more reaches the part) leaves every object either as it was or
 *     empty: never its older record back
 *   - st_replace (the settings, rewritten with the panel table only): the new record, the other copy erased; a cut
 *     anywhere leaves the old record or the new one, never the older one
 *   - every byte outside the objects' sectors is as it was: the app, the head / loader, the update staging, the
 *     user samples (kept), the calibration (SMK: BTIF, USRTRIM), the stock resources, key_mac
 *   - every sector erased is one of the objects' (the list printed), inside the store's allow-list (FL_STORE_OK),
 *     clear of the RAM driver's own protected net (FL_PROTECTED, SMK) and of each protected range listed here
 *   - a refused erase is counted, and the others still go */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fm1_cc.h"
#include "fm1_flash_map.h"
#ifndef NELEM
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#endif

static uint8_t nor[0x100000], before[0x100000], filled[0x100000];
static uint32_t erased[64], n_erased, refused, ticks;
static uint32_t refuse_at = 0xFFFFFFFFu;            /* an erase at this offset fails (a refused / failing part) */
static uint32_t power = 0xFFFFFFFFu;                /* flash operations left before the power goes (the cut) */
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)                   /* as felucca.c: the store's window, then the RAM driver's net */
{
    if ((off & 0xFFFu) || !FL_STORE_OK(off, 0x1000u) || !FL_GUARD(off, 0x1000u) || off == refuse_at) {
        refused++;
        return -8;
    }
    if (!power)
        return -5;                                  /* (the power is gone: nothing reaches the part) */
    power--;
    if (n_erased < 64u)
        erased[n_erased] = off;
    n_erased++;
    memset(nor + off, 0xFF, 0x1000u);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (!FL_STORE_OK(off, n))
        return -8;
    if (!power)
        return -5;
    power--;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
static void tick(void) { ticks++; }
static void fm1_wdt_feed(void) {}                   /* (the store's first-boot erases feed the watchdog) */
#include "../firmware/src/storage.c"

static int bad;
static void check(const char *what, int ok) { printf("factory reset: %-92s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

/* what must never change, per board ([lo, hi)) */
#if BOARD_SMK37
#define BOARD_NAME "SMK-37"
static const uint32_t KEEP[][2] = {
    {0x00000u, 0x04000u},                           /* head (SPL, isd_config) */
    {0x04000u, 0x9D000u},                           /* the app (the update loader writes it) */
    {0xBB000u, 0xC0000u},                           /* update staging + record */
    {0xC0000u, 0xC1000u},                           /* BTIF (BT pairing) */
    {0xC1000u, 0xC2000u},                           /* USRTRIM: the factory key / pad / wheel calibration */
    {0xC2000u, 0xEA000u},                           /* the user sample slots (kept) */
    {0xEA000u, 0xEB000u},                           /* the stock end marker's sector */
    {0xF4000u, 0xFE000u},                           /* USR: the stock resources */
    {0xFF000u, 0x100000u},                          /* key_mac */
};
#else
#define BOARD_NAME "FM-1"
static const uint32_t KEEP[][2] = {
    {0x00000u, 0x97000u},                           /* the app, the loader and the recovery */
    {0xA0000u, 0xDC000u},                           /* the user sample slots USR1..USR3 (kept) */
    {0xE0000u, 0xE5000u},                           /* update staging */
    {0xE7000u, 0xFB000u},                           /* the user sample slot USR4 (kept) */
    {0xFF000u, 0x100000u},                          /* the last sector (the flash's own) */
};
#endif

static int in_list(uint32_t s)
{
    uint32_t i;
    for (i = 0; i < n_erased && i < 64u; i++)
        if (erased[i] == s)
            return 1;
    return 0;
}
static int pos_of(uint32_t s)                       /* where sector s came in the erase order, -1 never */
{
    uint32_t i;
    for (i = 0; i < n_erased && i < 64u; i++)
        if (erased[i] == s)
            return (int)i;
    return -1;
}

/* every object's records: the value its payload bytes hold; newest[o] the current one, older[o] the other copy's */
static uint8_t newest[OBJ_COUNT], older[OBJ_COUNT];
static int cur0[OBJ_COUNT];                         /* the current copy before the reset */
#define BLOB 600u
static void save_val(uint32_t o, uint8_t v)
{
    uint8_t blob[BLOB];
    memset(blob, v, sizeof blob);
    st_save(o, blob, sizeof blob);
}
/* what an object reads now: its payload's value, 0 when it reads empty, 0xFFFF a record that is neither (damaged) */
static uint32_t reads(uint32_t o)
{
    uint8_t blob[BLOB];
    uint32_t i;
    int n = st_load(o, blob, sizeof blob);
    if (n < 0)
        return 0;
    if (n != (int)BLOB)
        return 0xFFFFu;
    for (i = 1; i < BLOB; i++)
        if (blob[i] != blob[0])
            return 0xFFFFu;
    return blob[0];
}
static void fill(void)                              /* the part, the objects' records: once, then copied back */
{
    static int done;
    uint32_t i, o, x = 0x12345678u;
    if (done) {
        memcpy(nor, filled, sizeof nor);
        return;
    }
    for (i = 0; i < sizeof nor; i++) {              /* every byte something (a xorshift): nothing reads as erased */
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        nor[i] = (uint8_t)x;
    }
    for (o = 0; o < OBJ_COUNT; o++) {               /* (a random sector cannot be programmed: erased first) */
        st_erase(st_sector(o, 0));
        st_erase(st_sector(o, 1));
    }
    for (o = 0; o < OBJ_COUNT; o++) {               /* A, then B, then (odd objects) A again: the current copy A or B */
        uint8_t v = (uint8_t)(1u + o * 3u);
        save_val(o, v);
        save_val(o, (uint8_t)(v + 1u));
        older[o] = v;
        newest[o] = (uint8_t)(v + 1u);
        if (o & 1u) {
            save_val(o, (uint8_t)(v + 2u));
            older[o] = (uint8_t)(v + 1u);
            newest[o] = (uint8_t)(v + 2u);
        }
    }
    memcpy(filled, nor, sizeof nor);
    done = 1;
}

int main(void)
{
    uint32_t i, o, c, k, empty = 1, same = 1, keep_hit = 0, guard = 1, objs = 1, order = 1, set_same = 1;
    uint32_t list[64], n_list;
    printf("factory reset (%s)\n", BOARD_NAME);
    fill();
    for (o = 0; o < OBJ_COUNT; o++) {
        st_hdr_t h;
        objs &= st_head(o, 0, &h) == 0 && st_head(o, 1, &h) == 0 && reads(o) == newest[o];
        cur0[o] = st_current(o, &h);
    }
    check("before: every object's two copies hold a valid record, the newer one current", objs);
    check("before: the current copy A for some objects, B for others", cur0[OBJ_SETTINGS] != cur0[OBJ_PROJECT0]);
    memcpy(before, nor, sizeof nor);
    n_erased = refused = ticks = 0;

    check("st_wipe_user: 0 sectors failed", st_wipe_user(tick) == 0);
    for (o = 0; o < OBJ_COUNT; o++) {
        st_hdr_t h;
        if (o == OBJ_SETTINGS)
            continue;
        empty &= reads(o) == 0 && st_head(o, 0, &h) != 0 && st_head(o, 1, &h) != 0;
    }
    check("every object but the settings reads empty (no copy valid): projects, presets, autosave, FM6 bank", empty);
    for (c = 0; c < 2u; c++)
        set_same &= !memcmp(nor + st_sector(OBJ_SETTINGS, c), before + st_sector(OBJ_SETTINGS, c), ST_SECTOR);
    check("the settings' two sectors untouched by st_wipe_user (st_replace rewrites them: the panel table kept)",
          set_same && reads(OBJ_SETTINGS) == newest[OBJ_SETTINGS]);
    check("two sectors erased per other object, and the hook called after each",
          n_erased == 2u * (OBJ_COUNT - 1u) && ticks == 2u * (OBJ_COUNT - 1u));
    for (o = 0; o < OBJ_COUNT; o++)
        if (o != OBJ_SETTINGS)
            order &= pos_of(st_sector(o, cur0[o] ? 0u : 1u)) < pos_of(st_sector(o, (uint32_t)cur0[o]));
    check("each object's non-current copy erased before its current one", order);
    n_list = n_erased < 64u ? n_erased : 64u;
    memcpy(list, erased, n_list * sizeof list[0]);
    for (i = 0; i < sizeof nor; i++) {
        uint32_t s = i & ~0xFFFu;
        if (in_list(s)) {
            if (nor[i] != 0xFFu)
                same = 0;
        } else if (nor[i] != before[i]) {
            same = 0;
        }
    }
    check("every byte outside the erased sectors identical (settings, app, loader, staging, samples, calibration ..)",
          same);
    for (i = 0; i < n_erased && i < 64u; i++) {
        for (k = 0; k < NELEM(KEEP); k++)
            keep_hit += erased[i] < KEEP[k][1] && erased[i] + 0x1000u > KEEP[k][0];
        guard &= FL_STORE_OK(erased[i], 0x1000u) && FL_GUARD(erased[i], 0x1000u);
#if BOARD_SMK37
        guard &= !FL_PROTECTED(erased[i], 0x1000u) && erased[i] >= FL_DATA_LO && erased[i] + 0x1000u <= FL_DATA_HI;
#endif
    }
    check("no erased sector overlaps a kept range (app, head, staging, samples, calibration, stock, key_mac)", keep_hit == 0);
    check("every erased sector inside the store's allow-list and clear of the driver's protected net", guard);
    for (o = 0; o < OBJ_COUNT; o++) {               /* the same, statically, for every object's sectors (the settings' too) */
        for (c = 0; c < 2u; c++) {
            uint32_t s = st_sector(o, c);
            for (k = 0; k < NELEM(KEEP); k++)
                keep_hit += s < KEEP[k][1] && s + 0x1000u > KEEP[k][0];
            guard &= FL_STORE_OK(s, 0x1000u) && FL_GUARD(s, 0x1000u);
        }
    }
    check("st_sector of every object (A/B, the settings' too) clear of every kept range, inside the allow-list",
          keep_hit == 0 && guard);

    {   /* a power cut after any number of erases: every object as it was, or empty; never its older record */
        uint32_t cut, resurrected = 0, damaged = 0, settings_moved = 0;
        for (cut = 0; cut <= 2u * (OBJ_COUNT - 1u); cut++) {
            fill();
            power = cut;
            st_wipe_user(0);
            power = 0xFFFFFFFFu;
            for (o = 0; o < OBJ_COUNT; o++) {
                uint32_t r = reads(o);
                resurrected += r == older[o];
                damaged += r != 0u && r != older[o] && r != newest[o];
                settings_moved += o == OBJ_SETTINGS && r != newest[o];
            }
        }
        check("a power cut between any two erases: each object as it was or empty, never its older record back",
              resurrected == 0 && damaged == 0 && settings_moved == 0);
    }

    {   /* st_replace: the settings rewritten (the panel table only, project.c), the other copy erased */
        uint8_t nb[BLOB];
        uint32_t ops, cut, resurrected = 0, damaged = 0, s_other;
        st_hdr_t h;
        memset(nb, 0xEE, sizeof nb);
        fill();
        n_erased = 0;
        power = 1000000u;
        check("st_replace: 0 (written, the other copy erased)", st_replace(OBJ_SETTINGS, nb, sizeof nb) == 0);
        ops = 1000000u - power;
        power = 0xFFFFFFFFu;
        c = (uint32_t)st_current(OBJ_SETTINGS, &h);
        s_other = st_sector(OBJ_SETTINGS, c ? 0u : 1u);
        same = 1;
        for (i = 0; i < ST_SECTOR; i++)
            same &= nor[s_other + i] == 0xFFu;
        check("  .. the new record current, the other copy erased (no older record left to come back)",
              reads(OBJ_SETTINGS) == 0xEEu && same && st_head(OBJ_SETTINGS, c ? 0u : 1u, &h) != 0);
        check("  .. both settings sectors written, the old copy last", n_erased == 2u && pos_of(s_other) == 1);
        for (i = 0; i < n_erased && n_list < 64u; i++)
            list[n_list++] = erased[i];
        for (cut = 0; cut <= ops; cut++) {
            fill();
            power = cut;
            st_replace(OBJ_SETTINGS, nb, sizeof nb);
            power = 0xFFFFFFFFu;
            resurrected += reads(OBJ_SETTINGS) == older[OBJ_SETTINGS];
            damaged += reads(OBJ_SETTINGS) != newest[OBJ_SETTINGS] && reads(OBJ_SETTINGS) != 0xEEu;
        }
        check("  .. a power cut anywhere in it: the old record or the new one, never the older one",
              resurrected == 0 && damaged == 0);
        fill();
        refuse_at = st_sector(OBJ_SETTINGS, (uint32_t)cur0[OBJ_SETTINGS] ? 0u : 1u);   /* (the copy it writes) */
        check("  .. a write the flash refuses: an error, the old record still current",
              st_replace(OBJ_SETTINGS, nb, sizeof nb) != 0 && reads(OBJ_SETTINGS) == newest[OBJ_SETTINGS]);
        refuse_at = 0xFFFFFFFFu;
    }
    printf("factory reset: erased (%s):", BOARD_NAME);
    for (i = 0; i < n_list; i++)
        printf(" %05X", (unsigned)list[i]);
    printf("   (the last two: the settings, rewritten with the panel table only)\n");

    {   /* a refused erase: counted, and the rest still erased */
        fill();
        refuse_at = st_sector(OBJ_PROJECT0, 1);
        n_erased = refused = 0;
        check("a refused sector counts as failed (1), the other sectors erased",
              st_wipe_user(0) == 1u && refused == 1u && n_erased == 2u * (OBJ_COUNT - 1u) - 1u);
        refuse_at = 0xFFFFFFFFu;
    }
    printf("%s\n", bad ? "FACTORY RESET TEST FAILED" : "factory reset test passed");
    return bad != 0;
}
