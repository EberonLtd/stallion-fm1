/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* User presets (editor protocol v2, cmds 16-21; the SAVE > USER page): 32
 * slots in two storage.c objects (OBJ_UPRESET0/1, an A/B sector pair each), 16
 * records each, mirrored in RAM so browsing never reads flash.
 *
 * The synth (CLIPPER_SYNTH): Stallion's preset format 2 (docs/superpowers/specs/2026-11-09-preset-format-2.md). A bank
 * is the object's whole payload: a magic, the version, the slot count and record size, 16 records, a CRC-32. A record:
 * the engine, the name, the MOD SEQ's steps and every value of the sound (all 24 matrix slots among them), each a byte:
 * the value - its minimum. Anything else in the sector (Clipper's or SLOOP's banks, which share it, a damaged copy)
 * reads as empty and is never written by a load: only a save into that bank replaces it.
 *
 * The groovebox (SLOOP's build, CLIPPER_SYNTH 0): SLOOP's record. A bank whose magic, record size or slot count
 * differ reads as empty; so does a record with another UP_VER.
 *
 * Both keep how many values a record holds and map by count when it differs: the engine's 8 are P_E0..P_E7, the
 * common ones before them P_LEVEL.. in order, missing ones take their defaults. So common parameters may only be added
 * just before P_E0 (else bump UP_VER).
 *
 * With -DUP_HOST (host test) only the part above #ifndef UP_HOST is built;
 * it needs nothing but core.h (the synth's: the engines and params.c's TP too). */
#ifndef UI_SMALL
#define UI_SMALL                                   /* (gfx.c: minsize on the device; a test alone: nothing) */
#endif
#define UP_PER_BANK 16u
#define UP_USED 0xA5u
#if CLIPPER_SYNTH
#define UP_VER 2u
#define UP_BANK_MAGIC 0x32505453u                /* "STP2" */
#define UP_NV 190u                               /* a record's value bytes (P_COUNT of them used: room to grow) */
#define UP_NE 8u                                 /* the engine's values, after the common ones */
#define UP_F_MS 1u                               /* flags: ms holds the MOD SEQ's steps */
typedef struct {
    uint8_t used, engine, nc, flags;             /* UP_USED (0: empty), the engine, how many common values, UP_F_* */
    char name[12];                               /* ASCII 32..126, 0-padded (no 0 when 12 long) */
    uint8_t ms[32];                              /* the MOD SEQ: 64 steps (lane by lane) + 8, two a byte (low first) */
    uint8_t v[UP_NV];                            /* nc common values (P_LEVEL ..), then UP_NE engine values (P_E0 ..),
                                                  * each value - its minimum */
} up_rec_t;
typedef struct {
    uint32_t magic;
    uint8_t ver, nslot;
    uint16_t rsize;
    up_rec_t r[UP_PER_BANK];
    uint32_t crc;                                /* CRC-32 (zlib) of every byte before */
} up_bank_t;
_Static_assert(sizeof(up_rec_t) == 238u && sizeof(up_bank_t) == 3820u && sizeof(up_bank_t) <= 3840u &&
               P_COUNT < UP_NV && P_E0 < 256 && __builtin_offsetof(up_bank_t, crc) == sizeof(up_bank_t) - 4u,
               "preset format 2: a bank of 16 records fills one object's payload (storage.c ST_PAYLOAD_MAX)");
_Static_assert(P_EDN < 128 && P_COUNT < 256, "user preset record: P_COUNT (UP_PUT's: P_EDN)");
static up_bank_t up_bank[UP_SLOTS / UP_PER_BANK] __attribute__((section(".pool")));
#ifdef ST_MAGIC
#define up_crc st_crc32
#else
UI_SMALL static uint32_t up_crc(const void *p, uint32_t n)   /* (a host test without storage.c) zlib CRC-32 */
{
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--)
        for (c ^= *b++, k = 0; k < 8u; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    return ~c;
}
#endif
UI_SMALL static int32_t up_vmin(uint32_t e, uint32_t i)   /* the stored byte's zero: the value's minimum */
{
    return i >= P_E0 ? ENGINES[e % NENGINES]->edit[(i - P_E0) & 7u].min : TP[i].min;
}
UI_SMALL static int up_has_ms(const up_rec_t *r) { return (r->flags & UP_F_MS) != 0; }   /* ms holds the MOD SEQ */
UI_SMALL static void up_ms_put(up_rec_t *r, int8_t (*ms)[MSEQ_STEPS])   /* (after up_rec_set) */
{
    uint32_t n;
    for (n = 0; n < 32u; n++) {
        uint32_t lo = (uint32_t)clamp(ms[(2u * n) / MSEQ_STEPS][(2u * n) % MSEQ_STEPS] + 8, 0, 15);
        uint32_t hi = (uint32_t)clamp(ms[(2u * n + 1u) / MSEQ_STEPS][(2u * n + 1u) % MSEQ_STEPS] + 8, 0, 15);
        r->ms[n] = (uint8_t)(lo | hi << 4);
    }
    r->flags |= UP_F_MS;
}
UI_SMALL static void up_ms_get(const up_rec_t *r, int8_t (*ms)[MSEQ_STEPS])
{
    uint32_t n;
    for (n = 0; n < 32u; n++) {
        ms[(2u * n) / MSEQ_STEPS][(2u * n) % MSEQ_STEPS] = (int8_t)((int32_t)(r->ms[n] & 15u) - 8);
        ms[(2u * n + 1u) / MSEQ_STEPS][(2u * n + 1u) % MSEQ_STEPS] = (int8_t)((int32_t)(r->ms[n] >> 4) - 8);
    }
}
#else
#define UP_PMAX 72u                              /* room for P_COUNT to grow */
#define UP_VER 1u
#define UP_BANK_MAGIC 0x31425055u                /* "UPB1" */
typedef struct {
    uint8_t used, ver, engine, np;               /* UP_USED, UP_VER, engine, P_COUNT when stored */
    char name[12];                               /* ASCII 32..126, 0-padded (no 0 when 12 long) */
    int16_t p[UP_PMAX];
    uint8_t note[16], flags[16];                 /* note 0 = rest; flags 1 accent, 2 slide, 4 tie */
} up_rec_t;
typedef struct {
    uint32_t magic;
    uint16_t rsize, nslot;
    up_rec_t r[UP_PER_BANK];
} up_bank_t;
_Static_assert(sizeof(up_rec_t) == 192, "user preset record layout (SLOOP's)");
_Static_assert(P_EDN < 128 && P_COUNT < 256, "user preset record: P_COUNT (UP_PUT's: P_EDN)");
#define UP_E0V ((uint32_t)P_E0)
static up_bank_t up_bank[UP_SLOTS / UP_PER_BANK];
#endif

UI_SMALL static up_rec_t *up_rec(uint32_t k) { return &up_bank[k / UP_PER_BANK].r[k % UP_PER_BANK]; }

UI_SMALL static int up_valid(const up_rec_t *r)
{
#if CLIPPER_SYNTH
    return r->used == UP_USED && r->engine < NENGINES && r->engine != ENGI_GONE && r->nc && r->nc + UP_NE <= UP_NV &&
           r->name[0];   /* (1.0: one of LOFI's, gone: an empty slot, core.h ENGI_GONE) */
#else
    return r->used == UP_USED && r->ver == UP_VER && r->engine < NENGINES && r->engine != ENGI_GONE && r->np >= 8u &&
           r->np <= UP_PMAX && r->name[0];
#endif
}

UI_SMALL static int up_used(uint32_t k) { return k < UP_SLOTS && up_valid(up_rec(k)); }

/* today's values v (P_COUNT) into record r (the synth's: of r->engine, set before) */
UI_SMALL static void up_rec_set(up_rec_t *r, const int16_t *v)
{
    uint32_t i;
#if CLIPPER_SYNTH
    memset(r->v, 0, sizeof r->v);
    r->nc = (uint8_t)P_E0;
    for (i = 0; i < P_COUNT; i++)
        r->v[i] = (uint8_t)clamp(v[i] - up_vmin(r->engine, i), 0, 255);
    r->v[P_JCHR] = (uint8_t)clamp(v[P_JCHR], 0, JQ_ALL);
    r->v[P_COUNT] = (uint8_t)((uint32_t)clamp(v[P_JCHR], 0, JQ_ALL) >> 8);   /* (Stallion) PONY's CHAR: its high bits
                                                                              * after the engine's values */
#else
    r->np = P_COUNT;
    for (i = 0; i < P_COUNT; i++)
        r->p[i] = v[i];
#endif
}

/* the record's values in today's P_* order (mapped by count, see the top); def = the defaults */
UI_SMALL static void up_params(const up_rec_t *r, int16_t *out, const int16_t *def)
{
    uint32_t i;
#if CLIPPER_SYNTH
    uint32_t nc = r->nc + UP_NE <= UP_NV ? r->nc : UP_NV - UP_NE;
    for (i = 0; i < P_E0; i++)
        out[i] = i < nc ? (int16_t)(r->v[i] + up_vmin(r->engine, i)) : def[i];
    for (i = 0; i < UP_NE; i++)
        out[P_E0 + i] = (int16_t)(r->v[nc + i] + up_vmin(r->engine, P_E0 + i));
    if (P_JCHR < nc && nc + UP_NE < UP_NV)
        out[P_JCHR] = (int16_t)(out[P_JCHR] | r->v[nc + UP_NE] << 8);
#else
    uint32_t nc = r->np - 8u, ncm = nc < UP_E0V ? nc : UP_E0V;   /* (the common ones it has, by place) */
    for (i = 0; i < P_E0; i++)
        out[i] = i < ncm ? r->p[i] : def[i];
    for (i = 0; i < 8u; i++)
        out[P_E0 + i] = r->p[nc + i];
#endif
}

#if CLIPPER_SYNTH
UI_SMALL static int up_bank_ok(const up_bank_t *bk)      /* ours: magic, version, slot count, record size, CRC */
{
    return bk->magic == UP_BANK_MAGIC && bk->ver == UP_VER && bk->nslot == UP_PER_BANK && bk->rsize == sizeof(up_rec_t) &&
           bk->crc == up_crc(bk, sizeof *bk - 4u);
}
UI_SMALL static void up_bank_seal(uint32_t b)           /* bank b's header and CRC, before it is written or backed up */
{
    up_bank_t *bk = &up_bank[b];
    bk->magic = UP_BANK_MAGIC;
    bk->ver = UP_VER;
    bk->nslot = UP_PER_BANK;
    bk->rsize = sizeof(up_rec_t);
    bk->crc = up_crc(bk, sizeof *bk - 4u);
}
#endif

UI_SMALL static void up_bank_check(uint32_t b, int len)  /* after loading bank b (len bytes, -1 = none): not ours -> empty */
{
    up_bank_t *bk = &up_bank[b];
#if CLIPPER_SYNTH
    if (len != (int)sizeof *bk || !up_bank_ok(bk))
#else
    if (len != (int)sizeof *bk || bk->magic != UP_BANK_MAGIC || bk->rsize != sizeof(up_rec_t) ||
        bk->nslot != UP_PER_BANK)
#endif
        memset(bk, 0, sizeof *bk);
}

UI_SMALL static int up_name_ok(const uint8_t *s, uint32_t n)   /* 1..12 printable ASCII */
{
    uint32_t i;
    if (!n || n > 12u)
        return 0;
    for (i = 0; i < n; i++)
        if (s[i] < 32u || s[i] > 126u)
            return 0;
    return 1;
}

UI_SMALL static void up_name(uint32_t k, char *b)       /* upper case, 0-terminated: b holds 13 */
{
    const up_rec_t *r = up_rec(k);
    uint32_t i;
    for (i = 0; i < 12u && r->name[i]; i++)
        b[i] = r->name[i] >= 'a' && r->name[i] <= 'z' ? (char)(r->name[i] - 32) : r->name[i];
    b[i] = 0;
}

#if !CLIPPER_SYNTH
UI_SMALL static void up_pat_norm(uint8_t *note, uint8_t *flags)   /* tie: no note; rest: no flags */
{
    *note &= 127u;
    if (*flags & 4u) {
        *note = 0;
        *flags = 4;
    } else {
        *flags = *note ? (uint8_t)(*flags & (SF_ACCENT | SF_SLIDE)) : 0u;
    }
}

UI_SMALL static void up_pat_from(up_rec_t *r, const step_t *st)   /* the first 16 steps -> the pattern */
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        r->note[i] = st[i].time == ST_NOTE && st[i].n ? st[i].note[0] : 0u;
        r->flags[i] = st[i].time == ST_TIE ? 4u : (uint8_t)(st[i].flags & (SF_ACCENT | SF_SLIDE));   /* (SF_RX is 4 too) */
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
}

