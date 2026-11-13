/* SPDX-License-Identifier: GPL-3.0-only */
/* Stallion: GLO > TOUCH end to end (specs/touch), on the harness of tests/editor_host.c (the real UI, the audio between
 * frames, storage.c / project.c on a RAM image of the flash):
 *   R1, R29   KEY TOUCH OFF (the default): every key plays at velocity 100, on the voices and on MIDI out, as before;
 *   R2, R10   VEL, the scan measuring (kt_live): a measured key plays at its landing's velocity, on the voices and on
 *             MIDI out; a top-row key at 100;
 *   R8        VEL+AT: a key's pressure is the poly aftertouch of the voices of its own note only (a three-note chord);
 *   R10       AT OUT POLY / CHAN: the keys' pressure on MIDI out, one message a key every 10 ms at most, on change only;
 *   R14, R15  MIDI poly and channel aftertouch and the keys' pressure through the AT shaping (HARD, OFFSET, ATTACK); at
 *             the defaults every MIDI pressure value reaches the voice as it came;
 *   R9        TOUCH CAL's screen: 3 gentle and 3 firm presses of each key, OCT+, the medians kept in the settings
 *             flash (a power-off and back: the same ends); KEY TOUCH turned to VEL; OCT- cancels;
 *   R28       every GLO > TOUCH setting (through the pages' values) saved and back after a power-off; settings saved
 *             by a firmware without them (no tail, a damaged tail) load with the defaults;
 *   R16-R18   the LEVER: with a key down OCT- / OCT+ bend (no octave), both held the latest; LEVER OFF, or no key: the
 *             octave as before (LEVER SPRNG); LATCH (the default) and HOLD keep the bend with the key let go (no key: OCT
 *             the octave, the bend kept), LATCH's other button back to 0, HOLD's through 0; a project load: 0; LEVER's
 *             four values saved and back, a record saved before the mode: LATCH (LEVER ON) or OFF;
 *   R21-R23   the RIBBON: with a key down SELECT moves it (not the tempo); the spring (a key down, and let go); HOLD;
 *   R20       MULTI: the keys' part only;  R27 the bar over the header and gone after;
 *   R33       no key, under a held EDIT / SEQ / SEL / GLO, in the menu: OCT and SELECT as before, the lever and ribbon at rest;
 *   R32       PRESS BEND: a chord, one key leaned on: its note +2 semitones (5 cents), the others within 1 cent;
 *   R34       pitch bend and CC 16 on MIDI out, 10 ms apart at least, also with LOCAL OFF;  R29 nothing touched: nothing;
 *   the pages: GLO > TOUCH .. TOUCH 4 show the 16 settings, each name within the column. */
#include "editor_host.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("touch: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static void midi_in(uint32_t st, uint32_t d1, uint32_t d2)    /* a USB-MIDI packet, as usb.c queues it */
{
    midi_in_q[mi_w % MQ] = (st >> 4) | st << 8 | d1 << 16 | d2 << 24;
    mi_w++;
}
static uint32_t mo[256], mo_t[256], mo_n;              /* MIDI out, as taken from the ring, and when */
static uint32_t mo_ms;                                 /* (the clock while the block ran) */
static void mo_take(void)
{
    while (mo_r != mo_w) {
        if (mo_n < 256u)
            mo[mo_n] = midi_out_q[mo_r % MQ], mo_t[mo_n] = mo_ms;
        mo_n++;
        mo_r++;
    }
}
static uint32_t clk_us;
static void blocks_ms(uint32_t ms)                      /* ms of audio, the clock with it (a block: 725.6 us) */
{
    uint32_t i, n = (ms * 44100u + 16000u) / 32000u;
    for (i = 0; i < n; i++) {
        mo_ms = fm1_ms;
        audio_blocks(1);
        for (clk_us += 726u; clk_us >= 1000u; clk_us -= 1000u)
            fm1_ms++;
        mo_take();
    }
}
static voice_t *voice_of(uint32_t p, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (trk[p].v[i].active && trk[p].v[i].gate && trk[p].v[i].note == note)
            return &trk[p].v[i];
    return 0;
}
/* the scan's samples of a landing of key k at driven reading force (key_touch.c), as the tick takes them */
static void land(uint32_t k, uint32_t force)
{
    static const uint16_t fall[] = {700, 300, 190, 190, 190, 190};
    uint32_t i;
    for (i = 0; i < 6u; i++) {
        kt_sample(k, fall[i], 0);
        if (kt_want_drive(k))
            kt_sample(k, force, 1);
    }
}
static void unland(uint32_t k)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        kt_sample(k, i ? 1023u : 650u, 0);
}
static void key_down_up(uint32_t k, int down)
{
    if (down)
        fm1_in.notes |= 1u << k;
    else
        fm1_in.notes &= ~(1u << k);
}
static void all_up(void)
{
    uint32_t k;
    fm1_in.notes = 0;
    for (k = 0; k < KT_NKEY; k++)
        unland(k);
    blocks_ms(40);
}
/* the knob value of GLO > TOUCH item id set, as a knob turn leaves it (ui_input.c: syn_changed) */
static void set_item(uint32_t id, int32_t v)
{
    int16_t *vp;
    (void)syn_desc(id, &vp);
    *vp = (int16_t)v;
    syn_changed(id, vp);
}
static int32_t get_item(uint32_t id)
{
    int16_t *vp;
    (void)syn_desc(id, &vp);
    return *vp;
}
static void power_cycle(void)                           /* RAM gone, the settings from the flash */
{
    memset(&touch_cfg, 0x5A, sizeof touch_cfg);
    memset(&touch_ext, 0x5A, sizeof touch_ext);
    memset(kt_key, 0, sizeof kt_key);
    kt_calibrated = 0;
    persist_boot();
}


