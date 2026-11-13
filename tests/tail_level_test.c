/* SPDX-License-Identifier: GPL-3.0-only */
/* A note over a quiet release tail as loud as the same note from silence (CLIPPER_SYNTH 1):
 *   build/host/tail_level_test   (TAIL_SEED=n: other draws; VERBOSE=1: every case; each case seeds its own draws
 *   from TAIL_SEED, the engine, preset, voice mode and note)
 * A long release (REL 110) can still be at -40 .. -60 dB when the same note comes again: voice.c voice_start counts
 * such a voice as sounding and keeps its phases and filter states, so two oscillators that DTUNE had drifted near
 * half a turn apart cancelled and the new note came out thin (or full) at random (RODEO, stallion-fm1 PR #8: AIR CHOIR
 * -8 .. +7 dB). For the engines with two oscillators (ANALOG, PALOMINO, STAMPEDE, RODEO, MARE), every preset at REL 110,
 * VINTAGE 0 (effects off), notes 48 and 60, in POLY and UNI4: the rms over 0.05 .. 0.55 s of ND notes each started
 * from silence (fresh voices, phases where the last note left them) and of ND notes each started over the previous
 * note's tail once its envelope is below -42 dB (env < 1 << 17), after a random hold and a random further wait (the
 * draws: where DTUNE left the oscillators). POLY fails when a start over a tail is more than TAIL_DB outside the
 * range of the starts from silence (VCO 1 runs free in PALOMINO, RODEO, STAMPEDE, MARE: an octave's relation, and so the
 * level, differs from silence too, up to ~5 dB). Before the fixes: ANALOG SINE PAD +11.5, a STAMPEDE keys sound -18.6, MARE
 * SPACE PAD +5.6, PALOMINO from silence already -7 .. +2 (a strings preset). UNI4 is shown (its mean over a tail against
 * from silence), not checked. The k-th start over a tail has the part's free LFOs (LFO 2, LFO 3 FREE) where they were
 * for the k-th start from silence: what the part's LFOs do to a note is the same either way, and not what is checked
 * here (MARE SPACE DRIFT's LFO 3 > CUT moves a note 17 dB over its cycle; the two sets came at different moments of it:
 * 4.4 dB "outside" before). Exit status: failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

#define ND 12u                                       /* draws a case, each way */
#define TAIL_DB 3.0                                  /* POLY: a start over a tail off the mean from silence */

