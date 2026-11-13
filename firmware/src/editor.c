/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Editor protocol: SysEx for the web editor (web/EDITOR_PROTOCOL.md; v2 = user presets + live sync,
 * v3 = four tracks: the v1 / v2 commands act on the selected track, cmds 27-30 reach any track;
 * v4 = TRACK_PARAM (31) and the TRACK_CHANGED push (32), enabled by WATCH bit 1;
 * v5 = SLOOP 2.0: INFO ends with the protocol version (5), steps carry level / ratchet bytes,
 * DRUM_STEP (33) reads / writes the drum track's 16 lanes, TRACK ends with the solo mask;
 * v6 = SLOOP 2.3: backup / restore (34-36); v7 = SLOOP 2.4: the steps' nudges and parameter locks (37-40);
 * v8 = SLOOP 2.4: the steps' fill conditions (41-42); v9 = SLOOP 2.4: the FM6 engine's patches (68-71,
 * editor_fm6.c: Felucca 1.0's numbers) and the patch bank as backup object 8); v10 = SLOOP 2.5: the SYN drum
 * kits (72-76, editor_dsyn.c) and backup object 9.
 *   F0 7D 46 4C cmd args.. F7     (7D = non-commercial ID, "FL")
 * Values are 14 bit, two 7-bit bytes LSB first, offset by 8192 (so -8192..8191).
 * Every request gets a reply with the same cmd; 23/24/26 are also pushed
 * while watched. Frames arrive through sx_frame (usb.c), replies leave
 * through ota_wire_send(). */
#ifndef UI_SMALL
#define UI_SMALL                                   /* (gfx.c: minsize on the device; a test alone: nothing) */
#endif
#define ED_HDR0 0x7D
#define ED_HDR1 0x46
#define ED_HDR2 0x4C
enum { ED_INFO = 1, ED_GET, ED_SET, ED_DUMP, ED_DESC, ED_STEP_GET, ED_STEP_SET, ED_PRESET, ED_PROJECT, ED_NAMES,
       ED_SMP_BEGIN, ED_SMP_WRITE, ED_SMP_END, ED_SMP_ERASE, ED_SMP_INFO,
       ED_UP_LIST, ED_UP_GET, ED_UP_PUT, ED_UP_STORE, ED_UP_LOAD, ED_UP_ERASE,   /* v2: user presets */
       ED_WATCH, ED_CHANGED, ED_RELOAD, ED_PING, ED_STEP_CHANGED,              /* v2: live sync */
       ED_TRACK, ED_TRACK_MIX, ED_TRACK_DUMP, ED_TRACK_STEP,                    /* v3: tracks */
       ED_TRACK_PARAM, ED_TRACK_CHANGED,                                        /* v4: any track's parameters */
       ED_DRUM_STEP,                                                            /* v5: the 16 drum lanes */
       ED_BK_LIST, ED_BK_GET, ED_BK_PUT,                                        /* v6: backup / restore */
       ED_LOCK_GET, ED_LOCK_SET, ED_MICRO_GET, ED_MICRO_SET,                    /* v7: parameter locks, nudges */
       ED_FILL_GET, ED_FILL_SET,                                                /* v8: fill conditions */
       ED_FM6_GET = 68, ED_FM6_PUT, ED_FM6_LIST, ED_FM6_ERASE };                /* v9: FM6 patches (Felucca's numbers) */
/* v10 (SLOOP 2.5): DSYN_LIST .. DSYN_PLAY = 72..76, editor_dsyn.c; backup object 9 = the SYN kits */
/* v11 (Clipper): HY_GET, HY_PUT = 77, 78, editor_hybrid.c */
#define ED_PROTO (CLIPPER_SYNTH ? 15u : 11u)           /* the protocol version INFO ends with (12: Clipper's FMX, 13: its
                                                        * MOD SEQ, PONY, VECTOR, 14: the TUNING; 15, Stallion: every
                                                        * parameter by a 14-bit id (scope 2), the FM-1's own settings
                                                        * (scope 3), the values 127.. in DUMP / UP_GET / UP_PUT) */

static uint8_t ed_out[600];
static uint32_t ed_n;

UI_SMALL static void ed_begin(uint32_t cmd)
{
    ed_out[0] = 0xF0;
    ed_out[1] = ED_HDR0;
    ed_out[2] = ED_HDR1;
    ed_out[3] = ED_HDR2;
    ed_out[4] = (uint8_t)cmd;
    ed_n = 5;
}
UI_SMALL static void ed_b(uint32_t v)
{
    if (ed_n < sizeof ed_out - 1u)
        ed_out[ed_n++] = (uint8_t)(v & 0x7Fu);
}
UI_SMALL static void ed_v(int32_t v)
{
    uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192);
    ed_b(u);
    ed_b(u >> 7);
}
UI_SMALL static void ed_str(const char *s, uint32_t max)   /* ASCII, 0-terminated */
{
    uint32_t i;
    for (i = 0; s && s[i] && i < max; i++)
        ed_b((uint8_t)s[i] & 0x7Fu);
    ed_b(0);
}
UI_SMALL static void ed_id(uint32_t k) { ed_b(k); ed_b(k >> 7); }   /* v15: a 14-bit id or count */
UI_SMALL static void ed_send(void)
{
    ed_out[ed_n++] = 0xF7;
    ota_wire_send(ed_out, ed_n);
}
UI_SMALL static int32_t ed_rv(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 7) - 8192; }
UI_SMALL static void ed_p15(const int16_t *p)            /* v15: the values of ids 127.. (the device's own): n, n x v14 */
{
    uint32_t i;
    ed_id(P_EDHN);
    for (i = 0; i < P_EDHN; i++)
        ed_v(p[P_EDH0 + i]);
}

/* ---- user sample slots (eng_sample.c): flash SMP_USER_OFF(k) (USR1..3 at 0xA0000.., USR4 at 0xE7000) ----
 * BEGIN erases the header sector (the slot is invalid from then on), WRITE fills the data
 * (offset >= 512, erasing each further sector when the write reaches its start), END sends
 * the header: the device checks the data CRC and writes the header last. */
UI_SMALL static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;                                 /* groups: msb byte, then up to 7 bytes */
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        for (j = 0; j < 7u && na && n < max; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return n;
}
UI_SMALL static void ed_pack7(const uint8_t *p, uint32_t n)      /* pack7: a top-bits byte, then up to 7 bytes */
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, m = 0, i;
        for (i = 0; i < k; i++)
            m |= (uint32_t)(p[i] >> 7) << i;
        ed_b(m);
        for (i = 0; i < k; i++)
            ed_b(p[i] & 127u);
        p += k;
        n -= k;
    }
}
static uint8_t ed_smp_buf[512] __attribute__((aligned(4)));
static uint8_t ed_smp_open[SMP_USER_SLOTS];        /* SMP_BEGIN done, END not yet: WRITE / END may act */
UI_SMALL static uint32_t ed_smp_slot(uint32_t k) { return SMP_USER_OFF(k); }
UI_SMALL static void ed_smp_inval(uint32_t k)
{
    fm1_irq_off();
    fl_inval(ed_smp_slot(k), SMP_USER_SIZE);
    fm1_irq_on();
}
UI_SMALL static int ed_smp_erase(uint32_t k, uint32_t all)  /* header sector, or the whole slot */
{
    uint32_t i, took;
    int rc = 0;
#if BOARD_SMK37
    if (!SMP_USER_HAS(k))
        return -8;                                 /* SMK: USR3 / USR4 have no flash */
#endif
    usr_nz[k] = 0;
    for (i = 0; i < 16u; i++)
        usr_zone[k][i].n = 0;                     /* a sounding voice ends instead of reading 0xFF */
#if CLIPPER_SYNTH
    wt_user_ok[k] = 0;                            /* (a wavetable there: WAVE stops reading it, its copies go) */
    if (wt_have == (int16_t)(WT_NFACTORY + k))
        wt_have = -1;
#endif
    for (i = 0; i < (all ? SMP_USER_SIZE / 0x1000u : 1u) && !rc; i++) {
        rc = fl_erase4k_quiet(ed_smp_slot(k) + i * 0x1000u, &took);
        fm1_wdt_feed();
    }
    ed_smp_inval(k);
    return rc;
}
UI_SMALL static int ed_smp_end(uint32_t k, const uint8_t *a, uint32_t na)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)ed_smp_buf;
    uint32_t i;
    if (!ed_smp_open[k] || usr_nz[k])
        return 6;                                  /* no BEGIN first (a header over a header: flash ANDs them) */
    if (ed_unpack7(a, na, ed_smp_buf, sizeof(smp_user_hdr_t)) != sizeof(smp_user_hdr_t))
        return 1;
#if CLIPPER_SYNTH
    if (h->magic == WT_USER_MAGIC) {               /* Clipper: a wavetable for WAVE (eng_wave.c), its spectra at +512 */
        const wt_user_hdr_t *w = (const wt_user_hdr_t *)ed_smp_buf;
        if (!wt_user_hdr_ok(w))
            return 2;
        ed_smp_open[k] = 0;
        ed_smp_inval(k);
        if (st_crc32(smp_user_xip(k) + SMP_USER_DATA, w->data_len) != w->crc)
            return 3;
        if (fl_write(ed_smp_slot(k), ed_smp_buf, sizeof(smp_user_hdr_t)))
            return 4;
        ed_smp_inval(k);
        smp_user_scan(k);                          /* (wt_user_scan) */
        return wt_user_ok[k] ? 0 : 5;
    }
#endif
    if (h->magic != SMP_USER_MAGIC || h->version != 1 || !h->nz || h->nz > 16u ||
        h->data_len > SMP_USER_SIZE - SMP_USER_DATA)
        return 2;
    for (i = 0; i < h->nz; i++)                    /* the zones checked before anything is written */
        if (!smp_zone_ok(&h->zone[i], h->data_len))
            return 2;
    ed_smp_open[k] = 0;
    ed_smp_inval(k);
    if (st_crc32(smp_user_xip(k) + SMP_USER_DATA, h->data_len) != h->crc)
        return 3;
    if (fl_write(ed_smp_slot(k), ed_smp_buf, sizeof(smp_user_hdr_t)))
        return 4;
    ed_smp_inval(k);
    smp_user_scan(k);
    return usr_nz[k] ? 0 : 5;
}