/* ---- Stallion: the LEVER and the RIBBON on the real UI (R16-R18, R20-R24, R27, R29, R33, R34) */
static uint32_t BTN(uint32_t b) { return 1u << panel.btn[b]; }
static void frames(uint32_t n)                         /* UI frames, 16 ms each, MIDI out taken */
{
    while (n--) {                                  /* (as ui_frame, the MIDI out's times a block at a time) */
        blocks_ms(16);
        ed_service();
        ui_input(); ui_leds(); ui_draw();
        autosave_tick();
        sections_flush();
    }
}
static void press(uint32_t bits) { fm1_in.buttons |= bits; edges_btn |= bits; }
static void release(uint32_t bits) { fm1_in.buttons &= ~bits; }
static void sel_turn(int32_t s) { encs[panel.enc[EN_SELECT]] += s * panel.dir[EN_SELECT]; }
static uint32_t mo_count(uint32_t st, int cc16)
{
    uint32_t k, n = 0;
    for (k = 0; k < (mo_n < 256u ? mo_n : 256u); k++)
        if ((mo[k] >> 8 & 0xF0u) == st && (!cc16 || (mo[k] >> 16 & 0x7Fu) == 16u))
            n++;
    return n;
}
static int32_t mo_last_pb(void)
{
    uint32_t k;
    int32_t v = 99999;
    for (k = 0; k < (mo_n < 256u ? mo_n : 256u); k++)
        if ((mo[k] >> 8 & 0xF0u) == 0xE0u)
            v = (int32_t)((mo[k] >> 16 & 0x7Fu) | (mo[k] >> 24 & 0x7Fu) << 7) - 8192;
    return v;
}
static int lr_rest(void)
{
    uint32_t p, ok = 1;
    for (p = 0; p < NPART; p++)
        ok &= part_ctl[p].lever == 0 && part_ctl[p].ribbon == 0;
    return ok && tctl.lever == 0 && tctl.ribbon == 0;
}
static void lever_ribbon(void)
{
    uint32_t ok;
    int32_t bpm, oct;
    go_home();
    ui.menu = 0;
    song.octave = 0;
    fm1_in.buttons = 0;
    set_item(X_LEVER, 1 + LV_SPRNG);               /* (the checks up to the modes' are SPRNG's: LATCH is the default) */
    set_item(X_RSPRING, 1);                        /* (the ribbon's too: RIB SPRING SPRNG; LATCH is the default) */
    frames(4);
    /* R33: no key: OCT- / OCT+ the octave, both together 0; SELECT the tempo on HOME (as before) */
    mo_n = 0;
    press(BTN(B_OCTUP));
    frames(2);
    release(BTN(B_OCTUP));
    frames(2);
    ok = song.octave == 1;
    press(BTN(B_OCTDN));
    frames(1);
    press(BTN(B_OCTUP));
    frames(2);
    release(BTN(B_OCTDN) | BTN(B_OCTUP));
    frames(2);
    ok &= song.octave == 0;
    bpm = song.g[G_BPM];
    sel_turn(1);
    frames(2);
    check("R33: no key down: OCT+ the octave, both OCT 0, SELECT the tempo; lever and ribbon at rest, nothing sent",
          ok && song.g[G_BPM] > bpm && lr_rest() && !mo_count(0xE0u, 0) && !mo_count(0xB0u, 1));
    song.g[G_BPM] = (int16_t)bpm;
    /* R16, R17: a key down: OCT+ the lever, the octave left alone */
    key_down_up(10, 1);
    frames(2);
    oct = song.octave;
    mo_n = 0;
    press(BTN(B_OCTUP));
    frames(4);                                     /* (64 ms) */
    ok = part_ctl[0].lever > 0 && part_ctl[0].lever < 32767;
    frames(6);
    check("R16: a key down: OCT+ bends up (the lever on the keys' part), the octave unchanged",
          ok && part_ctl[0].lever == 32767 && song.octave == oct && !ui.menu);
    press(BTN(B_OCTDN));                           /* R18: both: the most recent */
    frames(20);
    ok = part_ctl[0].lever == -32768;
    release(BTN(B_OCTDN));
    frames(20);
    ok &= part_ctl[0].lever == 32767;
    release(BTN(B_OCTUP));
    key_down_up(10, 0);                            /* (the key let go first: the lever still returns) */
    frames(14);
    check("R18: both held: OCT- (the latest) down; let go, OCT+ up again; let go: back to rest", ok && lr_rest() &&
          song.octave == oct);
    check("R34: the lever on MIDI out as pitch bend: 8191, -8192 at full, 0 at rest; 10 ms apart at least",
          mo_count(0xE0u, 0) > 10u && mo_last_pb() == 0);
    {
        uint32_t k, gap_ok = 1, lastt = 0, seen = 0, up = 0, dn = 0;
        for (k = 0; k < (mo_n < 256u ? mo_n : 256u); k++)
            if ((mo[k] >> 8 & 0xF0u) == 0xE0u) {
                int32_t v = (int32_t)((mo[k] >> 16 & 0x7Fu) | (mo[k] >> 24 & 0x7Fu) << 7) - 8192;
                if (seen && mo_t[k] - lastt < 10u)
                    gap_ok = 0;
                up |= v == 8191, dn |= v == -8192;
                lastt = mo_t[k], seen = 1;
            }
        check("     .. (the values and the gaps)", gap_ok && up && dn);
    }
    /* R16: LEVER OFF: the octave as before, a key down or not */
    set_item(X_LEVER, 0);
    key_down_up(10, 1);
    frames(2);
    press(BTN(B_OCTUP));
    frames(4);
    release(BTN(B_OCTUP));
    frames(2);
    check("R16: LEVER OFF: OCT+ with a key down shifts the octave as before", song.octave == oct + 1 && lr_rest());
    song.octave = (int8_t)oct;
    set_item(X_LEVER, 1 + LV_SPRNG);
    /* R33: under a held layer button (EDIT, SEQ, SEL, GLO) the keys select: no lever, no ribbon; SELECT the tempo */
    {
        static const uint8_t LB[4] = {LY_ERASE, LY_STEP, LY_SCALE, LY_MIX};
        uint32_t l;
        ok = 1;
        for (l = 0; l < 4u; l++) {
            key_down_up(10, 0);
            frames(2);
            press(ly_bit[LB[l]]);
            frames(2);
            key_down_up(12, 1);
            frames(2);
            bpm = song.g[G_BPM];
            press(BTN(B_OCTUP));
            sel_turn(2);
            frames(3);
            ok &= lr_rest() && song.g[G_BPM] != bpm;
            release(BTN(B_OCTUP));
            frames(1);
            key_down_up(12, 0);
            release(ly_bit[LB[l]]);
            frames(4);
            song.g[G_BPM] = (int16_t)bpm;
            ok &= lr_rest();
        }
        check("R33: EDIT, SEQ, SEL, GLO held with a key: no lever, no ribbon; SELECT the tempo under them", ok);
        go_home();
        ui.menu = 0;
        frames(2);
    }
    /* R33: the menu: OCT its back / OK, never the lever */
    key_down_up(10, 1);
    frames(2);
    ui.menu = 1;
    ui.menu_sel = 0;
    frames(1);
    press(BTN(B_OCTUP));
    frames(3);
    release(BTN(B_OCTUP));
    frames(2);
    ok = tctl.lv_want == 0 && lr_rest();
    menu_close();
    go_home();
    frames(2);
    check("R33: in the menu a key down leaves OCT to the menu (no lever)", ok && !ui.menu);
    /* R33: live recording armed: the lever and the ribbon (MOTION REC OFF here: motion_test checks what the lanes record;
     * a lane recorded here would play the lever's bend under the later checks); REC again: off */
    set_item(X_MREC, 0);
    key_down_up(10, 0);
    frames(2);
    press(BTN(B_REC));
    frames(1);
    release(BTN(B_REC));
    frames(2);
    ok = rec_wait || song.rec;
    key_down_up(10, 1);
    frames(2);
    bpm = song.g[G_BPM];
    press(BTN(B_OCTUP));
    sel_turn(1);
    frames(10);
    ok &= part_ctl[0].lever == 32767 && part_ctl[0].ribbon > 0 && song.g[G_BPM] == bpm;
    release(BTN(B_OCTUP));
    key_down_up(10, 0);
    frames(60);
    press(BTN(B_REC));
    frames(1);
    release(BTN(B_REC));
    frames(2);
    check("R33: live recording armed, a key down: OCT+ the lever, SELECT the ribbon (not the REC screen's tempo)", ok &&
          lr_rest() && !rec_wait && !song.rec);
    set_item(X_MREC, 1);
    /* R33: the NOTES page: OCT- / OCT+ its own (the sheet), never the lever */
    {
        uint32_t pg;
        for (pg = 0; pg < NPAGES && !(PAGES[pg].scope == SC_STEP); pg++)
            ;
        ui.home = 0;
        ui.page = (uint8_t)pg;
        song.seq_mode = 1;
        page_entered();
        frames(2);
        ok = on_notes_page();
        key_down_up(10, 1);
        frames(2);
        press(BTN(B_OCTDN));
        sel_turn(1);
        frames(4);
        ok &= tctl.lv_want == 0 && lr_rest();
        release(BTN(B_OCTDN));
        key_down_up(10, 0);
        frames(2);
        ui.sheet = 0;
        song.seq_mode = 0;
        go_home();
        frames(2);
        check("R33: the NOTES page with a key down: OCT and SELECT its own, no lever, no ribbon", ok && pg < NPAGES);
    }
    key_down_up(10, 1);
    frames(2);
    /* R21, R27: SELECT with a key down: the ribbon, not the tempo; the bar */
    bpm = song.g[G_BPM];
    mo_n = 0;
    sel_turn(12);
    frames(3);
    {
        int32_t x = 120 + tctl.ribbon * 114 / 32768;
        ok = part_ctl[0].ribbon == 16384 && song.g[G_BPM] == bpm && screen[10 * 240 + x] == 0xFFFFu &&
             screen[10 * 240 + 60] != 0xFFFFu;
        check("R21: a key down: SELECT the ribbon (12 detents of 24: half way up), the tempo left alone", part_ctl[0].ribbon == 16384 &&
              song.g[G_BPM] == bpm);
        check("R27: the bar over the header, its marker at the ribbon", ok);
    }
    frames(60);                                    /* (RIB DELAY 150 ms, then back: 0 by ~0.85 s) */
    check("R22: RIB SPRING: back to 0 (a key still down)", lr_rest());
    frames(40);
    check("R27: the bar gone 500 ms after rest", screen[10 * 240 + 177] != 0xFFFFu && !tb_on);
    check("R34: the ribbon on MIDI out as CC 16, 64 at the end", mo_count(0xB0u, 1) > 3u &&
          (mo[(mo_n - 1u) % 256u] >> 24) == 64u);
    /* R20: MULTI: the layer's part keeps its own */
    {
        int16_t was = trk[1].p[P_LYMODE];
        trk[1].p[P_LYMODE] = LY_MULTI;
        sel_turn(6);
        press(BTN(B_OCTDN));
        frames(6);
        ok = part_ctl[0].ribbon > 0 && part_ctl[0].lever < 0 && part_ctl[1].ribbon == 0 && part_ctl[1].lever == 0;
        release(BTN(B_OCTDN));
        trk[1].p[P_LYMODE] = LY_LAYER;
        frames(2);
        ok &= part_ctl[0].ribbon > 0 && part_ctl[1].ribbon == 0;   /* (LAYER, SPLIT: part 1's controls, which they follow) */
        trk[1].p[P_LYMODE] = was;
        check("R20: MULTI: the lever and the ribbon on the keys' part only; the other channel's part keeps its own", ok);
    }
    key_down_up(10, 0);                            /* R22: the last note key let go: back at once */
    frames(1);
    ok = part_ctl[0].ribbon < 32768 / 4;
    frames(60);
    check("R22: the last note key let go: the ribbon back at once", ok && lr_rest());
    /* R23: HOLD */
    set_item(X_RSPRING, 0);
    key_down_up(10, 1);
    frames(2);
    sel_turn(-3);
    frames(3);
    key_down_up(10, 0);
    frames(40);
    ok = part_ctl[0].ribbon == -4096;
    key_down_up(10, 1);
    frames(2);
    sel_turn(5);                                   /* (across 0: stops there) */
    frames(3);
    ok &= part_ctl[0].ribbon == 0;
    key_down_up(10, 0);
    frames(2);
    set_item(X_RSPRING, 1);
    check("R23: RIB SPRING OFF (HOLD): stays with the key let go; a turn across 0 stops at 0", ok);
    /* R34 with LOCAL OFF: out on MIDI, the voices left alone */
    midi_local_off = 1;
    key_down_up(10, 1);
    frames(2);
    mo_n = 0;
    press(BTN(B_OCTUP));
    frames(10);
    ok = mo_last_pb() == 8191 && part_ctl[0].lever == 0 && tctl.lever == 32767;
    release(BTN(B_OCTUP));
    key_down_up(10, 0);
    frames(14);
    midi_local_off = 0;
    check("R34: LOCAL OFF: the lever still goes out on MIDI (the voices left alone, as the keys' notes)", ok && lr_rest() &&
          mo_last_pb() == 0);
    /* R29: a key played and the knobs turned, no OCT, no SELECT: nothing of this moves */
    mo_n = 0;
    key_down_up(10, 1);
    frames(10);
    key_down_up(10, 0);
    frames(10);
    check("R29: a key played, OCT and SELECT left alone: the lever and the ribbon at 0, nothing sent for them", lr_rest() &&
          !mo_count(0xE0u, 0) && !mo_count(0xB0u, 1) && !tb_on);
    /* LEVER LATCH (the default): stays where let go, with the key let go too; no key: OCT the octave, the bend kept; a
     * key and the other button (held on): back to 0, stopped there */
    {
        int32_t lv;
        uint32_t ok2;
        set_item(X_LEVER, 1 + LV_LATCH);
        go_home();
        frames(2);
        key_down_up(10, 1);
        frames(2);
        mo_n = 0;
        press(BTN(B_OCTDN));
        frames(4);
        release(BTN(B_OCTDN));
        frames(2);
        lv = part_ctl[0].lever;
        ok = lv < 0 && lv > -32768 && tctl.lever == lv;
        key_down_up(10, 0);
        frames(60);                                /* (1 s) */
        ok &= part_ctl[0].lever == lv && tctl.lever == lv && mo_last_pb() == lv / 4;
        check("LATCH: a key down, OCT- a moment: part way down; let go, the key let go: it stays (voices, MIDI out)", ok);
        oct = song.octave = 0;
        press(BTN(B_OCTUP));
        frames(2);
        release(BTN(B_OCTUP));
        frames(2);
        ok2 = song.octave == oct + 1 && part_ctl[0].lever == lv;
        song.octave = (int8_t)oct;
        key_down_up(10, 1);
        frames(2);
        press(BTN(B_OCTUP));
        frames(30);                                /* (held on: 480 ms) */
        ok = part_ctl[0].lever == 0 && tctl.lever == 0 && song.octave == oct;
        release(BTN(B_OCTUP));
        key_down_up(10, 0);
        frames(20);
        check("     no key: OCT+ the octave, the bend kept; a key, OCT+ held: back to 0 and it stays there",
              ok2 && ok && lr_rest() && mo_last_pb() == 0);
        /* HOLD: OCT+ part way, kept with no key; OCT- held: through 0 to full down, kept; a project load: 0 */
        set_item(X_LEVER, 1 + LV_HOLD);
        key_down_up(10, 1);
        frames(2);
        press(BTN(B_OCTUP));
        frames(3);
        release(BTN(B_OCTUP));
        key_down_up(10, 0);
        frames(30);
        lv = part_ctl[0].lever;
        ok = lv > 0 && lv < 32767 && tctl.lever == lv;
        key_down_up(10, 1);
        frames(2);
        press(BTN(B_OCTDN));
        frames(20);
        release(BTN(B_OCTDN));
        key_down_up(10, 0);
        frames(30);
        ok &= part_ctl[0].lever == -32768 && mo_last_pb() == -8192;
        check("HOLD: OCT+ a moment: part way up, kept with no key; OCT- held: through 0 to full down, kept", ok);
        part_ctl_reset();                          /* (a project load / NEW) */
        frames(2);
        check("     a project load: the lever back to 0 (voices, MIDI out)", lr_rest() && mo_last_pb() == 0);
        set_item(X_LEVER, 1 + LV_LATCH);
    }
    /* RIB SPRING LATCH: SELECT down with a key: stays, the key let go too; a detent back up: back to 0 (voices, CC 16) */
    {
        int32_t r;
        set_item(X_RSPRING, 2);
        key_down_up(10, 1);
        frames(2);
        sel_turn(-6);
        frames(10);
        key_down_up(10, 0);
        frames(200);                               /* (past RIB DELAY, the key let go) */
        r = part_ctl[0].ribbon;
        ok = r == -8192 && tctl.ribbon == r;
        key_down_up(10, 1);
        frames(2);
        sel_turn(1);
        frames(200);
        ok &= part_ctl[0].ribbon == 0;
        key_down_up(10, 0);
        frames(60);
        check("RIB SPRING LATCH: SELECT down 6 with a key: stays with it let go; a detent back up: back to 0",
              ok && lr_rest());
        set_item(X_RSPRING, 1);
    }
}


