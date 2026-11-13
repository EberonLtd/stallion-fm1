/* SPDX-License-Identifier: GPL-3.0-only */
/* Stallion: NOTE PRESS, each recorded note's own poly pressure (specs/touch R39; firmware/src/motion.c np_*, project.c
 * np_put_p / np_get_p), on the harness of tests/editor_host.c as tests/motion_test.c. Tempo 120, 16 steps of 1/16: a
 * pass is 2756 blocks, a step 172, a quarter 43.
 *   R39  PRESS BEND (POLY AT -> PITCH, +2 semitones at 127): a three-note chord recorded live (MIDI notes held over
 *        steps 0..7), poly pressure on its middle note only (a ramp to 127, held, back to 0); on the next pass, the
 *        hand off: that note's voice gets its curve (127 where it was held, straight between the points: no block jumps
 *        more than a 4-bit point's share) and bends +2 semitones (5 cents), the other two voices pressure 0 and their
 *        pitch within 1 cent of where they were; a held key over a curve: the hand's; nothing recorded with no
 *        pressure (KEY TOUCH OFF: no curve, nothing read); undo with the pass, PRESS clear takes it, LENGTH x2, SHIFT
 *   R37  save / load through the flash carries it (and the lanes, unchanged); a project without it (as saved before)
 *        loads with none and plays bit for bit as on a unit that never had one, and is written byte for byte as without
 *        R39; tracks 2, 3 holding notes: the BEND lane fits, the curve does not, nothing of theirs touched
 * Exit status: the number of failed checks. */
#include "editor_host.c"
#include <sys/wait.h>
#include <unistd.h>

static int bad;
static void check(const char *what, int ok)
{
    printf("note press: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
#define PASS_B 2756u
static void midi_in(uint32_t st, uint32_t d1, uint32_t d2)
{
    midi_in_q[mi_w % MQ] = (st >> 4) | st << 8 | d1 << 16 | d2 << 24;
    mi_w++;
}
static void run(uint32_t nb) { audio_blocks(nb); mo_r = mo_w; }
static void stop(void) { transport_req = 2; run(4); song.rec = 0; }
static void play(void) { transport_req = 1; run(1); }
static uint32_t press_bend_pi(void)
{
    const engine_t *e = ENGINES[ENGI_STAMPEDE];
    uint32_t pi;
    for (pi = 0; pi < e->npresets && strcmp(e->presets[pi].name, "PRESS BEND"); pi++)
        ;
    return pi;
}
static void fresh(void)                                 /* stopped, empty 16 steps, PRESS BEND, no lane, no curve */
{
    stop();
    steps_clear(&trk[0]);
    trk[0].p[P_SLEN] = 16;
    trk[0].p[P_SDIV] = 2;
    song.g[G_BPM] = 120;
    song.g[G_SWING] = 0;
    trk[0].p[P_SSWING] = 0;
    set_engine_of(&trk[0], ENGI_STAMPEDE);
    apply_preset_to(&trk[0], press_bend_pi());
    trk[0].engine = trk[0].eng_req;
    trk[0].p[P_VINT] = 0;                               /* (no vibrato in the measure) */
    part_ctl_reset();
    touch_cfg = (touch_cfg_t)TOUCH_CFG_DEFAULTS;
    at_build();
    ml_mask = 0;
    np_on = 0;
    run(200);
}
static voice_t *voice_of(uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (trk[0].v[i].active && trk[0].v[i].gate && trk[0].v[i].note == note)
            return &trk[0].v[i];
    return 0;
}
static uint32_t ph_step(uint32_t note)                 /* the voice's phase step over a block */
{
    voice_t *v = voice_of(note);
    uint32_t a;
    if (!v)
        return 1;
    a = v->ph[0];
    audio_blocks(1);
    return v->ph[0] - a;
}
static double cents(uint32_t d, uint32_t d0) { return 1200.0 * log2((double)d / (double)d0); }
/* the pressure on note 64 recorded at block b of pass 1 (MIDI poly aftertouch) */
static uint32_t press_g(uint32_t b)
{
    if (b < 300u) return 0;
    if (b < 700u) return (b - 300u) * 127u / 400u;
    if (b < 1000u) return 127;
    if (b < 1100u) return 127u - (b - 1000u) * 127u / 100u;
    return 0;
}
/* a pass recording the chord 60 64 67 held from block 20 to 1300, pressure (scale/127 of press_g) on note 64 */
static void rec_pass(uint32_t scale)
{
    uint32_t b, last = 0;
    while (trk[0].seq_idx != 0 || !song.playing)
        run(1);
    song.rec = 1;
    for (b = 0; b < PASS_B; b++) {
        if (b == 20u)
            midi_in(0x90, 60, 100), midi_in(0x90, 64, 100), midi_in(0x90, 67, 100);
        if (b >= 20u && b < 1300u && press_g(b) * scale / 127u != last)
            last = press_g(b) * scale / 127u, midi_in(0xA0, 64, last);
        if (b == 1300u)
            midi_in(0x80, 60, 0), midi_in(0x80, 64, 0), midi_in(0x80, 67, 0);
        run(1);
    }
    song.rec = 0;
}

typedef uint64_t (*hfn_t)(uint32_t);
static uint64_t child(hfn_t f, uint32_t a)
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
        for (i = 0; i < NVOICE; i++)
            h = (h ^ trk[0].v[i].pat) * 1099511628211ull;
    }
    return h;
}
/* a project without a curve (its pattern: a chord over steps 0..7), loaded after (1) or without (0) one with curves
 * playing in RAM, then played: its sound and its voices' pressure */