UI_SMALL static int up_pat_empty(const up_rec_t *r)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (r->note[i])
            return 0;
    return 1;
}

#endif

/* UP_PUT arguments: slot, engine, name, P_COUNT x v14, 16 x (note, flags) [, (protocol 15) n (2 x 7 bit), n x v14: the
 * device's own values, P_EDH0 ..] -> *r (values not yet clamped); 0 ok, 1 bad arguments. *slot gets the slot byte when
 * there is one. The device's own values it does not bring: their defaults. (The synth keeps no pattern: its record's
 * steps are the MOD SEQ's, which UP_PUT does not carry: the preset loads the MOD SEQ's shapes to start from) */
UI_SMALL static int up_parse(const uint8_t *a, uint32_t na, up_rec_t *r, uint32_t *slot)
{
    uint32_t i, n, k;
    if (na < 3u)
        return 1;
    *slot = a[0];
    for (n = 0; 2u + n < na && a[2 + n]; n++)
        ;
    k = 3u + n;                                  /* after the name's 0 */
    if (a[0] >= UP_SLOTS || a[1] >= NENGINES || a[1] == ENGI_GONE || 2u + n >= na || !up_name_ok(a + 2, n) ||
        na < k + 2u * P_EDN + 32u)
        return 1;
    memset(r, 0, sizeof *r);
    r->used = UP_USED;
#if !CLIPPER_SYNTH
    r->ver = UP_VER;
#endif
    r->engine = a[1];
    for (i = 0; i < n; i++)
        r->name[i] = (char)a[2 + i];
    {
        int16_t v[P_COUNT];
        for (i = 0; i < P_EDN; i++, k += 2u)
            v[p_of_ed(i)] = (int16_t)((int32_t)((a[k] & 127u) | (a[k + 1] & 127u) << 7) - 8192);
#if CLIPPER_SYNTH
        {
            uint32_t x = k + 32u, nx = na >= x + 2u ? (uint32_t)(a[x] & 127u) | (uint32_t)(a[x + 1] & 127u) << 7 : 0u;
            for (i = 0; i < P_EDHN; i++, x += 2u)        /* (v15: after the pattern; 14: none, their defaults) */
                v[P_EDH0 + i] = i < nx && na >= x + 4u ? (int16_t)((int32_t)((a[x + 2] & 127u) | (a[x + 3] & 127u) << 7) - 8192)
                                                       : TP[P_EDH0 + i].def;
        }
#endif
        up_rec_set(r, v);
    }
#if !CLIPPER_SYNTH
    for (i = 0; i < 16u; i++, k += 2u) {
        r->note[i] = a[k];
        r->flags[i] = a[k + 1];
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
#endif
    return 0;
}

#ifndef UP_HOST
UI_SMALL static const param_desc_t *up_desc(uint32_t e, uint32_t i)
{
    return i >= P_E0 ? &ENGINES[e]->edit[i - P_E0] : &TP[i];
}

UI_SMALL static void up_values(const up_rec_t *r, int16_t *v)   /* mapped and clamped for its engine */
{
    int16_t def[P_COUNT];
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        def[i] = up_desc(r->engine, i)->def;
    up_params(r, v, def);
    for (i = 0; i < P_COUNT; i++)
        v[i] = (int16_t)clamp(v[i], up_desc(r->engine, i)->min, up_desc(r->engine, i)->max);
}

UI_SMALL static void up_boot(void)                      /* persist_boot: the banks from flash (ours, else empty: a load
                                                         * never writes) */
{
#if FELUCCA_FLASH
    uint32_t b;
    for (b = 0; b < UP_SLOTS / UP_PER_BANK; b++)
        up_bank_check(b, flash_ok ? st_load(OBJ_UPRESET0 + b, &up_bank[b], sizeof up_bank[b]) : -1);
#endif
}

/* record k = *r (0: erase), then the bank to flash: 0 ok, 2 flash error, 3 no flash (kept in RAM) */
UI_SMALL static int up_put(uint32_t k, const up_rec_t *r)
{
    up_bank_t *bk = &up_bank[k / UP_PER_BANK];
#if !CLIPPER_SYNTH
    bk->magic = UP_BANK_MAGIC;
    bk->rsize = sizeof(up_rec_t);
    bk->nslot = UP_PER_BANK;
#endif
    if (r)
        *up_rec(k) = *r;
    else
        memset(up_rec(k), 0, sizeof(up_rec_t));
#if CLIPPER_SYNTH
    up_bank_seal(k / UP_PER_BANK);                      /* (the whole bank: a foreign one there was read as empty) */
#endif
    if (!r) {
        uint32_t i;
        for (i = 0; i < NTRK; i++)
            if (trk[i].user == k + 1u)
                trk[i].user = 0;
    }
    up_gen++;
#if FELUCCA_FLASH
    if (flash_ok)
        return st_save(OBJ_UPRESET0 + k / UP_PER_BANK, bk, sizeof *bk) ? 2 : 0;
#endif
    return 3;
}

UI_SMALL static void up_slot_label(char *b, uint32_t k)  /* "U07" */
{
    b[0] = 'U';
#if FELUCCA_MULTI
    if (k >= UP_SLOTS) {                                /* (SMK-37) the multi sounds after the user presets: "M03" */
        b[0] = 'M';
        k -= UP_SLOTS;
    }
#endif
    b[1] = (char)('0' + (k + 1u) / 10u);
    b[2] = (char)('0' + (k + 1u) % 10u);
    b[3] = 0;
}

/* the selected part's sound -> slot k; name 0 or "": engine name + slot number ("ANALOG 07").
 * 1 = the drum track is selected (it has no sound to store) */
UI_SMALL static int up_store(uint32_t k, const char *name)
{
    up_rec_t r;
    uint32_t i;
    if (is_drum(TSEL))
        return 1;
    memset(&r, 0, sizeof r);
    r.used = UP_USED;
#if !CLIPPER_SYNTH
    r.ver = UP_VER;
#endif
    r.engine = TSEL->eng_req;
    if (name && name[0]) {
        for (i = 0; i < 12u && name[i]; i++)
            r.name[i] = name[i];
    } else {
        char b[16], l[4];
        str_cpy(b, ENGINES[TSEL->eng_req]->name, 9);
        up_slot_label(l, k);
        str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), l + 1, 3);
        for (i = 0; i < 12u && b[i]; i++)
            r.name[i] = b[i];
    }
    up_rec_set(&r, TSEL->p);
