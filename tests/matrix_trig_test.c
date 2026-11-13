/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* Stallion matrix patching: the matrix's gates, triggers and CC sources.
 *   GATE      the key held (and a note held by the pedal) opens the cutoff; the pedal up releases it
 *   E2END     ENV 2 run out raises the pitch
 *   TRIG/SYNC (Task 4) ENV 1 restarted by LFO 1, ENV 2 started and held by a trigger, the E2END loop, the key still
 *             starts the envelopes, LFO 1 restarted by a GATE, an engine with its own envelopes untouched, the fastest
 *             loops bounded and every voice ended after key-up, unpatched sounds bit for bit
 *   CC        (Task 5) CC A and CC B as sources, SUSTAIN PATCH, the switch while the pedal is down, the smoothing,
 *             the settings' side record */
#include <stddef.h>
#include <unistd.h>
#include <sys/wait.h>
#include "editor_host.c"                             /* (the whole firmware: the UI, the settings in a flash image) */

static int fails;
static void ok(int c, const char *what)
{
    printf("matrix_trig: %-96s %s\n", what, c ? "ok" : "FAIL");
    fails += !c;
}

/* ---- rendering (as synth4_test.c) */
#define CAP (4096u * 24u)
static double cl[CAP], cr[CAP];
static uint32_t ncap;
#define RECB (CAP / CTL)
static int32_t rec_lfo[RECB];                        /* per block: track 1's LFO 1, its first voice's ENV 2 state */
static uint8_t rec_e2st[RECB];
static int32_t rec_peak;                             /* the largest |sample| of the last run */
static uint32_t rec_bad;                             /* samples not finite or beyond 2^23 */
static void run(uint32_t nb)                         /* blocks of the mix, kept from 0 */
{
    static int32_t o[CTL * 2];
    uint32_t b, i;
    ncap = 0;
    rec_peak = 0, rec_bad = 0;
    for (b = 0; b < nb; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t a = o[2 * i] < 0 ? -o[2 * i] : o[2 * i], c = o[2 * i + 1] < 0 ? -o[2 * i + 1] : o[2 * i + 1];
            a = a > c ? a : c;
            rec_peak = a > rec_peak ? a : rec_peak;
            rec_bad += a >= (1 << 23);
            if (ncap < CAP) {
                cl[ncap] = o[2 * i];
                cr[ncap++] = o[2 * i + 1];
            }
        }
        if (b < RECB) {
            uint32_t k;
            rec_lfo[b] = trk[0].lfo_val;
            rec_e2st[b] = 255;
            for (k = 0; k < NVOICE; k++)
                if (trk[0].v[k].active) {
                    rec_e2st[b] = trk[0].v[k].e2st;
                    break;
                }
        }
    }
}
static double rms(uint32_t a, uint32_t n)
{
    double e = 0;
    uint32_t i;
    for (i = a; i < a + n && i < ncap; i++)
        e += cl[i] * cl[i];
    return sqrt(e / n);
}
static double bright(uint32_t a, uint32_t n)         /* the centroid's stand-in: the first difference's share */
{
    double e = 0, d = 0;
    uint32_t i;
    for (i = a + 1; i < a + n && i < ncap; i++)
        e += cl[i] * cl[i], d += (cl[i] - cl[i - 1]) * (cl[i] - cl[i - 1]);
    return sqrt(d / (e + 1e-9));
}
static double freq(uint32_t a, uint32_t n)           /* rising zero crossings (low-passed) a second */
{
    double lp = 0, prev = 0, first = -1, last = -1, k = 0;
    uint32_t i;
    for (i = a; i < a + n && i < ncap; i++) {
        lp += (cl[i] - lp) * 0.2;
        if (prev < 0 && lp >= 0) {
            if (first < 0)
                first = i;
            else
                k++;
            last = i;
        }
        prev = lp;
    }
    return k > 0 ? k * FS / (last - first) : 0;
}
static uint32_t ms(double m) { return (uint32_t)(m * FS / 1000.0); }
static uint32_t blocks(double m) { return (uint32_t)(m * FS / 1000.0 / CTL + 0.5); }
static void quiet(void)
{
    uint32_t i;
    for (i = 0; i < NPART; i++) {
        trk_all_off(&trk[i]);
        part_ctl[i].sus = 0;
    }
    run(400);
    for (i = 0; i < NPART; i++) {
        uint32_t k;
        for (k = 0; k < NVOICE; k++)
            trk[i].v[k].active = trk[i].v[k].gate = 0, trk[i].v[k].stage = 0, trk[i].v[k].env = trk[i].v[k].env_out = 0;
    }
}
static void slots_off(track_t *t)
{
    uint32_t k;
    for (k = 0; k < MX_SLOTS; k++)
        t->p[mx_pid(k)] = MS_OFF, t->p[mx_pid(k) + 1u] = MT_OFF, t->p[mx_pid(k) + 2u] = 0;
}
static void slot(track_t *t, uint32_t k, uint32_t src, uint32_t dst, int32_t amt)
{
    t->p[mx_pid(k)] = (int16_t)src, t->p[mx_pid(k) + 1u] = (int16_t)dst, t->p[mx_pid(k) + 2u] = (int16_t)amt;
}
/* ANALOG, plain: a saw (E0 0) or a sine (3) through the filter at cut, no effects, no modulation, full sustain */
static void analog(uint32_t wave, int32_t cut)
{
    track_t *t = &trk[0];
    quiet();
    host_preset(t, 0u, 0);
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    t->p[P_ED_FX] = -9;
    t->p[P_E0] = (int16_t)wave, t->p[P_E2] = 0, t->p[P_E3] = 0, t->p[P_E4] = (int16_t)cut;
    t->p[P_ED_PIT] = t->p[P_LD_PIT] = t->p[P_LD_FLT] = t->p[P_LD_SHP] = t->p[P_LD_AMP] = t->p[P_ED_FLT] = 0;
    t->p[P_ATK] = 0, t->p[P_SUS] = 127, t->p[P_REL] = 10, t->p[P_VELF] = 0, t->p[P_VOICE] = V_POLY;
    t->p[P_MWDST] = t->p[P_ATDST] = MD_OFF, t->p[P_PAN] = 0, t->p[P_GLIDE] = 0;
    t->p[P_E2LOOP] = 0, t->p[P_LFADE] = 0;
    slots_off(t);
    part_ctl[0].mw = part_ctl[0].at = 0, part_ctl[0].bend = 0, part_ctl[0].sus = 0;
    run(20);
}
static voice_t *voice_of(track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].note == note)
            return &t->v[i];
    return 0;
}
static uint32_t active_n(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active != 0;
    return n;
}