#if CLIPPER_SYNTH
/* a write of a matrix destination the part's engine does not use (engines.c mx_dst_live): refused, the
 * value stays as it was (the reply says so) */
UI_SMALL static int ed_dst_refused(const track_t *t, const int16_t *vp, int32_t v)
{
    uint32_t f;
    if (is_drum(t) || vp < t->p || vp >= t->p + P_COUNT)
        return 0;
    return mx_slot_of((uint32_t)(vp - t->p), &f) >= 0 && f == 1u && !mx_dst_ok(t->eng_req, v);
}
#else
#define ed_dst_refused(t, vp, v) 0
#endif
/* the engine byte of DUMP / RELOAD / TRACK: NENGINES = the drum track (no engine) */
UI_SMALL static uint32_t ed_eng(const track_t *t) { return is_drum(t) ? NENGINES : t->eng_req % NENGINES; }

/* ---- live sync (v2): while the editor WATCHes, device-side changes are pushed.
 * Shadows of the selected track's p[] + song.g[] and of its steps are kept in step
 * with what the editor knows (its own SET / STEP_SET update them); the main loop
 * compares and pushes CHANGED / STEP_CHANGED, or RELOAD after a load or when another
 * track was selected (v3: RELOAD and STEP_CHANGED carry the selected track). Pushes
 * go out only into a half-empty SysEx ring, so they never wait. */
#define ED_PUSH_MAX 4u                                   /* frames per pass */
#if CLIPPER_SYNTH
/* v15 (Stallion): scope 3, the FM-1's own settings, by the ids of params.c PAGES (ui_synth.c syn_desc: GLO > TUNING,
 * MIDI, MIDI 2, TOUCH; TOUCH CAL, an action, not) and the menu's (ui_menu.c: LIGHTS, BROWSE, STEP ADVANCE) after them */
enum { ED_S_LIGHTS = 96, ED_S_KEYS, ED_S_NOTES, ED_S_BROWSE, ED_S_ADV, ED_S_PASSIGN, ED_S_PRR };   /* (SEQ >
                                                                   * PARA ASSIGN, PARA ROUND ROBIN) */
static const uint8_t ED_SIDS[] = {X_TSYS, X_TROOT, X_TA4, X_MIN, X_MOUT, X_MBEND, X_MLOCAL, X_MPROG, X_MCCO, X_MBRTH,
                                  X_MCCA, X_MCCB, X_MSUS,
                                  X_KTOUCH, X_ATSENS, X_ATOUT, X_ATCURVE, X_ATOFS, X_ATATK, X_ATREL, X_LEVER, X_LTIME,
                                  X_RIBRNG, X_MREC, X_RSTEPS, X_RSPRING, X_RDELAY, X_RRET,
                                  X_KON, X_KHIT, X_KVEL, X_KRISE, X_KFALL, X_KHOLD, X_KSWELL, X_KCEIL,
                                  X_PDEAD, X_PROOM, X_PTOP, X_PSETL, X_PRISE, X_PFALL, X_PSHAPE, X_KDELAY, X_KSHAPE,
                                  ED_S_LIGHTS, ED_S_KEYS, ED_S_NOTES, ED_S_BROWSE, ED_S_ADV, ED_S_PASSIGN, ED_S_PRR};
#define ED_NSET ((uint32_t)sizeof ED_SIDS)
_Static_assert(X_COUNT <= ED_S_LIGHTS, "scope 3: the menu's ids after the pages'");
static const char *const ED_N_BROWSE[] = {"PRESET", "ALGORITHM"};
static const param_desc_t ED_SD[] = {                    /* (as the menu shows them: mi_value) */
    {"LIGHTS", F_ENUM, 0, LIGHTS_N - 1, LIGHTS_OFF, LIGHTS_NAME, 0},
    {"KEYS", F_ENUM, 0, KEYS_N - 1, KEYS_OFF, KEYS_NAME, 0},
    {"NOTES", F_ONOFF, 0, 1, 0, 0, 0},
    {"BROWSE", F_ENUM, 0, 1, 0, ED_N_BROWSE, 0},
    {"STEP ADV", F_ONOFF, 0, 1, 1, 0, 0},
    {"PARA ASSIGN", F_ENUM, 0, PA_N - 1, PA_PLAYED, PA_NAME, 0},
    {"ROUND ROBIN", F_ONOFF, 0, 1, 0, 0, 0}};
_Static_assert(NELEM(ED_SD) == ED_S_PRR + 1u - ED_S_LIGHTS, "a descriptor a menu setting");
UI_SMALL static int ed_sidx(uint32_t id)                  /* its place in ED_SIDS, -1: not a setting */
{
    uint32_t i;
    for (i = 0; i < ED_NSET; i++)
        if (ED_SIDS[i] == id)
            return (int)i;
    return -1;
}
UI_SMALL static const param_desc_t *ed_sget(uint32_t id, int16_t *v)   /* a setting's descriptor and value, 0: none */
{
    int16_t *vp = 0;
    const param_desc_t *d;
    if (ed_sidx(id) < 0)
        return 0;
    if (id >= ED_S_LIGHTS) {
        *v = (int16_t)(id == ED_S_LIGHTS ? lights_lvl % LIGHTS_N : id == ED_S_KEYS ? lights_keys % KEYS_N
                       : id == ED_S_NOTES ? lights_notes != 0u : id == ED_S_BROWSE ? browse_mode == BR_ALGO
                       : id == ED_S_PASSIGN ? para_assign % PA_N : id == ED_S_PRR ? para_rr != 0u : !step_adv_off);
        return &ED_SD[id - ED_S_LIGHTS];
    }
    d = syn_desc(id, &vp);
    if (!d || !vp)
        return 0;
    *v = *vp;
    return d;
}
UI_SMALL static void ed_sput(uint32_t id, int32_t v)      /* as the knob (syn_changed) or the menu (mi_set) sets it */
{
    int16_t *vp = 0;
    const param_desc_t *d;
    if (ed_sidx(id) < 0)
        return;
    if (id >= ED_S_LIGHTS) {
        d = &ED_SD[id - ED_S_LIGHTS];
        v = clamp(v, d->min, d->max);
        if (id == ED_S_LIGHTS)
            lights_lvl = (uint8_t)v;
        else if (id == ED_S_KEYS)
            lights_keys = (uint8_t)v;
        else if (id == ED_S_NOTES)
            lights_notes = (uint8_t)v;
        else if (id == ED_S_BROWSE)
            browse_mode = v ? BR_ALGO : BR_PRESET;
        else if (id == ED_S_PASSIGN)
            para_assign = (uint8_t)v;
        else if (id == ED_S_PRR)
            para_rr = (uint8_t)v;
        else
            step_adv_off = (uint8_t)!v;
        if (id == ED_S_KEYS && lights_keys && !lights_lvl)
            lights_lvl = LIGHTS_LOW;                     /* keys lit need a level: the lowest (as the menu) */
        settings_later = 1;
        ui.force = 1;
        return;
    }
    d = syn_desc(id, &vp);
    if (!d || !vp)
        return;
    *vp = (int16_t)clamp(v, d->min, d->max);
    syn_changed(id, vp);
    ui.force = 1;
}
#else
#define ED_NSET 0u
#endif
/* the values the pushes follow: the selected track's p[] as the protocol numbers them (core.h p_of_ed), song.g[], then
 * (v15) the device's own p[P_EDH0 ..] and the settings */
#define ED_NV (P_EDN + G_COUNT)
#define ED_NVX (ED_NV + P_EDHN)
#define ED_NVA (ED_NVX + ED_NSET)
/* v4: the parameters of the tracks that are not selected which TRACK_CHANGED follows (the mixer) */
static const uint8_t ED_TIDS[3] = {P_LEVEL, P_PAN, P_MUTE};
#define ED_NT (NTRK * 3u)
static struct {
    uint8_t on, eng, preset, sel;
    uint8_t v4;                                          /* WATCH bit 1: TRACK_CHANGED pushes too */
    uint8_t v15;                                         /* WATCH bit 2: CHANGED of scope 2 (ids 127..) and 3 too */
    uint32_t pos, last_ms, run_ms, resets;
    int16_t v[ED_NVA];                                   /* TSEL->p[] (protocol ids), song.g[], the device's own, settings */
    uint16_t t[ED_NVA];                                  /* ms (low 16 bits) of the last push */
    uint32_t st[NSTEP];                                  /* step signatures */
    int16_t tv[ED_NT];                                   /* v4: trk[k].p[ED_TIDS[j]] at k * 3 + j */
    uint16_t tt[ED_NT];
    uint32_t tpos;
} ed_w;