static int32_t o[CTL * 2];
static uint32_t rs, rs0 = 0xC0FFEE11u;               /* rs: the draws; rs0 the base seed (TAIL_SEED) */
static void seed(uint32_t e, uint32_t p, uint32_t mode, uint32_t note)  /* each case its own draws: another engine's
                                                      * presets added or changed no longer move this case's */
{
    uint32_t h = rs0 ^ 0x9E3779B9u;
    h = (h ^ e) * 0x85EBCA6Bu, h ^= h >> 13;
    h = (h ^ p) * 0xC2B2AE35u, h ^= h >> 16;
    h = (h ^ mode) * 0x85EBCA6Bu, h ^= h >> 13;
    h = (h ^ note) * 0xC2B2AE35u, h ^= h >> 16;
    rs = h;
    rng_state = h | 1u;                              /* and the firmware's own generators (libc.c, voice.c's note-ons,
                                                      * as unison_level_test seed_for) */
    uni_ph_seed = h * 3u + 1u, note_seed = h * 5u + 3u, uni_seed = h * 7u + 5u, puni_seed = h * 11u + 7u;
    pend_seed = h * 13u + 9u;
}
static uint32_t rnd(uint32_t n)
{
    rs = rs * 1664525u + 1013904223u;
    return (rs >> 8) % n;
}
static void run(uint32_t nb)
{
    while (nb--)
        mix_block(o, CTL);
}
static int32_t loudest(void)                         /* the loudest active voice's envelope, 0: all silent */
{
    uint32_t i;
    int32_t e = 0;
    for (i = 0; i < NVOICE; i++)
        if (trk[0].v[i].active && trk[0].v[i].env > e)
            e = trk[0].v[i].env;
    return e;
}
static void voices_seeded(uint32_t h)                /* every voice off, its state cleared, its phases at random places
                                                      * of its own (as after playing: never the power-on zeros, STAMPEDE's
                                                      * first note of a voice its own fixed place) */
{
    uint32_t i, k;
    memset(trk[0].v, 0, sizeof trk[0].v);
    for (i = 0; i < NVOICE; i++)
        for (k = 0; k < 3u; k++)
            h = h * 1664525u + 1013904223u, trk[0].v[i].ph[k] = h | 1u;
}
static void silence(void)                            /* every voice gone (engine.c's model change does so): the
                                                      * phases stay where they ran to */
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &trk[0].v[i];
        v->active = v->gate = 0;
        v->stage = 0;
        v->env = v->env_out = 0;
    }
}
typedef struct {                                     /* the part's own free-running modulation: LFO 2 and LFO 3 FREE
                                                      * (and LFO 1's held random value); a note does not restart them */
    uint32_t l2ph, l3ph, lfo_rnd;
    int16_t l2rnd, l3rnd;
    uint16_t l2cx, l3cx;
} freemod_t;
static void freemod_get(freemod_t *f)
{
    const track_t *t = &trk[0];
    f->l2ph = t->l2ph, f->l3ph = t->l3ph, f->lfo_rnd = t->lfo_rnd;
    f->l2rnd = t->l2rnd, f->l3rnd = t->l3rnd, f->l2cx = t->l2cx, f->l3cx = t->l3cx;
}
static void freemod_set(const freemod_t *f)
{
    track_t *t = &trk[0];
    t->l2ph = f->l2ph, t->l3ph = f->l3ph, t->lfo_rnd = f->lfo_rnd;
    t->l2rnd = f->l2rnd, t->l3rnd = f->l3rnd, t->l2cx = f->l2cx, t->l3cx = f->l3cx;
}
static double measure(uint32_t note)                 /* note on, the rms over blocks 69 .. 760 (0.05 .. 0.55 s) */
{
    uint32_t i, b;
    double s2 = 0;
    trk_note_on(&trk[0], note, 100);
    for (b = 0; b < 760; b++) {
        mix_block(o, CTL);
        if (b >= 69)
            for (i = 0; i < CTL; i++) {
                double x = (mix_l[i] + mix_r[i]) / 2.0;
                s2 += x * x;
            }
    }
    return sqrt(s2 / ((760 - 69) * CTL)) + 1.0;
}

static int bad;
static double w_sil, w_tail, w_uni, s_uni;
static uint32_t c_uni;                  /* an engine's worst: POLY from silence's spread, a POLY tail start
                                                      * off the mean, UNI4's mean over tails off the mean */
static const char *n_sil, *n_tail, *n_uni;
/* (Stallion 1.0) a case's figures: each case is measured in a child forked from main before any sound (spawn), from the
 * state the firmware boots in. In one process the part's free LFOs, the MOD SEQ, the effects and the engines' own state
 * went on from the case before: 308 of the 640 cases moved by more than 0.1 dB when the order was reversed (MARE INIT
 * UNI4 5.8 dB, SPACE DRIFT 5.6 dB) */