/* R32: PRESS BEND (STAMPEDE): POLY AT -> PITCH, +2 semitones at 127; a held three-note chord, one key leaned on to full */
static uint32_t ph_step(uint32_t note)                 /* channel I's phase step over a block (as bend_layer_test) */
{
    voice_t *v;
    uint32_t a;
    audio_blocks(1);
    v = voice_of(0, note);
    if (!v)
        return 1;
    a = v->ph[0];
    audio_blocks(1);
    return v->ph[0] - a;
}
static double cents(uint32_t d, uint32_t d0) { return 1200.0 * log2((double)d / (double)d0); }
static void press_bend(void)
{
    static const uint8_t K3[3] = {0, 4, 7};
    uint32_t i, pi, s0[3], s1[3];
    const engine_t *e = ENGINES[ENGI_STAMPEDE];
    char w[200];
    for (pi = 0; pi < e->npresets && strcmp(e->presets[pi].name, "PRESS BEND"); pi++)
        ;
    set_engine_of(&trk[0], ENGI_STAMPEDE);
    apply_preset_to(&trk[0], pi);
    trk[0].engine = trk[0].eng_req;
    trk[0].p[P_VINT] = 0;                          /* (no vibrato in the measure) */
    blocks_ms(50);
    set_item(X_KTOUCH, KT_VELAT);
    kt_live = 1;
    for (i = 0; i < 3u; i++) {
        key_down_up(K3[i], 1);
        land(K3[i], 600);
    }
    blocks_ms(400);
    for (i = 0; i < 3u; i++)
        s0[i] = ph_step(kb_nt[K3[i]][0]);
    for (i = 0; i < 60u; i++) {
        kt_key[4].d = (uint16_t)(KT_DBASE + 207);   /* key 4 a full octave under its rest: 127 */
        blocks_ms(5);
    }
    for (i = 0; i < 3u; i++)
        s1[i] = ph_step(kb_nt[K3[i]][0]);
    snprintf(w, sizeof w, "R32: PRESS BEND: the key leaned on %+.1f ct (pressure %u), the other two %+.2f %+.2f",
             cents(s1[1], s0[1]), voice_of(0, kb_nt[4][0]) ? voice_of(0, kb_nt[4][0])->pat : 0u, cents(s1[0], s0[0]),
             cents(s1[2], s0[2]));
    check(w, pi < e->npresets && fabs(cents(s1[1], s0[1]) - 200.0) <= 5.0 && fabs(cents(s1[0], s0[0])) <= 1.0 &&
             fabs(cents(s1[2], s0[2])) <= 1.0);
    all_up();
    kt_live = 0;
    set_item(X_KTOUCH, KT_OFF);
    set_engine_of(&trk[0], TRK_DEF[0][0]);
    apply_preset_to(&trk[0], TRK_DEF[0][1]);
    trk[0].engine = trk[0].eng_req;
    blocks_ms(50);
}

