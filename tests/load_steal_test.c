/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* CPU-pressure voice admission (voice.c vc_admit; CLIPPER_SYNTH 1; run_tests.sh builds it with NVOICE=16 and 8):
 *   build/host/load_steal_test
 *   CHORD    a sudden 16-note chord on the heaviest engine with every per-voice effect: voices given up at the note-ons
 *            until the predicted load fits the budget (16 build: fewer than 16 sound), the bass kept
 *   LIGHT    a light engine keeps every voice of the build
 *   TAILS    released tails go before any held voice
 *   STACK    UNI4: each note on all its 4 voices or none (stacks admitted and taken whole)
 *   CLICK    a voice given up for the load fades: the largest sample step at a take within 1.5 x the one before it
 *            (as tests/regress.c steal fades), plain sines on two parts (MULTI), the model made 12 x slower
 *   MONO     MONO / LEGATO keep their note while another part floods the load; LEGATO still glides (no new attack)
 *   FEEDBACK a CPU 1.6 x slower than the table (and then 0.8 x): the correction converges, the load it implies fits
 *   BOOT     from power-on (the correction at the build's boot value: VC_R0_SMK, VC_R0_FM1) on a chip 1, 2 and 2.5 x
 *            slower than the table, a PONY PAD triad held: within 10 % of the chip in 43 halves (0.25 s) or fewer
 *            learning up, 86 learning down (slower, by design), the held triad's load then at most 85 % (not the FM-1
 *            build at 2.5 x: its first triad admitted at 1 x takes 92 %, the shedder's to thin)
 * Exit status: the failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void ok(int c, const char *what)
{
    printf("load-steal: %-104s %s\n", what, c ? "ok" : "FAIL");
    bad += !c;
}

static int32_t o[2 * CTL];
static void blocks(uint32_t n)
{
    while (n--)
        mix_block(o, CTL);
}
static void silence(void)
{
    uint32_t p, i;
    for (p = 0; p < NPART; p++) {
        trk_all_off(&trk[p]);
        for (i = 0; i < NVOICE; i++)
            memset(&trk[p].v[i], 0, sizeof trk[p].v[i]);
        for (i = 0; i < P_E0; i++)                      /* (the last check's matrix, FILTER 2, SPREAD gone) */
            trk[p].p[i] = TP[i].def;
    }
    vc_ratio = 256;
    vc_base = VC_BASE;
    trk[1].p[P_LYMODE] = LY_OFF;
    blocks(40);
}
static uint32_t sounding(const track_t *t)               /* voices sounding, not fading out */
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].stage != 4u;
    return n;
}
static uint32_t held_note(const track_t *t, uint32_t note)   /* (or waiting for its voice's fade: voice-pressure) */
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += (t->v[i].active && t->v[i].stage != 4u && t->v[i].note == note && t->v[i].gate) ||
             ((t->v[i].pend & VP_ON) && t->v[i].pend_note == note);
    return n;
}

/* the bench's "everything on" (tests: FILTER 2 SPLIT, 12 matrix slots, SPREAD, VINTAGE, the part's effects) */
static void all_fx(track_t *t)
{
    static const uint8_t SRC[12] = {MS_LFO1, MS_LFO2, MS_ENV2, MS_KEY, MS_VEL, MS_RAND, MS_LFO3, MS_MW, MS_AT,
                                    MS_ENV1, MS_LFO1, MS_LFO2};
    static const uint8_t DST[12] = {MT_PITCH, MT_CUT, MT_SHAPE, MT_AMP, MT_PAN, MT_CUT2, MT_RES2, MT_PIT1, MT_PIT2,
                                    MT_CUT, MT_AMP, MT_L2RATE};
    uint32_t k;
    t->p[P_DIST] = 40, t->p[P_CHOR] = 100, t->p[P_DLY] = 100, t->p[P_REV] = 100;
    t->p[P_SPREAD] = 127, t->p[P_VINT] = 64;
    t->p[P_F2MODEL] = 2, t->p[P_F2ROUTE] = 2, t->p[P_F2CUT] = 70, t->p[P_F2RES] = 60;
    t->p[P_ITYPE] = 14, t->p[P_IMIX] = 100;
    for (k = 0; k < MX_SLOTS; k++) {
        uint32_t i = mx_pid(k);
        t->p[i] = SRC[k], t->p[i + 1u] = DST[k], t->p[i + 2u] = 12;
    }
}
static void heavy(track_t *t, uint32_t mode)            /* MARE (the heaviest voice), everything on */
{
    host_preset(t, ENGI_MARE, 14);
    all_fx(t);
    t->p[P_VOICE] = (int16_t)mode;
    t->p[P_ATK] = 0, t->p[P_SUS] = 127, t->p[P_REL] = 100;
    t->p[P_AMODE] = 0;
}

