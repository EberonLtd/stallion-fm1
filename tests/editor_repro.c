/* SPDX-License-Identifier: GPL-3.0-only */
/* Reproducers of what tests/editor_fuzz.c found in Clipper (CLIPPER_SYNTH=1), on tests/editor_host.c. Each case
 * returns 0 (passes) once fixed, 1 on the code as it was:
 *   editor_repro 1   a user wavetable (WAVE's USR1..4, an FWTB header in a sample slot) is left out of the backup: BK_LIST
 *                    lists the slot with 0 bytes, so the editor's restore of that backup erases the slot (SMP_ERASE)
 *   editor_repro 2   TRACK_MIX of track 4 (the groovebox's drum level) writes 0..127 into G_DRLVL, which Clipper uses as the
 *                    master EQ's LOW (88..112): the value goes past its range (the EQ reads up to +-12 dB, the page shows it)
 *   editor_repro 4   the backup of the user preset banks (objects 6, 7) holds the records only: each preset's Clipper 0.8+ values
 *                    (MOD SEQ RATE LEN SMTH START, SPICE, DICE, VINTAGE, LOOP: the bank's side record) and 1.0 values
 *                    (SPREAD .. matrix slot 12, the macros: its tail) are not in it, and a restore keeps the side / tail of
 *                    what the bank held before in RAM: a preset comes back with another preset's values (and its defaults
 *                    after the next power-on)
 *   editor_repro 3   with any step on track 3 (an old SLOOP project loaded, the editor's TRACK_STEP), a project did not keep
 *                    Clipper 1.0's values (SPREAD, LFO 3, ENV 2 LOOP, the MACROS, matrix slots 7..12, the delay MODE): a
 *                    section saved and loaded (RAM, flash), a power cycle, the autosave, a backup restored all brought them
 *                    back at their defaults; track 3's own nudges must stay its own
 * Stallion (specs/touch R28, R37; specs/stallion R43), each 0 once it holds:
 *   editor_repro 5   GLO > TOUCH and TOUCH CAL's ends (the settings object's tail) are backed up (object 15, listed to a
 *                    LIST with bit 0 only) and restored: a backup without it leaves them as they are, a damaged one is
 *                    refused, a power cycle after the restore still has them; the LEVER's mode (touch_ext, after the
 *                    ends) with them: each mode through object 15, one made before it (116 bytes) gives LATCH, a later
 *                    firmware's longer body gives its mode
 *   editor_repro 6   the MOTION lanes ride inside the projects: the backup of the working project (0) and of a section
 *                    (2) restored on a unit with no lane gives the lanes back, and the objects read again byte for byte
 *   editor_repro 7   the BEND page (P_BUP .. P_BTGT, device-only ids) and the matrix's new sources (BEND, POLAT, CHNAT,
 *                    RIBBN) and destinations (PIT1, PIT2, XMOD, CUTB; on PALOMINO) of a user preset survive the backup of
 *                    the preset banks (objects 6, 7) restored over other presets, and a power-on after; FILTER FM and RING
 *                    with them; a Clipper / SLOOP backup's bank is refused (rc 2) and leaves the presets as they were
 *   editor_repro 9   preset format 2: a user preset's 24 matrix slots through the banks' backup (objects 6, 7) and a power-on;
 *                    slots 13..24 of both parts of a project with a drum hit through RAM, flash, a power cycle, the
 *                    autosave and the backup (object 14); an object 14 made before them loads them off
 *   editor_repro 8   MARE's FILTER FM and RING with a drum hit (beside the project only) survive RAM, flash, a power cycle,
 *                    the autosave and the backup (object 14); an older object 14 gives 0 with a hit, keeps them without
 * Build as tests/editor_fuzz.sh builds editor_fuzz.c (CLIPPER_SYNTH=1). */
#include "editor_host.c"

static void ed(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint8_t f[700];
    f[0] = ED_HDR0; f[1] = ED_HDR1; f[2] = ED_HDR2; f[3] = (uint8_t)cmd;
    memcpy(f + 4, a, na);
    usb_sysex(f, 4 + na);                               /* through usb.c's SysEx assembly, as the editor sends it */
}
static uint32_t pack7(uint8_t *o, const uint8_t *p, uint32_t n)
{
    uint32_t w = 0;
    while (n) {
        uint32_t k = n > 7u ? 7u : n, m = 0, i;
        for (i = 0; i < k; i++) m |= (uint32_t)(p[i] >> 7) << i;
        o[w++] = (uint8_t)m;
        for (i = 0; i < k; i++) o[w++] = p[i] & 127u;
        p += k; n -= k;
    }
    return w;
}
static uint32_t v14(uint8_t *o, int32_t v) { uint32_t u = (uint32_t)(v + 8192); o[0] = u & 127u; o[1] = (u >> 7) & 127u; return 2; }

