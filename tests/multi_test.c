/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* Multi sounds (Stallion, SMK-37: firmware/src/multi.c, editor.c MULTI_LIST .. MULTI_ERASE = 100..104) on the real
 * firmware (tests/editor_host.c, CLIPPER_SYNTH=1, BOARD_SMK37=1, the store on a RAM image of the NOR):
 *   - PUT / GET / LIST round trip (the 47-byte record as sent), a bad record / slot refused, nothing written while playing
 *   - LOAD applies A's sound to part A, B's to part B, the mode and B's SPLIT, CH, OCT, LEVEL, PAN, FILT
 *   - links by name: a user preset by its name; a factory sound by engine + name, found in a reordered bank
 *   - a missing user preset (or factory name) reported (status 2 / 3, the names), nothing on the parts changed
 *   - ERASE; SAVE on M08 (SAVE > USER) on the device (the parts' links, "MULTI nn"; LAYER OFF refused); FACTORY RESET wipes them */
#include "editor_host.c"

static int bad;
static void check(const char *what, int ok) { printf("multi: %-100s %s\n", what, ok ? "ok" : "FAIL"); bad |= !ok; }

static uint8_t fr[64][700];
static uint32_t frn[64], nfr;
static void grab(const uint8_t *f, uint32_t n) { if (nfr < 64) { memcpy(fr[nfr], f, n); frn[nfr++] = n; } }
static const uint8_t *req(uint32_t cmd, const uint8_t *a, uint32_t na, uint32_t *n)   /* the reply after its header */
{
    uint8_t f[700];
    uint32_t i;
    f[0] = ED_HDR0; f[1] = ED_HDR1; f[2] = ED_HDR2; f[3] = (uint8_t)cmd;
    memcpy(f + 4, a, na);
    nfr = 0;
    fm1_ms += 3;
    usb_sysex(f, 4 + na);
    rx_take();
    for (i = nfr; i-- > 0;)
        if (fr[i][4] == cmd) { *n = frn[i] - 6u; return fr[i] + 5; }
    *n = 0;
    return 0;
}
static void n12(char *d, const char *s) { mu_name_set(d, s); }
static void link_f(mu_link_t *l, uint32_t e, uint32_t pi)    /* a factory link: engine e, preset pi's name */
{
    l->kind = MU_FACTORY;
    l->eng = (uint8_t)e;
    n12(l->name, ENGINES[e]->presets[pi].name);
}
static void link_u(mu_link_t *l, const char *nm) { l->kind = MU_USER; l->eng = 0; n12(l->name, nm); }
static int put(uint32_t slot, const mu_rec_t *r)               /* PUT: the reply's ok, -1 no / bad reply */
{
    uint8_t a[64];
    uint32_t n;
    const uint8_t *p;
    a[0] = (uint8_t)slot;
    memcpy(a + 1, r, sizeof *r);
    p = req(ED_MULTI_PUT, a, 1u + sizeof *r, &n);
    return p && n == 2u && p[0] == slot ? p[1] : -1;
}
static int load(uint32_t slot, char *an, char *bn)            /* LOAD: the status, -1 no / bad reply; the names */
{
    uint8_t a[1] = {(uint8_t)slot};
    uint32_t n;
    const uint8_t *p = req(ED_MULTI_LOAD, a, 1, &n);
    if (!p || n != 26u || p[0] != slot)
        return -1;
    memcpy(an, p + 2, 12); an[12] = 0;
    memcpy(bn, p + 14, 12); bn[12] = 0;
    return p[1];
}
static int erase(uint32_t slot)
{
    uint8_t a[1] = {(uint8_t)slot};
    uint32_t n;
    const uint8_t *p = req(ED_MULTI_ERASE, a, 1, &n);
    return p && n == 2u && p[0] == slot ? p[1] : -1;
}
static int list_used(uint32_t slot, char *nm)                  /* LIST: slot's used byte, -1 bad reply; its name */
{
    uint32_t n;
    const uint8_t *p = req(ED_MULTI_LIST, 0, 0, &n);
    if (!p || n != 1u + MU_SLOTS * 13u || p[0] != MU_SLOTS)
        return -1;
    memcpy(nm, p + 1 + slot * 13u + 1u, 12); nm[12] = 0;
    return p[1 + slot * 13u];
}
static int list_none(void)                                     /* LIST: every slot empty */
{
    uint32_t n, k;
    const uint8_t *p = req(ED_MULTI_LIST, 0, 0, &n);
    if (!p || n != 1u + MU_SLOTS * 13u || p[0] != MU_SLOTS)
        return 0;
    for (k = 0; k < MU_SLOTS; k++)
        if (p[1 + k * 13u])
            return 0;
    return 1;
}
static void parts_to(uint32_t ea, uint32_t pa, uint32_t eb, uint32_t pb)   /* the parts on these sounds, the layer OFF */
{
    set_engine_of(&trk[0], ea); apply_preset_to(&trk[0], pa);
    set_engine_of(&trk[1], eb); apply_preset_to(&trk[1], pb);
    trk[1].p[P_LYMODE] = LY_OFF;
    trk[1].p[P_LYSPLIT] = 64; trk[1].p[P_LYOCT] = 2; trk[1].p[P_LEVEL] = 100; trk[1].p[P_PAN] = 0;
    trk[1].p[P_TFLT] = 0; trk[1].p[P_LYCH] = 2;
}
static int eng_vals(const track_t *t, uint32_t e, uint32_t pi)   /* the part plays factory sound pi of engine e */
{
    uint32_t i;
    if (t->eng_req != e || t->preset != pi || t->user)
        return 0;
    for (i = 0; i < 8u; i++)
        if (t->p[P_E0 + i] != ENGINES[e]->presets[pi].e[i])
            return 0;
    return t->p[P_ATK] == ENGINES[e]->presets[pi].env[0] && t->p[P_REL] == ENGINES[e]->presets[pi].env[3];
}

int main(void)
{
    mu_rec_t r, g;
    uint32_t n, e, i, ea = 1, pa = 3, eb = 0, pb = 2;           /* DIGITAL's 4th sound in A, ANALOG's 3rd in B */
    const uint8_t *p;
    char an[13], bn[13], nm[13];
    harness_init();
    rx_hook = grab;
    check("the store: OBJ_MULTI at 0xB0000 / 0xB1000, after the SMK store's owner tag (0xAF000, never moved)",
          st_sector(OBJ_MULTI, 0) == 0xB0000u && st_sector(OBJ_MULTI, 1) == 0xB1000u && ST_OWNER_OFF == 0xAF000u &&
          st_sector(OBJ_SETTINGS, 1) == 0xAE000u && flash_ok);
    check("LIST on a fresh store: 16 slots, every one empty", list_none());
    {
        uint8_t a[1] = {4};
        p = req(ED_MULTI_GET, a, 1, &n);
        static const uint8_t z[47];
        check("GET of an empty slot: slot, used 0, 47 zeros", p && n == 2u + 47u && p[0] == 4u && p[1] == 0u && !memcmp(p + 2, z, 47));
    }

    /* PUT / GET / LIST */
    check("the test's sounds exist", ENGINES[ea]->npresets > pa && ENGINES[eb]->npresets > pb);
    memset(&r, 0, sizeof r);
    n12(r.name, "SPLIT TEST");
    r.mode = LY_SPLIT;
    link_f(&r.a, ea, pa);
    link_f(&r.b, eb, pb);
    r.split = 60; r.oct = 3; r.level = 90; r.pan = 64 + 20; r.filt = 64 - 10; r.ch = 5;
    check("PUT slot 3: ok 1", put(3, &r) == 1);
    {
        uint8_t a[1] = {3};
        p = req(ED_MULTI_GET, a, 1, &n);
        check("GET slot 3: slot, used 1, the 47 bytes as PUT", p && n == 49u && p[0] == 3u && p[1] == 1u && !memcmp(p + 2, &r, 47));
    }
    check("LIST: slot 3 used, its name (12, space-padded); the others empty",
          list_used(3, nm) == 1 && !memcmp(nm, "SPLIT TEST  ", 12) && list_used(2, nm) == 0 && list_used(15, nm) == 0);
    g = r; g.mode = 0;
    check("PUT refused (ok 0): mode 0, CH 0, OCT 5, SPLIT 0, an unknown engine, a link kind 2, a blank name, slot 16, a short record",
          put(5, &g) == 0 && (g = r, g.ch = 0, put(5, &g)) == 0 && (g = r, g.oct = 5, put(5, &g)) == 0 &&
          (g = r, g.split = 0, put(5, &g)) == 0 && (g = r, n12(g.name, ""), put(5, &g)) == 0 &&
          (g = r, g.a.eng = NENGINES, put(5, &g)) == 0 &&
          (g = r, g.b.kind = 2, put(5, &g)) == 0 && (g = r, n12(g.b.name, ""), put(5, &g)) == 0 && put(16, &r) == 0 &&
          (p = req(ED_MULTI_PUT, (const uint8_t *)"\005abc", 4, &n)) && n == 2u && p[1] == 0u && list_used(5, nm) == 0);
    song.playing = 1;
    check("PUT / ERASE while playing: ok 0, nothing written (a flash erase stops the audio)",
          put(6, &r) == 0 && erase(3) == 0 && list_used(6, nm) == 0 && list_used(3, nm) == 1);
    song.playing = 0;

    /* LOAD */
    parts_to(4, 1, 5, 0);
    trk[0].p[P_LEVEL] = 77;
    i = (uint32_t)load(3, an, bn);
    check("LOAD slot 3: status 0, A's and B's link names", i == 0u && !memcmp(an, r.a.name, 12) && !memcmp(bn, r.b.name, 12));
    check("LOAD: part A plays A's factory sound (engine, preset, its values)", eng_vals(&trk[0], ea, pa));
    check("LOAD: part B plays B's factory sound (as EDIT > LAYER > SOUND)", eng_vals(&trk[1], eb, pb));
    check("LOAD: the mode SPLIT, B's SPLIT 60, OCT 3, LEVEL 90, PAN +20, FILT -10, CH 5; A's own LEVEL kept",
          trk[1].p[P_LYMODE] == LY_SPLIT && trk[1].p[P_LYSPLIT] == 60 && trk[1].p[P_LYOCT] == 3 && trk[1].p[P_LEVEL] == 90 &&
          trk[1].p[P_PAN] == 20 && trk[1].p[P_TFLT] == -10 && trk[1].p[P_LYCH] == 5 && trk[0].p[P_LEVEL] == 77);
    {
        char why[200] = "";
        check("LOAD: every parameter of every part in its range", params_check(why, sizeof why));
        if (why[0]) printf("  %s\n", why);
    }

    /* a user preset by name; missing -> reported, nothing changed */
    parts_to(6, 2, 0, 0);
    check("a user preset 'MY PAD' in slot 6 (USER 07)", up_store(6, "MY PAD") == 0 && up_used(6));
    memset(&r, 0, sizeof r);
    n12(r.name, "PAD LAYER");
    r.mode = LY_MULTI;
    link_u(&r.a, "MY PAD");                                    /* (by its name, as written) */
    link_f(&r.b, eb, pb);
    r.split = 64; r.oct = 1; r.level = 100; r.pan = 64; r.filt = 64; r.ch = 9;
    check("PUT slot 0: A a user preset by name", put(0, &r) == 1);
    parts_to(3, 0, 2, 1);
    i = (uint32_t)load(0, an, bn);
    check("LOAD slot 0: status 0, part A the user preset (USER 07), B's sound, MULTI on CH 9, OCT 1",
          i == 0u && trk[0].user == 7u && trk[0].eng_req == 6u && eng_vals(&trk[1], eb, pb) && trk[1].p[P_LYMODE] == LY_MULTI &&
          trk[1].p[P_LYCH] == 9 && trk[1].p[P_LYOCT] == 1);
    check("the user preset erased", up_put(6, 0) == 0 && !up_used(6));
    parts_to(3, 0, 2, 1);
    trk[1].p[P_LYMODE] = LY_LAYER;
    {
        static track_t t0, t1;
        t0 = trk[0], t1 = trk[1];
        i = (uint32_t)load(0, an, bn);
        check("LOAD with A's user preset gone: status 2 (part A missing), its name reported",
              i == 2u && !memcmp(an, "MY PAD      ", 12) && !memcmp(bn, r.b.name, 12));
        check("  .. nothing loaded: both parts exactly as they were (no sound put in its place)",
              !memcmp(&t0, &trk[0], sizeof t0) && !memcmp(&t1, &trk[1], sizeof t1));
        g = r;
        link_f(&g.a, ea, pa);
        n12(g.b.name, "NO SUCH SND");
        check("a B link to a factory name the engine does not have: status 3 (part B missing), nothing changed",
              put(1, &g) == 1 && load(1, an, bn) == 3 && !memcmp(bn, "NO SUCH SND ", 12) &&
              !memcmp(&t0, &trk[0], sizeof t0) && !memcmp(&t1, &trk[1], sizeof t1));
        check("LOAD of an empty slot: status 1, both names 12 spaces, nothing changed",
              load(9, an, bn) == 1 && !strcmp(an, "            ") && !strcmp(bn, "            ") && !memcmp(&t0, &trk[0], sizeof t0) && !memcmp(&t1, &trk[1], sizeof t1));
        mu_ui(0, 0);
        check("LOAD on M01 on the device: 'MISSING: MY PAD'", !strcmp(ui.msg, "MISSING: MY PAD"));
    }

    /* a firmware update reorders a factory bank: the link (engine + name, no index) still finds its sound */
    {
        static engine_t copy;
        static preset_t rev[256];
        uint32_t np = ENGINES[ea]->npresets, ok = 1;
        copy = *ENGINES[ea];
        for (i = 0; i < np; i++)
            rev[i] = ENGINES[ea]->presets[np - 1u - i];
        copy.presets = rev;
        ok = mu_ffind(ENGINES[ea], r.name) < 0;               /* (a name it does not have: none) */
        link_f(&g.a, ea, pa);
        check("a bank reordered (reversed): the link finds its sound at its new place",
              ok && mu_ffind(&copy, g.a.name) == (int)(np - 1u - pa) && mu_ffind(ENGINES[ea], g.a.name) == (int)pa &&
              !strcmp(rev[np - 1u - pa].name, ENGINES[ea]->presets[pa].name));
    }
    {   /* every factory sound's link finds that sound (no two names alike in their first 12 letters) */
        uint32_t dup = 0;
        for (e = 0; e < NENGINES; e++)
            for (i = 0; i < ENGINES[e]->npresets; i++) {
                mu_link_t l;
                link_f(&l, e, i);
                if (mu_ffind(ENGINES[e], l.name) != (int)i) {
                    if (dup++ < 8) printf("  engine %s preset %u '%s' links to %d\n", ENGINES[e]->name, i, ENGINES[e]->presets[i].name, mu_ffind(ENGINES[e], l.name));
                }
            }
        check("every factory sound of every engine: its link (engine + name) finds it, and only it", dup == 0);
    }

    /* ERASE */
    check("ERASE slot 3: ok 1; GET used 0, LIST empty there; slot 0 kept",
          erase(3) == 1 && list_used(3, nm) == 0 && list_used(0, nm) == 1 && !mu_used(3));
    check("ERASE slot 16: ok 0", erase(16) == 0);

    /* SAVE on M08 (SAVE > USER) on the device */
    parts_to(ea, pa, eb, pb);
    mu_ui(2, 7);
    check("SAVE with the layer OFF: refused ('LAYER IS OFF'), nothing stored", !strcmp(ui.msg, "LAYER IS OFF") && !mu_used(7));
    mu_ui(0, 7);
    check("LOAD of an empty slot on the device: 'EMPTY SLOT'", !strcmp(ui.msg, "EMPTY SLOT"));
    trk[1].p[P_LYMODE] = LY_LAYER; trk[1].p[P_LEVEL] = 55; trk[1].p[P_PAN] = -30; trk[1].p[P_TFLT] = 12; trk[1].p[P_LYOCT] = 4;
    mu_ui(2, 7);
    check("SAVE on M08 (SAVE > USER) on the device: 'SAVED M08', named 'MULTI 08', the parts' links and B's settings",
          !strcmp(ui.msg, "SAVED M08") && mu_get(7, &g) && !memcmp(g.name, "MULTI 08    ", 12) && g.mode == LY_LAYER &&
          g.a.kind == MU_FACTORY && g.a.eng == ea && mu_name_eq(g.a.name, ENGINES[ea]->presets[pa].name) &&
          g.b.kind == MU_FACTORY && g.b.eng == eb && mu_name_eq(g.b.name, ENGINES[eb]->presets[pb].name) &&
          g.level == 55 && g.pan == 34 && g.filt == 76 && g.oct == 4 && g.ch == 2);
    parts_to(7, 0, 8, 0);
    mu_ui(0, 7);
    check("LOAD on M08 on the device: the parts back, 'LOADED MULTI 08'",
          !strcmp(ui.msg, "LOADED MULTI 08") && eng_vals(&trk[0], ea, pa) && eng_vals(&trk[1], eb, pb) &&
          trk[1].p[P_LYMODE] == LY_LAYER && trk[1].p[P_LEVEL] == 55 && trk[1].p[P_PAN] == -30 && trk[1].p[P_TFLT] == 12);
    {   /* the device's page: SAVE > USER, after U32 the multi sounds M01..M16 (the slot on KNOB 1) */
        uint32_t pg;
        char l[4];
        for (pg = 0; pg < NPAGES && PAGES[pg].graph != GR_USER; pg++) ;
        ui.home = 0;
        ui.page = (uint8_t)pg;
        ui.uslot = 0;
        edit_param(0, 200);
        check("SAVE > USER: KNOB 1 runs past U32 to M16 (slot 47), no further", ui.uslot == UP_SLOTS + MU_SLOTS - 1u);
        up_slot_label(l, UP_SLOTS + 7u);
        edit_param(0, -8);                                      /* back to M08 */
        edit_param(1, 1);                                       /* LOAD: one detent arms .. */
        check("  .. M08 (slot 39) labelled 'M08'; one detent of LOAD arms ('AGAIN: LOAD')",
              ui.uslot == UP_SLOTS + 7u && !strcmp(l, "M08") && !strcmp(ui.msg, "AGAIN: LOAD"));
        parts_to(7, 0, 8, 0);
        edit_param(1, 1);                                       /* .. a second acts */
        check("  .. a second detent loads the multi ('LOADED MULTI 08')", !strcmp(ui.msg, "LOADED MULTI 08") && eng_vals(&trk[0], ea, pa));
        up_slot_label(l, 6u);
        check("  .. U07 still 'U07' (the user presets as before)", !strcmp(l, "U07"));
        ui.force = 1;
        ui_draw();
        check("  .. and it draws (the multis' names read from the flash)", 1);
    }
    edit_param(2, 1);
    edit_param(2, 1);                                           /* ERASE, twice (arm, act) */
    check("SAVE > USER on M08: ERASE on the device: 'ERASED M08'", !strcmp(ui.msg, "ERASED M08") && !mu_used(7));

    {   /* a damaged current copy (its CRC fails): the other copy (A/B) answers, as for every object */
        st_hdr_t h;
        int c = st_current(OBJ_MULTI, &h);
        nor[st_sector(OBJ_MULTI, (uint32_t)c) + ST_PAYLOAD_OFF + 9] ^= 0x55;
        check("a damaged copy: the older copy answers (slot 7 still there, before its ERASE), LIST well formed",
              c >= 0 && st_current(OBJ_MULTI, &h) == (c ^ 1) && list_used(7, nm) == 1);
    }

    /* FACTORY RESET wipes them (user data) */
    check("FACTORY RESET (storage.c st_wipe_user): every multi sound gone", st_wipe_user(0) == 0 && list_none());
    printf(bad ? "multi: FAIL\n" : "multi: all ok\n");
    return bad;
}