/* ---- Task 3: GATE and E2END */
static void t_gate(void)
{
    track_t *t = &trk[0];
    char w[200];
    double off, on, ped;
    voice_t *v;
    analog(0, 20);
    trk_note_on(t, 48, 100), run(blocks(300)), off = bright(ms(150), ms(100));
    trk_note_off(t, 48), quiet();
    analog(0, 20);
    slot(t, 0, MS_GATE, MT_CUT, 63);
    trk_note_on(t, 48, 100), run(blocks(300)), on = bright(ms(150), ms(100));
    snprintf(w, sizeof w, "GATE > CUT +63: a held note brighter (x%.2f than with the slot off)", on / off);
    ok(on > off * 1.2, w);
    part_ctl_cc(t, 64, 127);                         /* the pedal down, the key up: held, still bright */
    trk_note_off(t, 48), run(blocks(200)), ped = bright(ms(100), ms(100));
    v = voice_of(t, 48);
    snprintf(w, sizeof w, "GATE: the pedal down, the key up: still bright (x%.2f of held, x%.2f of no slot), the voice held",
             ped / on, ped / off);
    ok(ped > on * 0.9 && ped > off * 1.2 && v && v->gate && v->stage < 3u, w);
    part_ctl_cc(t, 64, 0), run(1);                   /* the pedal up: the release */
    v = voice_of(t, 48);
    snprintf(w, sizeof w, "GATE: the pedal up: the release starts (stage %u, gate %u)", v ? v->stage : 0u, v ? v->gate : 0u);
    ok(!v || (v->stage == 3u && !v->gate), w);
    quiet();
}
static uint32_t ph_step(track_t *t, uint32_t note)  /* the voice's oscillator phase over one block: its pitch */
{
    voice_t *v = voice_of(t, note);
    uint32_t a = v ? v->ph[0] : 0u;
    run(1);
    v = voice_of(t, note);
    return v ? v->ph[0] - a : 0u;
}
static void t_e2end(void)
{
    track_t *t = &trk[0];
    char w[200];
    uint32_t a, b;
    double st;
    analog(3, 127);
    t->p[P_E2ATK] = 0, t->p[P_E2DEC] = 10, t->p[P_E2SUS] = 0, t->p[P_E2REL] = 0;
    slot(t, 0, MS_E2END, MT_PITCH, 63);
    trk_note_on(t, 45, 100), run(1);
    a = ph_step(t, 45);                              /* (D 10 runs out in ~4.5 ms: the pitch read at ~1.5 ms) */
    run(blocks(300));
    b = ph_step(t, 45);
    st = a && b ? 12.0 * log2((double)b / a) : 0;
    snprintf(w, sizeof w, "E2END > PITCH +63: up once ENV 2 has run out (%+.1f st at 300 ms against the start)", st);
    ok(st > 6.0, w);
    trk_note_off(t, 45), quiet();
}

