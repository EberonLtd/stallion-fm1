/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the user preset record (firmware/src/upreset.c, -DUP_HOST part):
 * UP_PUT parsing, a bank round trip through storage.c on a simulated NOR,
 * bank / record version checks, map-by-count, pattern <-> steps. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if CLIPPER_SYNTH   /* the synth build (run_tests.sh: upreset_syn_test): preset format 2, through hostsim's TP */
#define main hostsim_main
#include "hostsim.c"
#undef main
#else
#define __attribute__(x)
#include "../firmware/src/core.h"
#endif
#define UP_HOST 1

static uint8_t nor[0x100000];
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"

static int check(const char *what, int ok)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

#if CLIPPER_SYNTH
/* Stallion's preset format 2 (docs/superpowers/specs/2026-11-09-preset-format-2.md): one bank of 16 sounds an object,
 * every value a byte (value - its minimum), all 24 matrix slots, a magic, version and CRC; anything else in the sector
 * (Clipper's or SLOOP's banks) reads as empty and is left as it is */
static int16_t def_of(uint32_t e, uint32_t i) { return i >= P_E0 ? ENGINES[e]->edit[i - P_E0].def : TP[i].def; }
static int16_t min_of(uint32_t e, uint32_t i) { return i >= P_E0 ? ENGINES[e]->edit[i - P_E0].min : TP[i].min; }
static int16_t max_of(uint32_t e, uint32_t i) { return i >= P_E0 ? ENGINES[e]->edit[i - P_E0].max : TP[i].max; }
/* a sound of engine e: every value somewhere in its range (seeded), the 24 slots each its own cable, 6-bit DSTs too */
static void sound_of(uint32_t e, uint32_t seed, int16_t *v)
{
    uint32_t i, k, x = seed * 2654435761u + 12345u;
    for (i = 0; i < P_COUNT; i++) {
        int32_t lo = min_of(e, i), hi = max_of(e, i);
        x = x * 1103515245u + 12345u;
        v[i] = (int16_t)(lo + (int32_t)((x >> 8) % (uint32_t)(hi - lo + 1)));
    }
    for (k = 0; k < MX_SLOTS; k++) {
        v[mx_pid(k)] = (int16_t)(1u + (k + seed) % (MS_NSRC - 1u));
        v[mx_pid(k) + 1u] = (int16_t)(k & 1u ? 32u + (k + seed) % (MT_COUNT - 32u) : 1u + (k * 3u + seed) % 31u);   /* (odd
                                                                                     * slots: codes 32 and up) */
        v[mx_pid(k) + 2u] = (int16_t)(k & 1u ? -64 + (int32_t)k : 63 - (int32_t)k);
    }
}
static void ms_of(uint32_t seed, int8_t (*ms)[MSEQ_STEPS])
{
    uint32_t l, s;
    for (l = 0; l < MSEQ_LANES; l++)
        for (s = 0; s < MSEQ_STEPS; s++)
            ms[l][s] = (int8_t)((int32_t)((l * 7u + s * 5u + seed) % 16u) - 8);
}
static void ram_wipe(void) { memset(up_bank, 0, sizeof up_bank); }
static void boot_banks(void)                     /* as up_boot: each bank from flash, ours or empty */
{
    uint32_t b;
    for (b = 0; b < UP_SLOTS / UP_PER_BANK; b++)
        up_bank_check(b, st_load(OBJ_UPRESET0 + b, &up_bank[b], sizeof up_bank[b]));
}
static int save_bank(uint32_t b)
{
    up_bank_seal(b);
    return st_save(OBJ_UPRESET0 + b, &up_bank[b], sizeof up_bank[b]);
}
static int same_sound(uint32_t k, const int16_t *v)
{
    int16_t out[P_COUNT], def[P_COUNT];
    uint32_t i;
    int same = 1;
    for (i = 0; i < P_COUNT; i++)
        def[i] = def_of(up_rec(k)->engine, i);
    up_params(up_rec(k), out, def);
    for (i = 0; i < P_COUNT; i++)
        if (out[i] != v[i]) {
            if (same)
                printf("    value %u (%s): %d, stored %d\n", i, i >= P_E0 ? "E" : TP[i].label, out[i], v[i]);
            same = 0;
        }
    return same;
}

