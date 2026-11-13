/* SPDX-License-Identifier: GPL-3.0-only */
/* Stallion: the MOTION lanes (specs/touch R35-R38; firmware/src/motion.c, project.c ml_put / ml_get, ui_layers.c
 * ml_pick), on the harness of tests/editor_host.c (the real UI, the audio between frames, storage.c / project.c on a
 * RAM image of the flash). Tempo 120, 16 steps of 1/16: a pass is 2 s, a step 172 blocks, a quarter 43.
 *   R35  a ribbon sweep (0 -> full in 1 s, held, back) recorded over live recording plays back on the next pass within
 *        tolerance of the gesture (REC_LAT earlier, as heard), straight between the quarters: no block moves more than
 *        a little; a BEND (MIDI pitch bend + the lever) and a PRESS (channel pressure, shaped) lane the same; a lane
 *        playing sounds bit for bit as the same value played live; a lane with nothing recorded is not read
 *   R36  the hand over a lane: its live value heard while it moves; recording, it replaces only the quarters it
 *        covered; EDIT + KNOB 4 (RIBBON) and EDIT let go clears that lane alone, EDIT + OCT- brings it back; MOTION REC
 *        OFF records nothing
 *   R37  save / load (through the flash) and the editor's backup (object 0 restored) give the lanes back; a project without lanes (as saved before) loads with
 *        none and plays bit for bit as on a unit that never had one, and is written byte for byte as before; tracks 2
 *        and 3 holding notes: the lane that fits (BEND) in track 4's steps; a section stored and loaded carries them,
 *        LENGTH x2 copies them; the RAM they take (POOL: the build checks its 8 KB spare)
 *   R38  a lane playing goes out on MIDI (MIDI = SEQ): pitch bend, CC 16, channel pressure, at most once every 10 ms
 *        a kind, on change; at STOP each back to its rest once
 * Exit status: the number of failed checks. */
#include "editor_host.c"
#include <sys/wait.h>
#include <unistd.h>