static int repro_wt_backup(void)
{
    uint8_t a[700], hdr[sizeof(smp_user_hdr_t)];
    wt_user_hdr_t *h = (wt_user_hdr_t *)hdr;
    uint32_t n, off, i, len = 99, pos;
    a[0] = 0; ed(ED_SMP_BEGIN, a, 1);                   /* a wavetable into USR1, as the editor's Samples page sends it */
    for (off = 0; off < WT_USER_LEN; off += 256) {
        uint8_t d[256];
        uint32_t o = SMP_USER_DATA + off;
        for (i = 0; i < 256; i++) d[i] = (uint8_t)(off / 256 + i * 7);
        n = 0; a[n++] = 0; a[n++] = o & 127u; a[n++] = (o >> 7) & 127u; a[n++] = (o >> 14) & 127u;
        n += pack7(a + n, d, 256);
        ed(ED_SMP_WRITE, a, n);
    }
    memset(hdr, 0xFF, sizeof hdr);
    memset(h, 0, sizeof *h);
    h->magic = WT_USER_MAGIC; h->version = 1; h->nf = WT_NF; h->nh = WT_NH; memcpy(h->name, "MYTABLE", 7);
    h->data_len = WT_USER_LEN; h->crc = st_crc32(nor + SMP_USER_OFF(0) + SMP_USER_DATA, WT_USER_LEN);
    n = 0; a[n++] = 0; n += pack7(a + n, hdr, sizeof hdr);
    ed(ED_SMP_END, a, n);
    printf("SMP_END rc %u; wt_user_ok[0] %u\n", rx_n > 6 ? rx_last[6] : 99u, wt_user_ok[0]);
    ed(ED_SMP_INFO, a, 0);
    printf("SMP_INFO: USR1 zones %u (127 = a wavetable)\n", rx_n > 7 ? rx_last[7] : 99u);
    ed(ED_BK_LIST, a, 0);                               /* -> rc, count, per object: id, len (5), crc (5) */
    for (pos = 7; pos + 11 <= rx_n; pos += 11)
        if (rx_last[pos] == 32u) {
            len = 0;
            for (i = 0; i < 5; i++) len |= (uint32_t)rx_last[pos + 1 + i] << (7 * i);
        }
    printf("BK_LIST: object 32 (USR1) %u bytes (the slot holds %u)\n", len, SMP_USER_DATA + WT_USER_LEN);
    if (len != SMP_USER_DATA + WT_USER_LEN)
        return 1;                                       /* (the editor's restore then sends SMP_ERASE: the table is gone) */
    {   /* read it as the editor's backup does, erase the slot, write it back as its restore does: the same table */
        static uint8_t img[SMP_USER_DATA + WT_USER_LEN], was[SMP_USER_DATA + WT_USER_LEN];
        memcpy(was, nor + SMP_USER_OFF(0) + SMP_USER_DATA, WT_USER_LEN);
        for (off = 0; off < len; off += 256) {
            uint32_t k, w;
            n = 0; a[n++] = 32; for (i = 0; i < 5; i++) a[n++] = (off >> (7 * i)) & 127u; a[n++] = 0; a[n++] = 2;
            ed(ED_BK_GET, a, n);
            if (rx_n < 13 || rx_last[6]) { printf("BK_GET rc %u\n", rx_n > 6 ? rx_last[6] : 99u); return 1; }
            for (k = 14, w = 0; w < 256 && k < rx_n - 1; ) {
                uint32_t m = rx_last[k++], j;
                for (j = 0; j < 7 && w < 256 && k < rx_n - 1; j++) img[off + w++] = (uint8_t)(rx_last[k++] | ((m >> j) & 1u) << 7);
            }
        }
        a[0] = 0; ed(ED_SMP_ERASE, a, 1);
        a[0] = 0; ed(ED_SMP_BEGIN, a, 1);
        for (off = SMP_USER_DATA; off < len; off += 256) {
            n = 0; a[n++] = 0; a[n++] = off & 127u; a[n++] = (off >> 7) & 127u; a[n++] = (off >> 14) & 127u;
            n += pack7(a + n, img + off, 256);
            ed(ED_SMP_WRITE, a, n);
        }
        n = 0; a[n++] = 0; n += pack7(a + n, img, sizeof(smp_user_hdr_t));
        ed(ED_SMP_END, a, n);
        printf("restored: SMP_END rc %u, wt_user_ok[0] %u, the spectra %s\n", rx_n > 6 ? rx_last[6] : 99u, wt_user_ok[0],
               memcmp(was, nor + SMP_USER_OFF(0) + SMP_USER_DATA, WT_USER_LEN) ? "differ" : "the same");
        return !wt_user_ok[0] || memcmp(was, nor + SMP_USER_OFF(0) + SMP_USER_DATA, WT_USER_LEN) != 0;
    }
}

static int repro_eq_low(void)
{
    uint8_t a[8];
    a[0] = TRK_DRUM; v14(a + 1, 127); a[3] = 0;
    ed(ED_TRACK_MIX, a, 4);
    printf("TRACK_MIX track 4 level 127: g[G_DRLVL] (%s) = %d, its range %d..%d\n", GP[G_DRLVL].label, song.g[G_DRLVL],
           GP[G_DRLVL].min, GP[G_DRLVL].max);
    return song.g[G_DRLVL] < GP[G_DRLVL].min || song.g[G_DRLVL] > GP[G_DRLVL].max;
}

/* read backup object id (n bytes) as the editor's backup does */
static int bk_read(uint32_t id, uint8_t *img, uint32_t n)
{
    uint8_t a[16];
    uint32_t off, i;
    ed(ED_BK_LIST, a, 0);
    for (off = 0; off < n; off += 256) {
        uint32_t c = n - off > 256 ? 256 : n - off, k, w, m;
        m = 0; a[m++] = (uint8_t)id; for (i = 0; i < 5; i++) a[m++] = (off >> (7 * i)) & 127u; a[m++] = c & 127u; a[m++] = (uint8_t)(c >> 7);
        ed(ED_BK_GET, a, m);
        if (rx_n < 13 || rx_last[6]) return 1;
        for (k = 14, w = 0; w < c && k < rx_n - 1; ) {
            uint32_t mm = rx_last[k++], j;
            for (j = 0; j < 7 && w < c && k < rx_n - 1; j++) img[off + w++] = (uint8_t)(rx_last[k++] | ((mm >> j) & 1u) << 7);
        }
    }
    return 0;
}
static int bk_write(uint32_t id, const uint8_t *img, uint32_t n)
{
    uint8_t a[700];
    uint32_t off, i, m, crc = st_crc32(img, n);
    m = 0; a[m++] = 0; a[m++] = (uint8_t)id; for (i = 0; i < 5; i++) a[m++] = (n >> (7 * i)) & 127u; for (i = 0; i < 5; i++) a[m++] = (crc >> (7 * i)) & 127u;
    ed(ED_BK_PUT, a, m);
    for (off = 0; off < n; off += 256) {
        uint32_t c = n - off > 256 ? 256 : n - off;
        m = 0; a[m++] = 1; a[m++] = (uint8_t)id; for (i = 0; i < 5; i++) a[m++] = (off >> (7 * i)) & 127u;
        m += pack7(a + m, img + off, c);
        ed(ED_BK_PUT, a, m);
    }
    a[0] = 2; a[1] = (uint8_t)id; ed(ED_BK_PUT, a, 2);
    return rx_n > 7 ? rx_last[7] : 99;
}
static int bk_listed(uint32_t id)                     /* the length BK_LIST gives object id, -1: not listed */
{
    uint8_t a[2];
    uint32_t pos, i, len;
    ed(ED_BK_LIST, a, 0);
    for (pos = 7; pos + 11 <= rx_n; pos += 11)
        if (rx_last[pos] == id) {
            for (len = 0, i = 0; i < 5; i++) len |= (uint32_t)rx_last[pos + 1 + i] << (7 * i);
            return (int)len;
        }
    return -1;
}
static int repro_up_backup(void)
{
    static uint8_t img[4][4096];
    static const uint8_t OBJ[4] = {6, 7, 12, 13};      /* (12, 13: the side records and tails of 1.x; preset format 2
                                                         * lists neither: the banks hold every value) */
    int len[4];
    uint8_t a[16];
    static const uint8_t ID[3] = {P_SPREAD, P_MSRATE, P_VINT};
    int16_t want[3];
    uint32_t i, o;
    int bad = 0, rc;
    for (i = 0; i < 3; i++) {                           /* the sound: these three away from their defaults */
        const param_desc_t *d = &TP[ID[i]];
        want[i] = (int16_t)(d->def == d->max ? d->min : d->max);
        a[0] = 0; a[1] = ID[i]; v14(a + 2, want[i]); ed(ED_SET, a, 4);
    }
    a[0] = 0; memcpy(a + 1, "KEEP ME", 8); ed(ED_UP_STORE, a, 9);   /* -> user preset 1 */
    printf("UP_STORE rc %u\n", rx_n > 6 ? rx_last[6] : 99u);
    for (o = 0; o < 4; o++) {                           /* the editor's backup: every user preset object LIST gives */
        len[o] = bk_listed(OBJ[o]);
        if (len[o] > 0 && bk_read(OBJ[o], img[o], (uint32_t)len[o])) { printf("BK_GET %u failed\n", OBJ[o]); return 1; }
        printf("backup: object %u, %d bytes\n", OBJ[o], len[o]);
    }
    for (i = 0; i < 3; i++) { a[0] = 0; a[1] = ID[i]; v14(a + 2, TP[ID[i]].def); ed(ED_SET, a, 4); }
    a[0] = 0; memcpy(a + 1, "OTHER", 6); ed(ED_UP_STORE, a, 7);     /* another sound into slot 1 meanwhile */
    for (o = 0; o < 4; o++)                             /* the restore, in the editor's order (BK.RESTORE) */
        if (len[o] >= 0) {
            rc = bk_write(OBJ[o], img[o], (uint32_t)len[o]);
            printf("restore of object %u: rc %d\n", OBJ[o], rc);
        }
    a[0] = 0; ed(ED_UP_LOAD, a, 1);
    for (i = 0; i < 3; i++) {
        printf("%-6s stored %d, after the backup's restore %d (%s)\n", TP[ID[i]].label, want[i], trk[0].p[ID[i]], up_rec(0)->name);
        bad |= trk[0].p[ID[i]] != want[i];
    }
    up_boot();                                          /* the next power-on: the banks, their side records, tails from flash */
    a[0] = 0; ed(ED_UP_LOAD, a, 1);
    for (i = 0; i < 3; i++) {
        printf("%-6s after a power-on %d\n", TP[ID[i]].label, trk[0].p[ID[i]]);
        bad |= trk[0].p[ID[i]] != want[i];
    }
    return bad;
}