UI_SMALL static int16_t ed_cur(uint32_t i)                /* value i of ed_w.v, now */
{
    if (i < P_EDN)
        return TSEL->p[p_of_ed(i)];
    if (i < ED_NV)
        return song.g[i - P_EDN];
    if (i < ED_NVX)
        return TSEL->p[P_EDH0 + i - ED_NV];
#if CLIPPER_SYNTH
    {
        int16_t v = 0;
        (void)ed_sget(ED_SIDS[i - ED_NVX], &v);
        return v;
    }
#else
    return 0;
#endif
}
UI_SMALL static uint32_t ed_wi(uint32_t id)                /* the place of the selected track's p[id] in ed_w.v */
{
    return id >= P_EDH0 && id < P_EDH0 + P_EDHN ? ED_NV + id - P_EDH0 : ed_of_p(id);
}
UI_SMALL static uint32_t ed_step_sig(const track_t *t, uint32_t i)   /* all 10 bytes (a drum step's lanes too), v7: + its nudge and locks, v8: + its condition */
{
    const uint8_t *b = (const uint8_t *)&t->step[i % NSTEP];
    uint32_t k, h = 0x811C9DC5u;
    for (k = 0; k < sizeof t->step[0]; k++)
        h = (h ^ b[k]) * 16777619u;
    h = (h ^ (uint8_t)t->micro[i % NSTEP]) * 16777619u;
    h = (h ^ (step_fill(t, i) + 1u)) * 16777619u;        /* v8: its fill condition */
    for (k = 0; k < NLOCK; k++)
        if (t->lock[k].step == i)
            h = (h ^ (uint32_t)(t->lock[k].param | (uint16_t)t->lock[k].val << 8)) * 16777619u;
    return h;
}
/* the drum track's step as a v1..v4 step (old editors): its first 4 lanes as GM notes, ACC when one is hard */
UI_SMALL static void ed_dstep_old(const dstep_t *d, uint32_t *n, uint8_t *notes, uint32_t *time, uint32_t *flags, uint32_t *vel)
{
    uint32_t l, k = 0, hard = 0;
    for (l = 0; l < DRUM_LANES && k < 4u; l++)
        if (dstep_has(d, l)) {
            notes[k++] = LANE_NOTE[l];
            hard |= dstep_lvl(d, l) == LV_HARD;
        }
    *n = k;
    while (k < 4u)
        notes[k++] = 0;
    *time = *n ? ST_NOTE : ST_REST;
    *flags = hard ? SF_ACCENT : 0u;
    *vel = *n ? 100u : 0u;
}
/* a v1..v4 step written into the drum track: its notes onto their lanes */
UI_SMALL static void ed_dstep_from_old(dstep_t *d, const uint8_t *a)
{
    uint32_t i, n = a[0] > 4u ? 4u : a[0];
    memset(d, 0, sizeof *d);
    if (a[5] != ST_NOTE)
        return;
    for (i = 0; i < n; i++)
        dstep_set(d, lane_of_note(a[1 + i] & 0x7Fu), (a[6] & SF_ACCENT) ? LV_HARD : LV_NORM, 0);
}
/* a step's bytes, as STEP_GET sends them (v5: + level, ratchet) */
UI_SMALL static void ed_step_out(const track_t *t, uint32_t i)
{
    uint32_t k;
    if (is_drum(t)) {
        uint32_t n, time, flags, vel;
        uint8_t nt[4];
        ed_dstep_old(&t->dstep[i], &n, nt, &time, &flags, &vel);
        ed_b(n);
        for (k = 0; k < 4u; k++)
            ed_b(nt[k]);
        ed_b(time);
        ed_b(flags);
        ed_b(vel);
        ed_b(0);                                         /* v5: lvl, hi, rat (DRUM_STEP has the lanes' own) */
        ed_b(0);
        ed_b(0);
        return;
    }
    {
        const step_t *st = &t->step[i];
        ed_b(st->n);
        for (k = 0; k < 4u; k++)
            ed_b(st->note[k]);
        ed_b(st->time);
        ed_b(st->flags);
        ed_b(st->vel);
        ed_b(st->lvl);                                   /* v5 (7 bits each: 4 x 2-bit fields, the top 1 below) */
        ed_b(st->lvl >> 7 | (st->rat >> 7) << 1);
        ed_b(st->rat);
    }
}
/* a step written: n, 4 notes, time, flags, vel [, lvl, hi, rat] */
UI_SMALL static void ed_step_in(track_t *t, uint32_t i, const uint8_t *a, uint32_t na)
{
    uint32_t k;
    if (is_drum(t)) {
        ed_dstep_from_old(&t->dstep[i], a);
        return;
    }
    {
        step_t *st = &t->step[i];
        st->n = (uint8_t)(a[0] > 4u ? 4u : a[0]);
        for (k = 0; k < 4u; k++)
            st->note[k] = a[1 + k] & 0x7Fu;
        st->time = (uint8_t)(a[5] > ST_REST ? ST_REST : a[5]);
        st->flags = a[6] & (SF_ACCENT | SF_SLIDE);
        if (a[6] & SF_RX)                                /* (1.0) ratchets past x4: the third bit of each note */
            st->flags |= (uint8_t)(a[6] & (SF_RX | SF_CONT_ALL));
        st->vel = a[7] & 0x7Fu;
        if (na >= 11u) {
            st->lvl = (uint8_t)((a[8] & 0x7Fu) | (a[9] & 1u) << 7);
            st->rat = (uint8_t)((a[10] & 0x7Fu) | ((a[9] >> 1) & 1u) << 7);
        } else {
            st->lvl = st->rat = 0;
        }
    }
}
UI_SMALL static void ed_shadow(void)                              /* the editor is in sync */
{
    uint32_t i;
    for (i = 0; i < ED_NVA; i++)
        ed_w.v[i] = ed_cur(i);
    for (i = 0; i < NSTEP; i++)
        ed_w.st[i] = ed_step_sig(TSEL, i);
    for (i = 0; i < ED_NT; i++)
        ed_w.tv[i] = trk[i / 3u].p[ED_TIDS[i % 3u]];
    ed_w.eng = (uint8_t)ed_eng(TSEL);
    ed_w.preset = TSEL->preset;
    ed_w.sel = song.sel;
    sync_reload = 0;
}
UI_SMALL static int ed_room(void) { return so_w - so_r + 8u <= SXQ / 2u; }
UI_SMALL static void ed_known(uint32_t k, uint32_t id)            /* the editor's own change of trk[k].p[id]: no push */
{
    uint32_t j;
    if (k == song.sel) {
        ed_w.v[ed_wi(id)] = trk[k].p[id];
        return;
    }
    for (j = 0; j < 3u; j++)
        if (ED_TIDS[j] == id)
            ed_w.tv[k * 3u + j] = trk[k].p[id];
}

UI_SMALL static void ed_sync(void)                                /* main loop */
{
    uint32_t i, n = 0, nv, now = fm1_ms;
    if (!ed_w.on || now - ed_w.run_ms < 5u)
        return;
    ed_w.run_ms = now;
    if (!usb.config || usb.resets != ed_w.resets || now - ed_w.last_ms > 3000u) {
        ed_w.on = 0;                                     /* no host, USB reset, or 3 s without a request */
        return;
    }
    if (sync_reload || ed_eng(TSEL) != ed_w.eng || TSEL->preset != ed_w.preset || song.sel != ed_w.sel) {
        if (!ed_room())
            return;
        ed_shadow();
        ed_begin(ED_RELOAD);
        ed_b(ed_eng(TSEL));
        ed_b(TSEL->preset);
        ed_b(song.sel);
        ed_send();
        return;
    }
    for (i = 0; i < NSTEP && n < ED_PUSH_MAX; i++) {
        uint32_t h = ed_step_sig(TSEL, i);
        if (h == ed_w.st[i])
            continue;
        if (!ed_room())
            return;
        ed_w.st[i] = h;
        ed_begin(ED_STEP_CHANGED);
        ed_b(i);
        ed_b(song.sel);
        ed_send();
        n++;
    }
    for (i = 0, nv = ed_w.v15 ? ED_NVA : ED_NV; i < nv && n < ED_PUSH_MAX; i++) {   /* round robin: nothing starves */
        uint32_t k = (ed_w.pos + i) % nv;
        int16_t v = ed_cur(k);
        if (v == ed_w.v[k] || (uint16_t)(now - ed_w.t[k]) < 20u)
            continue;                                    /* coalesced: the latest value goes out later */
        if (!ed_room())
            return;
        ed_w.v[k] = v;
        ed_w.t[k] = (uint16_t)now;
        ed_begin(ED_CHANGED);
        if (k < ED_NV) {
            ed_b(k < P_EDN ? 0u : 1u);
            ed_b(k < P_EDN ? k : k - P_EDN);
        } else if (k < ED_NVX) {                         /* v15: the device's own, scope 2 */
            ed_b(2);
            ed_id(P_EDN + k - ED_NV);
        } else {                                         /* .. a setting, scope 3 */
#if CLIPPER_SYNTH
            ed_b(3);
            ed_b(ED_SIDS[k - ED_NVX]);
#endif
        }
        ed_v(v);
        ed_send();
        n++;
        ed_w.pos = k + 1u;
    }
    for (i = 0; ed_w.v4 && i < ED_NT && n < ED_PUSH_MAX; i++) {   /* v4: the other tracks' mix */
        uint32_t k = (ed_w.tpos + i) % ED_NT, tr = k / 3u, id = ED_TIDS[k % 3u];
        int16_t v = trk[tr].p[id];
        if (tr == song.sel || v == ed_w.tv[k] || (uint16_t)(now - ed_w.tt[k]) < 20u)
            continue;                                    /* (the selected track: CHANGED above) */
        if (!ed_room())
            return;
        ed_w.tv[k] = v;
        ed_w.tt[k] = (uint16_t)now;
        ed_begin(ED_TRACK_CHANGED);
        ed_b(tr);
        ed_b(id);
        ed_v(v);
        ed_send();
        n++;
        ed_w.tpos = k + 1u;
    }
}

/* descriptor of parameter id of track t: the engine parameters of the engine it asked for
 * (t->engine follows in the audio ISR, after a short fade) */
UI_SMALL static const param_desc_t *ed_tdesc(const track_t *t, uint32_t id)   /* the static ones: an engine's */
{                                                                     /* desc hook is the device display only */
    if(is_drum(t) && id==P_E0) return &DRUM_KIT_DESC;
    if (id >= P_E0 && id <= P_E7)
        return &ENGINES[t->eng_req % NENGINES]->edit[id - P_E0];
    return &TP[id];
}
/* descriptor and value slot of (scope, id): scope 0 = the selected track, 1 = global, (v15) 2 = the selected track by
 * a 14-bit id (core.h p_of_ed15); *wi: its place in ed_w.v (the editor's own change: no push) */