/* every per-sound range fits the record's byte (value - its minimum) */
static int t_ranges(void)
{
    uint32_t e, i;
    int fit = 1;
    for (e = 0; e < NENGINES; e++)
        for (i = 0; i < P_COUNT; i++)
            fit &= (i == P_JCHR ? max_of(e, i) - min_of(e, i) <= 65535 : max_of(e, i) - min_of(e, i) <= 255) &&
                   max_of(e, i) >= min_of(e, i);
    return check("every value of every engine fits a byte (PONY's CHAR two: its high byte after the engine's values)", fit) +
           check("the record holds P_COUNT values and CHAR's high byte (5 spare)", P_COUNT < UP_NV && UP_NV - P_COUNT == 6u) +
           check("a bank fits the object (3820 of 3840)", sizeof(up_bank_t) == 3820u && sizeof(up_rec_t) == 238u) +
           check("24 slots, P_COUNT 184 (PONY's EDIT 3, RODEO's KYBD PW0)", MX_SLOTS == 24u && P_COUNT == 184);
}

/* the 24 slots and every other value of three sounds saved, the RAM wiped, the banks read back from the simulated NOR
 * (a power cycle); then saved again (the other copy of the pair) with one changed, and read back again */
static int t_roundtrip(void)
{
    static int16_t v[3][P_COUNT];
    static const uint32_t SLOT[3] = {0, 17, 31}, ENG[3] = {ENGI_MARE, ENGI_HYBRID, 2};
    int8_t ms[MSEQ_LANES][MSEQ_STEPS], got[MSEQ_LANES][MSEQ_STEPS];
    uint32_t j, k;
    int bad = 0, ok = 1, d6 = 0;
    memset(nor, 0xFF, sizeof nor);
    ram_wipe();
    for (j = 0; j < 3; j++) {
        up_rec_t *r = up_rec(SLOT[j]);
        sound_of(ENG[j], j + 1u, v[j]);
        memset(r, 0, sizeof *r);
        r->used = UP_USED, r->engine = (uint8_t)ENG[j];
        memcpy(r->name, "TWENTY FOUR", 12);
        up_rec_set(r, v[j]);
        if (j == 1u) {
            ms_of(9, ms);
            up_ms_put(r, ms);
        }
    }
    for (k = 0; k < MX_SLOTS; k++)
        d6 += v[0][mx_pid(k) + 1u] >= 32;
    bad += check("the sounds use 6-bit destinations", d6 >= 8);
    bad += check("bank 0 saved", save_bank(0) == 0);
    bad += check("bank 1 saved", save_bank(1) == 0);
    ram_wipe();
    boot_banks();                                 /* power cycle */
    for (j = 0; j < 3; j++)
        ok &= up_used(SLOT[j]) && same_sound(SLOT[j], v[j]) && up_rec(SLOT[j])->engine == ENG[j];
    bad += check("power cycle: every value of three sounds back, 24 slots each", ok);
    up_ms_get(up_rec(17), got);
    bad += check("power cycle: the MOD SEQ's steps back", up_has_ms(up_rec(17)) && !memcmp(got, ms, sizeof got) &&
                                                             !up_has_ms(up_rec(0)));
    ok = 1;
    for (k = 0; k < UP_SLOTS; k++)
        ok &= up_used(k) == (k == 0 || k == 17 || k == 31);
    bad += check("power cycle: the other slots empty", ok);
    v[1][mx_pid(23) + 1u] = MT_RING, v[1][mx_pid(23) + 2u] = -64;   /* slot 24 changed, saved again (copy B) */
    v[1][mx_pid(12)] = MS_CCB;
    up_rec_set(up_rec(17), v[1]);
    bad += check("bank 1 saved again", save_bank(1) == 0);
    ram_wipe();
    boot_banks();
    bad += check("the newer copy wins: slot 24 RING -64, slot 13 CC B", same_sound(17, v[1]) && up_has_ms(up_rec(17)));
    return bad;
}