/* ---- 3: Clipper 1.0's values with steps on track 3 (an old SLOOP project loaded): a save, its load from RAM and from
 * flash, a power cycle, the autosave and its resume, the editor's backup and restore must all give them back, and track
 * 3's own nudges must stay its own (SLOOP plays them) */
static const uint8_t X10_ID[4] = {P_SPREAD, P_MAC3, P_E2LOOP, P_M7AMT};
static int16_t x10_want[4];
static void x10_set(int want)                           /* the four values and the delay MODE: wanted, or the defaults */
{
    uint8_t a[8];
    uint32_t i;
    for (i = 0; i < 4; i++) { a[0] = 0; a[1] = X10_ID[i]; v14(a + 2, want ? x10_want[i] : TP[X10_ID[i]].def); ed(ED_SET, a, 4); }
    a[0] = 1; a[1] = G_DMODE; v14(a + 2, want ? GP[G_DMODE].max : GP[G_DMODE].def); ed(ED_SET, a, 4);
}
static int x10_check(const char *when)
{
    uint32_t i;
    int bad = 0;
    printf("%-34s", when);
    for (i = 0; i < 4; i++) {
        printf(" %s %d/%d", TP[X10_ID[i]].label, trk[0].p[X10_ID[i]], x10_want[i]);
        bad |= trk[0].p[X10_ID[i]] != x10_want[i];
    }
    printf(" MODE %d/%d, track 3: step %u nudge %d", song.g[G_DMODE], GP[G_DMODE].max, trk[2].step[1].n, trk[2].micro[1]);
    bad |= song.g[G_DMODE] != GP[G_DMODE].max || trk[2].step[1].n != 1u || trk[2].micro[1] != -7;
    printf("  %s\n", bad ? "LOST" : "ok");
    return bad;
}
static void proj_op_on(uint32_t op, uint32_t slot)      /* ED_PROJECT op, then the audio a moment (its transport_req done) */
{
    uint8_t a[2];
    a[0] = (uint8_t)op; a[1] = (uint8_t)slot; ed(ED_PROJECT, a, 2);
    audio_blocks(8);
}
static void proj_op(uint32_t op) { proj_op_on(op, 0); }   /* .. on A */
static void power_cycle(void)                           /* a cold start: RAM's projects gone, the boot as main.c runs it */
{
    uint32_t i;
    memset(proj_slot, 0, sizeof proj_slot);
#ifdef PROJ_X10_MAGIC
    memset(proj_x10s, 0, sizeof proj_x10s);             /* (.pool: zeroed at boot) */
    memset(proj_xss, 0, sizeof proj_xss);
#endif
#ifdef PROJ_MX_MAGIC
    memset(proj_mxs, 0, sizeof proj_mxs);
#endif
    for (i = 0; i < NTRK; i++) {                        /* the sounds as before the autosave resumes */
        uint32_t k;
        for (k = P_SPREAD; k < P_E0; k++) trk[i].p[k] = TP[k].def;
        memset(trk[i].step, 0, sizeof trk[i].step);
        memset(trk[i].micro, 0, sizeof trk[i].micro);
    }
    song.g[G_DMODE] = GP[G_DMODE].def;
    persist_boot();
    audio_blocks(8);
}
static int repro_x10(void)
{
    static uint8_t img[6][4096];
    static const uint8_t OBJ[6] = {2, 3, 4, 5, 0, 14};  /* (the editor's restore order; 14 once fixed) */
    int len[6], bad = 0;
    uint8_t a[16];
    uint32_t i;
    a[0] = 2; a[1] = 1; a[2] = 1; a[3] = 60; a[4] = a[5] = a[6] = 0; a[7] = ST_NOTE; a[8] = 0; a[9] = 100;
    ed(ED_TRACK_STEP, a, 10);                           /* track 3, step 2: a note (as an old SLOOP project has them) */
    a[0] = 2; a[1] = 1; a[2] = 64 - 7; ed(ED_MICRO_SET, a, 3);   /* .. nudged early */
    for (i = 0; i < 4; i++) {
        const param_desc_t *d = &TP[X10_ID[i]];
        x10_want[i] = (int16_t)(d->def == d->max ? d->min : d->max);
    }
    x10_set(1);
    proj_op(1);           /* save into A */
    x10_set(0);
    proj_op(0);           /* load A (from RAM) */
    bad |= x10_check("section A saved, loaded");
    x10_set(0);
    proj_slot[0].magic ^= 1u;                           /* (RAM's copy gone: from flash) */
    proj_op(0);
    bad |= x10_check("loaded from flash");
    power_cycle();
    proj_op(0);
    bad |= x10_check("after a power cycle, loaded");
    x10_set(0);                                         /* live section B (SAVE + B), written when quiet */
    section_store(1);
    sections_write();
    x10_set(1);                                         /* stored again with the values (the project's bytes the */
    section_store(1);                                   /* same: they are only beside it), written when quiet */
    sections_write();
#ifdef PROJ_X10_MAGIC
    memset(proj_xss, 0, sizeof proj_xss);
    memset(proj_x10s, 0, sizeof proj_x10s);             /* a warm reset: the sections kept in RAM (.noinit), the */
#endif                                                  /* blocks not (.pool) */
    sec_dirty = 0;
    persist_boot();
    audio_blocks(8);
    x10_set(0);
    proj_op_on(0, 1);
    bad |= x10_check("live section B, warm reset, B");
    power_cycle();
    x10_set(0);
    proj_op_on(0, 1);
    bad |= x10_check("live section B, power cycle, B");
    proj_op(0);
    fm1_ms += 60000u;                                   /* idle long enough: the autosave */
    ui_input_ms = fm1_ms - 10000u;
    autosave_tick();
    power_cycle();
    autosave_resume();
    audio_blocks(8);
    bad |= x10_check("after a power cycle, autosave resumed");
    for (i = 0; i < 6; i++) {                           /* the editor's backup: every listed project object (and 14) */
        len[i] = bk_listed(OBJ[i]);
        if (len[i] > 0 && bk_read(OBJ[i], img[i], (uint32_t)len[i])) { printf("BK_GET %u failed\n", OBJ[i]); return 1; }
    }
    for (i = 0; i < 5; i++) {                           /* restored into another unit: no section, no autosave in it */
        uint32_t c, obj = i < 4 ? OBJ_PROJECT0 + i : OBJ_AUTOSAVE;
        for (c = 0; c < 2; c++) st_erase(st_sector(obj, c));
    }
    power_cycle();
    x10_set(0);
    for (i = 0; i < 6; i++)
        if (len[i] >= 0)
        {
            printf("restore of object %u (%d bytes): rc %d\n", OBJ[i], len[i], bk_write(OBJ[i], img[i], (uint32_t)len[i]));
            audio_blocks(8);
        }
    bad |= x10_check("backup restored: the working sound");
    x10_set(0);
    proj_op(0);
    bad |= x10_check("backup restored: A loaded");
    power_cycle();
    proj_op(0);
    bad |= x10_check("backup restored, power cycle, A");
    return bad;
}