UI_SMALL static const param_desc_t *ed_desc(uint32_t scope, uint32_t id, int16_t **vp, uint32_t *wi)
{
    if ((scope == 0 && id < P_EDN) || (scope == 2 && id < P_COUNT)) {
        uint32_t pid = scope ? p_of_ed15(id) : p_of_ed(id);
        *vp = &TSEL->p[pid];
        *wi = ed_wi(pid);
        return ed_tdesc(TSEL, pid);
    }
    if (scope == 1 && id < G_COUNT) {
        *vp = &song.g[id];
        *wi = P_EDN + id;
        return &GP[id];
    }
    return 0;
}
/* v15: DESC's tail (scope 2, 3): n names (an enum's, or an INT's list: the range split over them), then flags (bit 0
 * the descriptor follows the sound's values, bit 1 this value changes others'). Names while the frame has room */
UI_SMALL static void ed_desc15(const param_desc_t *d, uint32_t flags)
{
    uint32_t i, k = 0, at;
    if (d->names && d->fmt == F_ENUM)
        k = (uint32_t)(d->max - d->min) + 1u;
    else if (d->names && (d->fmt == F_INT || d->fmt == F_CHAR))   /* (0-terminated: params.c param_format; CHAR:
                                                         * its bits') */
        while (d->names[k] && k < 64u)
            k++;
    k = k > 64u ? 64u : k;
    at = ed_n;
    ed_b(0);
    for (i = 0; i < k && ed_n + 16u < sizeof ed_out; i++)
        ed_str(d->names[i], 12);
    ed_out[at] = (uint8_t)i;
    ed_b(flags);
}
#if CLIPPER_SYNTH
UI_SMALL static uint32_t ed_dflags(uint32_t pid)        /* (params.c track_desc: what follows what) */
{
    uint32_t hook = pid >= P_E0 && pid <= P_E7 && !is_drum(TSEL) && ENGINES[TSEL->eng_req % NENGINES]->desc;
    return (hook || pid == P_SPICE || pid == P_DICE || (pid >= P_IA && pid <= P_IC) || pid == P_F2CUT ||
            pid == P_FMODEL ? 1u : 0u) |                /* (MODL: PONY's own list) */
           (hook || pid == P_ITYPE || pid == P_AMODE || pid == P_F2LINK ? 2u : 0u);
}
#else
#define ed_dflags(pid) 0u
#endif

/* ---- v6: backup / restore (web/EDITOR_PROTOCOL.md). Objects: 0 the working project, 1 the settings
 * (colours, calibration, the song order, the lights, SYNC), 2..5 the projects A..D, 6..7 the user preset
 * banks, 8 the FM6 patch bank (v9), 32..34 the user sample slots USR1..3 (read only here: restored with
 * SMP_BEGIN / WRITE / END). (Stallion) 15 GLO > TOUCH and TOUCH CAL's ends (project.c touch_store_t, the settings
 * object's tail), listed only to a LIST whose first byte has bit 0 set (an editor that knows it: an older one, which
 * refuses a file with an object it does not know, never sees it).
 * LIST takes a snapshot of the working project and the settings; GET reads 1..256 bytes of an object.
 * PUT stages one object in RAM (begin: id, length, CRC-32; data; commit), checks it as a load would,
 * then writes it through the usual A/B commit: a cut-off restore never leaves half an object. */
#if FELUCCA_FLASH
#define ED_BK_RAW ((uint8_t *)&proj_tmp)                  /* the staging RAM (main loop, as the project loads) */
_Static_assert(sizeof proj_tmp >= sizeof(project_t) && sizeof proj_tmp >= sizeof(up_bank_t) &&
               sizeof proj_tmp >= sizeof(persist_t), "backup staging");
static persist_t ed_bk_set;                             /* LIST's snapshot of the settings */
static touch_store_t ed_bk_touch;                       /* .. and of GLO > TOUCH (Stallion) */
static uint8_t ed_bk_valid, ed_bk_put, ed_bk_id;
static uint32_t ed_bk_len, ed_bk_crc, ed_bk_pos, ed_bk_ms;
UI_SMALL static void ed_bk_u32(uint32_t v) { uint32_t i; for (i = 0; i < 5u; i++) ed_b((v >> (7u * i)) & 127u); }
UI_SMALL static uint32_t ed_bk_r32(const uint8_t *a)
{
    return (uint32_t)a[0] | (uint32_t)a[1] << 7 | (uint32_t)a[2] << 14 | (uint32_t)a[3] << 21 | (uint32_t)a[4] << 28;
}

UI_SMALL static const uint8_t *ed_bk_obj(uint32_t id, uint32_t *len)   /* 0 = no such object; *len 0 = empty */
{
    *len = 0;
    if (id == 0u) {
        *len = sizeof(project_t);
        return ED_BK_RAW;
    }
    if (id == 1u) {
        *len = sizeof ed_bk_set;
        return (const uint8_t *)&ed_bk_set;
    }
    if (id >= 2u && id <= 5u) {
        if (project_used(id - 2u))
            *len = sizeof proj_slot[0];
        return (const uint8_t *)&proj_slot[id - 2u];
    }
    if (id == 6u || id == 7u) {
        if (up_bank[id - 6u].magic == UP_BANK_MAGIC)
            *len = sizeof up_bank[0];
        return (const uint8_t *)&up_bank[id - 6u];
    }
    if (id == 8u) {                                       /* the FM6 patch bank, in flash (XIP) */
        if (fm6_bank_cur >= 0)
            *len = sizeof(fm6_bank_t);
        return (const uint8_t *)fm6_bank_flash(fm6_bank_cur < 0 ? 0u : (uint32_t)fm6_bank_cur);
    }
    if (id == 15u) {                                      /* (Stallion) GLO > TOUCH, TOUCH CAL's ends: LIST's snapshot */
        *len = sizeof ed_bk_touch;
        return (const uint8_t *)&ed_bk_touch;
    }
    if (id == 9u) {                                       /* SLOOP 2.5: the SYN drum kits (drum_synth.c dsu) */
        (void)dsu_kit(0);
        *len = sizeof dsu;
        return (const uint8_t *)&dsu;
    }
#if BOARD_SMK37
    if (id >= 32u && id < 32u + SMP_USER_SLOTS && !SMP_USER_HAS(id - 32u))
        return ED_BK_RAW;                                 /* SMK: a slot without flash, always empty (*len 0) */
#endif
#if CLIPPER_SYNTH
    if (id == 14u) {                                      /* (after Clipper 1.2) the projects' 1.0 blocks (project.c
                                                         * proj_x10_t): the slots A..D and LIST's snapshot of the working
                                                         * project (proj_x10s 0..4), each bound to its project by its sum */
        static proj_side_t bk14[5] __attribute__((section(".pool")));   /* (Stallion: each the 1.0 block, then its own
                                                         * block beside the project, then the matrix's slots 13..24 (preset
                                                         * format 2); Clipper 1.3's: the 1.0 blocks alone) */
        uint32_t i;
        for (i = 0; i < 5u; i++)
            bk14[i].c = proj_x10s[i], bk14[i].s = proj_xss[i], bk14[i].x = proj_mxs[i];
        *len = sizeof bk14;
        return (const uint8_t *)bk14;
    }
#endif
    if (id >= 32u && id < 32u + SMP_USER_SLOTS) {
        const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(id - 32u);
        if (h->magic == SMP_USER_MAGIC && h->version == 1u && h->nz && h->nz <= 16u &&
            h->data_len <= SMP_USER_SIZE - SMP_USER_DATA)
            *len = SMP_USER_DATA + h->data_len;
#if CLIPPER_SYNTH
        else if (wt_user_hdr_ok((const wt_user_hdr_t *)h))   /* a wavetable for WAVE: backed up as a sample is */
            *len = SMP_USER_DATA + WT_USER_LEN;
#endif
        return smp_user_xip(id - 32u);
    }
    return 0;
}
static const uint8_t ED_BK_IDS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
#if CLIPPER_SYNTH
                                     14,
#endif
                                     32, 33, 34, 35};   /* 35: USR4 (2.4), 9: SYN kits (2.5) */
static const uint8_t ED_BK_IDS2[] = {15};               /* (Stallion) after them, to a LIST with bit 0 of its first byte */

/* a flash erase silences the audio for ~50 ms and stalls USB: only while stopped (as the panel) */
UI_SMALL static uint32_t ed_flash_busy(void) { return song.playing || transport_req; }

