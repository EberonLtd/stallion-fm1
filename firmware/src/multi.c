/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* Multi sounds (Stallion, SMK-37 only: FELUCCA_MULTI): 16 slots, each a combination of two sounds by LINKS only: its
 * name, its mode (LAYER / SPLIT / MULTI: core.h LY_*), which single sound is part A (trk[0]) and which part B (trk[1]),
 * and B's own settings for the combination (split point, MIDI channel, octave, level, pan, the track filter). A multi
 * never holds a copy of a sound. A link is by NAME: a factory sound by its engine and its preset's name (a firmware
 * update that reorders a bank still finds it), a user preset by its name. A link that finds nothing is reported and
 * nothing is loaded: never a sound in its place.
 *
 * Stored in one storage.c object (OBJ_MULTI, A/B sectors after the SMK store's owner tag), read from flash when needed
 * (st_current leaves the payload in st_buf): no RAM copy of the slots. Wiped by FACTORY RESET (st_wipe_user). On the
 * keys: SAVE > USER, after U32 the slots M01..M16 (upreset.c up_ui: LOAD, ERASE, SAVE as for a user preset).
 * The record (47 bytes, every byte 0..127: the editor sends it as it is, web/EDITOR_PROTOCOL.md "Multi sounds"):
 * name 12 (ASCII, space-padded), mode, part A's link (kind 0 factory / 1 user, engine (user: 0), name 12), part B's
 * link, then B's SPLIT (P_LYSPLIT), OCT (P_LYOCT as stored), LEVEL (P_LEVEL), PAN + 64, FILT (P_TFLT) + 64, CH
 * (P_LYCH 1..16). An empty slot reads as 47 zeros. */
#define MU_MAGIC 0x314C554Du                     /* "MUL1" (the layout: a change takes a new magic) */
enum { MU_FACTORY, MU_USER };
enum { MU_OK, MU_EMPTY, MU_MISS_A, MU_MISS_B };  /* MULTI_LOAD's status */
typedef struct {
    uint8_t kind, eng;                           /* MU_FACTORY: the engine; MU_USER: 0 */
    char name[12];                               /* space-padded */
} mu_link_t;
typedef struct {
    char name[12];
    uint8_t mode;                                /* LY_LAYER, LY_SPLIT, LY_MULTI */
    mu_link_t a, b;
    uint8_t split, oct, level, pan, filt, ch;    /* part B's: pan and filt + 64 */
} mu_rec_t;
typedef struct {
    uint32_t magic;
    uint8_t used[MU_SLOTS];                      /* 1: the slot holds a record (checked when it was stored) */
    mu_rec_t r[MU_SLOTS];
} mu_bank_t;
_Static_assert(sizeof(mu_link_t) == 14u && sizeof(mu_rec_t) == 47u, "multi record: 47 bytes (the editor's)");
_Static_assert(sizeof(mu_bank_t) <= 1024u, "multi bank: one object, read into st_buf");
/* (ui.c: mu_gen, bumped on every change for the redraw) */

/* a 12-byte name (space-padded) set from / equal to s (0-terminated or 12 long; the trailing spaces aside). eq 0:
 * n12 = s; 1: 1 when they are equal (as the names are written: the editor's links from MULTI_LIST / UP_LIST) */
UI_SMALL static int mu_name_cmp(char *n12, const char *s, uint32_t eq)
{
    uint32_t i, end = 0;
    for (i = 0; i < 12u; i++) {
        char c;
        end = end || !s[i];                      /* (never reads past the 0) */
        c = end ? ' ' : s[i];
        if (!eq)
            n12[i] = c;
        else if (n12[i] != c)
            return 0;
    }
    return 1;
}
#define mu_name_set(n12, s) ((void)mu_name_cmp((n12), (s), 0u))
#define mu_name_eq(n12, s) mu_name_cmp((char *)(n12), (s), 1u)
UI_SMALL static void mu_name_str(char *b, const char *n12)   /* 0-terminated, the trailing spaces off: b holds 13 */
{
    uint32_t n = 12u;
    memcpy(b, n12, 12);
    while (n && b[n - 1u] == ' ')
        n--;
    b[n] = 0;
}
UI_SMALL static int mu_name_ok(const char *n12)  /* printable ASCII, the first letter not a space */
{
    uint32_t i;
    for (i = 0; i < 12u; i++)
        if (n12[i] < 32 || n12[i] > 126)
            return 0;
    return n12[0] != ' ';
}
UI_SMALL static int mu_link_ok(const mu_link_t *l)
{
    return (l->kind == MU_USER ? l->eng == 0u : l->kind == MU_FACTORY && l->eng < NENGINES && l->eng != ENGI_GONE) && mu_name_ok(l->name);   /* (1.0: not LOFI's retired place) */
}
UI_SMALL static int mu_rec_ok(const mu_rec_t *r)  /* (every byte is 0..127: SysEx) */
{
    return mu_name_ok(r->name) && r->mode >= LY_LAYER && r->mode <= LY_MULTI && mu_link_ok(&r->a) && mu_link_ok(&r->b) &&
           r->split && r->oct < 5u && r->ch >= 1u && r->ch <= 16u;
}

/* the bank, read from flash into st_buf (valid until the next storage call); 0: none (nothing stored, or not ours) */
UI_SMALL static const mu_bank_t *mu_bank(void)
{
#if FELUCCA_FLASH
    const mu_bank_t *b = (const mu_bank_t *)st_buf;
    st_hdr_t h;
    if (flash_ok && st_current(OBJ_MULTI, &h) >= 0 && h.len == sizeof *b && b->magic == MU_MAGIC)
        return b;
#endif
    return 0;
}
/* slot k into *r: 1 used, 0 empty (*r all zeros) */
UI_SMALL static int mu_get(uint32_t k, mu_rec_t *r)
{
    const mu_bank_t *b = mu_bank();
    memset(r, 0, sizeof *r);
    if (!b || k >= MU_SLOTS || b->used[k] != 1u)
        return 0;
    *r = b->r[k];
    return 1;
}
UI_SMALL static int mu_used(uint32_t k)
{
    mu_rec_t r;
    return mu_get(k, &r);
}
UI_SMALL static int mu_get_name(uint32_t k, char *b)   /* used: 1 and its name in b (13), else 0 (b as it was) */
{
    mu_rec_t r;
    if (!mu_get(k, &r))
        return 0;
    mu_name_str(b, r.name);
    return 1;
}

/* slot k = *r (0: erased), the bank to flash: 0 ok, 1 bad record / slot, 2 flash error */
UI_SMALL static int mu_put(uint32_t k, const mu_rec_t *r)
{
#if FELUCCA_FLASH
    mu_bank_t nb;                                /* (on the stack while it is written: no RAM kept) */
    const mu_bank_t *b;
    if (k >= MU_SLOTS || (r && !mu_rec_ok(r)))
        return 1;
    b = mu_bank();
    if (b)
        nb = *b;
    else
        memset(&nb, 0, sizeof nb);
    nb.magic = MU_MAGIC;
    nb.used[k] = r != 0;
    if (r)
        nb.r[k] = *r;
    mu_gen++;
    return flash_ok && !st_save(OBJ_MULTI, &nb, sizeof nb) ? 0 : 2;
#else
    (void)k;
    (void)r;
    return 2;
#endif
}

/* the link's factory preset index (in the engine's table e, so a test can reorder it) / user slot, -1 missing */
UI_SMALL static int mu_ffind(const engine_t *e, const char *n12)
{
    uint32_t i;
    for (i = 0; i < e->npresets; i++)
        if (mu_name_eq(n12, e->presets[i].name))
            return (int)i;
    return -1;
}
UI_SMALL static int mu_find(const mu_link_t *l)
{
    uint32_t k;
    if (l->kind == MU_FACTORY)
        return mu_ffind(ENGINES[l->eng], l->name);   /* (eng < NENGINES: checked when it was stored) */
    for (k = 0; k < UP_SLOTS; k++)               /* a user preset by its name (the first of that name) */
        if (up_used(k) && mu_name_eq(l->name, up_rec(k)->name))
            return (int)k;
    return -1;
}
UI_SMALL static void mu_link_of(const track_t *t, mu_link_t *l)   /* the link to the sound part t plays now */
{
    uint32_t u = user_of(t);
    l->kind = MU_USER;
    l->eng = 0;
    if (u < UP_SLOTS) {
        mu_name_set(l->name, up_rec(u)->name);
        return;
    }
    l->kind = MU_FACTORY;                        /* (every engine has its presets, INIT first; t->preset one of them) */
    l->eng = t->eng_req;
    mu_name_set(l->name, ENGINES[t->eng_req]->presets[t->preset].name);
}
UI_SMALL static void mu_load_link(track_t *t, const mu_link_t *l, int idx)   /* (found) */
{
    if (l->kind == MU_USER) {
        up_load_to(t, (uint32_t)idx);
        return;
    }
    if (l->eng != t->eng_req)
        set_engine_of(t, l->eng);
    apply_preset_to(t, (uint32_t)idx);
}
/* slot k onto the two parts: A's sound into part A, B's into part B (as EDIT > LAYER > SOUND), then the mode and B's
 * settings. A link that finds nothing: nothing changes. MU_OK .. MU_MISS_B; *r gets the record (zeros when empty) */
UI_SMALL static int mu_apply(uint32_t k, mu_rec_t *r)
{
    track_t *b = &trk[1];
    int ia, ib;
    if (!mu_get(k, r))
        return MU_EMPTY;
    ia = mu_find(&r->a);
    ib = mu_find(&r->b);
    if (ia < 0)
        return MU_MISS_A;
    if (ib < 0)
        return MU_MISS_B;
    mu_load_link(&trk[0], &r->a, ia);
    mu_load_link(b, &r->b, ib);
    b->p[P_LYMODE] = r->mode;                    /* (each in its range: checked when it was stored; B's notes went
                                                  * with its new sound: its panic) */
    b->p[P_LYSPLIT] = r->split;
    b->p[P_LYOCT] = r->oct;
    b->p[P_LEVEL] = r->level;
    b->p[P_PAN] = (int16_t)(r->pan - 64);
    b->p[P_TFLT] = (int16_t)(r->filt - 64);
    b->p[P_LYCH] = r->ch;
    sync_reload = 1;
    ui.force = 1;
    return MU_OK;
}

/* SAVE > USER on M01..M16 (upreset.c up_ui), the message in the top bar: 0 load, 1 erase, 2 save (the parts' sounds
 * linked, the mode and B's settings, named "MULTI nn") */
UI_SMALL static void mu_ui(uint32_t op, uint32_t k)
{
    mu_rec_t r;
    char nm[13];
    int rc;
    if (op == 0u) {
        rc = mu_apply(k, &r);
        mu_name_str(nm, rc == MU_MISS_B ? r.b.name : rc == MU_MISS_A ? r.a.name : r.name);
        ui_say(rc == MU_EMPTY ? "EMPTY SLOT" : rc == MU_OK ? "LOADED " : "MISSING: ", nm);
        return;
    }
    if (song.playing || transport_req) {        /* a flash erase stops the audio ~50 ms */
        ui_message("STOP BEFORE SAVE");
        return;
    }
    if (op == 2u) {
        memcpy(nm, "MULTI ", 6);
        up_slot_label(nm + 5, UP_SLOTS + k);     /* ("M08" over its space, then the space back: "MULTI 08") */
        nm[5] = ' ';
        mu_name_set(r.name, nm);
        r.mode = (uint8_t)trk[1].p[P_LYMODE];
        mu_link_of(&trk[0], &r.a);
        mu_link_of(&trk[1], &r.b);
        r.split = (uint8_t)trk[1].p[P_LYSPLIT];
        r.oct = (uint8_t)trk[1].p[P_LYOCT];
        r.level = (uint8_t)trk[1].p[P_LEVEL];
        r.pan = (uint8_t)(trk[1].p[P_PAN] + 64);
        r.filt = (uint8_t)(trk[1].p[P_TFLT] + 64);
        r.ch = (uint8_t)layer_ch();
        if (r.mode == LY_OFF) {
            ui_message("LAYER IS OFF");
            return;
        }
    }
    up_slot_label(nm, UP_SLOTS + k);
    if (mu_put(k, op == 2u ? &r : 0))
        ui_message(op == 2u ? "SAVE ERROR" : "ERASE ERROR");
    else
        ui_say(op == 2u ? "SAVED " : "ERASED ", nm);   /* ("SAVED M08", as a user preset's "SAVED U07") */
}