/* ---- Task 4: TRIG and SYNC inputs */
static int32_t time_idx(double m)                    /* the TIME index nearest m ms */
{
    int32_t i, b = 0;
    for (i = 0; i < 128; i++)
        if (fabs(TIME_MS_X10[i] / 10.0 - m) < fabs(TIME_MS_X10[b] / 10.0 - m))
            b = i;
    return b;
}
static int32_t lfo_idx(double hz)                    /* the LFO rate nearest hz */
{
    int32_t i, b = 0;
    for (i = 0; i < 128; i++)
        if (fabs(LFO_HZ_X100[i] / 100.0 - hz) < fabs(LFO_HZ_X100[b] / 100.0 - hz))
            b = i;
    return b;
}
static uint32_t minima(uint32_t a, uint32_t n, uint32_t win)   /* the level's dips (5 % down, then 5 % up) */
{
    uint32_t i, k = 0, falling = 0;
    double hi = 0, lo = 1e18;
    for (i = a; i + win <= a + n && i + win <= ncap; i += win) {
        double x = rms(i, win);
        if (!falling) {
            hi = x > hi ? x : hi;
            if (x < hi * 0.95)
                falling = 1, lo = x;
        } else {
            lo = x < lo ? x : lo;
            if (x > lo * 1.05)
                falling = 0, hi = x, k++;
        }
    }
    return k;
}
static void t_retrig1(void)
{
    track_t *t = &trk[0];
    char w[200];
    uint32_t a, b;
    analog(3, 127);
    t->p[P_ATK] = (int16_t)time_idx(200), t->p[P_DEC] = (int16_t)time_idx(150), t->p[P_SUS] = 40;
    t->p[P_LWAVE] = LW_SQR, t->p[P_LRATE] = (int16_t)lfo_idx(4);
    trk_note_on(t, 69, 100), run(blocks(1500));
    a = minima(ms(400), ms(1000), 1002u);   /* (10 cycles of A4 a window) */
    trk_note_off(t, 69), quiet();
    slot(t, 0, MS_LFO1, MT_TRIG1, 63);
    trk_note_on(t, 69, 100), run(blocks(1500));
    b = minima(ms(400), ms(1000), 1002u);
    snprintf(w, sizeof w, "LFO 1 square 4 Hz > TRIG1, ATK 200 ms: the level rises again (%u dips in 1 s; no slot %u)", b, a);
    ok(b >= 3u && a == 0u, w);
    trk_note_off(t, 69), run(blocks(500));
    snprintf(w, sizeof w, "TRIG1: the key up, the voice ends within the release (%u active after 0.5 s)", active_n(t));
    ok(active_n(t) == 0u, w);
    quiet();
}
static void t_trig2_pulse(void)
{
    track_t *t = &trk[0];
    char w[200];
    uint32_t b, nb = blocks(2200), n = 0, from = 0, k, pairs = 0;
    int inhi;
    double val[16], r = 1e9;
    int sg[16];
    analog(0, 20);
    t->p[P_E2ATK] = 1, t->p[P_E2DEC] = 40, t->p[P_E2SUS] = 100, t->p[P_E2REL] = 60;
    t->p[P_LWAVE] = LW_SQR, t->p[P_LRATE] = (int16_t)lfo_idx(1.5);
    slot(t, 0, MS_ENV2, MT_CUT, 63);
    slot(t, 1, MS_LFO1, MT_TRIG2, 63);
    trk_note_on(t, 48, 100), run(nb);
    inhi = rec_lfo[0] > 0;                           /* each half of the LFO's cycle: the brightness of its last third */
    for (b = 1; b < nb && b < RECB && n < 16u; b++)
        if ((rec_lfo[b] > 0) != inhi) {
            uint32_t len = b - from;
            if (from) {                              /* (the first half, from the note-on, left out) */
                val[n] = bright((from + len * 2u / 3u) * CTL, (len / 3u) * CTL);
                sg[n++] = inhi;
            }
            inhi = !inhi, from = b;
        }
    for (k = 0; k + 1u < n; k++)                     /* a high half, the low half after it */
        if (sg[k] && !sg[k + 1u]) {
            double x = val[k] / (val[k + 1u] + 1e-9);
            r = x < r ? x : r;
            pairs++;
        }
    snprintf(w, sizeof w, "LFO 1 square > TRIG2, ENV 2 > CUT: open in each high half, closed in the low (%u cycles, min x%.2f)",
             pairs, r);
    ok(pairs >= 2u && r > 1.3, w);
    trk_note_off(t, 48), quiet();
}