UI_SMALL static uint32_t ed_bk_commit(void)
{
    uint8_t *raw = ED_BK_RAW;
    uint32_t id = ed_bk_id, n = ed_bk_len;
    if (ed_bk_pos != n || st_crc32(raw, n) != ed_bk_crc)
        return 2;
    if (id == 1u)
        return settings_restore(raw, n);
    if (id == 15u)
        return touch_restore(raw, n);
    if (id <= 5u)
        return project_restore(id == 0u ? 4u : id - 2u, raw, n);
    if (id == 6u || id == 7u) {                           /* a user preset bank (n 0: empty) */
        const up_bank_t *b = (const up_bank_t *)raw;
        if (ed_flash_busy())
            return 3;
#if CLIPPER_SYNTH
        if (n && (n != sizeof *b || !up_bank_ok(b)))     /* (preset format 2: ours whole, magic, version, sizes, CRC; a
                                                         * Clipper / SLOOP backup's bank is not loaded) */
            return 2;
#else
        if (n && (n != sizeof *b || b->magic != UP_BANK_MAGIC || b->rsize != sizeof(up_rec_t) || b->nslot != UP_PER_BANK))
            return 2;
#endif
        if (!flash_ok || st_save(OBJ_UPRESET0 + id - 6u, raw, n))
            return 4;
        memset(&up_bank[id - 6u], 0, sizeof up_bank[0]);
        if (n)
            memcpy(&up_bank[id - 6u], raw, n);
        up_bank_check(id - 6u, (int)n);
        up_gen++;
        return 0;
    }
#if CLIPPER_SYNTH
    if (id == 14u) {                                      /* (after Clipper 1.2) the projects' 1.0 blocks, after 0 and 2..5:
                                                         * each to the project it belongs to (its sum), written with it */
        uint32_t b, i, rc = 0, sz = n == 5u * sizeof(proj_side_t) ? sizeof(proj_side_t) :
                                    n == 5u * PROJ_SIDE_120 ? PROJ_SIDE_120 :
                                    n == 5u * PROJ_XS_OLD ? PROJ_XS_OLD : sizeof(proj_x10_t);
        if (ed_flash_busy())
            return 3;
        if (n != 5u * sizeof(proj_side_t) && n != 5u * PROJ_SIDE_120 && n != 5u * PROJ_XS_OLD && n != 5u * sizeof(proj_x10_t))
            return 2;                                     /* (ours: the three blocks; Stallion's before the slots 13..24 (120
                                                         * bytes: those slots off), before FILTER FM / RING's extension,
                                                         * or Clipper 1.3's 1.0 blocks alone) */
        for (b = 0; b < 5u; b++) {
            proj_side_t x;
            memset(&x, 0, sizeof x);
            memcpy(&x, raw + b * sz, sz);
            if (!proj_x10_valid(&x.c))
                continue;
            if (!proj_xs_valid(&x.s) || x.s.sum != x.c.sum)
                memset(&x.s, 0, sizeof x.s);
            if (!proj_mx_valid(&x.x) || x.x.sum != x.c.sum)
                memset(&x.x, 0, sizeof x.x);
            for (i = 0; i < 4u; i++)
                if (proj_ok(&proj_slot[i]) && proj_slot[i].sum == x.c.sum) {
                    proj_x10s[i] = x.c;
                    proj_xss[i] = x.s;
                    proj_mxs[i] = x.x;
                    if (!flash_ok || proj_st_save(OBJ_PROJECT0 + i, &proj_slot[i]))
                        rc = 4;
                }
            if (proj_restored_sum && x.c.sum == proj_restored_sum) {
                proj_x10_set(x.c.m);                       /* (the working project restored: its sounds' values) */
                if (x.s.magic)
                    proj_xs_set(&x.s);
                if (x.x.magic)
                    proj_mx_set(&x.x);
            }
        }
        proj_restored_sum = 0;
        return rc;
    }
#endif
    if (id == 8u) {                                       /* the FM6 patch bank (n 0: empty) */
        if (ed_flash_busy())
            return 3;
        if (n && (n != sizeof(fm6_bank_t) || !fm6_bank_valid((const fm6_bank_t *)raw)))
            return 2;
        if (!flash_ok || st_save(OBJ_FM6BANK, raw, n))
            return 4;
        fm6_bank_scan();
        fm6_bank_changed();
        return 0;
    }
    if (id == 9u) {                                       /* SLOOP 2.5: the SYN drum kits, written with the settings */
        persist_t p;
        uint32_t k;
        if (ed_flash_busy())
            return 3;
        if (n != sizeof(dsu_bank_t) || !dsu_valid((const dsu_bank_t *)raw))
            return 2;
        memcpy(&dsu, raw, sizeof dsu);
        for (k = 0; k < DSU_N; k++)
            dsu_fix_kit(&dsu.k[k]);
        persist_fill(&p);
        if (!flash_ok || settings_write(&p))
            return 4;
        persist_saved = p;
        return 0;
    }
    return 1;
}

UI_SMALL static int ed_backup(uint32_t cmd, const uint8_t *a, uint32_t na)   /* 1: a backup command (reply built) */
{
    uint32_t i, len, rc;
    const uint8_t *p;
    if (cmd == ED_BK_LIST) {
        uint32_t nx = na >= 1u && (a[0] & 1u) ? (uint32_t)sizeof ED_BK_IDS2 : 0u;   /* (Stallion: an editor that knows 15) */
        rc = !flash_ok ? 4u : 0u;
        if (!rc) {
            proj_capture((project_t *)ED_BK_RAW);           /* the working project, as it is now */
            persist_fill(&ed_bk_set);
            touch_fill(&ed_bk_touch);
            ed_bk_valid = 1;
            ed_bk_put = 0;
        }
        ed_b(rc);
        ed_b(rc ? 0u : (uint32_t)sizeof ED_BK_IDS + nx);
        for (i = 0; !rc && i < sizeof ED_BK_IDS + nx; i++) {
            uint32_t id = i < sizeof ED_BK_IDS ? ED_BK_IDS[i] : ED_BK_IDS2[i - sizeof ED_BK_IDS];
            p = ed_bk_obj(id, &len);
            ed_b(id);
            ed_bk_u32(len);
            ed_bk_u32(st_crc32(p, len));
            fm1_wdt_feed();                               /* (a sample slot: up to 80 KiB through the CRC) */
        }
        return 1;
    }
    if (cmd == ED_BK_GET) {                               /* id, off (5), count (2) -> id, rc, off, count, data */
        uint32_t off = na >= 6u ? ed_bk_r32(a + 1) : 0u, count = na >= 8u ? (uint32_t)a[6] | (uint32_t)a[7] << 7 : 0u;
        p = na >= 1u ? ed_bk_obj(a[0], &len) : 0;
        rc = na != 8u || !p || !count || count > 256u || off > len || count > len - off ? 1u
             : !ed_bk_valid || (a[0] == 0u && ed_bk_put) ? 5u : 0u;   /* 5: LIST first (the snapshot is gone) */
        ed_b(na ? a[0] : 127u);
        ed_b(rc);
        ed_bk_u32(off);
        ed_b(rc ? 0u : count & 127u);
        ed_b(rc ? 0u : count >> 7);
        if (!rc)
            ed_pack7(p + off, count);
        return 1;
    }
    if (cmd == ED_BK_PUT) {                               /* op, id, ... -> op, id, rc */
        uint32_t op = na >= 2u ? a[0] : 99u, id = na >= 2u ? a[1] : 127u;
        rc = 1;
        if (!flash_ok) {
            rc = 4;
        } else if (op == 0u && na == 12u && (id <= 9u || id == 15u || (CLIPPER_SYNTH && id == 14u))) {  /* begin: id, length (5), CRC-32 (5) */
            len = ed_bk_r32(a + 2);
            if (id >= 2u || len) {                        /* (the working project and the settings are never empty) */
                if (len <= sizeof proj_tmp) {
                    ed_bk_put = 1;
                    ed_bk_valid = 0;                      /* (the staging RAM is the snapshot's) */
                    ed_bk_id = (uint8_t)id;
                    ed_bk_len = len;
                    ed_bk_crc = ed_bk_r32(a + 7);
                    ed_bk_pos = 0;
                    ed_bk_ms = fm1_ms;
                    rc = 0;
                }
            }
        } else if (!ed_bk_put || ed_bk_id != id || fm1_ms - ed_bk_ms > 15000u) {
            rc = 5;                                       /* no begin for this object (or too long ago) */
        } else if (op == 1u && na >= 9u && ed_bk_r32(a + 2) == ed_bk_pos) {   /* data: off (5), pack7 */
            uint32_t k = ed_unpack7(a + 7, na - 7u, ed_smp_buf, 256u);
            if (k && k <= ed_bk_len - ed_bk_pos) {
                memcpy(ED_BK_RAW + ed_bk_pos, ed_smp_buf, k);
                ed_bk_pos += k;
                ed_bk_ms = fm1_ms;
                rc = 0;
            }
        } else if (op == 2u && na == 2u) {                /* commit */
            rc = ed_bk_commit();
            ed_bk_put = 0;
            if (!rc) {
                sync_reload = 1;
                ui.force = 1;
            }
        } else if (op == 3u && na == 2u) {                /* abort */
            ed_bk_put = 0;
            rc = 0;
        }
        ed_b(op & 127u);
        ed_b(id & 127u);
        ed_b(rc);
        return 1;
    }
    return 0;
}
#else
UI_SMALL static int ed_backup(uint32_t cmd, const uint8_t *a, uint32_t na)   /* no flash: nothing to back up */
{
    (void)a;
    (void)na;
    if (cmd < ED_BK_LIST || cmd > ED_BK_PUT)
        return 0;
    ed_b(4);
    return 1;
}
#endif

#include "editor_fm6.c"                               /* v9: the FM6 patches (68..71) */
#include "editor_dsyn.c"                              /* v10: the SYN drum kits (72..76) */
#include "editor_hybrid.c"                            /* v11 (Clipper): the HYBRID patches (77, 78) */
#if CLIPPER_SYNTH
#include "editor_fmx.c"                               /* v12 (Clipper 0.6): the FMX patches (90..92) */
#endif

#if FELUCCA_MULTI
/* (Stallion, SMK-37) the multi sounds (multi.c; web/EDITOR_PROTOCOL.md "Multi sounds"). The editor finds them by
 * sending MULTI_LIST: a device without them never answers. The record: 47 bytes as stored (every byte 0..127) */
enum { ED_MULTI_LIST = 100, ED_MULTI_GET, ED_MULTI_PUT, ED_MULTI_LOAD, ED_MULTI_ERASE };
UI_SMALL static void ed_mu_out(const void *p, uint32_t n, uint32_t sp)   /* n bytes as they are (0..127); sp: a 0 as a
                                                                            * space (an empty slot's names) */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        ed_b(((const uint8_t *)p)[i] ? ((const uint8_t *)p)[i] : sp);
}
UI_SMALL static int ed_mu_handle(uint32_t cmd, const uint8_t *a, uint32_t na)   /* 1: a multi command (reply built) */
{
    mu_rec_t r;
    uint32_t k = na >= 1u ? a[0] : 127u, i;
    switch (cmd) {
    case ED_MULTI_LIST:                                    /* - -> count, per slot: used, name 12 */
        ed_b(MU_SLOTS);
        for (k = 0; k < MU_SLOTS; k++) {
            i = (uint32_t)mu_get(k, &r);
            ed_b(i);
            ed_mu_out(r.name, 12, ' ');
        }
        return 1;
    case ED_MULTI_GET:                                     /* slot -> slot, used, record (47) */
        i = (uint32_t)mu_get(k, &r);
        ed_b(k);
        ed_b(i);
        ed_mu_out(&r, sizeof r, 0);
        return 1;
    case ED_MULTI_PUT:                                     /* slot, record (47) -> slot, ok */
        if (na == 1u + sizeof r)
            memcpy(&r, a + 1, sizeof r);
        ed_b(k);
        ed_b(na == 1u + sizeof r && !ed_flash_busy() && !mu_put(k, &r));   /* (mu_put: the slot, the record checked) */
        return 1;
    case ED_MULTI_LOAD: {                                  /* slot -> slot, status, A's name 12, B's name 12 */
        uint32_t st = (uint32_t)mu_apply(k, &r);              /* (a slot past 15: empty, zeros) */
        ed_b(k);
        ed_b(st);
        ed_mu_out(r.a.name, 12, ' ');                      /* (always both, 12 bytes each: spaces when empty) */
        ed_mu_out(r.b.name, 12, ' ');
        return 1;
    }
    case ED_MULTI_ERASE:                                   /* slot -> slot, ok */
        ed_b(k);
        ed_b(!ed_flash_busy() && !mu_put(k, 0));
        return 1;
    }
    return 0;
}
#endif