#if CLIPPER_SYNTH
    up_ms_put(&r, TSEL->ms);                            /* (0.8) the MOD SEQ's steps */
#else
    up_pat_from(&r, TSEL->step);
#endif
    return up_put(k, &r);
}

/* slot k -> the selected part's sound: engine and every parameter except its mix (LEVEL,
 * PAN, MUTE: the TRACKS faders) and its pattern parameters (param_kept). LIVE: the pattern
 * stored in the record is not loaded: changing the sound never changes the sequence.
 * 0 ok, 1 empty (or the drum track is selected) */
UI_SMALL static int up_load_to(track_t *t, uint32_t k)
{
    const up_rec_t *r;
    int16_t v[P_COUNT];
    uint32_t i;
    int keep_arp = arp_user_on(t);                      /* (1.0: the player's arp only, seq.c arp_snd) */
    if (!up_used(k) || is_drum(t))
        return 1;
    r = up_rec(k);
    up_values(r, v);
    for (i = 0; i < P_COUNT; i++)                       /* (LEN etc. of a kept pattern changed too, and */
        if (param_kept(i) || (keep_arp && i >= P_AMODE && i <= P_AORDER))   /* a preset pattern then counted as edited;
                                                         * 0.7: the arp that runs goes on) */
            v[i] = t->p[i];
    if (!v[P_AMODE] && !keep_arp)
        v[P_AHOLD] = 0;                                 /* (Stallion 1.0) KBD HOLD ends with the sound, as with a factory
                                                         * sound (ui.c apply_preset_to): never a new sound latching keys */
#if CLIPPER_SYNTH
    if (t == &trk[0])
        hist_before_load();                             /* (0.7: the edits a step of their own, ui_synth.c) */
#endif
    sound_fade_begin(t);
    if (keep_arp)
        arp_keep_req |= (uint8_t)(1u << trk_index(t));  /* (0.7: its notes too) */
    arp_snd[trk_index(t) % NTRK] = (uint8_t)(keep_arp ? 0 : v[P_AMODE]);
    panic_req |= (uint8_t)(1u << trk_index(t));
    fm1_irq_off();                                      /* the audio ISR must not see half a sound */
    t->eng_req = r->engine;
    for (i = 0; i < P_COUNT; i++)
        t->p[i] = v[i];
#if CLIPPER_SYNTH
    if (up_has_ms(r))                                   /* (0.8) its MOD SEQ, or the shapes to start from */
        up_ms_get(r, t->ms);
    else
        mseq_init(t);
    mx_prune(t);                                        /* no cable the engine does not use */
#endif
    t->preset = 0;
    fm1_irq_on();
    fm6_track_loaded(t);                                /* FM6: a user preset holds the PTCH and the macros */
    t->user = (uint8_t)(k + 1u);
    sync_reload = 1;
    ui.force = 1;
    return 0;
}
UI_SMALL static int up_load(uint32_t k) { return up_load_to(TSEL, k); }