static void t_trig2_loop(void)
{
    track_t *t = &trk[0];
    char w[200];
    uint32_t b, nb = blocks(1000), cyc = 0;
    analog(0, 40);
    t->p[P_E2ATK] = 5, t->p[P_E2DEC] = 5, t->p[P_E2SUS] = 64, t->p[P_E2REL] = 5;
    slot(t, 0, MS_ENV2, MT_CUT, 40);
    slot(t, 1, MS_E2END, MT_TRIG2, 63);
    trk_note_on(t, 48, 100), run(nb);
    for (b = 1; b < nb; b++)                         /* each new start after the end: a whole cycle */
        cyc += rec_e2st[b] == 1u && rec_e2st[b - 1u] != 1u;
    snprintf(w, sizeof w, "E2END > TRIG2, A = D = R 5: ENV 2 cycles while the key is held (%u cycles in 1 s)", cyc);
    ok(cyc >= 4u, w);
    trk_note_off(t, 48), run(blocks(1000));
    snprintf(w, sizeof w, "E2END > TRIG2: the key up, the voice ends (%u active after 1 s)", active_n(t));
    ok(active_n(t) == 0u, w);
    quiet();
}
static void t_key_starts(void)
{
    track_t *t = &trk[0];
    char w[200];
    voice_t *v;
    int32_t e2;
    analog(3, 127);
    t->p[P_E2ATK] = (int16_t)time_idx(20), t->p[P_E2DEC] = 40, t->p[P_E2SUS] = 64, t->p[P_E2REL] = 20;
    slot(t, 0, MS_MW, MT_TRIG2, 63);                 /* (the wheel at 0: the input never high) */
    slot(t, 1, MS_ENV2, MT_CUT, 10);
    part_ctl[0].mw = 0;
    trk_note_on(t, 57, 100), run(blocks(12));       /* (ATK 20 ms: ENV 2 half way up at 12 ms) */
    v = voice_of(t, 57);
    e2 = v ? v->env2 : 0;
    run(blocks(60));
    snprintf(w, sizeof w, "TRIG2 patched from a source at 0: the key still sounds the note and starts ENV 2 (level %.0f, "
             "ENV 2 %.2f at 12 ms)", rms(ms(30), ms(20)), e2 / 16777216.0);
    ok(voice_of(t, 57) && rms(ms(30), ms(20)) > 300 && e2 > (1 << 22), w);
    trk_note_off(t, 57), quiet();
}
static void t_sync1(void)
{
    track_t *t = &trk[0];
    char w[200];
    int32_t a, b, c;
    uint32_t nb = blocks(10);
    analog(3, 127);
    t->p[P_LWAVE] = LW_SIN, t->p[P_LRATE] = (int16_t)lfo_idx(1.3);
    slot(t, 0, MS_GATE, MT_SYNC1, 63);
    trk_note_on(t, 57, 100), run(nb), a = trk[0].lfo_val;
    run(blocks(333) - nb);
    trk_note_on(t, 64, 100), run(nb), b = trk[0].lfo_val;   /* (the first note still held) */
    trk_note_off(t, 57), trk_note_off(t, 64), quiet();
    slots_off(t);                                    /* without the slot: the second note in another phase */
    trk_note_on(t, 57, 100), run(blocks(333));
    trk_note_on(t, 64, 100), run(nb), c = trk[0].lfo_val;
    trk_note_off(t, 57), trk_note_off(t, 64), quiet();
    snprintf(w, sizeof w, "GATE > SYNC1: two notes 333 ms apart start LFO 1 in the same phase (%d, %d; no slot %d)", a, b, c);
    ok(abs(a - b) < 328 && abs(a - c) > 3277, w);
}
/* the part rendered from the same state twice, each in a fork()ed child (every engine's own state, the FX, the noise
 * seeds as they were): as it is, and with add()'s slots; 1: the same samples */
