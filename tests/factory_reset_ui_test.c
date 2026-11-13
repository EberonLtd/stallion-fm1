/* SPDX-License-Identifier: GPL-3.0-only */
/* MENU > SYS > FACTORY RESET on the real firmware (tests/editor_host.c: storage.c, upreset.c, project.c, fm6_bank.c on a
 * RAM image of the FM-1's NOR, the real menu), CLIPPER_SYNTH=1:
 *   - the confirmation: OCT+ on the row asks (the question on its own screen); one press erases nothing; the question
 *     left alone goes after a few seconds; any other button, a HOME tap, a key or pad, a knob, OCT- or an OCT+ too soon
 *     after the first (a double tap) cancels; a second OCT+ after a moment, within the time, erases and restarts
 *   - before the first erase the audio and USB are stopped (FRESET_QUIET, felucca.c: as the UBOOT paths)
 *   - what it erases: every user object (projects, user presets, the working sound / project, the FM6 bank) read as
 *     empty; the settings replaced by a record with the HARDWARE CALIBRATION panel table only; every other byte of the
 *     flash (the app, the update staging, the user samples) as it was
 *   - a sector the flash refuses: ERASE FAILED - TRY AGAIN, no restart, the audio and USB back on (FRESET_RESUME,
 *     felucca.c; usb.c usb_reattach: usb_retry starts it again) so the keyboard plays on, and nothing saved
 *     (settings, autosave, the sections) over the erased sectors until a retry, which then erases and restarts
 *   - the hold on the saves survives a warm restart (the watchdog, a crash, an update: storage.c st_hold_word in
 *     .noinit, as the projects): a new process boots the failed image with that RAM, flushes everything, and the
 *     flash is unchanged, the row still says ERASE FAILED. Without the hold (the check can fail) the same boot
 *     writes the RAM projects back over the erased sectors. A power cycle (power-on RAM) clears it
 *   - SMK-37 (built with -DBOARD_SMK37=1: tests/hostsim.c takes the board's smk_input.h): the keys past the 27 note
 *     keys (keybed 0..4, 32..36) and the pads as DRUM KIT / STEPS come through seq.c ext_keys_block (ext_hit): a
 *     press cancels, a release alone does not; a pad as a note key cancels as a key does
 *   - the restart: a new process (statics as at power-on) boots the image with the RAM the reset left (.noinit
 *     survives a warm restart), and with power-on RAM (a power cut right after), and ends in exactly the state a fresh
 *     install boots into once HARDWARE CALIBRATION has taught it the same table (main.c: panel_setup, settings_save):
 *     settings, side record, GLO > TOUCH, the SYN kits, the panel table, the projects, the presets, the FM6 bank, the
 *     sound playing. Booting the image before the reset does not (the check can fail).
 *   - the restart is not a failed boot: FACTORY RESET twice within 30 s of a boot still boots normally (no USB RESCUE)
 *   factory_reset_ui_test DIR      (DIR: the images and the screens) */
#include <stdint.h>
static uint32_t freset_restarts;                      /* the restart the device makes (fm1_reboot): counted here */
static uint32_t freset_quiet, freset_quiet_erases;    /* the audio and USB stopped (felucca.c): how often, the erases then */
static uint32_t freset_resume, freset_resume_erases;  /* .. and on again (felucca.c FRESET_RESUME): how often, the erases then */
static uint8_t host_audio_on = 1;                     /* (fm1_audio_stop / fm1_audio_resume: the DMA's run bit) */
#define FRESET_REBOOT() (freset_restarts++)
/* (usb_detach without its register write: detached, down) */
#define FRESET_QUIET() (freset_quiet++, freset_quiet_erases = fl_erases, host_audio_on = 0, usb.detached = 1, usb.up = 0)
#define FRESET_RESUME() (freset_resume++, freset_resume_erases = fl_erases, host_audio_on = 1, usb_reattach())
#include "editor_host.c"
#include <unistd.h>

static int bad;
static void check(const char *what, int ok) { printf("factory reset ui: %-92s %s\n", what, ok ? "ok" : "FAIL"); bad |= !ok; }