/* a bank of Clipper's / SLOOP's format (magic "UPB1", 192-byte records, with its side record and tails) and a
 * Clipper 0.4 bank (232-byte records) in the sectors: both banks read as empty, and reading changes no byte of flash */
static void foreign_rec(uint8_t *o, uint32_t np, uint32_t eng, const char *name)
{
    uint32_t i;
    o[0] = 0xA5u, o[1] = 1u, o[2] = (uint8_t)eng, o[3] = (uint8_t)np;
    memcpy(o + 4, name, strlen(name));
    for (i = 0; i < 72u; i++) {                   /* p[]: some values, Clipper's "CL" mark in p[71] */
        int16_t w = (int16_t)(i == 71u ? 0x434C : (int32_t)(i * 3u % 120u));
        memcpy(o + 16 + 2u * i, &w, 2);
    }
}
static int t_foreign(void)
{
    static uint8_t b0[8u + 16u * 192u], b1[8u + 16u * 232u], tail[760], side[168], before[4u * 4096u];
    uint32_t k, n;
    int bad = 0, empty = 1;
    memset(nor, 0xFF, sizeof nor);
    memset(b0, 0, sizeof b0), memset(b1, 0, sizeof b1);
    n = 0x31425055u, memcpy(b0, &n, 4), memcpy(b1, &n, 4);   /* "UPB1" */
    b0[4] = 192, b0[5] = 0, b0[6] = 16, b0[7] = 0;
    b1[4] = 232, b1[5] = 0, b1[6] = 16, b1[7] = 0;
    foreign_rec(b0 + 8, 61, 3, "CLIPPER ONE");     /* a Clipper record (np 61, the packed stream marked) */
    foreign_rec(b0 + 8 + 192, 71, 1, "SLOOP TWO"); /* a SLOOP 2.6 record (np 71) */
    foreign_rec(b1 + 8, 86, 2, "CLIP 0.4");        /* a Clipper 0.4 record */
    memset(tail, 0x5A, sizeof tail);
    n = 0x30315055u, memcpy(tail, &n, 4);          /* (Clipper 1.0's tail, "UP10") */
    memset(side, 0x33, sizeof side);
    n = 0x39535055u, memcpy(side, &n, 4);          /* (its side record, "UPS9") */
    st_side = side, st_side_len = sizeof side;
    st_tail = tail, st_tail_len = sizeof tail;
    bad += check("a Clipper / SLOOP bank written (side record, tail)", st_save(OBJ_UPRESET0, b0, sizeof b0) == 0);
    st_side = 0, st_tail = 0;
    bad += check("a Clipper 0.4 bank written", st_save(OBJ_UPRESET0 + 1, b1, sizeof b1) == 0);
    memcpy(before, nor + st_sector(OBJ_UPRESET0, 0), 2u * 4096u);
    memcpy(before + 2u * 4096u, nor + st_sector(OBJ_UPRESET0 + 1, 0), 2u * 4096u);
    memset(up_bank, 0x77, sizeof up_bank);         /* (whatever RAM held) */
    boot_banks();
    for (k = 0; k < UP_SLOTS; k++)
        empty &= !up_used(k);
    bad += check("foreign banks: every slot empty", empty && up_bank[0].magic == 0 && up_bank[1].magic == 0);
    bad += check("foreign banks: flash not changed by loading",
                 !memcmp(before, nor + st_sector(OBJ_UPRESET0, 0), 2u * 4096u) &&
                     !memcmp(before + 2u * 4096u, nor + st_sector(OBJ_UPRESET0 + 1, 0), 2u * 4096u));
    {   /* our bank damaged (one byte, the CRC no longer matches), or of another version: empty */
        int16_t v[P_COUNT];
        sound_of(4, 5, v);
        ram_wipe();
        up_rec(3)->used = UP_USED, up_rec(3)->engine = 4, memcpy(up_rec(3)->name, "MINE", 4);
        up_rec_set(up_rec(3), v);
        up_bank_seal(0);
        up_rec(3)->v[40] ^= 1u;
        up_bank_check(0, (int)sizeof up_bank[0]);
        bad += check("our bank, a byte changed: the CRC refuses it", !up_used(3));
        up_rec(3)->used = UP_USED, up_rec(3)->engine = 4, memcpy(up_rec(3)->name, "MINE", 4);
        up_rec_set(up_rec(3), v);
        up_bank_seal(0);
        up_bank[0].ver = UP_VER + 1u;
        up_bank_check(0, (int)sizeof up_bank[0]);
        bad += check("our bank, another version: empty", !up_used(3));
        up_rec(3)->used = UP_USED, up_rec(3)->engine = 4, memcpy(up_rec(3)->name, "MINE", 4);
        up_rec_set(up_rec(3), v);
        up_bank_seal(0);
        up_bank_check(0, (int)sizeof up_bank[0] - 4);
        bad += check("our bank, short: empty", !up_used(3));
        /* a save into the foreign bank: ours from then on; the foreign copy is the pair's older one */
        up_rec(3)->used = UP_USED, up_rec(3)->engine = 4, memcpy(up_rec(3)->name, "MINE", 4);
        up_rec_set(up_rec(3), v);
        bad += check("a save over the foreign bank", save_bank(0) == 0);
        ram_wipe();
        boot_banks();
        bad += check("after it: our sound loads, bank 1 (0.4) still empty, its flash as it was",
                     up_used(3) && same_sound(3, v) && !up_used(16) &&
                         !memcmp(before + 2u * 4096u, nor + st_sector(OBJ_UPRESET0 + 1, 0), 2u * 4096u));
    }
    return bad;
}