int main(void)
{
    uint32_t i, k, n0, n1;
    harness_init();
    kt_live = 0;

    /* --- R1 / R29: OFF */
    {
        const touch_cfg_t d = TOUCH_CFG_DEFAULTS;
        voice_t *v;
        check("defaults: LEVER LATCH (touch_ext; GLO > TOUCH 3 LEVER shows 3: OFF, SPRNG, HOLD, LATCH)",
              touch_ext.lever_mode == LV_LATCH && get_item(X_LEVER) == 1 + LV_LATCH && XD[X_LEVER].max == 3 &&
              !strcmp(XD[X_LEVER].names[3], "LATCH") && !strcmp(XD[X_LEVER].names[0], "OFF"));
        check("defaults: RIB SPRING LATCH (touch_ext rib_sprng 0; GLO > TOUCH 4 SPRNG shows 2: HOLD, SPRNG, LATCH)",
              touch_cfg.rib_spring && !touch_ext.rib_sprng && get_item(X_RSPRING) == 2 && XD[X_RSPRING].max == 2 &&
              !strcmp(XD[X_RSPRING].names[2], "LATCH") && !strcmp(XD[X_RSPRING].names[0], "HOLD"));
        check("defaults: KEY TOUCH OFF, AT OUT OFF, LIN, 0, 0 ms, 0 ms; LEVER ON, RIB SPRING ON, MOTION REC ON",
              !memcmp(&touch_cfg, &d, sizeof d) && touch_cfg.key_touch == KT_OFF && touch_cfg.lever && touch_cfg.motion_rec &&
              touch_cfg.at_sens == 40 && touch_cfg.lever_time == 120 && touch_cfg.rib_steps == 24 &&
              touch_cfg.rib_delay == 150 && touch_cfg.rib_return == 400 && touch_cfg.rib_range == 12);
        mo_n = 0;
        kt_live = 1;                               /* (even measuring: OFF plays at 100) */
        land(5, 300);
        key_down_up(5, 1);
        blocks_ms(5);
        v = voice_of(0, kb_nt[5][0]);
        check("R1: KEY TOUCH OFF: a key plays at velocity 100 at once (voices and MIDI out)",
              v && v->vel == 100 && mo_n >= 1 && (mo[0] >> 8 & 0xF0u) == 0x90u && (mo[0] >> 24) == 100u);
        all_up();
        kt_live = 0;
    }

    /* --- R2 / R10 velocity */
    set_item(X_KTOUCH, KT_VEL);
    kt_live = 1;
    {
        voice_t *v;
        uint32_t want = kt_velocity(360, kt_key[3].floor_, kt_key[3].ceil);
        mo_n = 0;
        key_down_up(3, 1);
        blocks_ms(2);
        n0 = voice_of(0, kb_nt[3][0]) != 0;        /* (waiting for its landing) */
        land(3, 360);
        blocks_ms(2);
        v = voice_of(0, kb_nt[3][0]);
        check("R2: VEL: a measured key waits for its landing, then plays at its velocity", !n0 && v && v->vel == want && want != 100);
        check("R10: and goes out on MIDI with it", mo_n >= 1 && (mo[0] >> 24) == want);
        mo_n = 0;
        key_down_up(22, 1);                        /* C#5 and up: no converter */
        blocks_ms(2);
        v = voice_of(0, kb_nt[22][0]);
        check("R2: a top-row key plays at 100 at once", v && v->vel == 100 && mo_n >= 1 && (mo[0] >> 24) == 100u);
        all_up();
        key_down_up(9, 1);
        blocks_ms(13);
        v = voice_of(0, kb_nt[9][0]);
        check("R3: a measured key that never lands plays at 100 by 12 ms", v && v->vel == 100);
        all_up();
    }

    /* --- R8, R14: VEL+AT, a three-note chord, one key leaned on */
    set_item(X_KTOUCH, KT_VELAT);
    {
        static const uint8_t K3[3] = {0, 4, 7};
        voice_t *v[3];
        uint32_t ok = 1;
        for (i = 0; i < 3u; i++) {
            key_down_up(K3[i], 1);
            land(K3[i], 600);
        }
        blocks_ms(400);                            /* (settled: no pressure) */
        for (i = 0; i < 3u; i++)
            v[i] = voice_of(0, kb_nt[K3[i]][0]), ok &= v[i] && v[i]->pat == 0;
        check("R6: VEL+AT: a chord held still: no pressure", ok);
        {
            uint32_t prev = v[1]->pat, jump = 0, first = 0;
            for (i = 0; i < 40u * 7u; i++) {           /* (a block at a time: the steps the voice hears) */
                kt_key[4].d = (uint16_t)(KT_DBASE + 207);   /* key 4: a full octave under its rest */
                blocks_ms(1);
                if (v[1]->pat > prev && v[1]->pat - prev > jump)
                    jump = v[1]->pat - prev;
                if (!first && v[1]->pat)
                    first = v[1]->pat;
                prev = v[1]->pat;
            }
            /* (the FM-1 as the SMK-37) AT ATTACK / RELEASE 0: the keys' own pressure, read every 5 ms,
             * still glides AT_MIN_MS (touch.h): no step of the reading heard */
            check("R8: the keys' own pressure glides (AT_MIN_MS at the fastest): no block's step over 16 of 127",
                  jump > 0 && jump <= 16u && first < 64u);
        }
        check("R8: the key leaned on: its own note's poly aftertouch at 127, the other two none",
              v[1]->pat == 127 && v[0]->pat == 0 && v[2]->pat == 0 && part_ctl[0].at == 0);
        set_item(X_ATCURVE, AT_HARD);
        set_item(X_ATOFS, -4);
        kt_key[4].d = 600;
        blocks_ms(200);
        for (i = 0; i < 40u; i++) {
            kt_key[4].d = (uint16_t)(KT_DBASE + 330);   /* a smaller lean */
            blocks_ms(5);
        }
        check("R14: the keys' pressure through the AT shaping (HARD, OFFSET -4)",
              v[1]->pat == at_lut[kt_p[4]] && kt_p[4] > 0 && v[1]->pat != kt_p[4] && v[0]->pat == 0);
        set_item(X_ATCURVE, AT_LIN);
        set_item(X_ATOFS, 0);
        all_up();
    }

    /* --- R10 AT OUT */
    {
        uint32_t polys = 0, chans = 0, last = 0, maxgap_ok = 1, tlast = 0, ok = 1;
        set_item(X_ATOUT, AT_OUT_POLY);
        key_down_up(2, 1);
        land(2, 600);
        blocks_ms(300);
        mo_n = 0;
        for (i = 0; i < 100u; i++) {               /* 500 ms of a pressure rising and falling */
            kt_key[2].d = (uint16_t)(KT_DBASE + 415 - (i < 50u ? i * 4u : (100u - i) * 4u));
            blocks_ms(5);
        }
        for (k = 0; k < (mo_n < 256u ? mo_n : 256u); k++)
            if ((mo[k] >> 8 & 0xF0u) == 0xA0u) {
                ok &= (mo[k] >> 16 & 0x7Fu) == kb_nt[2][0] && (mo[k] >> 24) != last;
                if (polys && mo_t[k] - tlast < 10u)
                    maxgap_ok = 0;
                tlast = mo_t[k];
                last = mo[k] >> 24;
                polys++;
            }
        check("R10: AT OUT POLY: the key's pressure as polyphonic key pressure, on change only", polys > 5 && ok);
        check("     at most one message a key every 10 ms (500 ms: 50 at most)", polys <= 50u && maxgap_ok);
        all_up();
        set_item(X_ATOUT, AT_OUT_CHAN);
        key_down_up(2, 1);
        key_down_up(6, 1);
        land(2, 600);
        land(6, 600);
        blocks_ms(300);
        mo_n = 0;
        for (i = 0; i < 40u; i++) {
            kt_key[2].d = (uint16_t)(KT_DBASE + 300);
            kt_key[6].d = (uint16_t)(KT_DBASE + 200);   /* (the firmer) */
            blocks_ms(5);
        }
        last = 0;
        for (k = 0; k < (mo_n < 256u ? mo_n : 256u); k++) {
            polys += (mo[k] >> 8 & 0xF0u) == 0xA0u ? 1000u : 0u;
            if ((mo[k] >> 8 & 0xF0u) == 0xD0u)
                chans++, last = mo[k] >> 16 & 0x7Fu;
        }
        check("     AT OUT CHAN: channel pressure, the most any held key presses; no poly", chans > 0 && chans <= 20u &&
              last == (kt_p[6] > kt_p[2] ? kt_p[6] : kt_p[2]) && polys < 1000u);
        all_up();
        blocks_ms(20);
        check("     and back to 0 when the keys are let go", (mo[(mo_n - 1u) % 256u] >> 8 & 0xF0u) == 0xD0u &&
              (mo[(mo_n - 1u) % 256u] >> 16 & 0x7Fu) == 0u);
        set_item(X_ATOUT, AT_OUT_OFF);
    }
    kt_live = 0;

    /* --- R15 / R14: MIDI pressure */
    set_item(X_KTOUCH, KT_OFF);
    {
        voice_t *v;
        uint32_t ok = 1, q;
        midi_in(0x90, 60, 90);
        midi_in(0x90, 64, 90);
        blocks_ms(3);
        for (q = 0; q < 128u; q++) {
            midi_in(0xA0, 60, q);
            midi_in(0xD0, 127u - q, 0);
            audio_blocks(1);
            v = voice_of(0, 60);
            ok &= v && v->pat == q && part_ctl[0].at == 127u - q && voice_of(0, 64)->pat == 0;
        }
        check("R15: the defaults: every MIDI poly and channel pressure value reaches the voice as it came", ok);
        set_item(X_ATCURVE, AT_SOFT);
        set_item(X_ATOFS, 2);
        midi_in(0xA0, 60, 30);
        midi_in(0xD0, 64, 0);
        audio_blocks(1);
        v = voice_of(0, 60);
        check("R14: MIDI poly and channel pressure through the shaping (SOFT, OFFSET +2)",
              v->pat == at_shape1(30, AT_SOFT, 2) && part_ctl[0].at == at_shape1(64, AT_SOFT, 2) && v->pat != 30);
        set_item(X_ATCURVE, AT_LIN);
        set_item(X_ATOFS, 0);
        set_item(X_ATATK, 10);                     /* 100 ms */
        set_item(X_ATREL, 2);                      /* 20 ms */
        midi_in(0xA0, 60, 0);
        midi_in(0xD0, 0, 0);
        blocks_ms(200);
        midi_in(0xA0, 60, 127);
        midi_in(0xD0, 127, 0);
        blocks_ms(100);
        n0 = voice_of(0, 60)->pat;
        n1 = part_ctl[0].at;
        blocks_ms(700);
        check("R13 through the path: ATTACK 100 ms: 63 % of a step after 100 ms (+-10 %), then all of it",
              n0 >= 74 && n0 <= 86 && n1 >= 74 && n1 <= 86 && voice_of(0, 60)->pat == 127 && part_ctl[0].at == 127);
        midi_in(0xA0, 60, 0);
        blocks_ms(20);
        n0 = voice_of(0, 60)->pat;
        blocks_ms(200);
        check("     RELEASE 20 ms: 37 % left after 20 ms, then 0", n0 >= 38 && n0 <= 56 && voice_of(0, 60)->pat == 0);
        midi_in(0x90, 67, 90);
        blocks_ms(3);
        check("     a new note starts with no pressure", voice_of(0, 67) && voice_of(0, 67)->pat == 0);
        set_item(X_ATATK, 0);
        set_item(X_ATREL, 0);
        midi_in(0x80, 60, 0);
        midi_in(0x80, 64, 0);
        midi_in(0x80, 67, 0);
        midi_in(0xD0, 0, 0);
        blocks_ms(50);
    }

    lever_ribbon();                                /* (Stallion: the LEVER and the RIBBON) */
    press_bend();

    /* --- the pages */
    {
        static const char *const T[4] = {"TOUCH", "TOUCH 2", "TOUCH 3", "TOUCH 4"};
        uint32_t p, found = 0, ok = 1, ids = 0;
        for (p = 0; p < NPAGES; p++) {
            for (i = 0; i < 4u; i++)
                if (!strcmp(PAGES[p].title, T[i]) && PAGES[p].fam == FAM_GLO && PAGES[p].scope == SC_SYN) {
                    found++;
                    for (k = 0; k < 4u; k++) {
                        int16_t *vp;
                        const param_desc_t *d = syn_desc(PAGES[p].id[k], &vp);
                        char val[8];
                        const char *unit;
                        ok &= d && strlen(d->label) <= 5u && *vp >= d->min && *vp <= d->max;
                        param_format(d, d->max, val, &unit), ok &= strlen(val) <= 5u;
                        param_format(d, d->min, val, &unit), ok &= strlen(val) <= 5u;
                        ids |= 1u << (PAGES[p].id[k] - X_KTOUCH);
                    }
                }
        }
        check("pages: GLO > TOUCH .. TOUCH 4, the 16 settings, names and values within the column", found == 4 && ok &&
              ids == 0xFFFFu);
    }

    /* --- R28: saved, back after a power-off */
    {
        touch_cfg_t want;
        set_item(X_KTOUCH, KT_VELAT);
        set_item(X_ATSENS, 25);
        set_item(X_ATOUT, AT_OUT_POLY);
        set_item(X_ATCURVE, AT_HARD);
        set_item(X_ATOFS, -3);
        set_item(X_ATATK, 7);
        set_item(X_ATREL, 40);
        set_item(X_LEVER, 0);
        set_item(X_LTIME, 100);
        set_item(X_RIBRNG, 2);
        set_item(X_MREC, 0);
        set_item(X_RSTEPS, 96);
        set_item(X_RSPRING, 0);
        set_item(X_RDELAY, 300);
        set_item(X_RRET, 5);
        want = touch_cfg;
        check("R28: the pages set the settings (ms in 10 ms steps)", want.at_attack == 70 && want.at_release == 400 &&
              want.lever_time == 1000 && want.rib_delay == 3000 && want.rib_return == 50 && want.at_sens == 25 &&
              get_item(X_RRET) == 5 && settings_later);
        settings_save();
        power_cycle();
        check("R28: every GLO > TOUCH setting back after a power-off", !memcmp(&touch_cfg, &want, sizeof want) &&
              at_lut[127] == 127 && at_lut[60] == at_shape1(60, AT_HARD, -3) && at_smooth);
        {   /* a firmware without them saves the settings: no tail */
            persist_t p;
            persist_fill(&p);
            st_side = 0;
            st_tail = 0;
            (void)st_save3(OBJ_SETTINGS, &p, sizeof p, &dsu, sizeof dsu, set_tail, set_tail_n);
            power_cycle();
            {
                const touch_cfg_t d = TOUCH_CFG_DEFAULTS;
                check("R28: settings saved without them: the defaults", !memcmp(&touch_cfg, &d, sizeof d) && !at_smooth &&
                      at_lut[60] == 60 && !kt_calibrated);
            }
        }
        touch_cfg = want;
        settings_save();
        {   /* a damaged tail: the defaults */
            st_hdr_t h;
            int c = st_current(OBJ_SETTINGS, &h);
            nor[st_sector(OBJ_SETTINGS, (uint32_t)c) + ST_PAYLOAD_OFF + h.len + 20u] ^= 0x10u;
            power_cycle();
            check("R28: a damaged GLO > TOUCH record: the defaults", touch_cfg.key_touch == KT_OFF && touch_cfg.at_sens == 40);
        }
        touch_cfg = (touch_cfg_t)TOUCH_CFG_DEFAULTS;
        at_build();
        settings_save();
    }
    /* --- LEVER's four values saved and back; a record saved before the mode (a 104-byte body): LATCH, or OFF */
    {
        uint32_t m, ok = 1;
        for (m = 0; m < 4u; m++) {
            set_item(X_LEVER, (int32_t)m);
            settings_save();
            power_cycle();
            ok &= get_item(X_LEVER) == (int32_t)m && touch_cfg.lever == (m != 0u);
        }
        check("LEVER OFF, SPRNG, HOLD, LATCH: each saved and back after a power-off", ok);
        for (m = 0, ok = 1; m < 3u; m++) {
            set_item(X_RSPRING, (int32_t)m);
            settings_save();
            power_cycle();
            ok &= get_item(X_RSPRING) == (int32_t)m && touch_cfg.rib_spring == (m != 0u);
        }
        check("RIB SPRING HOLD, SPRNG, LATCH: each saved and back after a power-off", ok);
        set_item(X_RSPRING, 1);                     /* (SPRNG: the old record below cannot hold it) */
        for (m = 0; m < 2u; m++) {                  /* (LEVER ON, then OFF, as the firmware before saved them) */
            touch_store_t ts;
            persist_t p;
            set_item(X_LEVER, 1 + LV_HOLD);         /* (a mode the old record cannot hold) */
            touch_cfg.lever = (uint8_t)(m == 0u);
            touch_fill(&ts);
            ts.len = 104u;
            ts.crc = st_crc32((const uint8_t *)&ts + TOUCH_BODY_OFF, 104u);
            persist_fill(&p);
            side_fill(&side_out);
            st_side = &side_out;
            st_side_len = sizeof side_out;
            st_tail = &ts;
            st_tail_len = TOUCH_BODY_OFF + 104u;
            (void)st_save3(OBJ_SETTINGS, &p, sizeof p, &dsu, sizeof dsu, set_tail, set_tail_n);
            st_side = 0;
            st_tail = 0;
            power_cycle();
            if (m == 0u)
                check("a record saved before the mode (116 bytes), LEVER ON: LATCH; RIB SPRING ON: LATCH", touch_cfg.lever &&
                      touch_ext.lever_mode == LV_LATCH && get_item(X_LEVER) == 1 + LV_LATCH &&
                      touch_cfg.rib_spring && get_item(X_RSPRING) == 2);
            else
                check("     LEVER OFF: OFF (the mode LATCH when turned on)", !touch_cfg.lever && get_item(X_LEVER) == 0 &&
                      touch_ext.lever_mode == LV_LATCH);
        }
        touch_cfg = (touch_cfg_t)TOUCH_CFG_DEFAULTS;
        touch_ext = (touch_ext_t)TOUCH_EXT_DEFAULTS;
        settings_save();
    }

    /* --- R9: TOUCH CAL */
    {
        uint32_t ok = 1, ph, rep;
        set_item(X_KTOUCH, KT_OFF);
        set_item(X_TCAL, 1);
        ui_frame();
        check("R9: TOUCH CAL opens its screen; KEY TOUCH OFF: the keys measured all the same", touch_cal_on() && kt_cal_phase == 1 &&
              (kt_sample(0, 700, 0), kt_want_drive(0)));
        unland(0);
        for (ph = 0; ph < 2u; ph++) {
            for (k = 0; k < KT_NKEY; k++)
                for (rep = 0; rep < 3u; rep++) {
                    static const uint16_t G[3] = {830, 870, 850}, F[3] = {330, 370, 350};
                    land(k, (ph ? F[rep] : G[rep]) - k);
                    unland(k);
                }
            ok &= touch_cal_done() == KT_NKEY && touch_cal_keys() == 0;   /* (every light out) */
            ui_frame();                            /* (every key done: on to FIRMLY, then done) */
            if (!ph)
                ok &= kt_cal_phase == 2 && touch_cal_on() && touch_cal_keys() == 0xFFFFFu;
        }
        check("R9: 3 gentle, 3 firm presses of every key: done, the ends the medians", ok && !touch_cal_on() && kt_calibrated &&
              kt_key[0].ceil == 850 && kt_key[0].floor_ == 350 && kt_key[19].ceil == 831 && kt_key[19].floor_ == 331);
        check("    and KEY TOUCH OFF turned to VEL", touch_cfg.key_touch == KT_VEL);
        settings_save();
        power_cycle();
        check("R9: kept in the settings flash: the same ends after a power-off", kt_calibrated && kt_key[0].ceil == 850 &&
              kt_key[0].floor_ == 350 && kt_key[19].ceil == 831 && kt_key[19].floor_ == 331 && touch_cfg.key_touch == KT_VEL);
        {   /* played since: the ends learnt, not kept; a save with nothing else changed writes nothing */
            uint32_t pr;
            kt_learn(0, 1010);
            kt_learn(1, 200);
            pr = fl_progs;
            settings_save();
            check("    the ends learnt by playing are not saved: no flash write for them", fl_progs == pr &&
                  kt_key[0].ceil != 850);
            power_cycle();
        }
        set_item(X_TCAL, 1);
        ui_frame();
        land(0, 990);
        unland(0);
        edges_btn |= 1u << panel.btn[B_OCTDN];
        ui_frame();
        check("R9: OCT- cancels: nothing changed", !touch_cal_on() && !kt_cal_phase && kt_key[0].ceil == 850);
    }

    printf(bad ? "TOUCH TEST FAILED (%d)\n" : "touch test passed\n", bad);
    return bad ? 1 : 0;
}