/* ---- 5..7: Stallion's state in the editor's backup */
static int bk_listed_x(uint32_t id, uint32_t x)         /* bk_listed, the LIST's first byte x (bit 0: object 15 too) */
{
    uint8_t a[2];
    uint32_t pos, i, len;
    a[0] = (uint8_t)x;
    ed(ED_BK_LIST, a, 1);
    for (pos = 7; pos + 11 <= rx_n; pos += 11)
        if (rx_last[pos] == id) {
            for (len = 0, i = 0; i < 5; i++) len |= (uint32_t)rx_last[pos + 1 + i] << (7 * i);
            return (int)len;
        }
    return -1;
}
static int touch_same(const touch_cfg_t *c, const touch_cal_t *k, const char *when)
{
    int bad = memcmp(&touch_cfg, c, sizeof *c) || memcmp(&touch_cal, k, sizeof *k);
    printf("%-44s KEY TOUCH %u AT SENS %u CURVE %u OFFSET %d LEVER %u (mode %u) MOTION REC %u RIB RANGE %u RIB RETURN %u, "
           "CAL %u  %s\n", when, touch_cfg.key_touch, touch_cfg.at_sens, touch_cfg.at_curve, touch_cfg.at_offset, touch_cfg.lever,
           touch_ext.lever_mode, touch_cfg.motion_rec, touch_cfg.rib_range, touch_cfg.rib_return, touch_cal.cal,
           bad ? "DIFFERS" : "ok");
    return bad;
}
static int repro_touch_backup(void)
{
    static uint8_t img1[4096], img15[256], again[256];
    touch_cfg_t want = TOUCH_CFG_DEFAULTS, dflt = TOUCH_CFG_DEFAULTS;
    touch_cal_t kwant, knone;
    touch_store_t ts;
    int len1, len15, bad = 0, rc;
    uint32_t i;
    want.key_touch = KT_VELAT, want.at_sens = 55, want.at_out = AT_OUT_POLY, want.at_curve = AT_SOFT, want.at_offset = -3;
    want.lever = 0, want.rib_spring = 0, want.motion_rec = 0, want.rib_steps = 48, want.rib_range = 7;
    want.at_attack = 120, want.at_release = 300, want.lever_time = 450, want.rib_delay = 900, want.rib_return = 2500;
    memset(&kwant, 0, sizeof kwant);
    for (i = 0; i < KT_NKEY; i++) kwant.floor_[i] = (uint16_t)(400u + 7u * i), kwant.ceil[i] = (uint16_t)(850u + 5u * i);
    kwant.cal = 1;
    touch_cfg = want;                                   /* as GLO > TOUCH and TOUCH CAL leave them, saved */
    touch_ext.lever_mode = LV_HOLD;                     /* (not the default, LATCH) */
    kt_cal_take(&kwant);
    kwant = touch_cal;                                  /* (the ends as kept) */
    at_build();
    settings_save();
    if (bk_listed(15) >= 0) { printf("an older editor's LIST (no argument) lists object 15\n"); bad = 1; }
    len1 = bk_listed_x(1, 1);
    len15 = bk_listed_x(15, 1);
    printf("backup: object 1 %d bytes, object 15 %d bytes (touch_store_t %u)\n", len1, len15, (unsigned)sizeof(touch_store_t));
    if (len15 != (int)sizeof(touch_store_t) || len1 <= 0) return 1;
    if (bk_read(1, img1, (uint32_t)len1) || bk_read(15, img15, (uint32_t)len15)) { printf("BK_GET failed\n"); return 1; }
    touch_take(0, 0);                                   /* another unit: the defaults, saved */
    settings_save();
    memset(&knone, 0, sizeof knone);
    rc = bk_write(1, img1, (uint32_t)len1);             /* a backup made before (the settings, no object 15) */
    printf("restore of object 1 alone: rc %d\n", rc);
    bad |= rc != 0 || touch_same(&dflt, &knone, "a backup without object 15: as they were") || touch_ext.lever_mode != LV_LATCH;
    rc = bk_write(15, img15, (uint32_t)len15);
    printf("restore of object 15: rc %d\n", rc);
    bad |= rc != 0 || touch_same(&want, &kwant, "object 15 restored") || touch_ext.lever_mode != LV_HOLD;
    power_cycle();
    bad |= touch_same(&want, &kwant, "after a power cycle") || touch_ext.lever_mode != LV_HOLD;
    len15 = bk_listed_x(15, 1);
    bad |= len15 != (int)sizeof ts || bk_read(15, again, sizeof ts) || memcmp(again, img15, sizeof ts);
    printf("object 15 read again: %s\n", memcmp(again, img15, sizeof ts) ? "DIFFERS" : "the same bytes");
    memcpy(&ts, img15, sizeof ts);                      /* damaged: a byte of the body, the object's CRC right */
    ts.c.at_sens ^= 1u;
    rc = bk_write(15, (const uint8_t *)&ts, sizeof ts);
    printf("restore of a damaged object 15: rc %d (2: refused)\n", rc);
    bad |= rc != 2 || touch_same(&want, &kwant, "after the damaged one");
    memcpy(&ts, img15, sizeof ts);                      /* a shorter body (an older layout): the rest at the defaults */
    ts.len = 20u;
    ts.crc = st_crc32((const uint8_t *)&ts + TOUCH_BODY_OFF, 20u);
    rc = bk_write(15, (const uint8_t *)&ts, TOUCH_BODY_OFF + 20u);
    printf("restore of object 15 with GLO > TOUCH only (32 bytes): rc %d\n", rc);
    bad |= rc != 0 || touch_same(&want, &knone, "its calibration none") || touch_ext.lever_mode != LV_LATCH;
    {   /* the LEVER's mode: each one through object 15; one made before the mode (104-byte body): LATCH; a later
         * firmware's longer body (ours + 8 bytes): its mode, the rest passed over */
        static uint8_t big[sizeof(touch_store_t) + 8u];
        uint32_t m;
        touch_cfg = want;
        touch_cfg.lever = 1;
        for (m = 0; m < LV_MODES; m++) {
            touch_ext.lever_mode = (uint8_t)m;
            touch_fill(&ts);
            touch_ext.lever_mode = (uint8_t)((m + 1u) % LV_MODES);
            rc = bk_write(15, (const uint8_t *)&ts, sizeof ts);
            bad |= rc != 0 || touch_ext.lever_mode != m;
            power_cycle();
            bad |= touch_ext.lever_mode != m || !touch_cfg.lever;
            printf("LEVER mode %u through object 15 and a power cycle: rc %d, mode %u  %s\n", m, rc, touch_ext.lever_mode,
                   rc == 0 && touch_ext.lever_mode == m ? "ok" : "DIFFERS");
        }
        touch_ext.lever_mode = LV_SPRNG;
        touch_fill(&ts);
        ts.len = 104u;                                  /* (as the firmware before wrote it: 116 bytes) */
        ts.crc = st_crc32((const uint8_t *)&ts + TOUCH_BODY_OFF, 104u);
        rc = bk_write(15, (const uint8_t *)&ts, TOUCH_BODY_OFF + 104u);
        printf("restore of object 15 made before the mode (116 bytes, LEVER ON): rc %d, mode %u (LATCH %u)\n", rc,
               touch_ext.lever_mode, (unsigned)LV_LATCH);
        bad |= rc != 0 || touch_ext.lever_mode != LV_LATCH || !touch_cfg.lever;
        power_cycle();
        bad |= touch_ext.lever_mode != LV_LATCH || !touch_cfg.lever;
        touch_ext.lever_mode = LV_HOLD;
        touch_fill(&ts);
        memset(big, 0x77, sizeof big);
        memcpy(big, &ts, sizeof ts);
        ((touch_store_t *)(void *)big)->len = (uint16_t)(sizeof big - TOUCH_BODY_OFF);
        ((touch_store_t *)(void *)big)->crc = st_crc32(big + TOUCH_BODY_OFF, (uint32_t)(sizeof big - TOUCH_BODY_OFF));
        touch_ext.lever_mode = LV_SPRNG;
        rc = bk_write(15, big, sizeof big);
        printf("restore of a later firmware's object 15 (%u bytes): rc %d, mode %u\n", (unsigned)sizeof big, rc,
               touch_ext.lever_mode);
        bad |= rc != 0 || touch_ext.lever_mode != LV_HOLD;
        touch_cfg = dflt;
        touch_ext = (touch_ext_t)TOUCH_EXT_DEFAULTS;
        settings_save();
    }
    return bad;
}

