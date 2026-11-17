/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* The macros under a fader that moves in jumps: a chord held on ANALOG's WARM PAD, the four macros at their minimum,
 * BRITE stepped from -64 to +40 in jumps of 18 every 124 blocks (~90 ms), against the same sweep one block at a time.
 *   STEP     the cutoff BRITE adds (x 160 a step) moves at most 160 (one step) a block
 *   SOUND    the output's largest second difference (a step or a corner) over the stepped sweep at most 1.5 x the
 *            smooth sweep's + 400
 *   SETTLE   a sweep one step every 17 blocks (a fader read every 12.6 ms): BRITE at most 4 steps behind; 150 ms
 *            after it stops: at the value, the brightness within 3 % of the same sound played at that value
 *   LOAD     a sound loaded (from silence, under a held chord with its fade, an engine change; a note played meanwhile):
 *            its macros at once, from the block the new sound starts in
 *   CC       CC 16..19 (from a MIDI controller) jumped from the bottom to the top glide the same way: at most 1/32 of
 *            the jump a block, and arrive
 * Every value of the four macros (TIMBR MOTN SPACE too) as BRITE's. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails, checks;
static void ok(int c, const char *what)
{
    checks++;
    if (!c)
        fails++;
    printf("macro_smooth: %-74s %s\n", what, c ? "ok" : "FAIL");
}

/* BRITE as the voices take it this block, x 256 (the cutoff adds it x 160 / 256) */
static int32_t brite_q8(uint32_t k)
{
#ifdef MAC_GLIDE
    return mac_st[0].q8[k];
#else
    return trk[0].p[P_MAC1 + k] * 256;
#endif
}

#define WIN 1024u
static int32_t ob[CTL * 2];
static double win_x, win_d, last_x;
static uint32_t win_n;
static int32_t d2l[2], d2r[2], d2mx;              /* the last two samples; the largest |second difference| */
static double bri[4096];
static uint32_t nbri;
static void block(void)
{
    uint32_t i;
    mix_block(ob, CTL);
    for (i = 0; i < CTL; i++) {
        double x = (double)ob[2 * i] + ob[2 * i + 1], d = x - last_x;
        int32_t a = ob[2 * i] - 2 * d2l[0] + d2l[1], c = ob[2 * i + 1] - 2 * d2r[0] + d2r[1];
        a = a < 0 ? -a : a, c = c < 0 ? -c : c;
        d2mx = a > d2mx ? a : d2mx, d2mx = c > d2mx ? c : d2mx;
        d2l[1] = d2l[0], d2l[0] = ob[2 * i], d2r[1] = d2r[0], d2r[0] = ob[2 * i + 1];
        last_x = x;
        win_x += x * x, win_d += d * d;
        if (++win_n == WIN) {
            if (nbri < NELEM(bri))
                bri[nbri++] = sqrt(win_d / (win_x + 1.0));
            win_x = win_d = 0, win_n = 0;
        }
    }
}
static void blocks(uint32_t n)
{
    while (n--)
        block();
}
static void quiet(void)
{
    uint32_t i, k;
    for (i = 0; i < NPART; i++)
        trk_all_off(&trk[i]);
    blocks(500);
    for (i = 0; i < NPART; i++)
        for (k = 0; k < NVOICE; k++)
            trk[i].v[k].active = trk[i].v[k].gate = 0, trk[i].v[k].stage = 0, trk[i].v[k].env = trk[i].v[k].env_out = 0;
    blocks(400);
}
static void pad(int32_t m1)
{
    static const uint8_t ch[3] = {48, 55, 64};
    track_t *t = &trk[0];
    uint32_t i;
    quiet();
    host_preset(t, 0, host_pi(0, "WARM PAD"));
    t->p[P_DLY] = t->p[P_REV] = t->p[P_CHOR] = 0;
    t->p[P_LD_FLT] = t->p[P_LD_PIT] = t->p[P_LD_SHP] = 0;
    t->p[P_MAC1] = (int16_t)m1, t->p[P_MAC2] = -64, t->p[P_MAC3] = 0, t->p[P_MAC4] = 0;
    for (i = 0; i < 3u; i++)
        trk_note_on(t, ch[i], 100);
    blocks(700);                                       /* (past the pad's attack) */
}
static double bri_mean(uint32_t n)                    /* over the last n windows */
{
    double s = 0;
    uint32_t w;
    for (w = nbri - n; w < nbri; w++)
        s += bri[w];
    return s / n;
}

#define JUMP 18
#define EVERY 124u
#define NJUMP 6u
static void sweep(int stepped, int32_t *step_max, int32_t *d2)
{
    track_t *t = &trk[0];
    uint32_t b;
    int32_t prev, mx = 0;
    pad(-64);
    prev = brite_q8(0);
    d2mx = 0;
    for (b = 0; b < NJUMP * EVERY + 300u; b++) {
        int32_t v;
        if (b >= NJUMP * EVERY)
            v = -64 + JUMP * (int32_t)NJUMP;
        else if (stepped)
            v = -64 + JUMP * (int32_t)(b / EVERY + 1u);
        else
            v = -64 + (int32_t)((JUMP * (int32_t)NJUMP * (int32_t)(b + 1u)) / (int32_t)(NJUMP * EVERY));
        t->p[P_MAC1] = (int16_t)v;
        block();
        {
            int32_t c = brite_q8(0), d = ((c - prev) * 5) >> 3;
            d = d < 0 ? -d : d;
            mx = d > mx ? d : mx;
            prev = c;
        }
    }
    *step_max = mx;
    *d2 = d2mx;
}