/* a record with fewer common values (a later layout reads an earlier one): by count, the missing ones their defaults,
 * E0..E7 by place; with more (an earlier firmware reads a later one): the extra dropped */
static int t_count(void)
{
    int16_t v[P_COUNT], out[P_COUNT], def[P_COUNT];
    up_rec_t r;
    uint32_t i;
    int ok = 1;
    sound_of(2, 7, v);
    memset(&r, 0, sizeof r);
    r.used = UP_USED, r.engine = 2, r.name[0] = 'C';
    up_rec_set(&r, v);
    for (i = 0; i < P_COUNT; i++)
        def[i] = (int16_t)(1000 + i);
    memmove(&r.v[P_E0 - 3u], &r.v[P_E0], 8);       /* as written by a layout 3 values shorter */
    r.nc = (uint8_t)(P_E0 - 3u);
    up_params(&r, out, def);
    for (i = 0; i < P_COUNT; i++)
        ok &= out[i] == (i >= P_E0 - 3u && i < P_E0 ? def[i] : v[i]);
    memmove(&r.v[P_E0 + 2u], &r.v[P_E0 - 3u], 8);  /* .. and by one 2 longer (two more values before E0) */
    r.nc = (uint8_t)(P_E0 + 2u);
    up_params(&r, out, def);
    for (i = P_E0; i < P_COUNT; i++)
        ok &= out[i] == v[i];
    return check("map by count: fewer common values (defaults), more (dropped), E0..E7 kept", ok);
}