UI_SMALL static void ed_handle(const uint8_t *f, uint32_t n)   /* f: the bytes between F0 and F7 */
{
    uint32_t cmd = f[3], i;
    const uint8_t *a = f + 4;
    uint32_t na = n - 4u;
    int16_t *vp;
    const param_desc_t *d;
    ed_begin(cmd);
    if (ed_backup(cmd, a, na)) {                           /* v6: backup / restore */
        ed_send();
        return;
    }
    if (ed_fm6_handle(cmd, a, na)) {                       /* v9: FM6 patches */
        ed_send();
        return;
    }
#if !CLIPPER_SYNTH || BOARD_SMK37                                         /* (Clipper: no drum machine; the editor hides DRUM SYNTH) */
    if (ed_dsyn_handle(cmd, a, na)) {                      /* v10: the SYN drum kits */
        ed_send();
        return;
    }
#endif
    if (ed_hy_handle(cmd, a, na)) {                        /* v11: the HYBRID patches */
        ed_send();
        return;
    }
#if FELUCCA_MULTI
    if (ed_mu_handle(cmd, a, na)) {                        /* (Stallion, SMK-37) the multi sounds */
        ed_send();
        return;
    }
#endif
#if CLIPPER_SYNTH
    if (ed_fmx_handle(cmd, a, na) || ed_mseq_handle(cmd, a, na)) {   /* v12: the FMX patches; v13: the MOD SEQ */
        ed_send();
        return;
    }
#endif
    switch (cmd) {
    case ED_INFO:
        ed_str("FELUCCA " FELUCCA_VERSION, 24);
        ed_b(NENGINES);
        ed_b(P_EDN);
        ed_b(G_COUNT);
        ed_b(NSTEP);
        ed_b(P_E0 - P_EDHN);
        for (i = 0; i < NENGINES; i++)
            ed_str(ENGINES[i]->name, 8);
        ed_b(NTRK);                                       /* v3 */
        ed_b(ED_PROTO);                                   /* v5..: the protocol version (9: FM6 patches, 10: SYN kits, 11: HYBRID) */
#if CLIPPER_SYNTH
        ed_id(P_COUNT);                                   /* v15: how many ids scope 2 reaches */
        ed_b(MT_COUNT);                                   /* the matrix destinations each engine uses
                                                           * (engines.c mx_dst_live): the codes' count, then per engine
                                                           * its mask, 7 bits a byte from code 0 up (bit set: used) */
        for (i = 0; i < NENGINES; i++) {
            uint64_t m = mx_dst_live(i);
            uint32_t b;
            for (b = 0; b < MT_COUNT; b += 7u)
                ed_b((uint32_t)(m >> b));
        }
        ed_b(NENG_ORDER);                                 /* (1.0) the display order (engines.c ENG_ORDER): its count,
                                                           * then the engine IDs; older firmware ends before it */
        for (i = 0; i < NENG_ORDER; i++)
            ed_b(ENG_ORDER[i]);
#endif
        break;
    case ED_GET:
    case ED_SET: {
        uint32_t w = a[0] == 2u, id, wi = 0;             /* v15 scope 2: a 14-bit id */
        if (na < 2u + w)
            return;
        id = w ? (uint32_t)a[1] | (uint32_t)a[2] << 7 : a[1];
#if CLIPPER_SYNTH
        if (a[0] == 3u) {                                 /* v15 scope 3: a setting of the FM-1's */
            int16_t v;
            if (!ed_sget(id, &v))
                return;
            if (cmd == ED_SET && na >= 4u) {
                ed_sput(id, ed_rv(a + 2));
                (void)ed_sget(id, &v);
                ed_w.v[ED_NVX + (uint32_t)ed_sidx(id)] = v;   /* the editor's own change: no push */
            }
            ed_b(3);
            ed_b(id);
            ed_v(v);
            break;
        }
#endif
        if (!(d = ed_desc(a[0], id, &vp, &wi)))
            return;
        if (cmd == ED_SET && na >= 4u + w) {
            if (a[0] == 1 && id == G_ENGSEL) {            /* engine change: the safe path */
                set_engine((uint32_t)clamp(ed_rv(a + 2), 0, NENGINES - 1));
            } else if (d->max > d->min) {
                int32_t nv = clamp(ed_rv(a + 2 + w), d->min, d->max);
                if (!ed_dst_refused(TSEL, vp, nv))       /* (no cable the engine does not use) */
                    *vp = (int16_t)nv;
            }
            ui.force = 1;
            ed_w.v[wi] = *vp;                             /* the editor's own change: no push */
        }
        ed_b(a[0]);
        if (w)
            ed_id(id);
        else
            ed_b(id);
        ed_v(*vp);
        break;
    }
    case ED_DUMP:                                          /* [flags] (v15: bit 0, the values 127.. too) */
        for (i = 0; i < ED_NVX; i++)                      /* the editor gets them all here */
            ed_w.v[i] = ed_cur(i);
        ed_b(ed_eng(TSEL));
        ed_b(TSEL->preset);
        for (i = 0; i < P_EDN; i++)
            ed_v(TSEL->p[p_of_ed(i)]);
        for (i = 0; i < G_COUNT; i++)
            ed_v(song.g[i]);
        if (na >= 1u && (a[0] & 1u))
            ed_p15(TSEL->p);
        break;
    case ED_DESC: {
        uint32_t w = a[0] == 2u, id, wi = 0, pid;
        if (na < 2u + w)
            return;
        id = w ? (uint32_t)a[1] | (uint32_t)a[2] << 7 : a[1];
#if CLIPPER_SYNTH
        if (a[0] == 3u) {                                 /* v15 scope 3: a setting, as the FM-1 shows it */
            int16_t v;
            if (!(d = ed_sget(id, &v)))
                return;
        } else
#endif
        if (!(d = ed_desc(a[0], id, &vp, &wi)))
            return;
        pid = w ? p_of_ed15(id) : 0u;
        if (w)
            d = track_desc(TSEL, pid);                    /* v15: the descriptor as the FM-1 shows it now */
        ed_b(a[0]);
        if (w)
            ed_id(id);
        else
            ed_b(id);
        ed_b(d->fmt);
        ed_v(d->min);
        ed_v(d->max);
        ed_v(d->def);
        ed_str(d->label, a[0] >= 2u ? 12u : 8u);
        ed_str(d->unit, 8);
        if (a[0] >= 2u)
            ed_desc15(d, w ? ed_dflags(pid) : 0u);
        else if (d->fmt == F_ENUM && d->names)
            for (i = 0; i <= (uint32_t)(d->max - d->min) && i < 64u; i++)   /* (the 34 drum kits) */
                ed_str(d->names[i], 8);
        break;
    }
    case ED_STEP_GET:
    case ED_STEP_SET:
        if (na < 1u || a[0] >= NSTEP)
            return;
        if (cmd == ED_STEP_SET && na >= 9u) {
            fm1_irq_off();
            ed_step_in(TSEL, a[0], a + 1, na - 1u);
            fm1_irq_on();
            ui.force = 1;
        }
        ed_w.st[a[0]] = ed_step_sig(TSEL, a[0]);
        ed_b(a[0]);
        ed_step_out(TSEL, a[0]);
        break;
    case ED_PRESET: {                                      /* engine, preset[, part: 0 A, 1 B, the layer, as
                                                            * EDIT > LAYER > SOUND] */
        track_t *t = TSEL;
        if (na < 2u || a[0] >= NENGINES || a[0] == ENGI_GONE)   /* (1.0: LOFI's old number, core.h) */
            return;
#if CLIPPER_SYNTH
        if (na >= 3u && a[2] == 1u) {
            t = &trk[1];
            if (a[0] != t->eng_req)
                set_engine_of(t, a[0]);
            apply_preset_to(t, a[1]);
        } else
#endif
        {
            if (a[0] != t->eng_req)
                set_engine(a[0]);
            apply_preset(a[1]);
        }
        ui.force = 1;
        ed_b(ed_eng(t));
        ed_b(t->preset);
        break;
    }
    case ED_PROJECT:                                       /* 0 = load, 1 = save, 2 = query; slot 0..3 */
        if (na < 2u || a[0] > 2u)
            return;
#if FELUCCA_ARRANGER
        if (a[0] < 2u && (song.playing || transport_req)) {
            ed_b(a[0]); ed_b(a[1] & 3u); ed_b(project_used(a[1] & 3u));
            ed_b(1);                                  /* optional status: transport busy */
            break;
        }
#endif
        if (a[0] == 1u)
            project_save(a[1] & 3u);
        else if (a[0] == 0u)
            project_load(a[1] & 3u);
        ed_b(a[0]);
        ed_b(a[1] & 3u);
        ed_b(project_used(a[1] & 3u));
        break;
    case ED_NAMES:                                         /* preset names of an engine */
        if (na < 1u || a[0] >= NENGINES)
            return;
        ed_b(a[0]);
        ed_b(ENGINES[a[0]]->npresets);
        for (i = 0; i < ENGINES[a[0]]->npresets; i++)
            ed_str(ENGINES[a[0]]->presets[i].name, 12);
        for (i = 0; i < 2u; i++)                           /* then the two edit-page titles */
            ed_str(ENGINES[a[0]]->page_title[i], 8);
        break;
    case ED_SMP_BEGIN:                                     /* slot -> slot, rc */
    case ED_SMP_ERASE: {
        uint32_t rc;
        if (na < 1u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return;
        rc = ed_smp_erase(a[0], cmd == ED_SMP_ERASE) ? 1u : 0u;
        ed_smp_open[a[0]] = (uint8_t)(cmd == ED_SMP_BEGIN && !rc);
        ed_b(a[0]);
        ed_b(rc);
        break;
    }
    case ED_SMP_WRITE: {                                   /* slot, off (3 x 7 bit), pack7 data -> slot, off, rc */
        uint32_t off, len, rc = 0, took;
        if (na < 5u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return;
        off = (uint32_t)a[1] | (uint32_t)a[2] << 7 | (uint32_t)a[3] << 14;
        len = ed_unpack7(a + 4, na - 4u, ed_smp_buf, 256u);
        if (off < SMP_USER_DATA || (off & 0xFFu) || !len || off + len > SMP_USER_SIZE)
            rc = 1;
        else if (usr_nz[a[0]] || !ed_smp_open[a[0]] || !SMP_USER_HAS(a[0]))
            rc = 4;                                        /* slot in use: SMP_BEGIN first (voices read it) */
        else {
            if (!(off & 0xFFFu))                           /* first write into a sector: erase it */
                rc = fl_erase4k_quiet(ed_smp_slot(a[0]) + off, &took) ? 2u : 0u;
            if (!rc && fl_write(ed_smp_slot(a[0]) + off, ed_smp_buf, len))
                rc = 3;
        }
        ed_b(a[0]);
        ed_b(off);
        ed_b(off >> 7);
        ed_b(off >> 14);
        ed_b(rc);
        break;
    }
    case ED_SMP_END:                                       /* slot, pack7 header -> slot, rc */
        if (na < 2u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return;
        ed_b(a[0]);
        ed_b((uint32_t)ed_smp_end(a[0], a + 1, na - 1u));
        break;
    case ED_SMP_INFO:                                      /* -> per slot: zones (0 = empty), name, data KiB */
        ed_b(SMP_USER_SLOTS);
        ed_b(SMP_USER_SIZE / 1024u);
        for (i = 0; i < SMP_USER_SLOTS; i++) {
            const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(i);
            char nm[9] = {0};
            uint32_t j;
#if CLIPPER_SYNTH
            uint32_t used = usr_nz[i] || wt_user_ok[i];
            ed_b(wt_user_ok[i] ? 127u : usr_nz[i]);        /* (Clipper: 127 zones = a wavetable for WAVE) */
#else
            uint32_t used = usr_nz[i];
            ed_b(usr_nz[i]);
#endif
            for (j = 0; used && j < 8u; j++)
                nm[j] = h->name[j] >= 32 && h->name[j] < 127 ? h->name[j] : 0;
            ed_str(nm, 8);
            ed_b(used ? (h->data_len + 1023u) / 1024u : 0u);
        }
        break;
    case ED_UP_LIST: {                                     /* start, count -> start, count, total, per slot: used, engine, name */
        uint32_t s0, cnt;
        if (na < 2u)
            return;
        s0 = a[0];
        cnt = a[1] > 16u ? 16u : a[1];
        if (s0 >= UP_SLOTS)
            cnt = 0;
        else if (s0 + cnt > UP_SLOTS)
            cnt = UP_SLOTS - s0;
        ed_b(s0);
        ed_b(cnt);
        ed_b(UP_SLOTS);
        for (i = s0; i < s0 + cnt; i++) {
            char nm[13] = {0};
            uint32_t j, u = (uint32_t)up_used(i);
            for (j = 0; u && j < 12u; j++)
                nm[j] = up_rec(i)->name[j];
            ed_b(u);
            ed_b(u ? up_rec(i)->engine : 0u);
            ed_str(nm, 12);
        }
        break;
    }
    case ED_UP_GET: {                                      /* slot -> slot, used, engine, name, P_COUNT x v14, 16 x (note, flags) */
        int16_t v[P_COUNT];
        char nm[13] = {0};
        const up_rec_t *r;
        uint32_t u;
        if (na < 1u || a[0] >= UP_SLOTS)
            return;
        r = up_rec(a[0]);
        u = (uint32_t)up_used(a[0]);
        if (u)
            up_values(r, v);                               /* today's P_* order, clamped */
        for (i = 0; u && i < 12u; i++)
            nm[i] = r->name[i];
        ed_b(a[0]);
        ed_b(u);
        ed_b(u ? r->engine : 0u);
        ed_str(nm, 12);
        for (i = 0; i < P_EDN; i++)
            ed_v(u ? v[p_of_ed(i)] : 0);
        for (i = 0; i < 16u; i++) {
#if CLIPPER_SYNTH
            ed_b(0u);                                      /* (preset format 2: a synth preset holds the MOD SEQ, no pattern) */
            ed_b(0u);
#else
            ed_b(u ? r->note[i] : 0u);
            ed_b(u ? r->flags[i] : 0u);
#endif
        }
        if (na >= 2u && (a[1] & 1u)) {                     /* v15: the values 127.. (0 for an empty slot) */
            if (!u)
                memset(v, 0, sizeof v);
            ed_p15(v);
        }
        break;
    }
    case ED_UP_PUT: {                                      /* slot, engine, name, values, pattern -> slot, rc */
        static up_rec_t r;
        uint32_t slot = 0, rc;
        if (na < 1u)
            return;
        rc = (uint32_t)up_parse(a, na, &r, &slot);
        if (!rc) {
            int16_t v[P_COUNT];
            up_values(&r, v);                              /* each value inside its range */
#if CLIPPER_SYNTH
            mx_prune_p(v, r.engine);                       /* (no cable its engine does not use) */
            up_rec_set(&r, v);                             /* (0.8: the record's own layout; P_COUNT > its p[]) */
#else
            for (i = 0; i < P_COUNT; i++)
                r.p[i] = v[i];
#endif
            rc = ed_flash_busy() ? 3u : up_put(slot, &r) ? 2u : 0u;
        }
        ed_b(a[0]);
        ed_b(rc);
        break;
    }
    case ED_UP_STORE: {                                    /* slot, name -> slot, rc */
        char nm[13] = {0};
        uint32_t n0, rc = 1;
        if (na < 1u)
            return;
        for (n0 = 0; 1u + n0 < na && a[1 + n0] && n0 < 13u; n0++)
            ;
        if (a[0] < UP_SLOTS && (!n0 || up_name_ok(a + 1, n0))) {   /* "" = automatic name */
            for (i = 0; i < n0; i++)
                nm[i] = (char)a[1 + i];
            int r = ed_flash_busy() ? -3 : up_store(a[0], nm);
            rc = r == -3 ? 3u : r == 1 ? 1u : r ? 2u : 0u;   /* 1: the drum track is selected; 3: stop first */
        }
        ed_b(a[0]);
        ed_b(rc);
        break;
    }
    case ED_UP_LOAD:                                       /* slot -> slot, rc */
        if (na < 1u)
            return;
        ed_b(a[0]);
        ed_b(a[0] < UP_SLOTS && !up_load(a[0]) ? 0u : 1u);
        break;
    case ED_UP_ERASE:
        if (na < 1u)
            return;
        ed_b(a[0]);
        ed_b(a[0] >= UP_SLOTS ? 1u : ed_flash_busy() ? 3u : up_put(a[0], 0) ? 2u : 0u);
        break;
    case ED_WATCH:                                         /* on -> on */
        if (na < 1u)
            return;
        ed_w.on = a[0] & 1u;
        ed_w.v4 = (uint8_t)(ed_w.on && (a[0] & 2u));     /* v4: also TRACK_CHANGED; the reply says it is known */
        ed_w.v15 = (uint8_t)(CLIPPER_SYNTH && ed_w.on && (a[0] & 4u));   /* v15: also scope 2 (127..) and 3 */
        ed_w.resets = usb.resets;
        if (ed_w.on)
            ed_shadow();
        ed_b(ed_w.on | ed_w.v4 << 1 | ed_w.v15 << 2);
        break;
    case ED_PING:
        ed_b(0);
        break;
    case ED_TRACK:                                         /* [track] -> selected, NTRK, per track: engine, preset, level, mute, armed */
        if (na >= 1u && a[0] < NTRK && a[0] != song.sel) {
            track_select(a[0]);
            if (ed_w.on)
                ed_shadow();                               /* the editor re-reads it: no RELOAD for this */
        }
        ed_b(song.sel);
        ed_b(NTRK);
        for (i = 0; i < NTRK; i++) {
            ed_b(ed_eng(&trk[i]));
            ed_b(trk[i].preset);
            ed_v(i == TRK_DRUM ? song.g[G_DRLVL] : trk[i].p[P_LEVEL]);
            ed_b(trk[i].p[P_MUTE] != 0);
            ed_b((song.rec >> i) & 1u);
        }
        ed_b(song.solo & 15u);                            /* v5: the tracks soloed */
        break;
    case ED_TRACK_MIX: {                                   /* track [, level v14, mute] -> track, level, mute */
        track_t *t;
        int16_t *lv;
        if (na < 1u || a[0] >= NTRK)
            return;
        t = &trk[a[0]];
        lv = a[0] == TRK_DRUM ? &song.g[G_DRLVL] : &t->p[P_LEVEL];
        if (na >= 4u) {
            *lv = (int16_t)clamp(ed_rv(a + 1), a[0] == TRK_DRUM ? GP[G_DRLVL].min : 0, a[0] == TRK_DRUM ? GP[G_DRLVL].max : 127);
            t->p[P_MUTE] = (int16_t)(a[3] ? 1 : 0);
            if (a[0] == TRK_DRUM)                          /* the editor's own change: no push */
                ed_w.v[P_EDN + G_DRLVL] = *lv;
            else
                ed_known(a[0], P_LEVEL);
            ed_known(a[0], P_MUTE);
            ui.force = 1;
        }
        ed_b(a[0]);
        ed_v(*lv);
        ed_b(t->p[P_MUTE] != 0);
        break;
    }
    case ED_TRACK_DUMP:                                    /* track [, flags] -> track, engine, preset, P_COUNT x v14 [, v15] */
        if (na < 1u || a[0] >= NTRK)
            return;
        ed_b(a[0]);
        ed_b(ed_eng(&trk[a[0]]));
        ed_b(trk[a[0]].preset);
        for (i = 0; i < P_EDN; i++)
            ed_v(trk[a[0]].p[p_of_ed(i)]);
        if (na >= 2u && (a[1] & 1u))
            ed_p15(trk[a[0]].p);
        break;
    case ED_TRACK_STEP:                                    /* track, index [, step] -> track, index, step (as STEP_GET) */
        if (na < 2u || a[0] >= NTRK || a[1] >= NSTEP)
            return;
        if (na >= 10u) {
            fm1_irq_off();
            ed_step_in(&trk[a[0]], a[1], a + 2, na - 2u);
            fm1_irq_on();
            ui.force = 1;
        }
        if (a[0] == song.sel)
            ed_w.st[a[1]] = ed_step_sig(&trk[a[0]], a[1]);
        ed_b(a[0]);
        ed_b(a[1]);
        ed_step_out(&trk[a[0]], a[1]);
        break;
    case ED_DRUM_STEP: {                                   /* index [, on 16 bits, lvl 32 bits, rat 32 bits as 7-bit groups]
                                                            * -> index, on (3), lvl (5), rat (5): the drum track's 16 lanes */
        dstep_t *d;
        uint32_t on, lv, rt;
        if (na < 1u || a[0] >= NSTEP)
            return;
        d = &TDRUM->dstep[a[0]];
        if (na >= 14u) {
            on = (uint32_t)a[1] | (uint32_t)a[2] << 7 | (uint32_t)(a[3] & 3u) << 14;
            lv = (uint32_t)a[4] | (uint32_t)a[5] << 7 | (uint32_t)a[6] << 14 | (uint32_t)a[7] << 21 | (uint32_t)(a[8] & 15u) << 28;
            rt = (uint32_t)a[9] | (uint32_t)a[10] << 7 | (uint32_t)a[11] << 14 | (uint32_t)a[12] << 21 | (uint32_t)(a[13] & 15u) << 28;
            fm1_irq_off();
            memset(d, 0, sizeof *d);
            for (i = 0; i < DRUM_LANES; i++)
                if ((on >> i) & 1u)
                    dstep_set(d, i, (lv >> (2u * i)) & 3u, (rt >> (2u * i)) & 3u);
            fm1_irq_on();
            ui.force = 1;
            if (song.sel == TRK_DRUM)
                ed_w.st[a[0]] = ed_step_sig(TDRUM, a[0]);
        }
        on = dstep_mask(d);
        lv = (uint32_t)d->lvl[0] | (uint32_t)d->lvl[1] << 8 | (uint32_t)d->lvl[2] << 16 | (uint32_t)d->lvl[3] << 24;
        rt = (uint32_t)d->rat[0] | (uint32_t)d->rat[1] << 8 | (uint32_t)d->rat[2] << 16 | (uint32_t)d->rat[3] << 24;
        ed_b(a[0]);
        ed_b(on);
        ed_b(on >> 7);
        ed_b(on >> 14);
        for (i = 0; i < 5u; i++)
            ed_b(lv >> (7u * i));
        for (i = 0; i < 5u; i++)
            ed_b(rt >> (7u * i));
        break;
    }
    case ED_TRACK_PARAM: {                                 /* track, id [, v14] -> track, id, v14; id 127
                                                            * then a 14-bit id (core.h p_of_ed15): every value of any
                                                            * track, part B's FILTER 2, BEND, matrix 13..24 too */
        track_t *t;
        uint32_t id, x = na >= 4u && a[1] == 127u ? 2u : 0u;
        if (na < 2u || a[0] >= NTRK || (!x && a[1] >= P_EDN))
            return;
        t = &trk[a[0]];
        id = x ? (uint32_t)a[2] | (uint32_t)a[3] << 7 : a[1];
        if (id >= P_EDN + P_EDHN)
            return;
        id = p_of_ed15(id);
        d = ed_tdesc(t, id);
        if (na >= 4u + x) {
            int32_t nv = clamp(ed_rv(a + 2 + x), d->min, d->max);
            if (d->max > d->min && !ed_dst_refused(t, &t->p[id], nv))   /* as SET: clamped; a fixed value stays */
                t->p[id] = (int16_t)nv;
            ed_known(a[0], id);
            ui.force = 1;
        }
        ed_b(a[0]);
        ed_b(a[1]);
        if (x)
            ed_b(a[2]), ed_b(a[3]);
        ed_v(t->p[id]);
        break;
    }
    /* v7 (SLOOP 2.4): the parameter locks and nudges of a track (core.h plock_t / micro, seq.c lock_step) */
    case ED_LOCK_GET: {                                    /* track -> track, n, n x (step, param, v14) */
        const track_t *t;
        uint32_t n = 0;
        if (na < 1u || a[0] >= NTRK)
            return;
        t = &trk[a[0]];
        for (i = 0; i < NLOCK; i++)
            n += t->lock[i].step < NSTEP;
        ed_b(a[0]);
        ed_b(n);
        for (i = 0; i < NLOCK; i++)
            if (t->lock[i].step < NSTEP) {
                ed_b(t->lock[i].step);
                ed_b(ed_of_p(t->lock[i].param));
                ed_v(t->lock[i].val);
            }
        break;
    }
    case ED_LOCK_SET: {                                    /* track, step, param [, v14] (no value: delete)
                                                            * -> track, step, param, rc, has, v14 */
        track_t *t;
        uint32_t rc = 0, has, id = na >= 3u && a[2] < P_EDN ? p_of_ed(a[2]) : P_COUNT;
        int k;
        if (na < 3u || a[0] >= NTRK)
            return;
        t = &trk[a[0]];
        if (a[1] >= NSTEP || id >= P_COUNT)
            rc = 1;
        else if (!p_lockable(id))
            rc = 2;
        else {
            fm1_irq_off();
            if (na >= 5u)
                rc = lock_set(t, a[1], id, ed_rv(a + 3)) ? 0u : 3u;   /* 3: no free slot */
            else
                lock_del(t, a[1], id);
            fm1_irq_on();
            ui.force = 1;
            if (a[0] == song.sel)
                ed_w.st[a[1]] = ed_step_sig(t, a[1]);     /* the editor's own change: no push */
        }
        k = rc == 1u ? -1 : lock_find(t, a[1], id, 0);
        has = k >= 0;
        ed_b(a[0]);
        ed_b(a[1]);
        ed_b(a[2]);
        ed_b(rc);
        ed_b(has);
        ed_v(has ? t->lock[k].val : 0);
        break;
    }
    case ED_MICRO_GET:                                     /* track -> track, NSTEP x (nudge + 64) */
        if (na < 1u || a[0] >= NTRK)
            return;
        ed_b(a[0]);
        for (i = 0; i < NSTEP; i++)
            ed_b((uint32_t)(trk[a[0]].micro[i] + 64));
        break;
    case ED_MICRO_SET: {                                   /* track, step, nudge + 64 -> track, step, nudge + 64 (clamped) */
        track_t *t;
        if (na < 3u || a[0] >= NTRK || a[1] >= NSTEP)
            return;
        t = &trk[a[0]];
        fm1_irq_off();
        t->micro[a[1]] = (int8_t)clamp((int32_t)a[2] - 64, MICRO_MIN, MICRO_MAX);
        fm1_irq_on();
        ui.force = 1;
        if (a[0] == song.sel)
            ed_w.st[a[1]] = ed_step_sig(t, a[1]);
        ed_b(a[0]);
        ed_b(a[1]);
        ed_b((uint32_t)(t->micro[a[1]] + 64));
        break;
    }
    /* v8 (SLOOP 2.4): the steps' fill conditions (core.h FC_*, seq.c step_fill): 2 bits a step, as stored */
    case ED_FILL_GET:                                      /* track -> track, pack7 of the NSTEP / 4 bytes */
        if (na < 1u || a[0] >= NTRK)
            return;
        ed_b(a[0]);
        ed_pack7(trk[a[0]].fill, NSTEP / 4u);
        break;
    case ED_FILL_SET: {                                    /* track, step, cond -> track, step, cond (3 -> 0) */
        track_t *t;
        if (na < 3u || a[0] >= NTRK || a[1] >= NSTEP)
            return;
        t = &trk[a[0]];
        fm1_irq_off();
        step_fill_set(t, a[1], a[2] % 3u);
        fm1_irq_on();
        ui.force = 1;
        if (a[0] == song.sel)
            ed_w.st[a[1]] = ed_step_sig(t, a[1]);
        ed_b(a[0]);
        ed_b(a[1]);
        ed_b(step_fill(t, a[1]));
        break;
    }
    default:
        return;
    }
    ed_send();
}

/* main loop: editor frames first; anything else stays for ota_service() */
UI_SMALL static void ed_service(void)
{
    const uint8_t *p;
    uint32_t n;
    ed_sync();                                             /* v2 pushes (while watched) */
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != ED_HDR0 || p[1] != ED_HDR1 || p[2] != ED_HDR2)
        return;
    ed_w.last_ms = fm1_ms;                                 /* any request keeps WATCH alive */
    if (p[3] >= ED_SMP_BEGIN) {                            /* large frames: handled in place, then freed */
        ed_handle(p, n);
        ota_frame_done();
        return;
    }
    {
        static uint8_t f[64];
        uint32_t k = n > sizeof f ? sizeof f : n, i;
        for (i = 0; i < k; i++)
            f[i] = p[i];
        ota_frame_done();                                  /* free the frame buffer before replying */
        ed_handle(f, k);
    }
}