static int bad;
static void check(const char *what, int ok)
{
    printf("motion: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
#define PASS_B 2756u                                    /* blocks in a pass: 16 x 661500 units / (32 x 120) */
#define LAT_B 16u                                       /* REC_LAT in blocks */
static void midi_in(uint32_t st, uint32_t d1, uint32_t d2)
{
    midi_in_q[mi_w % MQ] = (st >> 4) | st << 8 | d1 << 16 | d2 << 24;
    mi_w++;
}
static uint32_t mo_pk[4096], mo_bk[4096], mo_cnt, blk;  /* MIDI out taken, the block it went in */
static void take_out(void)
{
    while (mo_r != mo_w) {
        if (mo_cnt < 4096u)
            mo_pk[mo_cnt] = midi_out_q[mo_r % MQ], mo_bk[mo_cnt] = blk;
        mo_cnt++;
        mo_r++;
    }
}
static void run(uint32_t nb) { uint32_t i; for (i = 0; i < nb; i++) { audio_blocks(1); blk++; take_out(); } }
static void stop(void) { transport_req = 2; run(4); song.rec = 0; }
static void fresh(void)                                 /* stopped, an empty 16-step pattern, no lane, controls at rest */
{
    stop();
    steps_clear(&trk[0]);
    trk[0].p[P_SLEN] = 16;
    trk[0].p[P_SDIV] = 2;
    song.g[G_BPM] = 120;
    song.g[G_SWING] = 0;
    trk[0].p[P_SSWING] = 0;
    part_ctl_reset();
    touch_cfg = (touch_cfg_t)TOUCH_CFG_DEFAULTS;
    at_build();
    run(200);                                           /* (the hand idle: past ML_HOLD) */
}
static void play(void) { transport_req = 1; run(1); }
/* the ribbon gesture of pass 1 at block b of the pass */
static int32_t rib_g(int32_t b)
{
    if (b < 200) return 0;
    if (b < 1578) return (int32_t)((int64_t)(b - 200) * 32767 / 1378);
    if (b < 1800) return 32767;
    if (b < 2000) return 32767 - (b - 1800) * 32767 / 200;
    return 0;
}

typedef uint64_t (*hfn_t)(uint32_t);
static uint64_t child(hfn_t f, uint32_t a)            /* f(a) from here, in a child (the state stays as it is here) */
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    pid = fork();
    if (pid == 0) {
        close(fd[0]);
        h = f(a);
        if (write(fd[1], &h, sizeof h) != (ssize_t)sizeof h)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != (ssize_t)sizeof h)
        h = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}
static uint64_t hash_blocks(uint32_t nb)
{
    static int32_t o[CTL * 2];
    uint64_t h = 1469598103934665603ull;
    uint32_t b, i;
    for (b = 0; b < nb; b++) {
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
    }
    return h;
}
/* 0: live (lever 7998, ribbon 16511, pressure 100), no lane; 1: the same values from lanes, live at rest; 2: at rest */
static uint64_t same_value(uint32_t how)
{
    uint32_t k;
    fresh();
    trk[0].p[P_M1SRC] = MS_AT, trk[0].p[P_M1DST] = MT_CUT, trk[0].p[P_M1AMT] = 60;   /* (the pressure heard too) */
    if (how == 1u) {
        for (k = 0; k < ML_N; k++)
            ml_lane[ML_BEND][k] = 31, ml_lane[ML_RIB][k] = 64, ml_lane[ML_PRESS][k] = 100;
        ml_mask = ML_ALL;
    }
    play();
    trk_note_on(&trk[0], 60, 100);
    if (how == 0u) {
        part_ctl[0].lever = 7998;                       /* (31 x 32767 / 127, 64 x 32767 / 127: a lane's values exactly) */
        part_ctl[0].ribbon = 16511;
        at_chan(0, 100, 0);
    }
    return hash_blocks(600);
}
/* a project without lanes, loaded after (1) or without (0) one with lanes, then a held chord: its sound */
static project_t old_p;
static uint64_t old_load(uint32_t had)
{
    if (had) {                                          /* (lanes in RAM, playing as the load comes) */
        memset(ml_lane, 23, sizeof ml_lane);
        ml_mask = ML_ALL;
        part_ctl[0].ml_on = ML_ON_BEND | ML_ON_RIB | ML_ON_AT;
        part_ctl[0].ml_bend = part_ctl[0].ml_rib = 5925, part_ctl[0].ml_at = 23;
    }
    project_apply(&old_p);
    run(4);
    play();
    trk_note_on(&trk[0], 64, 90);
    trk_note_on(&trk[0], 67, 90);
    return hash_blocks(600);
}

int main(void)
{
    static int8_t snap[ML_LANES][ML_N];
    uint32_t b, k, l;
    harness_init();
    blk = 0;

    /* ---------------------------------------------------------------- R35: record, play back */
    fresh();
    check("no lane, stopped and playing: nothing read (ml_on 0)", (play(), run(500), part_ctl[0].ml_on == 0 && !ml_mask));
    stop();
    play();
    song.rec = 1;                                       /* (REC while playing) */
    for (b = 0; b < PASS_B; b++) {
        part_ctl[0].ribbon = (int16_t)rib_g((int32_t)b);
        run(1);
    }
    song.rec = 0;
    check("a ribbon gesture over live recording: the RIBBON lane recorded, nothing else", ml_mask == 1u << ML_RIB);
    {
        int32_t worst = 0, mid = 0, dmax = 0, prev = 0, ok_on = 1;
        for (b = 0; b < PASS_B; b++) {
            int32_t v, want = rib_g((int32_t)(b + LAT_B) % (int32_t)PASS_B), d;
            run(1);
            v = part_ctl[0].ml_rib;
            ok_on &= (part_ctl[0].ml_on & ML_ON_RIB) != 0;
            d = v > want ? v - want : want - v;
            if (d > worst) worst = d;
            if (b > 300 && b < 1450 && d > mid) mid = d;
            if (b && b > 210 && b < 1560 && (v - prev > dmax || prev - v > dmax))
                dmax = v - prev > 0 ? v - prev : prev - v;
            prev = v;
        }
        printf("motion:   playback: worst %d, inside the sweep %d, largest step a block %d (Q15)\n", worst, mid, dmax);
        check("the next pass plays it on the part (ml_on RIBBON) with the hand at rest", ok_on);
        /* (between two quarters a lane is a straight line: at a corner of the gesture it is off by at most half a quarter
         * of the steepest slope there, 32767 / 200 a block x 43 / 2 blocks, plus a byte's rounding, 129) */
        check("within half a quarter's travel at the gesture's corners, 0.7 % inside the sweep (a byte's rounding)",
              worst <= 32767 / 200 * 43 / 2 + 129 && mid <= 230);
        check("interpolated: no block moves more than 1.2 % of the travel (no zipper: ~1.4 cent at RIB RANGE 12)", dmax <= 400);
    }
    /* the live control over the lane */
    part_ctl[0].ribbon = -16000;
    run(2);
    check("a live ribbon over the lane: the live value heard (ml_on RIBBON off)", !(part_ctl[0].ml_on & ML_ON_RIB));
    part_ctl[0].ribbon = 0;
    run(400);
    check("let go: the lane plays again after ML_HOLD", (part_ctl[0].ml_on & ML_ON_RIB) != 0);
    /* R36: overdub, steps 10..13 only */
    memcpy(snap, ml_lane, sizeof snap);
    while (trk[0].seq_idx != 0)
        run(1);
    song.rec = 1;
    for (b = 0; b < PASS_B; b++) {
        part_ctl[0].ribbon = (int16_t)(b >= 1722u && b < 2239u ? -16000 : 0);
        run(1);
    }
    song.rec = 0;
    {
        uint32_t keep = 1, repl = 1;
        for (k = 0; k < 16u * ML_SPS; k++) {
            if (k < 38u || k > 58u)
                keep &= ml_lane[ML_RIB][k] == snap[ML_RIB][k];
            if (k >= 41u && k <= 51u)
                repl &= ml_lane[ML_RIB][k] == ml_enc(ML_RIB, -16000);
        }
        check("overdub: the quarters the hand covered (steps 10..13) take its value", repl);
        check("overdub: every quarter it did not cover kept (the first sweep intact)", keep);
    }
    /* BEND (MIDI pitch bend + the lever) and PRESS (channel pressure through the shaping) */
    fresh();
    touch_cfg.at_curve = AT_HARD;
    at_build();
    play();
    song.rec = 1;
    for (b = 0; b < PASS_B; b++) {
        if (b == 300u) midi_in(0xE0, 0, 96);           /* (+0.5: 16384) */
        if (b == 600u) part_ctl[0].lever = 8000;       /* (+ the lever) */
        if (b == 900u) midi_in(0xE0, 0, 64), part_ctl[0].lever = 0;
        if (b == 1200u) midi_in(0xD0, 100, 0);         /* (HARD: 79) */
        if (b == 1600u) midi_in(0xD0, 0, 0);
        run(1);
    }
    song.rec = 0;
    check("BEND and PRESS lanes recorded (MIDI bend, the lever, channel pressure)", ml_mask == ((1u << ML_BEND) | (1u << ML_PRESS)));
    {
        int32_t b450 = 0, b750 = 0, p1400 = 0;
        for (b = 0; b < PASS_B; b++) {
            run(1);
            if (b == 450u - LAT_B) b450 = part_ctl[0].ml_bend;
            if (b == 750u - LAT_B) b750 = part_ctl[0].ml_bend;
            if (b == 1400u - LAT_B) p1400 = part_ctl[0].ml_at;
        }
        printf("motion:   BEND %d, %d; PRESS %d\n", b450, b750, p1400);
        check("BEND plays the bend (16384) and bend + lever (24384) within 1 %", abs(b450 - 16384) < 330 && abs(b750 - 24384) < 330);
        check("PRESS plays the shaped pressure (HARD 100 -> 79)", p1400 == 79);
    }
    /* the same value from a lane and live: the same sound */
    {
        uint64_t h0 = child(same_value, 0), h1 = child(same_value, 1), h2 = child(same_value, 2);
        check("a lane playing (bend, ribbon, pressure) sounds bit for bit as the same values played live", h0 == h1 && h0 != h2);
    }

    /* ---------------------------------------------------------------- R36: clear, undo, MOTION REC OFF */
    fresh();
    for (k = 0; k < ML_N; k++)
        ml_lane[ML_BEND][k] = 10, ml_lane[ML_RIB][k] = 20, ml_lane[ML_PRESS][k] = 30;
    ml_mask = ML_ALL;
    fm1_in.buttons = ly_bit[LY_ERASE];
    ui_frame();
    encs[panel.enc[EN_K4]] += 2 * panel.dir[EN_K4];   /* (two detents: RIBBON) */
    ui_frame();
    ui_frame();
    check("EDIT + KNOB 4: the screen names the lane to clear", ml_pick == 2u && !strcmp(layer_sub_shown, "let go: clear"));
    check("nothing cleared while EDIT is held", ml_mask == ML_ALL);
    fm1_in.buttons = 0;
    ui_frame();
    ui_frame();
    check("EDIT let go: the RIBBON lane cleared, BEND and PRESS kept", ml_mask == ((1u << ML_BEND) | (1u << ML_PRESS)));
    fm1_in.buttons = ly_bit[LY_ERASE];
    ui_frame();
    fm1_in.buttons |= 1u << panel.btn[B_OCTDN];
    ui_frame();
    fm1_in.buttons = 0;
    ui_frame();
    ui_frame();
    check("EDIT + OCT-: the lane back (undo)", ml_mask == ML_ALL && ml_lane[ML_RIB][5] == 20);
    fm1_in.buttons = ly_bit[LY_ERASE];
    ui_frame();
    encs[panel.enc[EN_K4]] += 9 * panel.dir[EN_K4];   /* (to ALL) */
    ui_frame();
    encs[panel.enc[EN_K4]] -= 9 * panel.dir[EN_K4];   /* (and back to --: nothing) */
    ui_frame();
    fm1_in.buttons = 0;
    ui_frame();
    check("turned back to --: EDIT let go clears nothing", ml_mask == ML_ALL);
    fresh();
    touch_cfg.motion_rec = 0;
    play();
    song.rec = 1;
    for (b = 0; b < PASS_B; b++) {
        part_ctl[0].ribbon = (int16_t)rib_g((int32_t)b);
        part_ctl[0].lever = (int16_t)(rib_g((int32_t)b) / 2);
        run(1);
    }
    song.rec = 0;
    check("MOTION REC OFF: a pass of gestures records no lane", ml_mask == 0);
    touch_cfg.motion_rec = 1;
    play();
    for (b = 0; b < 500u; b++) {
        part_ctl[0].ribbon = (int16_t)rib_g((int32_t)b + 200);
        run(1);
    }
    check("not recording (REC off): no lane either", ml_mask == 0);
    part_ctl[0].ribbon = 0;

    /* ---------------------------------------------------------------- R37: stores */
    fresh();
    for (l = 0; l < ML_LANES; l++)
        for (k = 0; k < ML_N; k++)
            ml_lane[l][k] = (int8_t)(l == ML_PRESS ? (k * 7u + l) % 128u : (int32_t)((k * 37u + l * 11u) % 255u) - 127);
    ml_mask = ML_ALL;
    memcpy(snap, ml_lane, sizeof snap);
    project_save(0);
    ml_mask = 0;
    memset(ml_lane, 0, sizeof ml_lane);
    proj_slot[0].magic = 0;                             /* (RAM gone: from the flash) */
    project_load(0);
    run(4);
    check("save / load through the flash: all three lanes back as they were", ml_mask == ML_ALL && !memcmp(snap, ml_lane, sizeof snap));
    {
        uint32_t z = 1;
        for (k = 0; k < NSTEP; k++)
            z &= !trk[1].step[k].note[0] && !trk[1].step[k].vel && !trk[2].step[k].note[3] && !trk[1].step[k].n;
        check("the stream's bytes are not left in the working tracks 2 and 3 (empty steps)", z);
    }
    /* the editor's backup: object 0 (the working project, as LIST captures it) restored on a unit with no lane */
    {
        static project_t bk;
        proj_capture(&bk);
        ml_mask = 0;
        memset(ml_lane, 0, sizeof ml_lane);
        check("the editor's backup (object 0 restored): the lanes come with the project",
              project_restore(4, &bk, sizeof bk) == 0 && (run(4), ml_mask == ML_ALL) && !memcmp(snap, ml_lane, sizeof snap));
    }
    /* a section stored and loaded; LENGTH x2 */
    section_store(1);
    ml_mask = 0;
    section_load(1);
    run(4);
    check("a section (SAVE + key) carries the lanes", ml_mask == ML_ALL && !memcmp(snap, ml_lane, sizeof snap));
    pattern_length(&trk[0], 1);
    check("LENGTH x2 copies them after themselves", trk[0].p[P_SLEN] == 32 && !memcmp(&ml_lane[ML_RIB][64], snap[ML_RIB], 64) &&
                                                       !memcmp(&ml_lane[ML_BEND][64], snap[ML_BEND], 64));
    /* a project without lanes: written as before, loads with none, plays as on a unit that never had one */
    fresh();
    {
        uint32_t same = 1;
        for (k = 0; k < NSTEP; k++)                    /* (junk in the free bytes of empty steps, as a cleared step may hold) */
            trk[1].step[k].note[1] = (uint8_t)k, trk[2].step[k].vel = (uint8_t)(k + 1u);
        proj_capture(&old_p);
        same = !memcmp(old_p.t[1].step, trk[1].step, sizeof trk[1].step) && !memcmp(old_p.t[2].step, trk[2].step, sizeof trk[2].step) &&
               !memcmp(old_p.t[3].dstep, trk[3].dstep, sizeof trk[3].dstep);
        check("no lane: the project's bytes as before (tracks 2..4's steps as they are)", same);
        for (k = 0; k < NSTEP; k++)
            trk[1].step[k].note[1] = 0, trk[2].step[k].vel = 0;
        proj_capture(&old_p);
    }
    ml_mask = ML_ALL;
    project_apply(&old_p);
    check("a project without lanes loads with none", ml_mask == 0);
    {
        uint64_t h0 = child(old_load, 0), h1 = child(old_load, 1);
        check(".. and plays bit for bit as on a unit that never had a lane", h0 == h1 && h0);
    }
    /* tracks 2 and 3 with notes (a SLOOP project): the lane that fits, in track 4's steps */
    fresh();
    trk[1].step[3].n = 1, trk[1].step[3].note[0] = 60, trk[1].step[3].time = ST_NOTE;
    trk[2].step[9].n = 1, trk[2].step[9].note[0] = 62, trk[2].step[9].time = ST_NOTE;
    memcpy(ml_lane, snap, sizeof ml_lane);
    ml_mask = ML_ALL;
    proj_capture(&old_p);
    check("tracks 2, 3 holding notes: their steps untouched", !memcmp(old_p.t[1].step, trk[1].step, sizeof trk[1].step) &&
                                                             !memcmp(old_p.t[2].step, trk[2].step, sizeof trk[2].step));
    ml_mask = 0;
    project_apply(&old_p);
    check(".. BEND kept in track 4's empty steps (one lane fits there), the others dropped",
          ml_mask == 1u << ML_BEND && !memcmp(ml_lane[ML_BEND], snap[ML_BEND], ML_N) && trk[1].step[3].note[0] == 60);
    trk[1].step[3].n = 0, trk[2].step[9].n = 0;
    check("the lanes' RAM: 2 x 3 x 256 bytes in POOL (the build checks its 8 KB spare)", sizeof ml_lane + sizeof ml_undo <= 1540u);

    /* ---------------------------------------------------------------- R38: MIDI out */
    fresh();
    song.g[G_MIDI] = 1;
    for (k = 0; k < ML_N; k++) {
        ml_lane[ML_RIB][k] = (int8_t)(k < 32u ? k * 4u : 127u - (k - 32u) * 4u);
        ml_lane[ML_BEND][k] = (int8_t)(k < 16u ? 0 : -60);
        ml_lane[ML_PRESS][k] = (int8_t)(k / 2u);
    }
    ml_mask = ML_ALL;
    mo_cnt = 0;
    play();
    run(PASS_B);
    stop();
    run(40);
    {
        uint32_t i, n[3] = {0, 0, 0}, last[3] = {0, 0, 0}, gap = 1, lastv[3] = {0, 0, 0}, ch = trk_midi_ch(0), w = 1;
        for (i = 0; i < mo_cnt && i < 4096u; i++) {
            uint32_t st = (mo_pk[i] >> 8) & 0xFFu, d1 = (mo_pk[i] >> 16) & 0x7Fu, d2 = (mo_pk[i] >> 24) & 0x7Fu, t, v;
            t = st == (0xE0u | ch) ? 0u : st == (0xB0u | ch) && d1 == 16u ? 1u : st == (0xD0u | ch) ? 2u : 3u;
            if (t > 2u)
                continue;
            if (n[t] && (mo_bk[i] - last[t]) * CTL < FS / 100u)
                gap = 0;
            v = t == 0u ? (d2 << 7 | d1) : t == 1u ? d2 : d1;
            if (n[t] && v == lastv[t])
                w = 0;                                  /* (a repeat: on change only) */
            n[t]++, last[t] = mo_bk[i], lastv[t] = v;
        }
        printf("motion:   MIDI out: %u pitch bends, %u CC 16, %u channel pressures\n", n[0], n[1], n[2]);
        check("lanes playing go out on MIDI: pitch bend, CC 16 and channel pressure", n[0] >= 2u && n[1] > 20u && n[2] > 20u);
        check(".. at most once every 10 ms a kind, only on change", gap && w);
        check(".. at STOP back to their rests (8192, 64, 0)", lastv[0] == 8192u && lastv[1] == 64u && lastv[2] == 0u);
    }
    song.g[G_MIDI] = 0;
    printf("motion: %d failed\n", bad);
    return bad;
}