static void t_chord(void)
{
    track_t *t = &trk[0];
    uint32_t i, n, low;
    char w[200];
    silence();
    heavy(t, V_POLY);
    blocks(8);
    for (i = 0; i < 16u; i++)
        trk_note_on(t, 40u + 2u * i, 100);
    n = sounding(t);
    low = held_note(t, 40);
    snprintf(w, sizeof w, "CHORD 16 notes at once, MARE everything on: %u sound (fit %u), predicted %u%% of the half (budget %u%%)",
             n, vc_fit(t), vc_load_now() * 100u / VC_Q, VC_BUDGET * 100u / VC_Q);
    ok(vc_load_now() <= VC_BUDGET && n >= 2u && (NVOICE < 16 || n < 16u), w);
    ok(low == 1u, "CHORD the bass (lowest note) kept");
    ok(held_note(t, 70) == 1u, "CHORD the newest note sounds");
    blocks(200);
    ok(sounding(t) == n, "CHORD nothing more given up while the chord holds");
}

static void t_light(void)
{
    track_t *t = &trk[0];
    uint32_t i;
    char w[160];
    silence();
    host_preset(t, 1, host_pi(1, "INIT"));              /* DIGITAL (1.0: LOFI before, gone) */
    t->p[P_VOICE] = V_POLY, t->p[P_SUS] = 127, t->p[P_AMODE] = 0;
    blocks(8);
    for (i = 0; i < 16u; i++)
        trk_note_on(t, 40u + 2u * i, 100);
    snprintf(w, sizeof w, "LIGHT 16 notes on DIGITAL: %u of %u voices sound (predicted %u%%)", sounding(t), NVOICE,
             vc_load_now() * 100u / VC_Q);
    ok(sounding(t) == NVOICE, w);
}

static void t_tails(void)
{
    track_t *t = &trk[0];
    uint32_t i, k, held_lost = 0, tails_left_at_loss = 0, taken0;
    char w[200];
    silence();
    heavy(t, V_POLY);
    blocks(8);
    for (i = 0; i < 3u; i++)                            /* three notes, released: long tails */
        trk_note_on(t, 80u + i, 100);
    blocks(20);
    for (i = 0; i < 3u; i++)
        trk_note_off(t, 80u + i);
    blocks(2);
    taken0 = vc_steals;
    for (k = 0; k < 16u; k++) {                         /* then held notes, one a block, past the budget */
        uint32_t tails = 0, h = 0;
        trk_note_on(t, 40u + k, 100);
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &t->v[i];
            tails += v->active && !v->gate && v->stage != 4u;
        }
        for (i = 0; i <= k; i++)
            h += held_note(t, 40u + i);
        if (h < k + 1u && !held_lost) {                 /* the first held note given up: no tail may be left */
            held_lost = 1;
            tails_left_at_loss = tails;
        }
        blocks(1);
    }
    snprintf(w, sizeof w, "TAILS 3 released tails, then 16 held notes: %u voices taken; tails left when a held note went: %u",
             vc_steals - taken0, tails_left_at_loss);
    ok(vc_steals > taken0 && held_lost && !tails_left_at_loss, w);
}

static void t_stack(void)
{
    track_t *t = &trk[0];
    uint32_t i, part = 0, dropped = 0;
    char w[200];
    silence();
    heavy(t, V_PUNI4);
    blocks(8);
    for (i = 0; i < 4u; i++)
        trk_note_on(t, 48u + 5u * i, 100);
    for (i = 0; i < 4u; i++) {
        uint32_t c = held_note(t, 48u + 5u * i);
        part += c != 0u && c != 4u;
        dropped += c == 0u;
    }
    snprintf(w, sizeof w, "STACK UNI4 x 4 notes on MARE everything on: %u notes cut short of their 4 voices, %u dropped whole, %u%%",
             part, dropped, vc_load_now() * 100u / VC_Q);
    ok(!part && (NVOICE < 16 || dropped) && held_note(t, 63) == 4u, w);
}