static int repro_motion_backup(void)
{
    static uint8_t img0[8192], img2[8192], again[8192];
    static int8_t want[ML_LANES][ML_N];
    int len0, len2, bad = 0, rc;
    uint32_t l, k;
    for (l = 0; l < ML_LANES; l++)                      /* three recorded lanes (tracks 2..4 empty, as the synth has them) */
        for (k = 0; k < ML_N; k++)
            want[l][k] = (int8_t)(l == ML_PRESS ? (k * 5u + 3u) % 128u : (int32_t)((k * 37u + l * 11u) % 255u) - 127);
    memcpy(ml_lane, want, sizeof want);
    ml_mask = ML_ALL;
    proj_op(1);                                         /* saved into A, then the editor's backup */
    len0 = bk_listed(0);
    len2 = bk_listed(2);
    printf("backup: object 0 %d bytes, object 2 %d bytes\n", len0, len2);
    if (len0 <= 0 || len2 <= 0 || len0 > (int)sizeof img0 || len2 > (int)sizeof img2) return 1;
    if (bk_read(0, img0, (uint32_t)len0) || bk_read(2, img2, (uint32_t)len2)) { printf("BK_GET failed\n"); return 1; }
    {   /* restored into another unit: no section, no lane */
        uint32_t c, i;
        for (i = 0; i < 4; i++) for (c = 0; c < 2; c++) st_erase(st_sector(OBJ_PROJECT0 + i, c));
    }
    power_cycle();
    memset(ml_lane, 0, sizeof ml_lane);
    ml_mask = 0;
    rc = bk_write(2, img2, (uint32_t)len2);
    printf("restore of object 2: rc %d\n", rc);
    bad |= rc != 0;
    rc = bk_write(0, img0, (uint32_t)len0);
    audio_blocks(8);
    printf("restore of object 0: rc %d, lanes %x, %s\n", rc, ml_mask, memcmp(ml_lane, want, sizeof want) ? "DIFFER" : "as recorded");
    bad |= rc != 0 || ml_mask != ML_ALL || memcmp(ml_lane, want, sizeof want);
    if (bk_listed(0) != len0 || bk_read(0, again, (uint32_t)len0) || memcmp(again, img0, (size_t)len0)) {
        printf("object 0 read again: DIFFERS\n"); bad = 1;
    } else printf("object 0 read again: the same bytes\n");
    if (bk_listed(2) != len2 || bk_read(2, again, (uint32_t)len2) || memcmp(again, img2, (size_t)len2)) {
        printf("object 2 read again: DIFFERS\n"); bad = 1;
    } else printf("object 2 read again: the same bytes\n");
    memset(ml_lane, 0, sizeof ml_lane);
    ml_mask = 0;
    power_cycle();
    proj_op(0);                                         /* section A as restored, loaded after a power cycle */
    printf("A loaded after a power cycle: lanes %x, %s\n", ml_mask, memcmp(ml_lane, want, sizeof want) ? "DIFFER" : "as recorded");
    bad |= ml_mask != ML_ALL || memcmp(ml_lane, want, sizeof want);
    memset(ml_lane, 0, sizeof ml_lane);                 /* (the next case: none) */
    ml_mask = 0;
    return bad;
}