/* the RAM a warm restart keeps (.noinit) */
typedef struct {
    project_t proj[4];
    typeof(settings) set;
    panel_t pan;
    uint32_t hold;                                    /* storage.c st_hold_word (.noinit.freset) */
} noinit_t;
static void noinit_take(noinit_t *r)
{
    memcpy(r->proj, proj_slot, sizeof r->proj); memcpy(&r->set, &settings, sizeof r->set); r->pan = panel;
    r->hold = st_hold_word;
}

static void save_file(const char *dir, const char *name, const void *p, size_t n)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (!f || fwrite(p, 1, n, f) != n) { printf("cannot write %s\n", path); exit(2); }
    fclose(f);
}
static void load_file(const char *dir, const char *name, void *p, size_t n)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (!f || fread(p, 1, n, f) != n) { printf("cannot read %s\n", path); exit(2); }
    fclose(f);
}
static void ppm(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(screen[i]);
        uint8_t px[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63u) << 2), (uint8_t)((c & 31u) << 3)};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

/* ---- the child: a power-on (as main.c: persist_boot, settings_init, panel_init, felucca_init) from IMG + RAM, then
 * the state it ends in, written to OUT */
typedef struct {
    persist_t per;
    side2_t side;
    touch_store_t touch;
    dsu_bank_t dsu;
    uint8_t up[sizeof up_bank];
    project_t proj[4];
    int32_t fm6_cur;
    uint32_t fm6_used, autosave_len, palette, lowcut, zoom, cpu1, kspeed, browse, mpe;
    panel_t pan;
    int16_t p[NPART][P_COUNT];
    uint8_t eng[NPART];
    int16_t g[G_COUNT];
    uint32_t held, row_failed;                        /* the saves held (project.c freset_held), the row says ERASE FAILED */
} state_t;
/* warm (0: none): after the boot, every save tried (the sections, a flush, the settings, the autosave), the flash then
 * written to DIR/warm */
static int child(const char *dir, const char *img, const char *ram, const char *out, const char *warm)
{
    static state_t s;
    noinit_t r;
    uint32_t i;
    load_file(dir, img, nor, sizeof nor);
    load_file(dir, ram, &r, sizeof r);
    memcpy(proj_slot, r.proj, sizeof proj_slot);
    memcpy(&settings, &r.set, sizeof settings);
    panel = r.pan;
    st_hold_word = r.hold;
    persist_boot();
    settings_init();
    panel_init();
    for (i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;
    fm6_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        if (i < NPART) { set_engine_of(t, TRK_DEF[i][0]); apply_preset_to(t, TRK_DEF[i][1]); t->engine = t->eng_req; }
        track_defaults_steps(t);
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    autosave_resume();
    memset(&s, 0, sizeof s);
    persist_fill(&s.per);
    side_fill(&s.side);
    touch_fill(&s.touch);
    s.dsu = dsu;
    memcpy(s.up, up_bank, sizeof s.up);
    memcpy(s.proj, proj_slot, sizeof s.proj);
    for (i = 0; i < 4u; i++)
        if (!proj_ok(&s.proj[i])) memset(&s.proj[i], 0, sizeof s.proj[i]);   /* (garbage that is not a project: none) */
    s.fm6_cur = fm6_bank_cur;
    s.fm6_used = fm6_bank_used;
    s.autosave_len = (uint32_t)st_load(OBJ_AUTOSAVE, &proj_tmp, sizeof proj_tmp);
    s.palette = settings.palette; s.lowcut = settings.lowcut; s.zoom = settings.zoom;
    s.cpu1 = cpu1_off; s.kspeed = knob_speed; s.browse = browse_mode; s.mpe = mpe_mode;
    s.pan = panel;
    for (i = 0; i < NPART; i++) { memcpy(s.p[i], trk[i].p, sizeof s.p[i]); s.eng[i] = trk[i].eng_req; }
    memcpy(s.g, song.g, sizeof s.g);
    s.held = (uint32_t)freset_held();
    if (warm) {                                       /* then everything that saves, as the main loop and the menu would */
        uint16_t c;
        s.row_failed = !strcmp(mi_value(MI_FRESET, &c), "ERASE FAILED");
        sec_dirty |= 15u;
        sections_write();
        persist_flush_now();
        settings.lowcut ^= 1u; settings_save();
        proj_capture(&autosave_buf); proj_st_save(OBJ_AUTOSAVE, &autosave_buf);
        save_file(dir, warm, nor, sizeof nor);        /* the flash after it */
    }
    save_file(dir, out, &s, sizeof s);
    return 0;
}
static void boot_in_child(const char *self, const char *dir, const char *img, const char *ram, const char *out, state_t *s)
{
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "'%s' --boot '%s' %s %s %s", self, dir, img, ram, out);
    if (system(cmd) != 0) { printf("child boot failed: %s\n", cmd); exit(2); }
    load_file(dir, out, s, sizeof *s);
}
static void warm_in_child(const char *self, const char *dir, const char *img, const char *ram, const char *out,
                          const char *after, state_t *s)
{
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "'%s' --warm '%s' %s %s %s %s", self, dir, img, ram, out, after);
    if (system(cmd) != 0) { printf("child boot failed: %s\n", cmd); exit(2); }
    load_file(dir, out, s, sizeof *s);
}