static void t_click(void)
{
    static const uint8_t N1[7] = {48, 52, 55, 59, 62, 65, 69};
    const uint32_t frames = 4u * FS;
    int32_t *L = calloc(frames + CTL, sizeof *L);
    uint32_t f, i, p, k, ev[8], nev = 0, nbad = 0;
    double worst = 0;
    char w[200];
    silence();
    for (p = 0; p < NPART; p++)
        xfade_sine(&trk[p]);
    trk[1].p[P_LYMODE] = LY_MULTI;                      /* (Clipper: the layer sounds, on its own notes) */
    for (f = 0; f < frames; f += CTL) {
        uint32_t ms = f * 1000u / FS, k0 = vc_steals;
        vc_ratio = 256u * 12u;                          /* the model 12 x slower: 7 sines fill the budget */
        if (f == 0)
            for (i = 0; i < 7u; i++)
                trk_note_on(&trk[0], N1[i], 90);
        if (f == FS / 2u / CTL * CTL)
            trk_note_on(&trk[1], 74, 90);
        if (ms >= 1000u && f % (FS / 2u) < CTL && ms < 3500u)
            trk_note_on(&trk[ms / 500u % 2u], 76u + ms / 250u, 90);
        mix_block(o, CTL);
        if (vc_steals != k0 && nev < 8u)
            ev[nev++] = f;
        for (i = 0; i < CTL; i++)
            L[f + i] = o[2 * i];
    }
    for (k = 0; k < nev; k++) {
        int32_t calm = 1, a = 0;
        if (ev[k] < FS / 4u)
            continue;
        for (i = ev[k] - FS / 4u; i < ev[k] - 2u * CTL; i++)
            calm = abs(L[i] - L[i - 1]) > calm ? abs(L[i] - L[i - 1]) : calm;
        for (i = ev[k]; i < ev[k] + 2u * CTL; i++)
            a = abs(L[i] - L[i - 1]) > a ? abs(L[i] - L[i - 1]) : a;
        if ((double)a / calm > worst)
            worst = (double)a / calm;
        nbad += a * 2 > calm * 3;
    }
    free(L);
    snprintf(w, sizeof w, "CLICK %u takes for the load; largest sample step at a take %.2f x the one before (limit 1.50 x)", nev,
             worst);
    ok(nev >= 4u && !nbad, w);
}

static void t_mono(uint32_t mode)
{
    static const char *const MN[4] = {"POLY", "MONO", "LEGATO", "UNISON"};
    track_t *m = &trk[0], *h = &trk[1];
    uint32_t k, lost = 0, taken0;
    char w[200];
    silence();
    host_preset(m, 0, 0);
    m->p[P_VOICE] = (int16_t)mode, m->p[P_AMODE] = 0, m->p[P_SUS] = 100, m->p[P_GLIDE] = 40;
    heavy(h, V_POLY);
    h->p[P_LYMODE] = LY_MULTI;                          /* (Clipper: the layer, on its own notes) */
    trk_note_on(m, 40, 100);
    blocks(10);
    taken0 = vc_steals;
    for (k = 0; k < FS / CTL; k++) {
        if (k % 3u == 0u)
            trk_note_on(h, 50u + k / 3u % 40u, 100);
        blocks(1);
        lost += !(m->v[0].active && m->v[0].gate && m->v[0].note == 40u && m->v[0].stage != 4u);
    }
    snprintf(w, sizeof w, "MONO %s part held its note while another part flooded the load (%u taken): lost in %u blocks",
             MN[mode], vc_steals - taken0, lost);
    ok(!lost && vc_steals > taken0, w);
    if (mode == V_LEGATO) {                             /* a second key held: the same voice glides, no new attack */
        uint32_t st, ag = m->v[0].age;
        st = m->v[0].stage;
        trk_note_on(m, 43, 100);
        ok(m->v[0].note == 43u && m->v[0].age == ag && m->v[0].stage == st && sounding(m) == 1u,
           "MONO LEGATO under load: the next held key glides on the same voice (no new attack)");
    }
}