typedef struct {
    int f, uni;                                      /* failed; a UNI4 case */
    double dev, sil, umean;                          /* off its range (UNI4: its mean); POLY from silence's spread; UNI4's
                                                      * mean over a tail off silence's */
    char line[320];                                  /* what it prints (a failure, VERBOSE) */
} case_t;
/* engine e, preset p, voice mode, note: the levels (dB) from silence and over a tail, ND each */
static void one(uint32_t e, uint32_t p, uint32_t mode, uint32_t note, case_t *r)
{
    freemod_t fm[ND];
    double fs[ND], tl[ND], pm = 0, tm = 0, ref, smin = 1e9, smax = -1e9, tmin = 1e9, tmax = -1e9, dev;
    uint32_t i, k, b, quiet = 0, uni = mode != V_POLY;
    const char *pn = ENGINES[e]->presets[p].name;
    int f;
    seed(e, p, mode, note);
    for (k = 0; k < NPART; k++)
        trk_all_off(&trk[k]);
    silence();
    voices_seeded(rs);                               /* the voices' phases from this case's draws: no phases, filters or
                                                      * drift left by the case before (another engine's preset) */
    for (i = 0; i < P_E0; i++)
        trk[0].p[i] = TP[i].def;
    host_preset(&trk[0], e, p);
    trk[0].p[P_AMODE] = 0, trk[0].p[P_DLY] = trk[0].p[P_REV] = 0, trk[0].p[P_CHOR] = 0;
    trk[0].p[P_VOICE] = (int16_t)mode;
    trk[0].p[P_REL] = 110;
    trk[0].p[P_VINT] = 0;                            /* (VINTAGE: each voice's pitch drifts, a level that moves by design) */
    trk[0].p[P_ITYPE] = 0;                           /* (the INSERT off as the sends: MARE's stereo delay carried the
                                                      * last note's repeats into the next start from silence, the starts
                                                      * from silence came up to 5 dB louder one after the other, RIFF
                                                      * ECHOES) */
    run(16);
    for (k = 0; k < ND; k++) {                       /* from silence: the phases where the last (random) note left them */
        silence();
        freemod_get(&fm[k]);
        fs[k] = 20 * log10(measure(note));
        run(rnd(300));                               /* held on a while more */
        trk_note_off(&trk[0], note);
        run(1 + rnd(40));
        pm += pow(10, fs[k] / 10);
    }
    ref = 10 * log10(pm / ND);
    for (k = 0; k < ND; k++) {                       /* over the tail of the last one */
        trk_note_off(&trk[0], note);
        for (b = 0; b < 40000u && loudest() >= (1 << 17); b++)
            mix_block(o, CTL);
        run(rnd(200));                               /* further down (or gone: then from silence) */
        quiet += loudest() > 0;
        freemod_set(&fm[k]);                         /* the part's LFOs where they were for the k-th start from
                                                      * silence: a free LFO on the cutoff moves a note's level by when
                                                      * it is struck (MARE SPACE DRIFT, LFO 3 > CUT: 17 dB over its
                                                      * cycle at note 60), the same from silence and over a tail; the
                                                      * tail starts came seconds apart, the starts from silence in one
                                                      * sweep of it: their ranges were of different moments */
        tl[k] = 20 * log10(measure(note));
        run(rnd(300));
        tm += pow(10, tl[k] / 10);
    }
    trk_note_off(&trk[0], note);
    for (k = 0; k < ND; k++) {
        smin = fs[k] < smin ? fs[k] : smin, smax = fs[k] > smax ? fs[k] : smax;
        tmin = tl[k] < tmin ? tl[k] : tmin, tmax = tl[k] > tmax ? tl[k] : tmax;
    }
    if (uni) {                                       /* UNI4: random spreads, so the means */
        dev = fabs(10 * log10(tm / ND) - ref);
        f = 0;                                       /* (shown, not checked: 12 random stacks each way differ by up to
                                                      * ~4.5 dB from the draws alone, the kept phases gave 3.7 .. 4.7:
                                                      * no margin. unison_level_test checks UNI4 against POLY) */
    } else {
        dev = fmax(tmax - smax, smin - tmin);       /* (free from silence too: no wider over a tail) */
        f = dev > TAIL_DB;
    }
    r->f = f, r->uni = (int)uni, r->dev = dev, r->sil = smax - smin, r->umean = 10 * log10(tm / ND) - ref;
    r->line[0] = 0;
    if (f || getenv("VERBOSE"))
        snprintf(r->line, sizeof r->line, "tail: %-7s %-4s %-14s note %2u: from silence %+5.1f .. %+5.1f dB, over a tail (%2u/%u sounding) "
               "%+5.1f .. %+5.1f dB (mean %+5.1f) of silence's mean%s\n", ENGINES[e]->name, uni ? "UNI4" : "POLY", pn,
               note, smin - ref, smax - ref, quiet, ND, tmin - ref, tmax - ref, 10 * log10(tm / ND) - ref,
               f ? "  FAIL" : "");
}
static void tally(const case_t *r, const char *pn)  /* (main) a case's figures into its engine's */
{
    fputs(r->line, stdout);
    if (r->uni) {
        s_uni += r->umean, c_uni++;
        if (r->dev > w_uni)
            w_uni = r->dev, n_uni = pn;
    } else {
        if (r->dev > w_tail)
            w_tail = r->dev, n_tail = pn;
        if (r->sil > w_sil)
            w_sil = r->sil, n_sil = pn;
    }
    bad += r->f;
}
typedef struct { pid_t pid; int fd; } child_t;
static child_t spawn(uint32_t e, uint32_t p, uint32_t mode, uint32_t note)
{
    int fd[2];
    child_t c = {-1, -1};
    fflush(stdout);
    if (pipe(fd))
        return c;
    if (!(c.pid = fork())) {
        case_t r;
        close(fd[0]);
        one(e, p, mode, note, &r);
        _exit(write(fd[1], &r, sizeof r) == (ssize_t)sizeof r ? 0 : 2);
    }
    close(fd[1]);
    c.fd = fd[0];
    if (c.pid < 0)
        close(fd[0]), c.fd = -1;
    return c;
}
static int reap(child_t c, case_t *r)
{
    int st = 0;
    size_t got = 0;
    while (c.fd >= 0 && got < sizeof *r) {
        ssize_t k = read(c.fd, (char *)r + got, sizeof *r - got);
        if (k <= 0)
            break;
        got += (size_t)k;
    }
    if (c.fd >= 0)
        close(c.fd);
    if (c.pid > 0)
        waitpid(c.pid, &st, 0);
    return c.pid > 0 && got == sizeof *r && WIFEXITED(st) && !WEXITSTATUS(st) ? 0 : -1;
}