int main(void)
{
    track_t *t = &trk[0];
    int32_t st_step, sm_step, st_d2, sm_d2;
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NPART; i++)
        host_preset(&trk[i], i ? 1u : ENGI_HYBRID, 0);
    clipper_proj_neutral();
    trk[1].p[P_LYMODE] = LY_OFF;

    sweep(1, &st_step, &st_d2);
    sweep(0, &sm_step, &sm_d2);
    printf("macro_smooth:   jumps of %d every %u blocks: cutoff step %d a block, second difference %d\n", JUMP, EVERY,
           (int)st_step, (int)st_d2);
    printf("macro_smooth:   one block at a time:          cutoff step %d a block, second difference %d\n", (int)sm_step,
           (int)sm_d2);
    ok(st_step <= 160, "STEP: BRITE moves the cutoff at most one step a block under jumps of 18");
    ok(st_d2 <= sm_d2 + sm_d2 / 2 + 400, "SOUND: the stepped sweep no sharper than the smooth one");

    {   /* SETTLE: one step every 17 blocks, then held */
        int32_t lag = 0, v = -64;
        uint32_t b;
        double swept, held;
        pad(-64);
        for (b = 0; v < 40; b++) {
            int32_t l;
            if (b % 17u == 16u)
                v++;
            t->p[P_MAC1] = (int16_t)v;
            block();
            l = v * 256 - brite_q8(0);
            lag = l > lag ? l : lag;
        }
        blocks(206);                                   /* 150 ms */
        nbri = 0;
        blocks(400);
        swept = bri_mean(nbri);
        printf("macro_smooth:   a step every 17 blocks: %.2f steps behind at most; then %d\n", lag / 256.0,
               (int)brite_q8(0));
        ok(lag <= 4 * 256 && brite_q8(0) == 40 * 256, "SETTLE: a slow sweep follows closely and arrives");
        pad(40);
        nbri = 0;
        blocks(400);
        held = bri_mean(nbri);
        printf("macro_smooth:   brightness after the sweep %.4f, the sound played at +40 %.4f\n", swept, held);
        ok(fabs(swept - held) <= 0.03 * held, "SETTLE: after the sweep the sound is the one played at +40");
    }

    {   /* LOAD: from silence, under a held chord (the fade), an engine change */
        uint32_t k, b, at_once = 1;
        static const int16_t want[4] = {40, 30, 90, 60};
        pad(-64);
        quiet();
        host_preset(t, 0, host_pi(0, "WARM PAD"));
        for (k = 0; k < 4u; k++)
            t->p[P_MAC1 + k] = want[k];
        trk_note_on(t, 60, 100);
        block();
        for (k = 0; k < 4u; k++)
            at_once &= brite_q8(k) == want[k] * 256;
        ok(at_once, "LOAD: from silence, the sound's macros at once");

        pad(-64);
        t->p[P_MAC3] = 0;
        sound_fade_begin(t);                           /* (as ui.c loads a preset under sounding voices) */
        host_preset_req(t, 0, host_pi(0, "WARM PAD"));
        for (k = 0; k < 4u; k++)
            t->p[P_MAC1 + k] = want[k];
        trk_note_on(t, 67, 100);                       /* (a note during the fade: it starts on the new sound) */
        for (b = 0; b < 40u && t->xf_on; b++)
            block();
        at_once = !t->xf_on && t->v[0].active | t->v[1].active | t->v[2].active | t->v[3].active;
        for (k = 0; k < 4u; k++)
            at_once &= brite_q8(k) == want[k] * 256;
        printf("macro_smooth:   under a chord: the fade over after %u blocks, BRITE then %d (x 256)\n", (unsigned)b,
               (int)brite_q8(0));
        ok(at_once, "LOAD: under a held chord, after its fade, the new sound's macros at once");

        pad(-64);
        host_preset_req(t, ENGI_PONY, 1);              /* (as set_engine_of: the old engine fades, then the new) */
        for (k = 0; k < 4u; k++)
            t->p[P_MAC1 + k] = want[k];
        trk_note_on(t, 67, 100);
        for (b = 0; b < 40u && (t->xf_on || t->engine != ENGI_PONY); b++)
            block();
        at_once = t->engine == ENGI_PONY;
        for (k = 0; k < 4u; k++)
            at_once &= brite_q8(k) == want[k] * 256;
        ok(at_once, "LOAD: an engine change under a chord, the new sound's macros at once");
    }

    {   /* CC 16..19 glide as the faders do; every macro the same way */
        int32_t mx[4] = {0, 0, 0, 0}, prev[4];
        uint32_t k, b;
        static const uint8_t to[4] = {127, 127, 127, 127};
        pad(-64);
        for (k = 0; k < 4u; k++)
            prev[k] = brite_q8(k);
        for (k = 0; k < 4u; k++)
            part_ctl_cc(t, 16u + k, to[k]);
        for (b = 0; b < 400u; b++) {
            block();
            for (k = 0; k < 4u; k++) {
                int32_t d = brite_q8(k) - prev[k];
                d = d < 0 ? -d : d;
                mx[k] = d > mx[k] ? d : mx[k];
                prev[k] = brite_q8(k);
            }
        }
        printf("macro_smooth:   CC 16..19 to 127: the largest move a block (x 256) %d %d %d %d\n", (int)mx[0], (int)mx[1],
               (int)mx[2], (int)mx[3]);
        ok(mx[0] <= 127 * 256 / 32 && mx[1] <= 127 * 256 / 32 && mx[2] <= 127 * 256 / 32 && mx[3] <= 127 * 256 / 32,
           "CC: CC 16..19 glide, at most 1/32 of the jump a block");
        ok(brite_q8(0) == 63 * 256 && brite_q8(1) == 63 * 256 && brite_q8(2) == 127 * 256 && brite_q8(3) == 127 * 256,
           "CC: and arrive at the value");
    }
    trk_all_off(t);
    printf("macro_smooth: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