/* a simulated CPU f x the table's cost: what audio.c would measure for the half just rendered */
static uint32_t sim_us(uint32_t f100)
{
    uint32_t pred = vc_acc * CTL / HALF_FRAMES;         /* (the half's voice load as the table predicts it) */
    return (uint32_t)(((uint64_t)VC_BASE + (uint64_t)pred * f100 / 100u) * VC_HALF_US / VC_Q);
}
static void t_feedback(void)
{
    track_t *t = &trk[0];
    uint32_t h, i, fit1, fit2, worst = 0;
    char w[220];
    silence();
    host_preset(t, ENGI_PONY, 8);                       /* PONY, everything on */
    all_fx(t);
    t->p[P_VOICE] = V_POLY, t->p[P_SUS] = 127, t->p[P_AMODE] = 0, t->p[P_ATK] = 0;
    fit1 = vc_fit(t);
    for (i = 0; i < 6u; i++)
        trk_note_on(t, 48u + 3u * i, 100);
    for (h = 0; h < 400u; h++) {                        /* 2.3 s: halves rendered, measured 1.6 x slower */
        blocks(HALF_FRAMES / CTL);
        vc_half(sim_us(160));
        if (h % 40u == 39u)                             /* a new note now and then */
            trk_note_on(t, 70u + h / 40u, 100);
        if (h > 200u) {
            uint32_t q = (uint32_t)((uint64_t)sim_us(160) * VC_Q / VC_HALF_US);
            worst = q > worst ? q : worst;
        }
        vc_acc = 0;
    }
    fit2 = vc_fit(t);
    snprintf(w, sizeof w, "FEEDBACK a CPU 1.6 x slower: correction %u.%02u (want 1.60 +- 0.10), fit %u -> %u voices, "
             "its load at most %u%% after", vc_ratio / 256u, vc_ratio % 256u * 100u / 256u, fit1, fit2, worst * 100u / VC_Q);
    ok(vc_ratio > 256u * 150u / 100u && vc_ratio < 256u * 170u / 100u && fit2 < fit1 && worst <= VC_Q * 90u / 100u, w);
    for (h = 0; h < 800u; h++) {                        /* then 0.8 x: down again (slower) */
        blocks(HALF_FRAMES / CTL);
        vc_half(sim_us(80));
        vc_acc = 0;
    }
    snprintf(w, sizeof w, "FEEDBACK then 0.8 x: correction %u.%02u (want 0.80 +- 0.08)", vc_ratio / 256u,
             vc_ratio % 256u * 100u / 256u);
    ok(vc_ratio > 256u * 72u / 100u && vc_ratio < 256u * 88u / 100u, w);
}

/* from power-on on a chip f100 / 100 x slower than the table (the render with no voice too), the correction from the
 * build's boot value r0: the halves of playing until it is within 10 % of the chip, and where it ends */
static void t_boot(uint32_t f100, uint32_t r0, const char *build, int fit)
{
    track_t *t = &trk[0];
    uint32_t h, i, settle = 0, worst = 0;
    char w[220];
    silence();
    vc_ratio = (uint16_t)r0;
    host_preset(t, ENGI_PONY, 0);                       /* PONY PAD as it is */
    for (h = 0; h < 200u; h++) {                        /* 1.2 s, nothing sounds: the floor learnt */
        blocks(HALF_FRAMES / CTL);
        vc_half((uint32_t)((uint64_t)VC_BASE * f100 / 100u * VC_HALF_US / VC_Q));
        vc_acc = 0;
    }
    for (i = 0; i < 3u; i++)                            /* a triad held */
        trk_note_on(t, 60u + 4u * i - (i == 2u), 100);
    for (h = 0; h < 400u; h++) {
        uint32_t pred = vc_acc * CTL / HALF_FRAMES, us;
        blocks(HALF_FRAMES / CTL);
        pred = vc_acc * CTL / HALF_FRAMES;
        us = (uint32_t)(((uint64_t)VC_BASE + pred) * f100 / 100u * VC_HALF_US / VC_Q);
        vc_half(us);
        vc_acc = 0;
        if (vc_ratio * 100u > 256u * f100 * 110u / 100u || vc_ratio * 100u < 256u * f100 * 90u / 100u)
            settle = h + 1u;
        else if (h > settle + 5u && us * 100u / VC_HALF_US > worst)
            worst = us * 100u / VC_HALF_US;
    }
    snprintf(w, sizeof w, "BOOT %s on a chip %u.%02u x slower: correction from %u.%02u within 10 %% after %u halves "
             "(%u ms), ends %u.%02u", build, f100 / 100u, f100 % 100u, r0 / 256u, r0 % 256u * 100u / 256u, settle,
             settle * HALF_FRAMES * 1000u / FS, vc_ratio / 256u, vc_ratio % 256u * 100u / 256u);
    ok(settle <= (r0 * 100u > 256u * f100 ? 86u : 43u) && vc_ratio * 100u <= 256u * f100 * 105u / 100u &&
       vc_ratio * 100u >= 256u * f100 * 95u / 100u && (!fit || worst <= 85u), w);
}

int main(void)
{
    host_tracks_init();
    t_chord();
    t_light();
    t_tails();
    t_stack();
    t_click();
    t_mono(V_MONO);
    t_mono(V_LEGATO);
    t_feedback();
    t_boot(200, VC_R0_SMK, "SMK-37", 1);
    t_boot(250, VC_R0_SMK, "SMK-37", 1);
    t_boot(100, VC_R0_SMK, "SMK-37", 1);
    t_boot(200, VC_R0_FM1, "FM-1", 1);
    t_boot(250, VC_R0_FM1, "FM-1", 0);              /* (its first triad admitted at 1 x: 92 %, the shedder's) */
    printf("load-steal (NVOICE %u): %d failed\n", NVOICE, bad);
    return bad != 0;
}