static uint64_t render_hash(track_t *t, uint32_t nb, void (*add)(track_t *))
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    fflush(stdout);
    if (pipe(fd))
        return 0;
    pid = fork();
    if (pid == 0) {
        static int32_t o[CTL];
        uint32_t i, k;
        h = 1469598103934665603ull;
        if (add)
            add(t);
        for (i = 0; i < nb; i++) {
            track_render(t, o, CTL);
            for (k = 0; k < CTL; k++)
                h = (h ^ (uint32_t)o[k]) * 1099511628211ull;
        }
        if (write(fd[1], &h, sizeof h) != (ssize_t)sizeof h)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (pid < 0 || read(fd[0], &h, sizeof h) != (ssize_t)sizeof h)
        h = 0;
    close(fd[0]);
    if (pid > 0)
        waitpid(pid, 0, 0);
    return h;
}
static int same_render(track_t *t, uint32_t nb, void (*add)(track_t *))
{
    uint64_t a = render_hash(t, nb, 0), b = render_hash(t, nb, add);
    return a && a == b;
}
static uint32_t free_slot(const track_t *t)
{
    uint32_t k;
    for (k = 0; k < MX_SLOTS; k++)
        if (t->p[mx_pid(k)] == MS_OFF || t->p[mx_pid(k) + 2u] == 0)
            return k;
    return MX_SLOTS - 1u;
}
static void add_trig1(track_t *t) { slot(t, free_slot(t), MS_LFO1, MT_TRIG1, 63); }
static void add_low(track_t *t)                      /* TRIG1 / SYNC1 / SYNC2 cables under the edge level */
{
    slot(t, free_slot(t), MS_DC, MT_TRIG1, 10);
    slot(t, free_slot(t), MS_DC, MT_SYNC1, 10);
    slot(t, free_slot(t), MS_DC, MT_SYNC2, 10);
}
static void t_trig_fm6(void)
{
    track_t *t = &trk[0];
    char w[200];
    uint32_t pi, bad = 0;
    for (pi = 0; pi < ENGINES[ENGI_FM6]->npresets; pi += 7u) {
        quiet();
        host_preset(t, ENGI_FM6, pi);
        slots_off(t);
        t->p[P_LWAVE] = LW_SQR, t->p[P_LRATE] = (int16_t)lfo_idx(8);
        trk_note_on(t, 48, 100), trk_note_on(t, 55, 90), run(2);
        bad += !same_render(t, 400, add_trig1);
        trk_note_off(t, 48), trk_note_off(t, 55);
    }
    quiet();
    snprintf(w, sizeof w, "FM6 (its own envelopes): LFO 1 > TRIG1 ignored, bit for bit as without (%u presets differ)", bad);
    ok(!bad, w);
}
static void t_trig_bounded(void)
{
    track_t *t = &trk[0];
    char w[200];
    uint32_t e, k, bad = 0, hang = 0, n = 0;
    int32_t pk = 0;
    static const uint8_t SRC[12] = {MS_E2END, MS_E2END, MS_E2END, MS_E2END, MS_LFO1, MS_LFO2, MS_NOISE, MS_E2END,
                                    MS_LFO1, MS_NOISE, MS_LFO2, MS_E2END};
    static const uint8_t DST[12] = {MT_TRIG2, MT_TRIG1, MT_SYNC1, MT_SYNC2, MT_TRIG1, MT_TRIG2, MT_TRIG2, MT_TRIG2,
                                    MT_SYNC1, MT_SYNC2, MT_TRIG1, MT_TRIG1};
    for (e = 0; e < NENGINES; e++) {
        if (!ENGINES[e]->npresets || e == ENGI_HYBRID)
            continue;
        quiet();
        host_preset(t, e, 0);
        slots_off(t);
        for (k = 0; k < MX_SLOTS; k++)
            slot(t, k, SRC[k % 12u], DST[k % 12u], k & 1u ? 63 : 40);
        t->p[P_ATK] = t->p[P_DEC] = t->p[P_REL] = 0;
        t->p[P_E2ATK] = t->p[P_E2DEC] = t->p[P_E2REL] = 0;
        t->p[P_LRATE] = 127, t->p[P_L2RATE] = 127, t->p[P_LWAVE] = LW_SQR, t->p[P_L2WAVE] = LW_SQR;
        t->p[P_VOICE] = V_POLY;
        for (k = 0; k < 8u; k++)
            trk_note_on(t, 40u + k * 5u, 100);
        run(blocks(2000));
        bad += rec_bad, pk = rec_peak > pk ? rec_peak : pk;
        for (k = 0; k < 8u; k++)
            trk_note_off(t, 40u + k * 5u);
        run(blocks(1000));
        bad += rec_bad;
        hang += active_n(t) != 0u;
        n++;
    }
    quiet();
    slots_off(t);
    snprintf(w, sizeof w, "12 slots of E2END / LFO / NOISE into TRIG / SYNC, times 0, 8 notes 2 s, %u engines: bounded (peak "
             "%d), every voice ended 1 s after key-up (%u engines hung)", n, pk, hang);
    ok(!bad && !hang && n > 10u, w);
}
static void t_unpatched(void)
{
    track_t *t = &trk[0];
    char w[200];
    uint32_t e, bad = 0, n = 0;
    for (e = 0; e < NENGINES; e++) {
        if (!ENGINES[e]->npresets)
            continue;
        quiet();
        host_preset(t, e, 0);
        slots_off(t);                                /* (a factory sound has none; host_preset leaves the last test's) */
        trk_note_on(t, 48, 100), trk_note_on(t, 60, 80), run(2);
        bad += !same_render(t, 300, add_low);
        trk_note_off(t, 48), trk_note_off(t, 60);
        n++;
    }
    quiet();
    snprintf(w, sizeof w, "every engine's preset 0: TRIG1 / SYNC1 / SYNC2 cables never past the level, bit for bit as without "
             "(%u of %u differ)", bad, n);
    ok(!bad, w);
}