static void bend_other(uint32_t n, const uint16_t *id)  /* another sound into slot 1 meanwhile: the defaults, but */
{                                                       /* FILTER FM / RING (another preset's UP13 values in RAM) */
    uint8_t a[16];
    uint32_t i;
    for (i = 0; i < n; i++) trk[0].p[id[i]] = TP[id[i]].def;
    trk[0].p[P_MFFM] = 9;                               /* (VCO3, VCO4) */
    trk[0].p[P_MRING] = -9;
    a[0] = 0; memcpy(a + 1, "OTHER", 6); ed(ED_UP_STORE, a, 7);
}
static int bend_check(uint32_t n, const uint16_t *id, const int16_t *v, int old)   /* old: no FILTER FM / RING (0) */
{
    uint8_t a[2];
    uint32_t i, o;
    int bad = 0;
    for (o = 0; o < 2; o++) {
        if (o) up_boot();                               /* a power-on: the banks, side records, tails from flash */
        for (i = 0; i < n; i++) trk[0].p[id[i]] = TP[id[i]].def;
        a[0] = 0; ed(ED_UP_LOAD, a, 1);
        printf("%s (%s):", o ? "after a power-on" : "after the restore", up_rec(0)->name);
        for (i = 0; i < n; i++) {
            int16_t w = old && (id[i] == P_MFFM || id[i] == P_MRING) ? TP[id[i]].def : v[i];
            printf(" %s %d/%d", TP[id[i]].label, trk[0].p[id[i]], w);
            bad |= trk[0].p[id[i]] != w;
        }
        printf("%s\n", bad ? "  WRONG" : "");
    }
    return bad;
}
static int repro_bend_backup(void)
{
    static uint8_t img[2][4096];
    static const uint8_t OBJ[2] = {6, 7};               /* (preset format 2: the banks hold every value) */
    static const uint16_t ID[] = {P_BUP, P_BDN, P_BVCO1, P_BVCO2, P_BVCF, P_BTGT, P_M1SRC, P_M1DST, P_M2SRC, P_M2DST,
                                  P_M7SRC, P_M7DST, P_M12SRC, P_M12DST, P_FSLOPE, P_SPREAD, P_MAC3, P_MFFM, P_MRING};
    static const int16_t V[] = {5, 19, 0, 0, -40, BT_LEAD, MS_RIB, MT_CUTB, MS_CAT, MT_PIT2, MS_BEND, MT_XMOD, MS_PAT,
                                MT_PIT1, 2, 0, 0, 12, -12};   /* (SPREAD, MACRO 3: away from their defaults, below;
                                                              * destinations PALOMINO uses, CUTB for HPF: a cable into one
                                                              * the engine does not use is never kept, engines.c mx_prune) */
    enum { N = sizeof ID / sizeof ID[0] };
    int16_t v[N];
    int len[2], bad = 0, rc;
    uint8_t a[16];
    uint32_t i, o;
    _Static_assert(sizeof V / sizeof V[0] == N, "one value each");
    set_engine_of(&trk[0], ENGI_PALOMINO);              /* (PALOMINO: XMOD is its) */
    trk[0].engine = trk[0].eng_req;
    for (i = 0; i < N; i++) {
        const param_desc_t *d = &TP[ID[i]];
        v[i] = ID[i] == P_SPREAD || ID[i] == P_MAC3 ? (int16_t)(d->def == d->max ? d->min : d->max) : V[i];
        if (v[i] < d->min || v[i] > d->max || v[i] == d->def) { printf("%s: %d not a test value\n", d->label, v[i]); return 1; }
        trk[0].p[ID[i]] = v[i];
    }
    a[0] = 0; memcpy(a + 1, "BEND ME", 8); ed(ED_UP_STORE, a, 9);   /* -> user preset 1 */
    printf("UP_STORE rc %u\n", rx_n > 6 ? rx_last[6] : 99u);
    for (o = 0; o < 2; o++) {
        len[o] = bk_listed(OBJ[o]);
        if (len[o] > 0 && bk_read(OBJ[o], img[o], (uint32_t)len[o])) { printf("BK_GET %u failed\n", OBJ[o]); return 1; }
    }
    bend_other(N, ID);
    for (o = 0; o < 2; o++)
        if (len[o] >= 0) {
            rc = bk_write(OBJ[o], img[o], (uint32_t)len[o]);
            printf("restore of object %u: rc %d\n", OBJ[o], rc);
            bad |= rc != 0;
        }
    bad |= bend_check(N, ID, v, 0);
    {   /* a Clipper / SLOOP backup's bank (magic "UPB1"): refused (rc 2), the presets as they were */
        static uint8_t ub1[8u + 16u * 192u];
        memset(ub1, 0, sizeof ub1);
        ub1[0] = 'U', ub1[1] = 'P', ub1[2] = 'B', ub1[3] = '1', ub1[4] = 192, ub1[6] = 16;
        ub1[8] = UP_USED, ub1[9] = 1, ub1[11] = 61, ub1[12] = 'X';
        rc = bk_write(6, ub1, sizeof ub1);
        printf("restore of a Clipper bank as object 6: rc %d (2: refused)\n", rc);
        bad |= rc != 2;
        bad |= bend_check(N, ID, v, 0);
    }
    return bad;
}

