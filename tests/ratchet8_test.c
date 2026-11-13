/* SPDX-License-Identifier: GPL-3.0-only */
/* (Stallion 1.0) ratchets up to x8 on a synth step (core.h SF_RX, step_rat_get / step_rat_set; seq.c seq_ratchets,
 * rat_span):
 *   x1..x8   a note x h starts exactly h times in its step, hit k at the first block past k / h of it: x1..x4 on the
 *            same blocks as before (the formula seq.c always had), x5..x8 the same way
 *   swing    an even (longer) and an odd (shorter) step x8: 8 hits, even, all inside the step
 *   nudge    a step nudged late, one before an early one, one nudged early: 8 hits each, even, before the next step
 *   tempo    240 BPM, 1/32 steps, full swing (the shortest step there is): 8 hits, each its own block, at GATE 1 and 127
 *   bits     x5..x8 and a note going on (SF_CONT) never share a step; taking a note out takes its bit with it
 *   project  x1..x8 on every note of a chord: capture -> bytes -> import -> apply, the same steps back
 * Exit status: the number of failed checks. */
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? i : 0; }
#include "../firmware/src/project.c"
static struct { uint8_t force; } ui;
static uint8_t sync_reload;
#include "../firmware/src/arranger_scene.c"

static int fails;
static void check(int ok, const char *what)
{
    printf("ratchet8: %-92s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static uint64_t blk;
static uint64_t starts[4096];
static uint32_t nstarts;
static void run_block(void)
{
    int32_t out[CTL * 2];
    uint32_t a = vage;
    mix_block(out, CTL);
    for (; a != vage; a++)                               /* (a voice started: a hit) */
        if (nstarts < 4096u)
            starts[nstarts++] = blk;
    blk++;
}
static void reset(uint32_t bpm, uint32_t div, uint32_t len)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    host_preset(&trk[0], 0, 7);                          /* TRAP PLUCK (POLY) */
    song.g[G_BPM] = (int16_t)bpm;
    song.g[G_SWING] = 0;
    song.rec = 0;
    trk[0].p[P_SDIV] = (int16_t)div;
    trk[0].p[P_SLEN] = (int16_t)len;
    trk[0].p[P_SSWING] = 0;
    trk[0].p[P_SGATE] = 64;
    nstarts = 0;
    blk = 0;
}
static void note_step(uint32_t i, uint32_t hits)
{
    step_t *s = &trk[0].step[i];
    memset(s, 0, sizeof *s);
    s->n = 1;
    s->note[0] = 60;
    s->time = ST_NOTE;
    s->vel = 100;
    step_rat_set(s, 0, hits - 1u);
}
/* the hits from block b0 on, before b1: n of them, at[] */
static uint32_t hits_in(uint64_t b0, uint64_t b1, uint64_t *at)
{
    uint32_t i, n = 0;
    for (i = 0; i < nstarts; i++)
        if (starts[i] >= b0 && starts[i] < b1)
            at[n++] = starts[i];
    return n;
}
/* h hits evenly over span units at adv units a block: each gap within a block of span / h */
static int even(const uint64_t *at, uint32_t h, uint32_t span, uint32_t adv)
{
    uint32_t k;
    double want = (double)span / h / adv;
    for (k = 1; k < h; k++) {
        double g = (double)(at[k] - at[k - 1]);
        if (g < want - 1.01 || g > want + 1.01)
            return 0;
    }
    return 1;
}

/* x1..x8 on step 0 of a 1/16 grid at 120 BPM: exactly h starts in the step, hit k on the first block whose position
 * in the step is past k / h of it (into * h / slen >= k: seq.c's formula since SLOOP 2.0, so x1..x4 as before) */
static void t_counts(void)
{
    uint32_t h, ok_all = 1, ok_old = 1;
    for (h = 1; h <= 8u; h++) {
        uint64_t at[16], f;
        uint32_t n, k, slen = div_units(2), adv = CTL * 120u, good = 1;
        reset(120, 2, 4);
        note_step(0, h);
        transport_req = 1;
        while (blk < (uint64_t)(div_samples(2) * 3u / CTL))
            run_block();
        n = hits_in(0, blk, at);
        f = n ? at[0] : 0;
        for (k = 1; k < n && k < h; k++) {
            uint64_t need = ((uint64_t)k * slen + h - 1u) / h, j = (need + adv - 1u) / adv;
            good &= at[k] == f + j;
        }
        if (n != h || !good)
            printf("ratchet8:   x%u: %u starts%s\n", h, n, good ? "" : ", off the blocks of k / h");
        ok_all &= n == h && good && even(at, h, slen, adv);
        if (h <= 4u)
            ok_old &= n == h && good;
    }
    check(ok_old, "x1..x4: exactly 1..4 starts a step, on the same blocks as before (k / h of the step)");
    check(ok_all, "x5..x8: exactly 5..8 starts a step, evenly spaced (within a block)");
}

/* SWING 60 on 1/16: step 0 (even) is longer, step 1 (odd) shorter; x8 on both */
static void t_swing(void)
{
    uint64_t at[16], b1;
    uint32_t u = div_units(2), sw = swing_units(60, u), adv = CTL * 120u, n0, n1, i;
    reset(120, 2, 4);
    trk[0].p[P_SSWING] = 60;
    note_step(0, 8);
    note_step(1, 8);
    transport_req = 1;
    b1 = (uint64_t)((u + sw) / adv);                     /* (step 1 fires about here) */
    while (blk < (uint64_t)(2u * u / adv + 4u))
        run_block();
    n0 = hits_in(0, b1 - 2u, at);
    i = n0 == 8u && even(at, 8, u + sw, adv);
    n1 = hits_in(b1 - 2u, (uint64_t)(2u * u / adv + 4u), at);
    check(i && n1 == 8u && even(at, 8, u - sw, adv) && at[7] < (uint64_t)(2u * u / adv + 1u),
          "SWING 60: the long even step and the short odd step each play 8 even hits inside it");
}

/* nudges: step 1 late (+20 / 64), step 3 early (-16 / 64) and step 2 before it: 8 hits each, even, none lost */
static void t_nudge(void)
{
    uint64_t at[32];
    uint32_t u = div_units(2), adv = CTL * 120u, ok = 1, s;
    int32_t m[4] = {0, 20, 0, -16};
    reset(120, 2, 4);
    for (s = 0; s < 4u; s++) {
        note_step(s, 8);
        trk[0].step[s].note[0] = (uint8_t)(60u + s);
        trk[0].micro[s] = (int8_t)m[s];
    }
    transport_req = 1;
    while (blk < (uint64_t)(4u * u / adv + 2u))
        run_block();
    for (s = 0; s < 3u; s++) {                          /* (step 3 into the next loop: steps 0..2) */
        int32_t from = (int32_t)(s * u) + m[s] * (int32_t)(u / 64u), to = (int32_t)((s + 1u) * u) + m[s + 1] * (int32_t)(u / 64u);
        uint64_t b0 = (uint64_t)(from / (int32_t)adv), b1 = (uint64_t)(to / (int32_t)adv) + 1u;
        uint32_t n = hits_in(b0, b1, at), good = n == 8u && even(at, 8, (uint32_t)(to - from), adv);
        if (!good)
            printf("ratchet8:   step %u (nudge %d, the next %d): %u hits\n", s + 1u, m[s], m[s + 1], n);
        ok &= good;
    }
    check(ok, "NUDGE: late (+20), before an early one (-16): each step's 8 hits evenly over the time it sounds, none lost");
}

/* the fastest: 240 BPM, 1/32 steps, SWING 100 (the odd step half as long): x8, every hit in a block of its own */
static void t_fast(void)
{
    uint32_t g, ok = 1;
    for (g = 0; g < 2u; g++) {
        uint64_t at[32];
        uint32_t u = div_units(3), sw = swing_units(100, u), adv = CTL * 240u, n, k, sep = 1;
        reset(240, 3, 2);
        trk[0].p[P_SSWING] = 100;
        trk[0].p[P_SGATE] = g ? 127 : 1;
        note_step(0, 8);
        note_step(1, 8);
        trk[0].step[1].note[0] = 72;
        transport_req = 1;
        while (blk < (uint64_t)(2u * u / adv + 2u))
            run_block();
        n = hits_in((uint64_t)((u + sw) / adv), (uint64_t)((2u * u + adv - 1u) / adv), at);   /* the odd step (not step 0 again) */
        for (k = 1; k < n; k++)
            sep &= at[k] > at[k - 1];
        ok &= n == 8u && sep && even(at, 8, u - sw, adv);
        if (n != 8u || !sep)
            printf("ratchet8:   GATE %u: %u hits\n", g ? 127u : 1u, n);
    }
    check(ok, "240 BPM, 1/32, SWING 100 (a 1/64 step): x8 plays 8 hits, each in a block of its own, at GATE 1 and 127");
    check(div_units(3) / 2u / 8u / 240u >= 2u * CTL,
          "the shortest step's hit is two blocks or more at 240 BPM (8 hits fit up to ~640 BPM)");
}

/* the bits: x5..x8 never share a step with a note going on; taking a note out takes its bit */
static void t_bits(void)
{
    step_t s;
    int ok;
    uint32_t k;
    memset(&s, 0, sizeof s);
    s.n = 3, s.time = ST_NOTE;
    for (k = 0; k < 3u; k++)
        s.note[k] = (uint8_t)(60u + k);
    s.flags = SF_ACCENT | SF_CONT(0);                    /* note 0 goes on from the step before */
    step_rat_set(&s, 1, 2);                              /* x3: as before */
    ok = (step_cf(&s) & SF_CONT(0)) && step_rat_get(&s, 1) == 2u && s.rat == (2u << 2) && !(s.flags & SF_RX);
    step_rat_set(&s, 2, 6);                              /* x7: note 0 starts here now */
    ok &= (s.flags & SF_RX) && !(step_cf(&s) & SF_CONT_ALL) && step_rat_get(&s, 0) == 0u && step_rat_get(&s, 1) == 2u &&
          step_rat_get(&s, 2) == 6u && step_hits(&s) == 7u && step_has_rat(&s) && (s.flags & SF_ACCENT);
    step_rat_set(&s, 1, 4);                              /* x5: rat bits 0, still a ratchet */
    ok &= step_rat_get(&s, 1) == 4u && step_has_rat(&s);
    step_rat_set(&s, 1, 3);
    step_rat_set(&s, 2, 7);
    ok &= step_rat_get(&s, 1) == 3u && step_rat_get(&s, 2) == 7u;
    step_rat_set(&s, 2, 0);                              /* none past x4 left: SF_RX goes */
    ok &= !(s.flags & SF_RX) && step_rat_get(&s, 1) == 3u && step_rat_get(&s, 2) == 0u;
    step_rat_set(&s, 0, 9);                              /* (past x8: x8) */
    ok &= step_rat_get(&s, 0) == RAT_MAX;
    step_cont_on(&s, 1);                                 /* a note going on: x5..x8 back to x4 */
    ok &= !(s.flags & SF_RX) && step_rat_get(&s, 0) == 3u && (step_cf(&s) & SF_CONT(1));
    memset(&s, 0, sizeof s);
    s.n = 1, s.time = ST_NOTE;
    step_rat_set(&s, 0, 4);                              /* x5 alone: the rat byte is 0 */
    ok &= s.rat == 0u && step_has_rat(&s) && step_hits(&s) == 5u;
    check(ok, "bits: x5..x8 in SF_RX + SF_CONT(k), never with a note going on; x1..x4 as before; past x8: x8");
    check(sizeof(step_t) == 10u && sizeof(dstep_t) == 10u && sizeof(project_t) == 3840u,
          "a step stays 10 bytes, a project 3840 (one flash object)");
}

/* a project: x1..x8 on the notes of chords, captured, as bytes, imported, applied */
static void t_project(void)
{
    static project_t q, q2;
    static uint8_t buf[sizeof(project_t)];
    step_t want[8];
    uint32_t i, k, ok = 1;
    reset(120, 2, 8);
    for (i = 0; i < 8u; i++) {
        step_t *s = &trk[0].step[i];
        memset(s, 0, sizeof *s);
        s->n = (uint8_t)(1u + i % 4u);
        s->time = ST_NOTE;
        s->vel = 90;
        for (k = 0; k < s->n; k++) {
            s->note[k] = (uint8_t)(48u + i + 4u * k);
            step_rat_set(s, k, (i + k) % 8u);            /* every note x1..x8, x5..x8 on each step */
        }
        if (i == 3u)
            s->flags |= SF_SLIDE;
        want[i] = *s;
    }
    proj_capture(&q);
    memcpy(buf, &q, sizeof q);                           /* (the flash object) */
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    ok = proj_import(&q2, buf, (int)sizeof buf) && proj_ok(&q2);
    proj_apply(&q2, 1);
    for (i = 0; i < 8u; i++) {
        const step_t *s = &trk[0].step[i];
        ok &= memcmp(s, &want[i], sizeof *s) == 0;
        for (k = 0; k < s->n; k++)
            ok &= step_rat_get(s, k) == (i + k) % 8u;
    }
    check(ok, "project: x1..x8 on every note of a chord, saved and loaded back the same (the format's size unchanged)");
}

int main(void)
{
    t_counts();
    t_swing();
    t_nudge();
    t_fast();
    t_bits();
    t_project();
    printf("ratchet8: %d failed\n", fails);
    return fails;
}