/* UP_PUT (protocol 15): the 127 values, the pattern, then n values of the ids 127.. (slots 13..24 among them) */
static int t_parse(void)
{
    static uint8_t a[1200];
    int16_t v[P_COUNT];
    up_rec_t r;
    uint32_t n = 0, i, slot = 99;
    sound_of(ENGI_MARE, 3, v);
    a[n++] = 9, a[n++] = ENGI_MARE;
    memcpy(a + n, "PUT 24", 7), n += 7;
    for (i = 0; i < P_EDN; i++, n += 2)
        a[n] = (uint8_t)((v[p_of_ed(i)] + 8192) & 127), a[n + 1] = (uint8_t)(((v[p_of_ed(i)] + 8192) >> 7) & 127);
    memset(a + n, 0, 32), n += 32;
    a[n++] = P_EDHN & 127u, a[n++] = P_EDHN >> 7;
    for (i = 0; i < P_EDHN; i++, n += 2)
        a[n] = (uint8_t)((v[P_EDH0 + i] + 8192) & 127), a[n + 1] = (uint8_t)(((v[P_EDH0 + i] + 8192) >> 7) & 127);
    memset(&r, 0, sizeof r);
    ram_wipe();
    if (up_parse(a, n, &r, &slot) || slot != 9u)
        return check("UP_PUT with ids 127..183 parses", 0);
    *up_rec(9) = r;
    return check("UP_PUT with ids 127..183: all 24 slots (and WAV34, PONY's EDIT 3, RODEO's KYBD PW0) stored", P_EDHN == 57u && up_valid(&r) && same_sound(9, v));
}
#endif

static uint32_t put_frame(uint8_t *a, uint32_t slot, uint32_t eng, const char *name, int32_t base)
{
    uint32_t n = 0, i;
    a[n++] = (uint8_t)slot;
    a[n++] = (uint8_t)eng;
    for (i = 0; name[i]; i++)
        a[n++] = (uint8_t)name[i];
    a[n++] = 0;
    for (i = 0; i < P_COUNT; i++) {
        uint32_t u = (uint32_t)(base + (int32_t)i + 8192);
        a[n++] = u & 127u;
        a[n++] = (u >> 7) & 127u;
    }
    for (i = 0; i < 16u; i++) {
        a[n++] = (uint8_t)(i % 3u ? 40u + i : 0u);  /* rests on 0, 3, 6 .. */
        a[n++] = (uint8_t)(i == 3u ? 4u : i % 3u ? 1u : 2u);   /* step 3: tie; slide on a rest drops */
    }
    return n;
}

int main(void)
{
    uint8_t a[640];
    up_rec_t r, got;
    uint32_t n, slot = 99, i;
    int bad = 0, len, ok;
    int16_t v[P_COUNT], def[P_COUNT];
    memset(nor, 0xFF, sizeof nor);
#if CLIPPER_SYNTH                                           /* (the synth's record, preset format 2: the rest below
                                                             * is the plain build's layout, SLOOP's) */
    (void)a, (void)r, (void)got, (void)n, (void)slot, (void)i, (void)len, (void)ok, (void)v, (void)def;
    bad = t_ranges();
    bad += t_roundtrip();
    bad += t_foreign();
    bad += t_count();
    bad += t_parse();
    printf("%s\n", bad ? "USER PRESET TEST FAILED" : "user preset test passed");
    return bad != 0;