/* ---- 8: MARE's FILTER FM and RING (P_MFFM, P_MRING) with a drum hit: track 4's nudges are its own, so they ride only
 * beside the project (project.c proj_xs_t's extension): RAM, flash, a power cycle, the autosave, the backup (object
 * 14); a backup made before the extension (108-byte entries) gives 0 with a hit, and leaves the nudges' values
 * without one */
static const int8_t FFM_V[4] = {12, -12, 24, -24};      /* track 1's VCO3, VCO4, track 2's */
static void ffm_set(int want)
{
    uint32_t k;
    for (k = 0; k < 2u; k++)
        trk[k].p[P_MFFM] = want ? FFM_V[2u * k] : MAR_VOFF, trk[k].p[P_MRING] = want ? FFM_V[2u * k + 1u] : MAR_VOFF;
}
static int ffm_check(const char *when, int want)
{
    uint32_t k;
    int bad = 0;
    printf("  %-48s", when);
    for (k = 0; k < 2u; k++) {
        printf(" FLTFM %d RING %d", trk[k].p[P_MFFM], trk[k].p[P_MRING]);
        bad |= trk[k].p[P_MFFM] != (want ? FFM_V[2u * k] : MAR_VOFF) || trk[k].p[P_MRING] != (want ? FFM_V[2u * k + 1u] : MAR_VOFF);
    }
    printf("%s\n", bad ? "  WRONG" : "");
    return bad;
}
static int ffm_restore(uint8_t img[6][4096], const int *len, int old)   /* into another unit; old: 14 as before */
{
    static const uint8_t OBJ[6] = {2, 3, 4, 5, 0, 14};
    static uint8_t o14[4096];
    uint32_t i, b;
    for (i = 0; i < 5; i++) {
        uint32_t c, obj = i < 4 ? OBJ_PROJECT0 + i : OBJ_AUTOSAVE;
        for (c = 0; c < 2; c++) st_erase(st_sector(obj, c));
    }
    power_cycle();
    ffm_set(0);
    for (b = 0; b < 5u; b++)                            /* (each entry's first 108 bytes: the block without it) */
        memcpy(o14 + b * PROJ_XS_OLD, img[5] + b * sizeof(proj_side_t), PROJ_XS_OLD);
    for (i = 0; i < 6; i++)
        if (len[i] >= 0) {
            int rc = i == 5 && old ? bk_write(14, o14, 5u * PROJ_XS_OLD) : bk_write(OBJ[i], img[i], (uint32_t)len[i]);
            if (rc) { printf("restore of object %u: rc %d\n", OBJ[i], rc); return 1; }
            audio_blocks(8);
        }
    return 0;
}
static int ffm_backup(uint8_t img[6][4096], int *len)
{
    static const uint8_t OBJ[6] = {2, 3, 4, 5, 0, 14};
    uint32_t i;
    for (i = 0; i < 6; i++) {
        len[i] = bk_listed(OBJ[i]);
        if (len[i] > 0 && bk_read(OBJ[i], img[i], (uint32_t)len[i])) { printf("BK_GET %u failed\n", OBJ[i]); return 1; }
    }
    if (len[5] != (int)(5u * sizeof(proj_side_t))) { printf("object 14: %d bytes\n", len[5]); return 1; }
    return 0;
}
static int repro_ffm_backup(void)
{
    static uint8_t img[6][4096];
    int len[6], bad = 0;
    trk[3].dstep[5].on[0] = 1;                          /* a drum hit */
    ffm_set(1);
    proj_op(1);                                         /* save into A */
    ffm_set(0);
    proj_op(0);
    bad |= ffm_check("drum hit: A saved, loaded", 1);
    ffm_set(0);
    proj_slot[0].magic ^= 1u;                           /* (RAM's copy gone: from flash) */
    proj_op(0);
    bad |= ffm_check("drum hit: loaded from flash", 1);
    power_cycle();
    proj_op(0);
    bad |= ffm_check("drum hit: after a power cycle, loaded", 1);
    fm1_ms += 60000u;                                   /* idle long enough: the autosave */
    ui_input_ms = fm1_ms - 10000u;
    autosave_tick();
    power_cycle();
    autosave_resume();
    audio_blocks(8);
    bad |= ffm_check("drum hit: after a power cycle, autosave resumed", 1);
    if (ffm_backup(img, len))
        return 1;
    if (ffm_restore(img, len, 0))
        return 1;
    bad |= ffm_check("drum hit: backup restored: the working sound", 1);
    ffm_set(0);
    proj_op(0);
    bad |= ffm_check("drum hit: backup restored: A loaded", 1);
    if (ffm_restore(img, len, 1))
        return 1;
    bad |= ffm_check("drum hit: an older backup (no extension): 0", 0);
    power_cycle();                                      /* no hit: the nudges hold them, an older 14 leaves them */
    trk[3].dstep[5].on[0] = 0;
    ffm_set(1);
    proj_op(1);
    if (ffm_backup(img, len) || ffm_restore(img, len, 1))
        return 1;
    bad |= ffm_check("no hit: an older backup: the nudges' values kept", 1);
    return bad;
}

/* ---- 9: preset format 2, the 24 matrix slots (docs/superpowers/specs/2026-11-09-preset-format-2.md). A user preset with
 * every slot set (6-bit destinations, the amounts' ends) through the backup of the banks (objects 6, 7; 12 and 13 are
 * gone) restored over another preset, and a power-on after; a project with a drum hit holding slots 13..24 of both
 * parts (only beside the project, proj_mx_t): RAM, flash, a power cycle, the autosave, the backup (object 14) restored
 * into another unit; an object 14 made before the slots (120-byte entries) gives them off */