static project_t old_p;
static uint64_t old_load(uint32_t had)
{
    if (had) {
        memset(np_cv, 0xFF, sizeof np_cv);
        np_on = 1;
    }
    project_apply(&old_p);
    run(4);
    play();
    return hash_blocks(1500);
}

int main(void)
{
    static uint8_t snap[NP_B];
    static int8_t lsnap[ML_LANES][ML_N];
    uint32_t b, k;
    harness_init();

    /* ---------------------------------------------------------------- R39: record, play back */
    fresh();
    check("the factory sound PRESS BEND is there", press_bend_pi() < ENGINES[ENGI_STAMPEDE]->npresets);
    play();
    rec_pass(0);
    check("a chord recorded with no pressure (KEY TOUCH OFF): no curve kept", !np_on && trk[0].step[0].n == 3u);
    play();
    rec_pass(127);
    check("a chord recorded with poly pressure on one note: a curve kept (and the PRESS lane, R35)",
          np_on != 0 && (ml_mask & 1u << ML_PRESS));
    {
        uint32_t k64 = 3, k60 = 3, k67 = 3, mx = 0, other = 0;
        for (k = 0; k < trk[0].step[0].n; k++) {
            if (trk[0].step[0].note[k] == 64) k64 = k;
            if (trk[0].step[0].note[k] == 60) k60 = k;
            if (trk[0].step[0].note[k] == 67) k67 = k;
        }
        for (b = 0; b < 16u; b++)
            for (k = 0; k < NP_PPS; k++) {
                if (np_get(b, k64, k) > mx) mx = np_get(b, k64, k);
                other |= np_get(b, k60, k) | np_get(b, k67, k);
            }
        check(".. its note's points reach 15 (127), the other two notes' all 0", mx == 15u && !other);
    }
    {
        uint32_t s0[3], s1[3], p1[3], dmax = 0, prev = 0, ok_lv = 1;
        static const uint8_t N3[3] = {60, 64, 67};
        while (trk[0].seq_idx != 0)
            run(1);
        run(150);                                       /* (pass 2, block 150: no pressure yet) */
        for (k = 0; k < 3u; k++)
            s0[k] = ph_step(N3[k]);
        for (b = 152; b < 1150u; b++) {                 /* (the curve as it plays: no jump; the others at 0) */
            voice_t *v = voice_of(64);
            uint32_t x = v ? v->pat : 0;
            if (b > 152u && (x > prev ? x - prev : prev - x) > dmax)
                dmax = x > prev ? x - prev : prev - x;
            prev = x;
            ok_lv &= voice_of(60) && voice_of(67) && !voice_of(60)->pat && !voice_of(67)->pat;
            if (b == 850u)
                for (k = 0; k < 3u; k++)
                    p1[k] = voice_of(N3[k]) ? voice_of(N3[k])->pat : 999u, s1[k] = ph_step(N3[k]);
            else
                run(1);
        }
        printf("note press:   pass 2: pressure %u %u %u at block 850; largest step a block %u; %+.1f %+.2f %+.2f ct\n",
               p1[0], p1[1], p1[2], dmax, cents(s1[1], s0[1]), cents(s1[0], s0[0]), cents(s1[2], s0[2]));
        check("playback: the pressed note's voice at 127 where it was held, the other two at 0 (every block)",
              p1[1] == 127u && !p1[0] && !p1[2] && ok_lv);
        check("playback: interpolated, no block steps more than 4 (a point is 8.5, a quarter 43 blocks)", dmax <= 4u);
        check("playback: PRESS BEND bends the pressed note +2 semitones (5 ct), the others within 1 ct",
              fabs(cents(s1[1], s0[1]) - 200.0) <= 5.0 && fabs(cents(s1[0], s0[0])) <= 1.0 && fabs(cents(s1[2], s0[2])) <= 1.0);
    }
    /* a key held over the curve: the hand's */
    while (trk[0].seq_idx != 0)
        run(1);
    run(30);
    midi_in(0x90, 64, 100);
    run(2);
    midi_in(0xA0, 64, 20);
    run(800);
    check("a note held over its curve: the hand's pressure, not the curve", voice_of(64) && voice_of(64)->pat == 20u);
    midi_in(0x80, 64, 0);
    run(4);
    /* undo, clear, LENGTH x2, SHIFT */
    memcpy(snap, np_cv, sizeof snap);
    fm1_irq_off();
    ml_clear(1u << ML_RIB);
    check("clearing RIBBON keeps the curves", np_on && !memcmp(snap, np_cv, sizeof snap));
    ml_clear(1u << ML_PRESS);
    check("clearing PRESS takes them", !np_on);
    np_on = 1;
    ml_undo_mark();
    np_on = 0;
    memset(np_cv, 0, sizeof np_cv);
    ml_undo_swap();
    check("undo brings them back with the lanes", np_on && !memcmp(snap, np_cv, sizeof snap));
    ml_rotate(16, 1);
    check("SHIFT moves them a step later", !memcmp(&np_cv[NP_B / NSTEP], snap, 15u * NP_B / NSTEP) &&
                                          !memcmp(np_cv, &snap[15u * NP_B / NSTEP], NP_B / NSTEP));
    ml_rotate(16, -1);
    check(".. and back", !memcmp(snap, np_cv, sizeof snap));
    ml_double(16);
    check("LENGTH x2 copies them after themselves", !memcmp(&np_cv[16u * NP_B / NSTEP], snap, 16u * NP_B / NSTEP));
    memcpy(np_cv, snap, sizeof snap);
    fm1_irq_on();

    /* ---------------------------------------------------------------- R37: stores */
    stop();                                             /* (a project is saved and loaded stopped) */
    ml_mask |= 1u << ML_PRESS;                          /* (the PRESS lane the pass recorded, cleared above: back) */
    memcpy(lsnap, ml_lane, sizeof lsnap);
    {
        uint8_t m = ml_mask;
        project_save(0);
        np_on = 0, ml_mask = 0;
        memset(np_cv, 0, sizeof np_cv);
        memset(ml_lane, 0, sizeof ml_lane);
        proj_slot[0].magic = 0;
        project_load(0);
        run(4);
        check("save / load through the flash: the curves back, the lanes too",
              np_on && !memcmp(snap, np_cv, sizeof snap) && m && ml_mask == m && !memcmp(lsnap, ml_lane, sizeof lsnap));
        {
            uint32_t z = 1;
            for (k = 0; k < NSTEP; k++)
                z &= !trk[1].step[k].note[0] && !trk[1].step[k].vel && !trk[2].step[k].note[3] && !trk[2].step[k].rat &&
                     !trk[3].dstep[k].rat[3];
            check(".. none of the stream left in the working tracks 2..4", z);
        }
        ml_mask = 0;
        {
            static project_t a;
            proj_capture(&a);
            ml_mask = m;
            np_on = 0;
            project_apply(&a);
            check("curves without any lane: saved and loaded (at the stream's start)", np_on && !memcmp(snap, np_cv, sizeof snap) && !ml_mask);
        }
    }
    {
        static project_t a, c;
        np_on = 0;
        ml_mask = ML_ALL;
        memcpy(ml_lane, lsnap, sizeof ml_lane);
        proj_capture(&a);                               /* (no curve) */
        memset(np_cv, 0x5A, sizeof np_cv);              /* (junk in RAM, but no curve: not written) */
        proj_capture(&c);
        check("no curve: the project byte for byte as without R39 (the lanes' stream alone)", !memcmp(&a, &c, sizeof a));
        np_on = 1;
        project_apply(&a);
        check(".. and loads with none", !np_on && ml_mask == ML_ALL);
    }
    /* an old project (no curve, no lane: a chord over steps 0..7) plays bit for bit as on a unit that never had a curve */
    fresh();
    trk[0].step[0].n = 3, trk[0].step[0].note[0] = 60, trk[0].step[0].note[1] = 64, trk[0].step[0].note[2] = 67;
    trk[0].step[0].time = ST_NOTE, trk[0].step[0].vel = 100;
    for (k = 1; k < 8u; k++)
        trk[0].step[k].time = ST_TIE;
    trk[0].seq_active = 1;
    proj_capture(&old_p);
    {
        uint64_t h0 = child(old_load, 0), h1 = child(old_load, 1);
        check("a project without curves plays bit for bit (sound and voices' pressure) as on a unit without", h0 == h1 && h0);
    }
    /* tracks 2, 3 holding notes: only track 4's steps: the BEND lane fits, the curve not */
    fresh();
    trk[1].step[3].n = 1, trk[1].step[3].note[0] = 60, trk[1].step[3].time = ST_NOTE;
    trk[2].step[9].n = 1, trk[2].step[9].note[0] = 62, trk[2].step[9].time = ST_NOTE;
    memcpy(ml_lane, lsnap, sizeof ml_lane);
    ml_mask = 1u << ML_BEND;
    memcpy(np_cv, snap, sizeof np_cv);
    np_on = 1;
    proj_capture(&old_p);
    check("tracks 2, 3 holding notes: their steps untouched", !memcmp(old_p.t[1].step, trk[1].step, sizeof trk[1].step) &&
                                                             !memcmp(old_p.t[2].step, trk[2].step, sizeof trk[2].step));
    np_on = 0, ml_mask = 0;
    project_apply(&old_p);
    check(".. the BEND lane kept, the curve dropped (no room)", ml_mask == 1u << ML_BEND && !np_on);
    trk[1].step[3].n = 0, trk[2].step[9].n = 0;
    check("the curves' RAM: 2 x 512 bytes in POOL (the build checks its 8 KB spare)", sizeof np_cv + sizeof np_undo <= 1026u);
    printf("note press: %d failed\n", bad);
    return bad;
}