#else

    n = put_frame(a, 5, 2, "Bass One", -40);
    bad += check("UP_PUT frame < 640 bytes", 5u + n + 1u < 640u);
    bad += check("UP_PUT parses", up_parse(a, n, &r, &slot) == 0 && slot == 5u && r.engine == 2u &&
                                      up_valid(&r) && !memcmp(r.name, "Bass One", 8) && !r.name[8]);
    ok = 1;
    for (i = 0; i < P_COUNT; i++)
        ok &= r.p[i] == (int16_t)(-40 + (int32_t)i);
    bad += check("UP_PUT values (negative v14 too)", ok);
    bad += check("pattern: rest drops flags, tie has no note",
                 r.note[0] == 0 && r.flags[0] == 0 && r.note[1] == 41 && r.flags[1] == 1 && r.note[3] == 0 &&
                     r.flags[3] == 4);
    bad += check("UP_PUT short frame -> args", up_parse(a, n - 1u, &r, &slot) == 1);
    n = put_frame(a, 32, 0, "X", 0);
    bad += check("UP_PUT slot 32 -> args", up_parse(a, n, &r, &slot) == 1);
    n = put_frame(a, 0, NENGINES, "X", 0);
    bad += check("UP_PUT bad engine -> args", up_parse(a, n, &r, &slot) == 1);
    n = put_frame(a, 0, 0, "", 0);
    bad += check("UP_PUT empty name -> args", up_parse(a, n, &r, &slot) == 1);
    n = put_frame(a, 0, 0, "THIRTEEN CHRS", 0);
    bad += check("UP_PUT 13-char name -> args", up_parse(a, n, &r, &slot) == 1);
    n = put_frame(a, 0, 0, "TWELVE CHARS", 0);
    bad += check("UP_PUT 12-char name ok", up_parse(a, n, &r, &slot) == 0 && !memcmp(r.name, "TWELVE CHARS", 12));
    {
        char nm[13];
        *up_rec(31) = r;
        memcpy(up_rec(31)->name, "Low case", 9);
        up_name(31, nm);
        bad += check("name shown upper case", !strcmp(nm, "LOW CASE"));
    }

    /* bank round trip through storage.c */
    n = put_frame(a, 17, 3, "Keys", 7);
    up_parse(a, n, &r, &slot);
    up_bank[1].magic = UP_BANK_MAGIC;
    up_bank[1].rsize = sizeof(up_rec_t);
    up_bank[1].nslot = UP_PER_BANK;
    *up_rec(17) = r;
    bad += check("bank fits one object", sizeof(up_bank_t) <= ST_PAYLOAD_MAX);
    bad += check("bank save", st_save(OBJ_UPRESET0 + 1, &up_bank[1], sizeof up_bank[1]) == 0);
    memset(up_bank, 0, sizeof up_bank);
    len = st_load(OBJ_UPRESET0 + 1, &up_bank[1], sizeof up_bank[1]);
    up_bank_check(1, len);
    got = *up_rec(17);
    bad += check("bank load: the record is back", up_used(17) && !memcmp(&got, &r, sizeof r));
    bad += check("other slots empty", !up_used(16) && !up_used(18) && !up_used(0));
    len = st_load(OBJ_UPRESET0, &up_bank[0], sizeof up_bank[0]);
    up_bank_check(0, len);
    bad += check("bank 0 never written -> empty", len < 0 && !up_used(0) && up_bank[0].magic == 0);
    bad += check("banks in 0xDC000..0xDFFFF", st_sector(OBJ_UPRESET0, 0) == 0xDC000u &&
                                                   st_sector(OBJ_UPRESET0 + 1, 1) == 0xDF000u &&
                                                   st_sector(OBJ_PROJECT0 + 3, 1) + 4096u <= 0xA0000u);
    up_bank[1].rsize = 190;                                 /* another record layout */
    up_bank_check(1, (int)sizeof up_bank[1]);
    bad += check("bank with another record size -> empty", !up_used(17));
    up_bank[1].magic = UP_BANK_MAGIC;
    up_bank[1].rsize = sizeof(up_rec_t);
    up_bank[1].nslot = UP_PER_BANK;
    *up_rec(17) = r;
    up_rec(17)->ver = UP_VER + 1u;
    bad += check("record with another version -> empty", !up_used(17));

    /* map by count: a record from a build with 2 parameters fewer */
    for (i = 0; i < P_COUNT; i++)
        def[i] = (int16_t)(1000 + i);
    r.np = P_COUNT - 2u;
    for (i = 0; i < P_COUNT; i++)
        r.p[i] = (int16_t)i;
    up_params(&r, v, def);
    ok = 1;
    for (i = 0; i < P_E0; i++)
        ok &= v[i] == (i < P_E0 - 2u ? (int16_t)i : def[i]);
    for (i = 0; i < 8u; i++)
        ok &= v[P_E0 + i] == (int16_t)(P_E0 - 2u + i);
    bad += check("np < P_COUNT: mapped by count", ok);
    /* a record saved before the SLICER (P_COUNT 53, P_E0 45): the four SLICER parameters (just
     * before P_E0) take their defaults, everything else keeps its id */
    r.np = 53;
    for (i = 0; i < 53u; i++)
        r.p[i] = (int16_t)(2000 + i);
    up_params(&r, v, def);
    ok = P_SLCR == 45 && P_SLDEPTH + 1 == P_CHORD && P_CHORD == 49 && P_TFLT == 50 && P_VLEAD == 52 && P_E0 == 53;
    for (i = 0; i < 45u; i++)
        ok &= v[i] == (int16_t)(2000 + i);
    for (i = P_SLCR; i <= P_VLEAD; i++)
        ok &= v[i] == def[i];
    for (i = 0; i < 8u; i++)
        ok &= v[P_E0 + i] == (int16_t)(2000 + 45 + i);
    bad += check("old record (np 53): SLICER, CHORD and FILTER defaults, E0..E7 kept", ok);
    /* a record of SLOOP 2.0 .. 2.3 (P_COUNT 58, P_E0 50): only the FILTER (2.4) takes its default */
    r.np = 58;
    for (i = 0; i < 58u; i++)
        r.p[i] = (int16_t)(3000 + i);
    up_params(&r, v, def);
    ok = 1;
    for (i = 0; i < P_TFLT; i++)
        ok &= v[i] == (int16_t)(3000 + i);
    ok &= v[P_TFLT] == def[P_TFLT] && v[P_STRUM] == def[P_STRUM] && v[P_VLEAD] == def[P_VLEAD];
    for (i = 0; i < 8u; i++)
        ok &= v[P_E0 + i] == (int16_t)(3000 + 50 + i);
    bad += check("2.3 record (np 58): FILTER, STRUM, VLEAD defaults, CHORD and E0..E7 kept", ok);
    /* a record of SLOOP 1.0 (P_COUNT 57, P_E0 49): CHORD (SLOOP 2.0) takes its default */
    r.np = 57;
    for (i = 0; i < 57u; i++)
        r.p[i] = (int16_t)(3000 + i);
    up_params(&r, v, def);
    ok = 1;
    for (i = 0; i < P_CHORD; i++)
        ok &= v[i] == (int16_t)(3000 + i);
    ok &= v[P_CHORD] == def[P_CHORD];
    for (i = 0; i < 8u; i++)
        ok &= v[P_E0 + i] == (int16_t)(3000 + 49 + i);
    bad += check("SLOOP 1.0 record (np 57): CHORD default, the rest kept", ok);
    r.np = P_COUNT;
    for (i = 0; i < P_COUNT; i++)
        r.p[i] = (int16_t)i;
    up_params(&r, v, def);
    ok = 1;
    for (i = 0; i < P_COUNT; i++)
        ok &= v[i] == (int16_t)i;
    bad += check("np == P_COUNT: as stored", ok);

    {   /* steps -> pattern (UP_STORE) */
        step_t st[NSTEP];
        memset(st, 0, sizeof st);
        for (i = 0; i < 16u; i++)
            st[i].time = ST_REST;
        st[0] = (step_t){{60, 64, 67, 0}, 3, ST_NOTE, SF_ACCENT | SF_SLIDE, 100};
        st[1] = (step_t){{0}, 0, ST_TIE, 0, 0};
        st[2] = (step_t){{50}, 0, ST_NOTE, SF_ACCENT, 0};   /* n = 0: empty */
        st[20] = (step_t){{70}, 1, ST_NOTE, 0, 90};         /* beyond 16: not stored */
        up_pat_from(&r, st);
        bad += check("steps -> pattern", r.note[0] == 60 && r.flags[0] == 3 && r.note[1] == 0 && r.flags[1] == 4 &&
                                             r.note[2] == 0 && r.flags[2] == 0 && !up_pat_empty(&r));
        memset(st, 0, sizeof st);
        up_pat_from(&r, st);
        bad += check("empty sequencer -> empty pattern", up_pat_empty(&r));
    }
    printf("%s\n", bad ? "USER PRESET TEST FAILED" : "user preset test passed");
    return bad != 0;
#endif
}
