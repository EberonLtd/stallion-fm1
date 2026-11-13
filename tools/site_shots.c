/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* The device screens the site and the guide show, from the real UI on the harness of tests/ui_pages_test.c (never a
 * photo, never drawn by hand): each new engine's EDIT page on one of its own factory sounds, HOME with a chord held,
 * the ENV page, a MARE patch on the MATRIX page with six cables, and on the SMK-37 build the KEY AT and PAD AT graphs
 * with the AT PRESSURE bar showing.
 *
 * Needs the matrix patching work (Stallion 1.0: the MATRIX page, MS_GATE, MT_TRIG2, MT_CUTB, MT_FFM). Built and run by
 * tools/make_screens.py from the repo root, once for the FM-1 and once with -DBOARD_SMK37=1:
 *   cc -O2 -w -DCLIPPER_SYNTH=1 -Ibuild/gen -Ifirmware/src -Ifirmware/hal -Itests tools/site_shots.c -lm -lpthread
 *   ./a.out OUT_DIR          writes OUT_DIR/shot-*.ppm */
#define UI_PAGES_HARNESS_ONLY
#include "ui_pages_test.c"

static int page_idx(const char *t)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (!strcmp(PAGES[i].title, t))
            return (int)i;
    return -1;
}
static int go_page(const char *t)
{
    int i = page_idx(t);
    if (i < 0) {
        printf("site_shots: no page %s\n", t);
        return 0;
    }
    open_family(PAGES[i].fam);
    ui.page = (uint8_t)i;
    ui.fam_last[PAGES[i].fam] = (uint8_t)i;
    page_entered();
    ui.force = 1;
    frame();
    return 1;
}
static void redraw(void) { ui.force = 1; frame(); frame(); }
static int sound(track_t *t, uint32_t e, const char *name)    /* the engine and its factory sound of that name */
{
    const engine_t *en = ENGINES[e];
    uint32_t i;
    set_engine_of(t, e);
    for (i = 0; i < en->npresets; i++)
        if (!strcmp(en->presets[i].name, name)) {
            apply_preset_to(t, i);
            t->engine = t->eng_req;
            frames(3);
            return 1;
        }
    printf("site_shots: no sound %s on engine %u\n", name, e);
    apply_preset_to(t, 0);
    t->engine = t->eng_req;
    return 0;
}
static void slot_set(track_t *t, uint32_t k, int s, int d, int a)
{
    t->p[mx_pid(k)] = (int16_t)s;
    t->p[mx_pid(k) + 1u] = (int16_t)d;
    t->p[mx_pid(k) + 2u] = (int16_t)a;
}
static void hold(uint32_t keys) { fm1_in.notes |= keys; frames(8); }
static void let_go(void) { fm1_in.notes = 0; frames(40); }
static int bad;
static void shot(const char *name, int ok)
{
    if (!ok)
        bad = 1;
    ppm(name);
}

int main(int argc, char **argv)
{
    uint32_t i;
    track_t *t = &trk[0];
    outdir = argc > 1 ? argv[1] : "build/host";
    bank_resolve();
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;                         /* the default screen (MONO: white and the four knob colours) */
    palette_set(4);
    host_tracks_init();
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], i ? 1u : 0u); apply_preset_to(&trk[i], 0); trk[i].engine = trk[i].eng_req; }
    clipper_proj_neutral();
    song.g[G_BPM] = 120;
    go_home(); ui.force = 1; frames(4);