static void press(uint32_t b) { edges_btn = 1u << panel.btn[b]; ui_frame(); }
static void frames(uint32_t ms) { uint32_t t = fm1_ms + ms; while ((int32_t)(fm1_ms - t) < 0) ui_frame(); }
static void open_reset_row(void) { ui.menu = 1; ui.menu_sel = MI_FRESET; ui.force = 1; ui_frame(); }
static int store_full(void)
{
    uint32_t o, n = 0;
    st_hdr_t h;
    for (o = 0; o < OBJ_COUNT; o++) n += st_current(o, &h) >= 0;
    return n == OBJ_COUNT;
}
static int store_empty(void)                          /* every object but the settings: no copy valid */
{
    uint32_t o, n = 0;
    st_hdr_t h;
    for (o = 0; o < OBJ_COUNT; o++) n += o == OBJ_SETTINGS || (st_head(o, 0, &h) != 0 && st_head(o, 1, &h) != 0);
    return n == OBJ_COUNT;
}
static int settings_panel_only(const panel_t *want)  /* the settings: one record, the panel table, no SYN kits / tail */
{
    persist_t p;
    st_hdr_t h;
    uint32_t t = 0;
    int c = st_current(OBJ_SETTINGS, &h);
    if (c < 0 || st_head(OBJ_SETTINGS, (uint32_t)c ^ 1u, &h) == 0)   /* (the other copy erased) */
        return 0;
    if (st_load(OBJ_SETTINGS, &p, sizeof p + 64u) != (int)sizeof p)
        return 0;
    st_tail_load(OBJ_SETTINGS, &t, sizeof t);
    return p.magic == PERSIST_MAGIC && !memcmp(&p.panel, want, sizeof p.panel) && t != TOUCH_STORE_MAGIC;
}
static uint32_t proj_objs(const uint8_t *img)       /* the projects with a valid copy in a flash image */
{
    static uint8_t keep[sizeof nor];
    uint32_t o, n = 0;
    st_hdr_t h;
    memcpy(keep, nor, sizeof nor);
    memcpy(nor, img, sizeof nor);
    for (o = 0; o < 4u; o++) n += st_current(OBJ_PROJECT0 + o, &h) >= 0;
    memcpy(nor, keep, sizeof nor);
    return n;
}
static void home_tap(void)
{
    uint32_t bit = 1u << panel.btn[B_HOME];
    fm1_in.buttons |= bit; edges_btn = bit; ui_frame(); ui_frame();
    fm1_in.buttons &= ~bit; ui_frame();
}
static void note_on(void) { fm1_in.notes = 1u << 5; ui_frame(); fm1_in.notes = 0; ui_frame(); }