static int16_t s24_v[2][3u * MX_SLOTS];
static void s24_make(void)
{
    uint32_t j, s, d;
    for (j = 0; j < 2u; j++) {
        uint64_t lv = mx_dst_live(trk[j].engine) & ~1ull;   /* (engine-dests) the part's live destinations only: a
                                                         * dead one is cleared on load, which is not what this is about */
        d = MT_COUNT - (uint32_t)j;
        for (s = 0; s < MX_SLOTS; s++) {
            s24_v[j][3u * s] = (int16_t)(1u + (s * 5u + j * 3u) % (MS_NSRC - 1u));
            do d = d > 1u ? d - 1u : MT_COUNT - 1u; while (!((lv >> d) & 1u));
            s24_v[j][3u * s + 1u] = (int16_t)d;
            s24_v[j][3u * s + 2u] = (int16_t)(s & 1u ? -64 + (int32_t)(s + j) : 63 - (int32_t)(s + j));
        }
    }
}
static void s24_set(uint32_t from, int want)           /* slots from+1 .. 24 of parts 1 and 2: wanted, or off */
{
    uint32_t j, s, f;
    for (j = 0; j < 2u; j++)
        for (s = from; s < MX_SLOTS; s++)
            for (f = 0; f < 3u; f++)
                trk[j].p[mx_pid(s) + f] = want ? s24_v[j][3u * s + f] : 0;
}
static int s24_check(const char *when, uint32_t from, uint32_t parts, int want)
{
    uint32_t j, s, f, wrong = 0;
    for (j = 0; j < parts; j++)
        for (s = from; s < MX_SLOTS; s++)
            for (f = 0; f < 3u; f++)
                wrong += trk[j].p[mx_pid(s) + f] != (want ? s24_v[j][3u * s + f] : 0);
    printf("  %-56s slot 24 of part 1: %d %d %d, %u value(s) wrong%s\n", when, trk[0].p[mx_pid(23)], trk[0].p[mx_pid(23) + 1u],
           trk[0].p[mx_pid(23) + 2u], wrong, wrong ? "  WRONG" : "");
    return wrong != 0;
}
static int s24_restore(uint8_t img[6][4096], const int *len, int old)   /* into another unit; old: 14 before the slots */
{
    static const uint8_t OBJ[6] = {2, 3, 4, 5, 0, 14};
    static uint8_t o14[4096];
    const uint32_t ow = (uint32_t)(sizeof(proj_x10_t) + sizeof(proj_xs_t));
    uint32_t i, b;
    for (i = 0; i < 5; i++) {
        uint32_t c, obj = i < 4 ? OBJ_PROJECT0 + i : OBJ_AUTOSAVE;
        for (c = 0; c < 2; c++) st_erase(st_sector(obj, c));
    }
    power_cycle();
    s24_set(12, 0);
    for (b = 0; b < 5u; b++)
        memcpy(o14 + b * ow, img[5] + b * sizeof(proj_side_t), ow);
    for (i = 0; i < 6; i++)
        if (len[i] >= 0) {
            int rc = i == 5 && old ? bk_write(14, o14, 5u * ow) : bk_write(OBJ[i], img[i], (uint32_t)len[i]);
            if (rc) { printf("restore of object %u: rc %d\n", OBJ[i], rc); return 1; }
            audio_blocks(8);
        }
    return 0;
}
static int repro_slots24(void)
{
    static uint8_t img[6][4096], ub[2][4096];
    int len[6], ul[2], bad = 0, o, rc;
    uint8_t a[16];
    s24_make();
    /* the user preset */
    s24_set(0, 1);
    a[0] = 0; memcpy(a + 1, "TWENTY4", 8); ed(ED_UP_STORE, a, 9);
    printf("UP_STORE rc %u\n", rx_n > 6 ? rx_last[6] : 99u);
    bad |= bk_listed(12) != -1 || bk_listed(13) != -1;
    printf("  objects 12, 13 listed: %d, %d (none)\n", bk_listed(12), bk_listed(13));
    for (o = 0; o < 2; o++) {
        ul[o] = bk_listed(6u + (uint32_t)o);
        if (ul[o] > 0 && bk_read(6u + (uint32_t)o, ub[o], (uint32_t)ul[o])) { printf("BK_GET %d failed\n", 6 + o); return 1; }
        printf("  object %d: %d bytes\n", 6 + o, ul[o]);
    }
    bad |= ul[0] != (int)sizeof(up_bank_t);
    s24_set(0, 0);
    a[0] = 0; memcpy(a + 1, "OTHER", 6); ed(ED_UP_STORE, a, 7);   /* another sound into slot 1 meanwhile */
    for (o = 0; o < 2; o++)
        if (ul[o] >= 0) {
            rc = bk_write(6u + (uint32_t)o, ub[o], (uint32_t)ul[o]);
            printf("  restore of object %d: rc %d\n", 6 + o, rc);
            bad |= rc != 0;
        }
    a[0] = 0; ed(ED_UP_LOAD, a, 1);
    bad |= s24_check("user preset: backup restored, loaded", 0, 1, 1);
    s24_set(0, 0);
    up_boot();                                          /* a power-on: the banks from flash */
    a[0] = 0; ed(ED_UP_LOAD, a, 1);
    bad |= s24_check("user preset: after a power-on, loaded", 0, 1, 1);
    /* the project, with a drum hit */
    trk[3].dstep[5].on[0] = 1;
    s24_set(12, 1);
    proj_op(1);                                         /* save into A */
    s24_set(12, 0);
    proj_op(0);
    bad |= s24_check("drum hit: A saved, loaded", 12, 2, 1);
    s24_set(12, 0);
    proj_slot[0].magic ^= 1u;                           /* (RAM's copy gone: from flash) */
    proj_op(0);
    bad |= s24_check("drum hit: loaded from flash", 12, 2, 1);
    power_cycle();
    s24_set(12, 0);
    proj_op(0);
    bad |= s24_check("drum hit: after a power cycle, loaded", 12, 2, 1);
    fm1_ms += 60000u;                                   /* idle long enough: the autosave */
    ui_input_ms = fm1_ms - 10000u;
    autosave_tick();
    power_cycle();
    s24_set(12, 0);
    autosave_resume();
    audio_blocks(8);
    bad |= s24_check("drum hit: after a power cycle, autosave resumed", 12, 2, 1);
    if (ffm_backup(img, len) || s24_restore(img, len, 0))
        return 1;
    bad |= s24_check("drum hit: backup restored: the working sound", 12, 2, 1);
    s24_set(12, 0);
    proj_op(0);
    bad |= s24_check("drum hit: backup restored: A loaded", 12, 2, 1);
    if (s24_restore(img, len, 1))
        return 1;
    s24_set(12, 1);
    proj_op(0);
    bad |= s24_check("an object 14 before the slots: A loads them off", 12, 2, 0);
    trk[3].dstep[5].on[0] = 0;
    return bad;
}

int main(int argc, char **argv)
{
    int which = argc > 1 ? atoi(argv[1]) : 1, r;
    harness_init();
    r = which == 1 ? repro_wt_backup() : which == 2 ? repro_eq_low() : which == 3 ? repro_x10() : which == 5 ? repro_touch_backup()
        : which == 6 ? repro_motion_backup() : which == 7 ? repro_bend_backup() : which == 8 ? repro_ffm_backup() : which == 9 ? repro_slots24() : repro_up_backup();
    printf("editor_repro %d: %s\n", which, r ? "FAIL (the bug is there)" : "ok");
    return r;
}
