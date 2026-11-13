/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* Voices given up on a long release (CLIPPER_SYNTH 1):  build/host/quiet_steal_test   (VERBOSE=1: every voice given up)
 * Chords played on the SMK-37 (PONY PAD: POLY, ATK 70 DEC 100 SUS 115 REL 95, a triad every ~0.8 s, each let go
 * before the next), as captured from the keyboard (SEQ, ms / note / velocity, 0 = off), replayed on part 1:
 *   CPU      the SMK-37's load as GLO > SYSTEM showed it (a held triad 69 %: the render with no voice 30 %, a voice
 *            13 %, a fading one too), each half (8 blocks) judged as audio.c judges it (shed_voice at a half's start,
 *            shed_judge at its end). On the chip the tails of the last chord took the render over 85 % as the next
 *            chord came: each was given up over 1.5 ms (KILL_BLOCKS), a tick at every chord change
 *   FAST     the same chords twice as fast, the load unbounded: the part's 8 voices run out on loud tails (its own
 *            voice taken: faded over OWN_BLOCKS, its note then from silence)
 *   LAYER    FAST with part 2 holding four notes: the budget of 8 runs out while part 1 has free voices (a tail
 *            given up, the note at once on a free voice)
 * Checks: every voice louder than -42 dB given up (budget, overload) fades out over at least STEAL_MIN_MS, one of the
 * part's own taken for a note over OWN_BLOCKS, never a step down of more than its fade's step a block (a cut); no held
 * note given up while a released one sounds; no voice given up while the load is under 85 % without it (CPU: no tail
 * lost for nothing); a note that takes a loud voice starts on it from silence once it has faded (not at the tail's
 * level, past its attack); no voice given up inside the render (a note's fade ending takes no second voice: one
 * victim a note-on); onsets: no note waits while its part has a free voice (LAYER: a chord's notes and every arp step
 * start in the block they come), on a full part at most OWN_BLOCKS. A held 8-note chord on a load of 134 % (HEAVY)
 * has no more late halves (over 100 %) than main's shedder (1.5 ms fades, every half judged). FM6 (its envelope
 * pinned while released): of equally loud tails the oldest is taken (the budget, the overload, the part's own). A
 * note waiting for its voice's fade never starts after all-off, an engine switch or a change to MONO. A triad from
 * silence gives nothing up. Exit status: failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
/* (Stallion 1.0) the pad these checks play: PONY's factory bank has no PONY PAD now; WARM PAD is its closest (the same
 * waves, pulse width, sub, HPF and chorus, a little darker and slower) */
#define PONY_PAD "WARM PAD"
#include <math.h>

#define STEAL_MIN_MS 5.0
#define LOAD_BASE 30                                 /* % of a half: the render with no voice (SMK-37, PONY PAD) */
#define LOAD_VOICE 13                                /* % a voice */
#define LOUD (1 << 17)                               /* -42 dB */

typedef struct { uint16_t ms; uint8_t note, vel; } ev_t;
static const ev_t SEQ[] = {
    {195,62,0},{200,65,0},{235,69,0},{375,71,95},{375,64,95},{387,67,90},{995,64,0},
    {1012,67,0},{1041,71,0},{1198,72,95},{1204,65,95},{1209,69,86},{1749,69,0},{1755,72,0},
    {1772,65,0},{2185,67,95},{2198,64,102},{2198,60,102},{2753,60,0},{2782,64,0},{2811,67,0},
    {2997,69,102},{3015,65,102},{3015,62,95},{3548,62,0},{3571,65,0},{3594,69,0},{3809,71,90},
    {3821,67,95},{3821,64,90},{4314,64,0},{4325,67,0},{4348,71,0},{4523,72,102},{4529,69,111},
    {4535,65,102},{5428,69,0},{5439,65,0},{5439,72,0},{5725,67,95},{5736,64,102},{5747,60,90},
    {5985,60,0},{5996,64,0},{6025,67,0},{6194,69,102},{6194,62,95},{6200,65,111},{6449,62,0},
    {6466,65,0},{6496,69,0},{6664,67,102},{6664,71,95},{6671,64,90},{7023,64,0},{7035,67,0},
    {7064,71,0},{7250,72,95},{7262,69,102},{7268,65,90},{7900,69,0},{7910,65,0},{7917,72,0},
    {8759,64,11},{8817,60,1},{8846,67,5},{8955,64,0},{8961,60,0},{8976,67,0},{9364,64,49},
    {9381,67,30},{9387,60,33},{9496,60,0},{9506,64,0},{9518,67,0},{9775,65,69},{9775,69,61},
    {9787,62,58},{9925,62,0},{9936,65,0},{9947,69,0},{10203,71,61},{10215,64,55},{10220,67,61},
    {10366,67,0},{10366,64,0},{10389,71,0},{10627,72,65},{10633,69,78},{10639,65,55},{10789,69,0},
    {10795,65,0},{10806,72,0},{11057,74,69},{11074,67,61},{11074,71,65},{11207,71,0},{11213,67,0},
    {11230,74,0},{11562,67,73},{11567,64,78},{11579,60,55},{11717,60,0},{11717,64,0},{11723,67,0},
    {12031,69,73},{12038,65,73},{12044,62,69},{12141,65,0},{12147,62,0},{12158,69,0},{12472,71,86},
    {12478,64,86},{12491,67,82},{12599,67,0},{12610,64,0},{12622,71,0},{12914,72,90},{12914,65,86},
    {12914,69,86},{13023,69,0},{13039,65,0},{13046,72,0},{28619,64,1},{28630,67,1},{28672,60,1},
    {29322,64,0},{29322,67,0},{29322,60,0},{29594,67,30},{29606,64,29},{29624,60,29},{29821,64,0},
    {29821,60,0},{29844,67,0},{30053,64,61},{30053,67,49},{30064,60,45},{30220,60,0},{30220,64,0},
    {30237,67,0},{30447,67,45},{30447,64,55},{30464,60,42},{30621,60,0},{30621,64,0},{30639,67,0},
    {30842,67,42},{30848,64,58},{30859,60,49},{31033,64,0},{31033,60,0},{31050,67,0},{31242,67,52},
    {31248,64,61},{31265,60,52},{31440,60,0},{31440,64,0},{31457,67,0},{32199,67,42},{32212,64,58},
    {32227,60,42},{32768,60,0},{32773,64,0},{32791,67,0},{33012,67,65},{33018,64,82},{33030,60,69},
    {33220,60,0},{33220,64,0},{33238,67,0},{33401,67,61},{33407,64,82},{33412,60,73},{33574,60,0},
    {33580,64,0},{33591,67,0},{33766,67,58},{33772,64,78},{33784,60,69},{33945,60,0},{33958,64,0},
    {33974,67,0},{34119,67,65},{34126,64,82},{34138,60,73},{34282,60,0},{34300,64,0},{34311,67,0},
};
#define NSEQ (sizeof SEQ / sizeof SEQ[0])

static const ev_t HEAVY[] = {                        /* 8 notes held 3 s */
    {100,48,100},{100,55,100},{100,60,100},{100,64,100},{100,67,100},{100,71,100},{100,74,100},{100,77,100},
    {3100,48,0},{3100,55,0},{3100,60,0},{3100,64,0},{3100,67,0},{3100,71,0},{3100,74,0},{3100,77,0},
};

static int32_t o[2 * CTL];
static int bad;
static void check(int ok, const char *what)
{
    printf("quiet_steal: %s  %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static double db(int32_t env) { return env > 0 ? 20 * log10(env / 16777216.0) : -200; }

typedef struct {
    uint32_t given, loud, short_fade, own_short, cut, held_first, needless, raised, in_render, late_halves, max_delay,
             notes, waited, fast;
    double shortest_ms, own_ms;
} res_t;

enum { R_CPU = 1, R_LAYER = 2, R_MAIN = 4 };

static void parts_init(void)
{
    uint32_t p;
    for (p = 0; p < NPART; p++)
        memset(&trk[p], 0, sizeof trk[p]);          /* (as at power-on: no engine switch, no layer note left) */
    host_tracks_init();
    trk[1].p[P_LYMODE] = LY_MULTI;                   /* (part 2 sounds, by its own notes only) */
}

/* seq (times x num / den) on PONY PAD, part 1. R_CPU: the load model and the shedder (R_MAIN: main's, 1.5 ms fades,
 * every half judged); R_LAYER: part 2 holds four notes. Fills r */
static void run(res_t *r, const ev_t *seq, uint32_t nseq, uint32_t num, uint32_t den, int how, double secs)
{
    track_t *T = &trk[0];
    uint32_t nb = (uint32_t)(secs * FS / CTL), b, k = 0, j, p;
    uint8_t over = 0, req = 0, late = 0;
    uint32_t vsum = 0;
    int shed_now = 0;
    struct { uint32_t at; int32_t env0, last; uint8_t on, cut, note, own, fast; } g[NVOICE];
    struct { uint32_t at; uint8_t on, note, v; } w[NVOICE];
    memset(r, 0, sizeof *r);
    memset(g, 0, sizeof g);
    memset(w, 0, sizeof w);
    r->shortest_ms = r->own_ms = 1e9;
    parts_init();
    host_preset(T, ENGI_PONY, host_pi(ENGI_PONY, PONY_PAD));
    if (how & R_LAYER) {
        host_preset(&trk[1], ENGI_PONY, host_pi(ENGI_PONY, PONY_PAD));
        trk_note_on(&trk[1], 36, 90), trk_note_on(&trk[1], 43, 90), trk_note_on(&trk[1], 48, 90),
            trk_note_on(&trk[1], 55, 90);
    }
    for (b = 0; b < nb; b++) {
        voice_t before[NVOICE];
        uint32_t busy, kills;
        memcpy(before, T->v, sizeof before);         /* (what this block gives up: the shed, the note-ons) */
        if ((how & R_CPU) && b % 8u == 0u) {         /* a half starts: audio.c's shed */
            shed_now = 0;
            if (req) {
                req = 0;
                shed_now = shed_voice((how & R_MAIN) ? 1 : late);
                if (how & R_MAIN)                    /* (main: 1.5 ms, the half judged as ever) */
                    shed_now = shed_now ? 2 : 0;
                /* (needless: the load without the voices already fading was under 85 %) */
                for (j = 0, busy = 0; j < NVOICE; j++)
                    busy += before[j].active && before[j].stage != 4u;
                if (shed_now && LOAD_BASE + LOAD_VOICE * (int)busy <= 85)
                    r->needless++;
            }
        }
        while (k < nseq && (uint64_t)seq[k].ms * num * FS / (1000u * den) <= (uint64_t)b * CTL) {
            uint32_t d;
            if (seq[k].vel) {
                voice_t pre[NVOICE];
                memcpy(pre, T->v, sizeof pre);
                trk_note_on(T, seq[k].note, seq[k].vel);
                r->notes++;
                for (d = 0; d < NVOICE; d++)          /* a voice restarted in place over a loud tail */
                    if (T->v[d].age != pre[d].age && pre[d].active && pre[d].stage != 4u && pre[d].note != seq[k].note &&
                        T->v[d].stage != 4u && pre[d].env >= LOUD)
                        r->raised++;
                for (d = 0; d < NVOICE; d++)          /* waiting for its voice's fade: its onset later */
                    if ((T->v[d].pend & VP_ON) && T->v[d].pend_note == seq[k].note) {
                        w[d].on = 1, w[d].at = b, w[d].note = seq[k].note;
                        r->waited++;
                    }
            } else {
                trk_note_off(T, seq[k].note);
            }
            k++;
        }
        for (j = 0; j < NVOICE; j++) {               /* voices given up now: held while a released one sounds? */
            const voice_t *v = &T->v[j], *p0 = &before[j];
            if (v->stage == 4u && p0->active && p0->stage != 4u) {
                uint32_t d, rel = 0;
                for (d = 0; d < NVOICE; d++)
                    rel += d != j && before[d].active && !before[d].gate && before[d].stage != 4u;
                r->given++;
                r->held_first += p0->gate && rel;
                if (p0->env >= LOUD && shed_now == SHED_FAST && b % 8u == 0u && !(v->pend & VP_ON)) {
                    r->fast++;                       /* (the shed after a late half: 1.5 ms, as main's) */
                } else if (p0->env >= LOUD) {
                    r->loud++;
                    g[j].on = 1, g[j].at = b, g[j].env0 = p0->env, g[j].last = p0->env, g[j].cut = 0;
                    g[j].note = p0->note, g[j].own = (v->pend & VP_ON) != 0;
                }
                if (getenv("VERBOSE"))
                    printf("  %7.3f s v%u (%2d, %s, %.1f dB) given up%s%s\n", b * (double)CTL / FS, j, p0->note,
                           p0->gate ? "held" : "released", db(p0->env), shed_now && b % 8u == 0u ? " (shed)" : "",
                           (v->pend & VP_ON) ? " (its own, for a note)" : "");
            }
        }
        kills = voice_kills;
        mix_block(o, CTL);
        r->in_render += voice_kills - kills;         /* (given up inside the render: a second victim) */
        for (p = 0, busy = 0; p < NPART; p++)
            for (j = 0; j < NVOICE; j++)
                busy += trk[p].v[j].active;          /* (rendering, the fading ones too) */
        vsum += busy;
        for (j = 0; j < NVOICE; j++)                 /* the notes that waited: started now? */
            if (w[j].on && !(T->v[j].pend & VP_ON)) {
                const voice_t *v = &T->v[j];
                w[j].on = 0;
                if (v->active && v->stage != 4u && v->note == w[j].note && b - w[j].at > r->max_delay)
                    r->max_delay = b - w[j].at;
            }
        for (j = 0; j < NVOICE; j++)                 /* the fades: how long, any cut */
            if (g[j].on) {
                const voice_t *v = &T->v[j];
                int32_t e = v->active && v->stage == 4u ? v->env : 0;
                if (g[j].last - e > g[j].env0 / (g[j].own ? (int32_t)OWN_BLOCKS : 4) + 1)
                    g[j].cut = 1;
                g[j].last = e;
                if (v->active && v->stage != 4u && v->note == g[j].note && !g[j].own) {
                    g[j].on = 0;                     /* (its note played again within the fade: it goes on) */
                } else if (!(v->active && v->stage == 4u)) {
                    double ms = (b + 1u - g[j].at) * 1000.0 * CTL / FS;
                    if (g[j].own) {
                        r->own_ms = ms < r->own_ms ? ms : r->own_ms;
                        r->own_short += ms < STEAL_MIN_MS;   /* (voice-pressure: 5 ms, OWN_BLOCKS 7; 2 ms before) */
                    } else {
                        r->shortest_ms = ms < r->shortest_ms ? ms : r->shortest_ms;
                        r->short_fade += ms < STEAL_MIN_MS;
                    }
                    r->cut += g[j].cut;
                    g[j].on = 0;
                    if (v->active && v->stage == 1u && v->env > (1 << 18))
                        r->raised++;                 /* the note on it began loud: not from silence */
                }
            }
        if ((how & R_CPU) && b % 8u == 7u) {         /* a half ends: its load, judged as audio.c does */
            uint32_t load = LOAD_BASE * 8u + LOAD_VOICE * vsum;   /* (x 8 blocks) */
            r->late_halves += load > 100u * 8u;
            if (shed_judge(&over, shed_now, load > 85u * 8u))
                req = 1;
            late = load > 100u * 8u;
            vsum = 0;
        }
        if (!(how & R_CPU))
            vsum = 0;
    }
    if (r->shortest_ms > 1e8)
        r->shortest_ms = 0;
    if (r->own_ms > 1e8)
        r->own_ms = 0;
}

static void report(const char *name, const res_t *r, int how)
{
    char w[300];
    printf("quiet_steal: %s: %u voices given up (%u louder than -42 dB, %u more shed fast after a late half): shortest fade %.1f ms (the part's own for a "
           "note %.1f ms), %u under %.0f ms, %u own under 5 ms, %u cut; %u held while a tail sounded; %u needless; "
           "%u loud notes not from silence; %u given up inside the render; %u of %u notes waited, at most %u blocks "
           "(%.1f ms)\n",
           name, r->given, r->loud, r->fast, r->shortest_ms, r->own_ms, r->short_fade, STEAL_MIN_MS, r->own_short, r->cut,
           r->held_first, r->needless, r->raised, r->in_render, r->waited, r->notes, r->max_delay,
           r->max_delay * 1000.0 * CTL / FS);
    snprintf(w, sizeof w, "%s: loud voices fade out over >= %.0f ms (the part's own for a note too), never cut",
             name, STEAL_MIN_MS);
    check(r->loud > 0u && !r->short_fade && !r->own_short && !r->cut, w);
    snprintf(w, sizeof w, "%s: no held note given up while a released one sounds", name);
    check(!r->held_first, w);
    snprintf(w, sizeof w, "%s: no voice given up the load did not need", name);
    check(!r->needless, w);
    snprintf(w, sizeof w, "%s: a note on a taken voice starts from silence", name);
    check(!r->raised, w);
    snprintf(w, sizeof w, "%s: no voice given up inside the render (one victim a note-on)", name);
    check(!r->in_render, w);
    if (how & R_LAYER) {
        snprintf(w, sizeof w, "%s: a free voice in the part: every note starts in its block (chords, arp steps)", name);
        check(!r->waited && !r->max_delay, w);
    } else {
        snprintf(w, sizeof w, "%s: a note waits for its voice's fade at most %u blocks", name, OWN_BLOCKS);
        check(r->max_delay <= OWN_BLOCKS, w);
    }
}

/* part 1 PONY PAD: 60..67 held, 61 62 63 let go; then note 72 takes the quietest of them (61: let go first) and waits
 * for its fade. Returns that voice's index (NVOICE: none waits) */
static uint32_t pending_setup(void)
{
    track_t *T = &trk[0];
    uint32_t j;
    parts_init();
    host_preset(T, ENGI_PONY, host_pi(ENGI_PONY, PONY_PAD));
    for (j = 0; j < 8u; j++)
        trk_note_on(T, 60u + j, 100);
    for (j = 0; j < 600u; j++)
        mix_block(o, CTL);
    for (j = 61; j <= 63u; j++) {
        trk_note_off(T, j);
        mix_block(o, CTL);
    }
    for (j = 0; j < 20u; j++)
        mix_block(o, CTL);
    trk_note_on(T, 72, 100);
    for (j = 0; j < NVOICE; j++)
        if ((T->v[j].pend & VP_ON) && T->v[j].pend_note == 72u)
            return j;
    return NVOICE;
}
static int started(uint32_t note, int any_index)  /* a voice of part 1 started on note (not the tail fading for it) */
{
    uint32_t j;
    for (j = any_index ? 0u : 1u; j < NVOICE; j++)
        if (trk[0].v[j].active && trk[0].v[j].stage != 4u && trk[0].v[j].note == note &&
            trk[0].v[j].pitch16 == (int32_t)note * 16)
            return 1;
    return 0;
}

/* FM6 (ownenv: the envelope pinned at full while released): six notes, the first one played again (the newest, the
 * lowest index), all let go: the tails tie on level; the oldest is v1 */
static int fm6_setup(void)
{
    track_t *T = &trk[0];
    uint32_t j, pi;
    for (pi = 0; pi < ENGINES[ENGI_FM6]->npresets; pi++) {
        int ok = 1;
        parts_init();
        host_preset(T, ENGI_FM6, pi);
        for (j = 0; j < 6u; j++)
            trk_note_on(T, 60u + j, 100);
        for (j = 0; j < 100u; j++)
            mix_block(o, CTL);
        trk_note_off(T, 60);
        trk_note_on(T, 60, 100);
        for (j = 0; j < 100u; j++)
            mix_block(o, CTL);
        for (j = 0; j < 6u; j++)
            trk_note_off(T, j < 5u ? 61u + j : 60u);
        for (j = 0; j < 10u; j++)
            mix_block(o, CTL);
        for (j = 0; j < 6u; j++)
            ok &= T->v[j].active && !T->v[j].gate && T->v[j].stage != 4u && T->v[j].env == T->v[0].env;
        ok &= T->v[0].note == 60u && T->v[0].age > T->v[1].age && T->v[1].note == 61u;
        for (j = 2; j < 6u; j++)
            ok &= T->v[j].age > T->v[1].age;
        if (ok)
            return 1;
    }
    return 0;
}

int main(void)
{
    res_t r, m;
    uint32_t j, kills0, pv;
    char w[200];
    run(&r, SEQ, NSEQ, 1, 1, R_CPU, 14.0);           /* CPU: the first 13 s (the played chords, the replay's 9 s) */
    report("CPU (SMK-37 load)", &r, R_CPU);
    run(&m, SEQ, NSEQ, 1, 1, R_CPU | R_MAIN, 14.0);
    printf("quiet_steal: CPU: %u late halves (over 100 %%), %u given up (main's shedder: %u late, %u given up, %u "
           "needless; shown, not checked: a voice fading over 5.8 ms costs the render that long, and this load model does not "
           "teach the admission the chip: tests/pressure_test.c does, its fades counted)\n", r.late_halves,
           r.given, m.late_halves, m.given, m.needless);
    run(&r, SEQ, NSEQ, 1, 2, 0, 18.0);               /* FAST: all of it, twice as fast */
    report("FAST (8 voices)", &r, 0);
    run(&r, SEQ, NSEQ, 1, 2, R_LAYER, 18.0);         /* LAYER: part 2 holds four */
    report("LAYER (4 held on part 2)", &r, R_LAYER);

    run(&r, HEAVY, sizeof HEAVY / sizeof HEAVY[0], 1, 1, R_CPU, 3.0);
    run(&m, HEAVY, sizeof HEAVY / sizeof HEAVY[0], 1, 1, R_CPU | R_MAIN, 3.0);
    printf("quiet_steal: HEAVY (8 held, 134 %% load): %u late halves, %u given up (main's shedder: %u late, %u given "
           "up)\n", r.late_halves, r.given, m.late_halves, m.given);
    check(r.late_halves <= m.late_halves && r.given <= m.given && !r.needless,
          "HEAVY: no more late halves than main's shedder, no voice more given up");

    pv = pending_setup();                            /* one note-on, one victim */
    check(pv < NVOICE, "a note on the full part waits for its voice's fade (setup)");
    host_preset(&trk[1], ENGI_PONY, host_pi(ENGI_PONY, PONY_PAD));
    kills0 = voice_kills;
    trk_note_on(&trk[1], 48, 100);                   /* another part's note while it waits: the budget holds it */
    j = voice_kills - kills0;
    for (kills0 = voice_kills, pv = 0; pv < 40u; pv++)
        mix_block(o, CTL);
    snprintf(w, sizeof w, "a waiting note's fade ends: no second voice given up (part 2's note took %u, the fade's end "
             "%u)", j, voice_kills - kills0);
    check(j == 1u && voice_kills == kills0, w);

    pv = pending_setup();                            /* all-off while it waits */
    trk_all_off(&trk[0]);
    for (j = 0; j < 100u; j++)
        mix_block(o, CTL);
    check(pv < NVOICE && !started(72, 1), "all-off: the note waiting for its voice's fade never starts");
    pv = pending_setup();                            /* an engine switch while it waits */
    host_preset_req(&trk[0], ENGI_PALOMINO, 0);
    for (j = 0; j < 200u; j++)
        mix_block(o, CTL);
    check(pv < NVOICE && trk[0].engine == ENGI_PALOMINO && !started(72, 1),
          "an engine switch: the note waiting for its voice's fade never starts on the new engine");
    pv = pending_setup();                            /* VOICE to MONO while it waits (on v1) */
    trk[0].p[P_VOICE] = V_MONO;
    for (j = 0; j < 100u; j++)
        mix_block(o, CTL);
    check(pv == 1u && !started(72, 0), "VOICE to MONO: the note waiting on another voice than MONO's never starts");

    /* FM6: equally loud tails: the oldest goes */
    j = fm6_setup();
    check(j, "FM6: six released voices, level tied (setup)");
    shed_voice(0);
    check(j && trk[0].v[1].stage == 4u && trk[0].v[0].stage != 4u, "FM6: the overload sheds the oldest of tied tails");
    j = fm6_setup();
    trk_note_on(&trk[0], 72, 100);
    check(j && (trk[0].v[1].stage == 4u || trk[0].v[1].note == 72u) && trk[0].v[0].note == 60u &&
              trk[0].v[0].stage != 4u, "FM6: a note on the full part takes the oldest of tied tails");
    j = fm6_setup();
    host_preset(&trk[1], ENGI_PONY, host_pi(ENGI_PONY, PONY_PAD));
    trk_note_on(&trk[1], 36, 100), trk_note_on(&trk[1], 40, 100);   /* the budget full (6 + 2) */
    trk_note_on(&trk[1], 43, 100);
    check(j && trk[0].v[1].stage == 4u && trk[0].v[0].stage != 4u,
          "FM6: another part's note takes the oldest of tied tails");

    parts_init();                                    /* a triad from silence */
    host_preset(&trk[0], ENGI_PONY, host_pi(ENGI_PONY, PONY_PAD));
    kills0 = voice_kills;
    trk_note_on(&trk[0], 60, 100), trk_note_on(&trk[0], 64, 100), trk_note_on(&trk[0], 67, 100);
    for (j = 0; j < 400u; j++)
        mix_block(o, CTL);
    check(voice_kills == kills0, "a POLY triad from silence takes no voice");
    printf("quiet_steal: %d failed\n", bad);
    return bad;
}