int main(int argc, char **argv)
{
    static uint8_t fresh[sizeof nor], full[sizeof nor];
    static state_t s_fresh, s_reset, s_full, s_cal, s_cold;
    noinit_t r;
    const char *dir = argc > 1 ? argv[1] : ".";
    uint32_t i, e0, k, other_same = 1;
    panel_t learned;
    uint8_t pk[FM6_PACKED];
    if (argc == 6 && !strcmp(argv[1], "--boot"))
        return child(argv[2], argv[3], argv[4], argv[5], 0);
    if (argc == 7 && !strcmp(argv[1], "--warm"))
        return child(argv[2], argv[3], argv[4], argv[5], argv[6]);
    harness_init();
    for (i = 0; i < SMP_USER_SLOTS; i++)            /* the user samples: some bytes in each slot (kept by the reset) */
        for (k = 0; k < 4096u; k++)
            nor[SMP_USER_OFF(i) + k] = (uint8_t)(i * 31u + k * 7u + 1u);
    memcpy(fresh, nor, sizeof nor);                  /* a fresh install: the store never written */
    memset(&r, 0xA5, sizeof r);                      /* .. and the RAM at power-on: garbage */
    save_file(dir, "fresh.nor", fresh, sizeof fresh);
    save_file(dir, "fresh.ram", &r, sizeof r);
    /* a fresh install, then HARDWARE CALIBRATION (OCT- + OCT+ at power-on, main.c: panel_setup, then settings_save at
     * the defaults): a learned table, one knob the other way round */
    panel.dir[EN_K1] = -1;
    learned = panel;
    settings_save();
    save_file(dir, "cal.nor", nor, sizeof nor);

    /* everything a user can save, saved */
    settings.palette = 1; settings.lowcut = 1; settings.zoom = 1; palette_set(1);
    cpu1_off = 1; lights_lvl = 5; lights_keys = 2; knob_speed = 3; browse_mode = BR_ALGO; mpe_mode = 2;
    usb_rate = 2; clk_out = 1; step_adv_off = 1;
    kat_cfg.on = (uint8_t)!kat_cfg.on;
    settings_save();
    trk[0].p[P_DIST] = (int16_t)(TP[P_DIST].def == TP[P_DIST].max ? TP[P_DIST].min : TP[P_DIST].max);
    for (i = 0; i < 4u; i++) project_save(i);
    up_store(0, "MINE");
    up_store(UP_PER_BANK, "MINE TOO");
#if FELUCCA_MULTI
    trk[1].p[P_LYMODE] = LY_LAYER;                    /* (Stallion, SMK-37) a multi sound (multi.c), SAVE > MULTI */
    mu_ui(2, 0);
    trk[1].p[P_LYMODE] = LY_OFF;
#endif
    for (i = 0; i < FM6_PACKED; i++) pk[i] = (uint8_t)(i & 63u);
    fm6_bank_put(0, pk);
    proj_capture(&autosave_buf);
    proj_st_save(OBJ_AUTOSAVE, &autosave_buf);
    check("before: every user object in flash (settings, 4 projects, 2 preset banks, autosave, FM6 bank; SMK-37: the multi sounds)", store_full());
    memcpy(full, nor, sizeof nor);
    noinit_take(&r);
    save_file(dir, "full.nor", full, sizeof full);
    save_file(dir, "full.ram", &r, sizeof r);

    /* the confirmation */
    e0 = fl_erases;
    open_reset_row();
    ppm(dir, "freset-row.ppm");
    press(B_OCTUP);
    check("OCT+ on FACTORY RESET: the question, nothing erased, no restart",
          ui.menu == MENU_FRESET && fl_erases == e0 && !freset_restarts && store_full());
    draw_menu();
    ppm(dir, "freset-ask.ppm");
    frames(FR_WAIT_MS + 200u);
    check("the question left alone: gone after its time (back to the menu), nothing erased",
          ui.menu == 1 && fl_erases == e0 && !freset_restarts && store_full());
    press(B_OCTUP); frames(1000); press(B_FX);
    check("another button: cancels", ui.menu == 1 && fl_erases == e0 && !freset_restarts);
    press(B_OCTUP); frames(1000); home_tap();
    check("a HOME tap: cancels (the menu stays open)", ui.menu == 1 && fl_erases == e0 && !freset_restarts);
    press(B_OCTUP); frames(1000); note_on();
    check("a key or pad played: cancels", ui.menu == 1 && fl_erases == e0 && !freset_restarts);
    press(B_OCTUP); frames(1000); encs[panel.enc[EN_K2]] = 1; ui_frame();
    check("a knob turned: cancels", ui.menu == 1 && fl_erases == e0 && !freset_restarts);
    press(B_OCTUP); frames(1000); press(B_OCTDN);
    check("OCT-: cancels (the menu stays open)", ui.menu == 1 && fl_erases == e0 && !freset_restarts);
    press(B_OCTUP); ui_frame(); press(B_OCTUP);
    check("OCT+ twice in a quick double tap: cancels, nothing erased", ui.menu == 1 && fl_erases == e0 && !freset_restarts);
    press(B_OCTUP); frames(FR_WAIT_MS + 200u); press(B_OCTUP);
    check("OCT+ after the time ran out: no erase (it asks again)", fl_erases == e0 && !freset_restarts && store_full());
    ui.menu = 1; ui.force = 1; ui_frame();
    check("still everything in flash after all that", store_full() && !memcmp(nor, full, sizeof nor) && !freset_quiet);
#if BOARD_SMK37
    {   /* SMK-37: the keys past the FM-1's 27 and the pads as DRUM KIT / STEPS come as outer-key events (smk_input.h
         * smk_extq, seq.c ext_keys_block: ext_hit), not as note keys */
        static const uint8_t KEY[3] = {32u, 36u, 0u};    /* (the first past the 27, the top C, the bottom C) */
        static const char *const KN[3] = {"SMK-37: keybed key 32 (the first past the 27 note keys): cancels",
                                          "SMK-37: keybed key 36 (the top C): cancels",
                                          "SMK-37: keybed key 0 (the bottom C, below the 27): cancels"};
        for (k = 0; k < 3u; k++) {
            press(B_OCTUP); frames(1000);
            smk__key_event(KEY[k], 100u); ui_frame(); smk__key_event(KEY[k], 0u); ui_frame();
            check(KN[k], ui.menu == 1 && fl_erases == e0 && !freset_restarts && smk_extq_r == smk_extq_w);
        }
        smk__key_event(33u, 100u); ui_frame();             /* (held before the question: its release comes during it) */
        press(B_OCTUP); frames(1000); smk__key_event(33u, 0u); ui_frame();
        check("SMK-37: (the check can fail) a key past the 27 released during the question: still asking",
              ui.menu == MENU_FRESET && fl_erases == e0);
        fr_cancel(); ui_frame();
        smk_pad_drums = 1;
        press(B_OCTUP); frames(1000); smk__padq(3u, 90u); ui_frame(); smk__padq(3u, 0u); ui_frame();
        check("SMK-37: a pad hit as DRUM KIT (no note key): cancels", ui.menu == 1 && fl_erases == e0 && !freset_restarts);
        smk_pad_drums = 0; smk_pad_steps = 1;
        press(B_OCTUP); frames(1000); smk__padq(12u, 90u); ui_frame(); smk__padq(12u, 0u); ui_frame();
        check("SMK-37: a pad hit as STEPS: cancels", ui.menu == 1 && fl_erases == e0 && !freset_restarts);
        smk_pad_steps = 0;
        press(B_OCTUP); frames(1000); smk__padq(9u, 90u); ui_frame(); smk__padq(9u, 0u); ui_frame();
        check("SMK-37: a pad hit as a note key (the default): cancels", ui.menu == 1 && fl_erases == e0 && !freset_restarts);
        check("SMK-37: still everything in flash after the keys and pads", store_full() && !memcmp(nor, full, sizeof nor));
    }
#endif

    {   /* a sector the flash refuses */
        uint32_t e1 = fl_erases, e2, p2;
        fl_erase_refuse = st_sector(OBJ_PROJECT0 + 2, 1);
        press(B_OCTUP); frames(1000); press(B_OCTUP);
        check("the flash refuses a sector: no restart, the screen says ERASE FAILED - TRY AGAIN",
              !freset_restarts && ui.menu == MENU_FRESET && fr_failed && fl_erases > e1);
        check("  .. the audio and USB stopped before the first erase", freset_quiet == 1u && freset_quiet_erases == e1);
        check("  .. and on again after the last (the keyboard plays on; USB not held off: usb_retry attaches it again)",
              freset_resume == 1u && freset_resume_erases == fl_erases && host_audio_on && !usb.detached);
        check("  .. the saves held (the row says ERASE FAILED)", freset_held() && st_held());
        draw_menu();
        ppm(dir, "freset-fail.ppm");
        noinit_take(&r);                              /* (a warm restart now: this flash, this RAM) */
        save_file(dir, "failed.nor", nor, sizeof nor);
        save_file(dir, "failed.ram", &r, sizeof r);
        e2 = fl_erases; p2 = fl_progs;
        settings.lowcut = 0; settings_save();         /* (a setting changed: would be saved) */
        sec_dirty = 15u; sections_write();            /* (the sections in RAM: would be written back) */
        persist_flush_now();                          /* (UPDATE MODE's flush) */
        check("  .. nothing saved over the erased sectors (settings, sections, autosave, a flush) until a retry",
              fl_erases == e2 && fl_progs == p2 && proj_st_save(OBJ_AUTOSAVE, &autosave_buf) != 0);
        menu_close(); trk[0].p[P_DIST]++; frames(AUTOSAVE_IDLE + AUTOSAVE_GAP + 3000u);
        check("  .. nor by the autosave, the menu closed and the sound changed", fl_erases == e2 && fl_progs == p2);
        trk[0].p[P_DIST]--;
        open_reset_row();
        press(B_FX);
        check("  .. back on the list, a key leaves the message", ui.menu == 1 && !freset_restarts);
        fl_erase_refuse = 0xFFFFFFFFu;
    }
    e0 = fl_erases;
    press(B_OCTUP); frames(1000); press(B_OCTUP);
    check("the retry: OCT+, then OCT+ again a second later: erased, and the restart", freset_restarts == 1u && fl_erases > e0);
    check("  .. the audio and USB stopped before its first erase", freset_quiet == 2u && freset_quiet_erases == e0);
    check("  .. not on again (it restarts), the saves no longer held", freset_resume == 1u && !host_audio_on && !st_held());

    {   /* a warm restart after ERASE FAILED (the watchdog, a crash, an update): the hold and the projects both kept */
        static uint8_t failed[sizeof nor], after[sizeof nor];
        static state_t s_warm, s_lost, s_off;
        noinit_t f;
        uint32_t n_ram = 0;
        load_file(dir, "failed.nor", failed, sizeof failed);
        load_file(dir, "failed.ram", &f, sizeof f);
        for (i = 0; i < 4u; i++) n_ram += proj_ok(&f.proj[i]);
        warm_in_child(argv[0], dir, "failed.nor", "failed.ram", "warm.state", "warm-after.nor", &s_warm);
        load_file(dir, "warm-after.nor", after, sizeof after);
        check("ERASE FAILED, then a warm restart: the RAM projects valid, the hold kept (the row says ERASE FAILED)",
              n_ram == 4u && f.hold == ST_HOLD_MAGIC && s_warm.held == 1u && s_warm.row_failed == 1u);
        check("  .. the boot and every save after it (sections, flush, settings, autosave): the flash unchanged",
              !memcmp(after, failed, sizeof after) && proj_objs(failed) < 4u);
        f.hold = 0;                                   /* (the check can fail: the hold lost, the projects kept) */
        save_file(dir, "lost.ram", &f, sizeof f);
        warm_in_child(argv[0], dir, "failed.nor", "lost.ram", "lost.state", "lost-after.nor", &s_lost);
        load_file(dir, "lost-after.nor", after, sizeof after);
        check("  .. (the check can fail) without the hold the same boot writes the RAM projects back",
              s_lost.held == 0u && memcmp(after, failed, sizeof after) != 0 && proj_objs(after) == 4u);
        warm_in_child(argv[0], dir, "failed.nor", "fresh.ram", "off.state", "off-after.nor", &s_off);
        check("  .. a switch off and on (power-on RAM): the hold gone, saves again", s_off.held == 0u && s_off.row_failed == 0u);
    }

    /* what it erased */
    check("every user object but the settings reads empty (projects, presets, autosave, FM6 bank), both copies", store_empty());
    check("the settings: one record with the learned panel table only (HARDWARE CALIBRATION kept), the old copy erased",
          settings_panel_only(&learned));
    for (i = 0; i < sizeof nor; i++) {
        uint32_t s = i & ~0xFFFu, o, c, user = 0;
        for (o = 0; o < OBJ_COUNT; o++) for (c = 0; c < 2u; c++) user |= st_sector(o, c) == s;
        if (!user && nor[i] != full[i]) { other_same = 0; break; }
    }
    check("every other byte of the flash as it was (app area, staging, the user samples)", other_same && !fl_bad);
    noinit_take(&r);
    save_file(dir, "reset.nor", nor, sizeof nor);
    save_file(dir, "reset.ram", &r, sizeof r);

    /* the restart, against a fresh install */
    boot_in_child(argv[0], dir, "fresh.nor", "fresh.ram", "fresh.state", &s_fresh);
    boot_in_child(argv[0], dir, "reset.nor", "reset.ram", "reset.state", &s_reset);
    boot_in_child(argv[0], dir, "full.nor", "full.ram", "full.state", &s_full);
    boot_in_child(argv[0], dir, "cal.nor", "fresh.ram", "cal.state", &s_cal);
    boot_in_child(argv[0], dir, "reset.nor", "fresh.ram", "cold.state", &s_cold);
    check("(the check can fail: booting the image before the reset is not a fresh install)",
          memcmp(&s_full, &s_cal, sizeof s_full) != 0 && s_full.cpu1 == 1u);
    check("after the reset and a restart: 2ND CORE ON, MONO, KNOB SPEED, BROWSE, MPE at their defaults",
          s_reset.cpu1 == 0u && s_reset.palette == 4u && s_reset.kspeed == s_fresh.kspeed && s_reset.browse == s_fresh.browse &&
          s_reset.mpe == s_fresh.mpe);
    check("  .. the settings word (LIGHTS, KEYS ..), the side record, GLO > TOUCH / KEY AT, the SYN kits as a fresh install",
          !memcmp(&s_reset.per, &s_cal.per, sizeof s_reset.per) && !memcmp(&s_reset.side, &s_fresh.side, sizeof s_reset.side) &&
          !memcmp(&s_reset.touch, &s_fresh.touch, sizeof s_reset.touch) && !memcmp(&s_reset.dsu, &s_fresh.dsu, sizeof s_reset.dsu));
    check("  .. the panel table: the learned one (HARDWARE CALIBRATION kept), not the default",
          !memcmp(&s_reset.pan, &learned, sizeof s_reset.pan) && memcmp(&s_reset.pan, &PANEL_DEFAULT, sizeof s_reset.pan));
    check("  .. no project, no user preset, no FM6 bank, no autosave",
          !memcmp(s_reset.proj, s_fresh.proj, sizeof s_reset.proj) && !memcmp(s_reset.up, s_fresh.up, sizeof s_reset.up) &&
          s_reset.fm6_cur == -1 && s_reset.autosave_len == 0xFFFFFFFFu);
    check("  .. the default sound playing (every part's engine and parameters), the global values",
          !memcmp(s_reset.p, s_fresh.p, sizeof s_reset.p) && !memcmp(s_reset.eng, s_fresh.eng, sizeof s_reset.eng) &&
          !memcmp(s_reset.g, s_fresh.g, sizeof s_reset.g));
    check("the whole state after the restart = a fresh install's after the same HARDWARE CALIBRATION, byte for byte",
          !memcmp(&s_reset, &s_cal, sizeof s_reset));
    check("  .. and after a power cut instead (power-on RAM): the same, the panel table from the flash",
          !memcmp(&s_cold, &s_cal, sizeof s_cold));

    {   /* the restart is an intentional one (main.c: UBOOT, UPDATE MODE): not counted as a failed boot */
        uint32_t b1, b2, b3, n0 = freset_restarts;
        bootguard.magic = 0;                           /* (a power-on: garbage) */
        b1 = bootguard_begin(&bootguard);              /* the boot; the main loop clears it only after 30 s */
        fm1_ms += 3000u;
        open_reset_row(); press(B_OCTUP); frames(1000); press(B_OCTUP);
        b2 = bootguard_begin(&bootguard);              /* the restart, 4 s into the boot */
        fm1_ms += 3000u;
        open_reset_row(); press(B_OCTUP); frames(1000); press(B_OCTUP);
        b3 = bootguard_begin(&bootguard);
        check("FACTORY RESET twice within 30 s of boot: each restart a normal boot, no STALLION USB RESCUE",
              freset_restarts == n0 + 2u && b1 == BOOT_NORMAL && b2 == BOOT_NORMAL && b3 == BOOT_NORMAL &&
              bootguard.failed == 0u);
    }
    printf("%s\n", bad ? "FACTORY RESET UI TEST FAILED" : "factory reset ui test passed");
    return bad;
}