UI_SMALL static uint32_t up_count(void)                 /* used slots */
{
    uint32_t k, n = 0;
    for (k = 0; k < UP_SLOTS; k++)
        n += (uint32_t)up_used(k);
    return n;
}

UI_SMALL static uint32_t up_nth(uint32_t n)             /* slot of the n-th used one (n < up_count()) */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_used(k) && !n--)
            return k;
    return 0;
}

UI_SMALL static uint32_t up_rank(uint32_t slot)         /* used slots before it */
{
    uint32_t k, n = 0;
    for (k = 0; k < slot && k < UP_SLOTS; k++)
        n += (uint32_t)up_used(k);
    return n;
}

/* SAVE > USER page actions, with the message in the top bar */
UI_SMALL static void up_ui(uint32_t op, uint32_t k)     /* 0 load, 1 erase, 2 save */
{
    char l[4];
    int rc;
#if FELUCCA_MULTI
    if (k >= UP_SLOTS) {                                /* (SMK-37) M01..M16: a multi sound (multi.c) */
        mu_ui(op, k - UP_SLOTS);
        ui.force = 1;
        return;
    }
#endif
    up_slot_label(l, k);
    if (op != 1u && is_drum(TSEL)) {
        ui_message("DRUM TRACK: NO SOUND");
        return;
    }
    if (op < 2u && !up_used(k)) {
        ui_message("EMPTY SLOT");
        return;
    }
    if (op == 0u) {
        up_load(k);
        ui_say("LOADED ", l);
        return;
    }
    if (song.playing || transport_req) {               /* a flash erase stops the audio ~50 ms */
        ui_message("STOP BEFORE SAVE");
        return;
    }
    rc = op == 1u ? up_put(k, 0) : up_store(k, 0);
    if (rc == 3)
        ui_message(op == 1u ? "ERASED (RAM)" : "SAVED (RAM)");
    else if (rc)
        ui_message(op == 1u ? "ERASE ERROR" : "SAVE ERROR");
    else
        ui_say(op == 1u ? "ERASED " : "SAVED ", l);
    ui.force = 1;
}
#endif
#if FELUCCA_MULTI && !defined(UP_HOST)
#include "multi.c"                                       /* (Stallion, SMK-37) the multi sounds: links to these and the
                                                          * factory sounds */
#endif