/* ---- Task 5: CC A, CC B, SUSTAIN */
static void cc_defaults(void)
{
    uint32_t i;
    midi_cca = 64, midi_ccb = 4, midi_sus_patch = 0;
    for (i = 0; i < NPART; i++)
        part_ctl[i].cca = part_ctl[i].ccb = 0, part_ctl[i].cca_s = part_ctl[i].ccb_s = 0, part_ctl[i].sus = 0;
}
static void t_cca(void)
{
    track_t *t = &trk[0];
    char w[200];
    double a, b;
    voice_t *v;
    cc_defaults();
    analog(0, 20);
    slot(t, 0, MS_CCA, MT_CUT, 63);
    trk_note_on(t, 48, 100), run(blocks(200)), a = bright(ms(150), 674u);   /* (two cycles of C3) */
    midi_cc(t, 64, 127), run(blocks(21)), b = bright(ms(5), 674u);         /* (from 5 to 20 ms) */
    trk_note_off(t, 48), run(4);
    v = voice_of(t, 48);
    snprintf(w, sizeof w, "CC A (64) > CUT +63: the pedal brightens a held note within 20 ms (x%.2f), and holds it (SUSTAIN ON)",
             b / a);
    ok(b > a * 1.2 && v && v->gate && v->stage < 3u, w);
    midi_cc(t, 64, 0), quiet();
}
static double pan_energy(track_t *t, uint32_t src)  /* a held note panned hard right by src at 127: L, R energy */
{
    double e = 0;
    uint32_t i;
    analog(3, 127);
    slot(t, 0, src, MT_PAN, 63);
    if (src == MS_MW)
        part_ctl[0].mw = 127;
    else
        midi_cc(t, 4, 127);
    trk_note_on(t, 60, 100), run(blocks(400));
    for (i = ms(300); i < ms(350); i++)
        e += cl[i] * cl[i] + cr[i] * cr[i];
    trk_note_off(t, 60), quiet();
    part_ctl[0].mw = 0;
    midi_cc(t, 4, 0);
    return e;
}
static void t_ccb(void)
{
    track_t *t = &trk[0];
    char w[200];
    double el = 0, er = 0, eb, em;
    uint32_t i;
    int16_t lv;
    cc_defaults();
    eb = pan_energy(t, MS_CCB);
    em = pan_energy(t, MS_MW);                       /* (the wheel: a control with no job on the level) */
    analog(3, 127);
    lv = t->p[P_LEVEL];
    slot(t, 0, MS_CCB, MT_PAN, 63);
    midi_cc(t, 4, 127);
    trk_note_on(t, 60, 100), run(blocks(400));
    for (i = ms(300); i < ms(350); i++)
        el += cl[i] * cl[i], er += cr[i] * cr[i];
    snprintf(w, sizeof w, "CC B (4) > PAN +63: CC 4 127 pans right (L/R %.3f), the level as MW > PAN's (x%.3f), no expression cut",
             sqrt(el / (er + 1)), sqrt(eb / (em + 1)));
    ok(sqrt(el / (er + 1)) < 0.1 && part_ctl[0].exoff == 0 && t->p[P_LEVEL] == lv && fabs(sqrt(eb / (em + 1)) - 1) < 0.01, w);
    trk_note_off(t, 60), midi_cc(t, 4, 0), quiet();
}
static void t_sus_patch(void)
{
    track_t *t = &trk[0];
    char w[200];
    double a, b;
    voice_t *v;
    cc_defaults();
    midi_sus_patch = 1;
    analog(0, 20);
    slot(t, 0, MS_CCA, MT_CUT, 63);
    trk_note_on(t, 48, 100), run(blocks(200)), a = bright(ms(150), 674u);   /* (two cycles of C3) */
    midi_cc(t, 64, 127), run(blocks(21)), b = bright(ms(5), 674u);         /* (from 5 to 20 ms) */
    trk_note_off(t, 48), run(1);
    v = voice_of(t, 48);
    snprintf(w, sizeof w, "SUSTAIN PATCH: CC 64 down, the key up: the note releases (stage %u), CC A still brightens (x%.2f)",
             v ? v->stage : 0u, b / a);
    ok((!v || v->stage == 3u) && !part_ctl[0].sus && b > a * 1.2, w);
    midi_cc(t, 64, 0), quiet();
    cc_defaults();
}
static void t_sus_switch(void)
{
    track_t *t = &trk[0];
    char w[200];
    uint32_t k;
    cc_defaults();
    analog(0, 60);
    midi_cc(t, 64, 127);                             /* the pedal down, notes held */
    for (k = 0; k < 4u; k++)
        trk_note_on(t, 48u + k * 4u, 100);
    run(blocks(100));
    midi_sus_patch = 1;                              /* SUSTAIN to PATCH, the pedal still down */
    for (k = 0; k < 4u; k++)
        trk_note_off(t, 48u + k * 4u);
    run(blocks(100));
    midi_cc(t, 64, 0);                               /* the pedal up */
    run(blocks(500));
    snprintf(w, sizeof w, "SUSTAIN to PATCH with the pedal down and notes held, keys up, pedal up: no hung note (%u active)",
             active_n(t));
    ok(active_n(t) == 0u && !part_ctl[0].sus, w);
    quiet();
    cc_defaults();
}
static void t_cc_smooth(void)
{
    track_t *t = &trk[0];
    char w[200];
    int32_t s1, s8;
    uint32_t b;
    cc_defaults();
    analog(3, 127);
    slot(t, 0, MS_CCA, MT_CUT, 10);
    run(20);
    midi_cc(t, 64, 127);
    run(1), s1 = part_ctl[0].cca_s;
    for (b = 1; b < 8u; b++)
        run(1);
    s8 = part_ctl[0].cca_s;
    snprintf(w, sizeof w, "CC smoothing: a step 0 -> 127, %.0f %% after 1 block, %.0f %% after 8", s1 * 100.0 / 32766,
             s8 * 100.0 / 32766);
    ok(s1 < 32766 * 9 / 10 && s1 > 0 && s8 >= 32766 * 9 / 10, w);
    midi_cc(t, 64, 0), quiet();
    cc_defaults();
}
static void set_item(uint32_t id, int32_t v);
static void t_cc_reset(void)                       /* CC 121 and a project load / NEW: CC A, CC B back to 0 */
{
    track_t *t = &trk[0];
    char w[200];
    uint32_t k;
    int z[2];
    cc_defaults();
    for (k = 0; k < 2u; k++) {
        midi_cc(t, 64, 127);                         /* CC A on 64: the pedal down; CC B (4) up */
        midi_cc(t, 4, 100);
        run(4);
        if (k == 0)
            midi_cc(t, 121, 0);                      /* reset all controllers */
        else
            part_ctl_reset();                        /* (project.c's load, NEW) */
        run(1);
        z[k] = part_ctl[0].cca == 0 && part_ctl[0].ccb == 0 && part_ctl[0].cca_s == 0 && part_ctl[0].ccb_s == 0 &&
               !part_ctl[0].sus;
    }
    snprintf(w, sizeof w, "CC A (64, pedal down) and CC B read 0 after CC 121 (%s) and after a project load (%s)",
             z[0] ? "yes" : "no", z[1] ? "yes" : "no");
    ok(z[0] && z[1], w);
    midi_cc(t, 64, 127), midi_cc(t, 4, 100);         /* CC A, CC B moved to other numbers: their values go */
    set_item(X_MCCA, 64);                            /* (the same number: kept) */
    z[0] = part_ctl[0].cca == 127;
    set_item(X_MCCA, 20), set_item(X_MCCB, 21);
    z[1] = part_ctl[0].cca == 0 && part_ctl[0].ccb == 0;
    snprintf(w, sizeof w, "CC A, CC B: the value kept on the same number (%s), 0 when the number changes (%s)",
             z[0] ? "yes" : "no", z[1] ? "yes" : "no");
    ok(z[0] && z[1], w);
    midi_cc(t, 64, 0);
    set_item(X_MCCA, 64), set_item(X_MCCB, 4);
    quiet();
    cc_defaults();
}
static void set_item(uint32_t id, int32_t v)       /* (as touch_test.c: a value set on GLO > MIDI 3) */
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
static void t_midi_side(void)
{
    char w[200];
    persist_t p;
    int a, b, c;
    set_item(X_MCCA, 20), set_item(X_MCCB, 21), set_item(X_MSUS, 1);
    settings_save();
    midi_cca = 99, midi_ccb = 98, midi_sus_patch = 0;   /* (the RAM gone) */
    persist_boot();
    a = midi_cca == 20 && midi_ccb == 21 && midi_sus_patch == 1 && get_item(X_MCCA) == 20 && get_item(X_MSUS) == 1;
    persist_fill(&p);                                /* a side record without the block (a save before it) */
    side_fill(&side_out);
    st_side = &side_out;
    st_side_len = offsetof(side2_t, md);
    (void)st_save3(OBJ_SETTINGS, &p, sizeof p, &dsu, sizeof dsu, set_tail, set_tail_n);
    st_side = 0;
    persist_boot();
    b = midi_cca == 64 && midi_ccb == 4 && midi_sus_patch == 0;
    {   /* the value shows the CC's other job */
        const param_desc_t *d;
        int16_t *vp;
        set_item(X_MCCA, 64), d = syn_desc(X_MCCA, &vp);
        c = d->unit && !strcmp(d->unit, "SUST");
        set_item(X_MCCB, 4), d = syn_desc(X_MCCB, &vp);
        c &= !d->unit;
        set_item(X_MCCB, 11), d = syn_desc(X_MCCB, &vp);
        c &= d->unit && !strcmp(d->unit, "VOL");
    }
    snprintf(w, sizeof w, "settings: CC A 20, CC B 21, PATCH back after a power-off (%s); an older record: 64 / 4 / ON (%s); "
             "the CC's job shown (%s)", a ? "yes" : "no", b ? "yes" : "no", c ? "yes" : "no");
    ok(a && b && c && sizeof(side2_t) <= ST_SIDE_MAX, w);
    settings_save();
    cc_defaults();
}