static const uint32_t ENG[] = { 0u /* ANALOG */, ENGI_PALOMINO, ENGI_STAMPEDE, ENGI_RODEO, ENGI_MARE };
int main(void)
{
    uint32_t j, p, m, k;
    if (getenv("TAIL_SEED"))                         /* (other draws: TAIL_SEED=n) */
        rs0 = (uint32_t)strtoul(getenv("TAIL_SEED"), 0, 0);
    host_tracks_init();
    song.master_q12 = 600;                           /* the limiter off: what the voices sum to */
    for (j = 0; j < sizeof ENG / sizeof ENG[0]; j++) {
        const engine_t *en = ENGINES[ENG[j]];
        int b0 = bad;
        w_sil = w_tail = w_uni = s_uni = 0, c_uni = 0, n_sil = n_tail = n_uni = "";
        for (p = 0; p < en->npresets; p++) {        /* a preset's four cases at once, each in a child of its own */
            child_t ch[4];
            for (m = 0; m < 4u; m++)
                ch[m] = spawn(ENG[j], p, m >> 1 ? V_PUNI4 : V_POLY, m & 1u ? 60u : 48u);
            for (m = 0; m < 4u; m++) {
                case_t r;
                if (reap(ch[m], &r)) {
                    printf("tail: %-7s %-14s case %u: its measure failed  FAIL\n", en->name, en->presets[p].name, m);
                    bad++;
                } else
                    tally(&r, en->presets[p].name);
            }
        }
        printf("tail: %-7s %2u presets: POLY from silence within %4.1f dB (%s), over a tail %4.1f outside that (%s); "
               "UNI4's mean over a tail %4.1f off (%s), %+4.1f on average  %s\n", en->name, en->npresets, w_sil, n_sil,
               w_tail, n_tail, w_uni, n_uni, s_uni / c_uni, bad == b0 ? "ok" : "FAIL");
    }
    printf("tail: %d failed\n", bad);
    return bad;
}