#if !SITE_SMK
    /* HOME with a chord held: the sound, its engine, the four knobs, the scope */
    sound(t, ENGI_PALOMINO, "GOLDEN PAD");
    go_home(); frames(2);
    hold(1u << 4 | 1u << 6 | 1u << 8);
    redraw();
    shot("shot-home", ui.home);
    let_go();

    /* each new engine's EDIT 1 (oscillators) and EDIT 2 (filter), on its own sounds */
    sound(t, ENGI_PALOMINO, "XMOD BELL");
    shot("shot-palomino-edit", go_page("EDIT 1"));
    sound(t, ENGI_PALOMINO, "GOLD BRASS");
    shot("shot-palomino-edit2", go_page("EDIT 2"));
    sound(t, ENGI_STAMPEDE, "HERD BRASS");
    shot("shot-stampede-edit", go_page("EDIT 1"));
    sound(t, ENGI_STAMPEDE, "BRASS SWELL");
    shot("shot-stampede-edit2", go_page("EDIT 2"));
    sound(t, ENGI_RODEO, "RODEO BRASS");
    shot("shot-rodeo-edit", go_page("EDIT 1"));
    sound(t, ENGI_RODEO, "BP QUACK");
    shot("shot-rodeo-edit2", go_page("EDIT 2"));
    sound(t, ENGI_MARE, "MARE BASS");
    shot("shot-mare-edit", go_page("EDIT 1"));
    sound(t, ENGI_MARE, "SPACE PAD");
    shot("shot-mare-edit2", go_page("EDIT 2"));
    sound(t, ENGI_MARE, "MARE BELLS");
    t->p[P_MFFM] = 48;                            /* (turned up, as an owner would: the page's two new values) */
    t->p[P_MRING] = 64;
    shot("shot-mare-edit3", go_page("EDIT 3"));

    /* the ENV page of a pad, a chord held so the graph shows where it is */
    sound(t, ENGI_PALOMINO, "GOLDEN PAD");
    hold(1u << 4 | 1u << 8);
    shot("shot-env", go_page("ENV"));
    let_go();

    /* the four engines' HOME screens, each on one of its own sounds, a chord held */
    {
        static const struct { uint32_t e; const char *snd, *name; } H[] = {
            {ENGI_PALOMINO, "GOLDEN PAD", "shot-home-palomino"}, {ENGI_STAMPEDE, "DUSK BRASS", "shot-home-stampede"},
            {ENGI_RODEO, "NEON PAD", "shot-home-rodeo"}, {ENGI_MARE, "SPACE PAD", "shot-home-mare"}};
        for (i = 0; i < 4u; i++) {
            sound(t, H[i].e, H[i].snd);
            go_home(); frames(2);
            hold(1u << 4 | 1u << 6 | 1u << 8);
            redraw();
            shot(H[i].name, ui.home);
            let_go();
        }
    }

    /* FILTER on the OTA model at 48 dB, FILTER 2 linked to it and split left and right */
    sound(t, ENGI_PALOMINO, "PALOMINO PAD");
    t->p[P_TFLT] = -20;                           /* the track filter a little to the low-pass side */
    t->p[P_FMODEL] = FM_OTA;
    t->p[P_FSLOPE] = 2;                           /* 48 dB */
    t->p[P_F2LINK] = 1;
    t->p[P_F2MODEL] = FM_BP;                      /* (FILTER 2: OFF, then FM_*) */
    t->p[P_F2CUT] = 76;                           /* LINK: an offset, 64 = the first's cutoff: +12 */
    t->p[P_F2RES] = 90;
    t->p[P_F2ROUTE] = 2;                          /* SPLIT */
    shot("shot-filter", go_page("FILTER"));
    shot("shot-filter2", go_page("FILTER 2"));

    /* the reverb on PLATE, and the octave-up shimmer on it */
    sound(t, ENGI_PALOMINO, "GOLDEN PAD");
    song.g[G_RTYPE] = 1;                          /* PLATE */
    song.g[G_RSIZE] = 110;
    song.g[G_RDAMP] = 40;
    song.g[G_RSHIM] = 90;
    shot("shot-reverb", go_page("REVERB"));
    shot("shot-reverb2", go_page("REVERB 2"));

    /* the INSERT as a pitch shifter, an octave up with feedback */
    sound(t, ENGI_PALOMINO, "SYNC LEAD");
    t->p[P_ITYPE] = 12;                           /* PITCH */
    t->p[P_IA] = 100;                             /* +12 st */
    t->p[P_IB] = 64;                              /* 0 ct */
    t->p[P_IC] = 40;
    shot("shot-insert", go_page("INSERT"));

    /* KEY TOUCH: GLO > TOUCH, velocity and pressure, pressure out as poly aftertouch; TOUCH 3: lever and ribbon */
    sound(t, ENGI_STAMPEDE, "PRESS BEND");
    touch_set(X_KTOUCH, 2);                       /* V+AT */
    touch_set(X_ATOUT, 2);                        /* POLY */
    shot("shot-touch", go_page("TOUCH"));
    sound(t, ENGI_RODEO, "LASSO LEAD");
    touch_set(X_LEVER, 3);                        /* LATCH */
    touch_set(X_RIBRNG, 12);
    touch_set(X_MREC, 1);
    shot("shot-touch3", go_page("TOUCH 3"));
    /* TOUCH CAL, first prompt */
    go_home(); frames(2);
    touch_cal_open();
    frames(3);
    ppm("shot-touchcal");
    touch_cal_close("");
    frames(10);

    /* MARE on the MATRIX page: six cables, one of each signal colour */
    sound(t, ENGI_MARE, "MARE BASS");
    for (i = 0; i < MX_SLOTS; i++)
        slot_set(t, i, 0, 0, 0);
    slot_set(t, 0, MS_KEY, MT_PIT1, 63);
    slot_set(t, 1, MS_GATE, MT_TRIG2, 63);
    slot_set(t, 2, MS_ENV2, MT_CUT, 40);
    slot_set(t, 3, MS_LFO1, MT_CUTB, -20);
    slot_set(t, 4, MS_MW, MT_FFM, 32);
    slot_set(t, 5, MS_DC, MT_E1 + 6, 30);
    xv[X_MSLOT] = 3;
    shot("shot-matrix", go_page("MATRIX"));
    redraw();
    ppm("shot-matrix");
#else
    /* the SMK-37's KEY AT and PAD AT graphs (SITE_SMK: needs those pages and graphs in the host build, which the
     * tree has only with BOARD_SMK37, whose input layer the host harness does not have) */
    sound(t, ENGI_STAMPEDE, "VIB LEAD");
    pat_cfg = (pat_cfg_t){60, SH_SLOW, SH_EASE, 0, 80, 800, 250, 120, 120, 0};   /* (the SMK-37's defaults, pat_cfg.h) */
    smk_live_id = 1;
    smk_live_vel = 100;
    smk_live_t0 = fm1_ms - 1400u;
    shot("shot-smk-keyat", go_page("KEY AT"));
    smk_live_t0 = fm1_ms - 1400u;
    redraw();
    ppm("shot-smk-keyat");
    smk_live_id = 0;
    shot("shot-smk-padat", go_page("PAD AT"));
    shot("shot-smk-aftertouch", go_page("AFTERTOUCH"));
#endif
    printf("site_shots: %s\n", bad ? "FAILED (a page or a sound is missing)" : "done");
    return bad;
}