/* MENU > SEQ > PARA ASSIGN, PARA ROUND ROBIN (eng_mare.c mar_para): kept with the settings (the side
 * record's BROWSE part), back after a power-off; a fresh install (no settings at all) and an older record without the
 * part read PLAYED, OFF; the editor's scope 3 sets and reads them */
static void t_para_side(void)
{
    char w[240];
    persist_t p;
    int a, b, c, d, e;
    int16_t v = -1;
    d = para_assign == PA_PLAYED && para_rr == 0;    /* (as the program started: a cold boot's zeroed RAM) */
    para_assign = PA_HIGH, para_rr = 1;
    settings_save();
    para_assign = 0, para_rr = 0;                    /* (the RAM gone) */
    persist_boot();
    a = para_assign == PA_HIGH && para_rr == 1;
    ed_sput(ED_S_PASSIGN, PA_LOW), ed_sput(ED_S_PRR, 0);
    settings_save();
    para_assign = 2, para_rr = 1;
    persist_boot();
    b = para_assign == PA_LOW && para_rr == 0 && ed_sget(ED_S_PASSIGN, &v) && v == PA_LOW;
    para_assign = PA_HIGH, para_rr = 1;
    persist_fill(&p);                                /* a side record without the part (a save before it) */
    side_fill(&side_out);
    st_side = &side_out;
    st_side_len = offsetof(side2_t, br);
    (void)st_save3(OBJ_SETTINGS, &p, sizeof p, &dsu, sizeof dsu, set_tail, set_tail_n);
    st_side = 0;
    persist_boot();
    c = para_assign == PA_PLAYED && para_rr == 0;
    memset(nor, 0xFF, sizeof nor);                   /* a fresh install: the flash erased, the RAM zeroed */
    para_assign = 0, para_rr = 0;
    persist_boot();
    d &= para_assign == PA_PLAYED && para_rr == 0;
    ed_sput(ED_S_PASSIGN, 7);                        /* (clamped) */
    e = para_assign == PA_HIGH;
    snprintf(w, sizeof w, "settings: PARA ASSIGN HIGH, ROUND ROBIN ON back after a power-off (%s); LOW, OFF from the "
             "editor (%s); an older record: PLAYED, OFF (%s); a fresh install: PLAYED, OFF (%s); SET clamps (%s)",
             a ? "yes" : "no", b ? "yes" : "no", c ? "yes" : "no", d ? "yes" : "no", e ? "yes" : "no");
    ok(a && b && c && d && e, w);
    para_assign = PA_PLAYED, para_rr = 0;
    settings_save();
}

static void t_midi3_page(void)
{
    char w[200];
    uint32_t i, found = 0, good = 0;
    for (i = 0; i < NPAGES; i++)
        if (!strcmp(PAGES[i].title, "MIDI 3")) {
            int16_t *vp;
            const param_desc_t *a = page_desc(&PAGES[i], 0, &vp), *b = page_desc(&PAGES[i], 1, &vp),
                               *c = page_desc(&PAGES[i], 2, &vp);
            found++;
            good = PAGES[i].fam == FAM_GLO && PAGES[i].scope == SC_SYN && !page_hidden(i) && a && b && c &&
                   !strcmp(a->label, "CC A") && !strcmp(b->label, "CC B") && !strcmp(c->label, "SUST") &&
                   PAGES[i - 1u].fam == FAM_GLO && !strcmp(PAGES[i - 1u].title, "MIDI 2") && ed_sidx(X_MSUS) >= 0;
        }
    snprintf(w, sizeof w, "GLO > MIDI 3 after MIDI 2: CC A, CC B, SUST, reachable, in the editor's settings (%u found)", found);
    ok(found == 1u && good, w);
}

int main(void)
{
    harness_init();
    clipper_proj_neutral();
    trk[1].p[P_LYMODE] = LY_OFF;
    t_gate();
    t_e2end();
    t_retrig1();
    t_trig2_pulse();
    t_trig2_loop();
    t_key_starts();
    t_sync1();
    t_trig_fm6();
    t_trig_bounded();
    t_unpatched();
    t_cca();
    t_ccb();
    t_sus_patch();
    t_sus_switch();
    t_cc_smooth();
    t_cc_reset();
    t_midi_side();
    t_para_side();
    t_midi3_page();
    printf("matrix_trig: %s (%d failed)\n", fails ? "FAIL" : "all ok", fails);
    return fails != 0;
}
