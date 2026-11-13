/* SPDX-License-Identifier: GPL-3.0-only */
/* Every factory preset plays a phrase that fits it (a bass line, held chords, comping or a melody),
 * 4.75 s each, through the whole mix (sends, master), for tools/level_presets.py.
 *   argv[1]: a WAV of them all, argv[2]: the list (engine preset role name), argv[3]: mono int32 raw (the master's input,
 *   before its limiter, MASTER all the way up: 32768 full scale, may pass it), argv[4] (optional):
 *   each one's peak before the master's limiter (dBFS, MASTER all the way up) and the limiter's gain reduction (dB)
 *   cc -O2 -Ibuild/gen -Ifirmware/src -Ifirmware/hal tests/preset_level.c -lm -o build/host/preset_level */
#define main hostsim_main
#include "hostsim.c"
#include <stdlib.h>
#include <sys/mman.h>
static uint32_t pl_seed;                              /* PL_SEED (0: the default draws) */
#undef main

enum { R_BASS, R_HELD, R_COMP, R_MELODY, R_ONE, R_ONEHELD };
static int has(const char *n, const char *w) { return strstr(n, w) != 0; }
#if CLIPPER_SYNTH
/* (Stallion) the phrase of every factory sound by its place in its engine's table (append-only: a renamed preset keeps
 * its phrase), one letter a preset: B a bass line, H held chords (pads, organs, strings; an arp's or a slow attack's
 * sound: the chord held), C comping (keys, brass, a stab played as a chord), M a melody (leads, plucks, bells, FX),
 * O one key a chord (TRIO, MACRO CHORD), P one key held (a MACRO CHORD pad). A preset appended to an engine needs
 * its letter: the run stops without one (level_presets.py --check, run_tests.sh) */
static const char *const ROLES[] = {
    "MBBBBBBBBBBBMMMMMMMMMBHCHHHMCMCM",   /*  0 ANALOG */
    "MCCCCCCCCHHHBBBBBCCCCMMHHHHCMMMH",   /*  1 FM */
    "MCHBCHMBMCHHMBCCCHBCHHMHHHCMMMBC",   /*  2 PHASE */
    "",   /*  3 (no engine: was LOFI, core.h ENGI_GONE) */
    "MHHHHHCMHCHHHMHHHHHCCCHMHHHH",   /*  4 SAMPLE */
    "HHHHHHHHHHHHHHHHHHHHHMMMMMMMMMBB",   /*  5 FORMANT */
    "MBBBBBBBMMMMMMMMMMMMMMHCOOOOOHHH",   /*  6 TRIO */
    "HHHHHHMHHHHHHHHHHHCCHHHHMMMHHHHB",   /*  7 DRAWBAR */
    "MHHHHHHHCMHHHCHHHHHHHHHCMHHHHHHM",   /*  8 GRAIN */
    "MCCCCCCCCHHHHCMMBBBBCCCHHHHHHMMM",   /*  9 FM6 */
    "MCCCCCCCCCCCCHBBBMMHHHHHHHHBBMMC",   /* 10 PHYS */
    "MBBBBBCCCCCHHHHMMMMHHHHHHHMMCHMM",   /* 11 NOISE */
    "MHHBMCCBBHCHMHMBMHCBCMCHBMHHBMCM",   /* 12 HYBRID */
    "MHHHHHHHHCCMCCCHHHBBBBBBMMMMMCCC",   /* 13 WAVE */
    "BBBBBBBBBBBBBBBBBBBBMMBBBBBB",   /* 14 ACID */
    "HHHHHHBHHHHHHHHHHHHHHHHHHHHHHCCC",   /* 15 STRINGS */
    "MOOPHBCHHCHBHOPOOPHMHMBHMPPHBMMM",   /* 16 MACRO */
    "MMMMMMMBBBBBBCCCCCCCHHHHHHHMHMMC",   /* 17 VPS */
    "MHHHHHHHHHBBBBMMMMMCCCHHHMMMCCCC",   /* 18 TERRAIN */
    "MCCCCCHHHBBBBBBCCHMHHHHHCMCCMMMM",   /* 19 FMX */
    "MMMMMMMMMMCCCMMCBBBBHHHHHHH",   /* 20 WSEQ */
    "MHHHHHHHHCCCCCCMCHHHBBBBBMMMMMMC",   /* 21 PONY */
    "MHHHHHHHHHHCCCCCCCBBBBMMMMCCHHMH",   /* 22 VECTOR */
    "MHHHHHHHCCCHBCHMHCBHMMMMMBBHBMHH",   /* 23 PALOMINO */
    "MHCCCCCHHHHHHMMHCCHHHMMHMBBBCHCM",   /* 24 STAMPEDE */
    "MCCCCCCHHHCHHHHCCCCHMHBMMMBBBCCC",   /* 25 RODEO (20 DRIVE LEAD: a melody; 22 GRIT BASS) */
    "HMMPHMHMHHHMMPMPMBBBBBPHPCMCHHMH",   /* 26 MARE (1.0 rebuild; BRASS SWELL H: its slow swell) */
};
_Static_assert(sizeof ROLES / sizeof ROLES[0] == NENGINES, "a ROLES row for every engine");
static int role_at(uint32_t e, uint32_t pi)
{
    static const char C[] = "BHCMOP";
    const char *r = ROLES[e];
    if (strlen(r) != ENGINES[e]->npresets)
        return -1;
    return (int)(strchr(C, r[pi]) ? strchr(C, r[pi]) - C : -1);
}
#else
static int role_of(const char *n)
{
    if (has(n, "BASS") || has(n, "808") || has(n, "TAURUS") || has(n, "ACID") || has(n, "REESE") || has(n, "WOBBLE") || has(n, "SUB") || has(n, "BOOM"))
        return R_BASS;
    if (has(n, "PAD") || has(n, "STR") || has(n, "CHOIR") || has(n, "OOH") || has(n, "ORGAN") || has(n, "ORGN") || has(n, "GOSPEL") ||
        has(n, "B3") || has(n, "CLOUD") || has(n, "HAZE") || has(n, "ATMOS") || has(n, "ENSEMBLE") || has(n, "DRONE") ||
        has(n, "SHIMMER") || has(n, "OCEAN") || has(n, "RISER") || has(n, "WIND") || has(n, "HISS") || has(n, "VINYL") ||
        has(n, "RAIN") || has(n, "BOWL") || has(n, "BOW") || has(n, "TANPURA") || has(n, "HALO") || has(n, "SWELL") ||
        has(n, "VOWEL") || has(n, "GLASS HARP"))                 /* (0.9: GRAIN's and SAMPLE's slow pads) */
        return R_HELD;
    if ((has(n, "STAB") && !has(n, "HORN") && !has(n, "STRING")) || has(n, "CHORD"))
        return R_ONE;                                  /* TRIO stabs: one key plays the chord */
    if (has(n, "RHODES") || has(n, "WURLI") || has(n, "CLAV") || has(n, "KEYS") || has(n, "STAB") || has(n, "PNO") || has(n, "PIANO") ||
        has(n, "VIBES") || has(n, "BRASS") || has(n, "HORN") || has(n, "ACCORDION") || has(n, "POLY"))
        return R_COMP;
    return R_MELODY;
}

#endif

typedef struct { uint8_t step, note, len; } ev_t;   /* 1/16 steps at 100 BPM */
static const ev_t BASS[] = {{0, 36, 3}, {4, 36, 1}, {6, 43, 2}, {8, 41, 3}, {12, 39, 2}, {14, 38, 2},
                            {16, 36, 3}, {20, 36, 1}, {22, 43, 2}, {24, 46, 3}, {28, 43, 4}};
static const ev_t MELODY[] = {{0, 72, 2}, {2, 75, 2}, {4, 77, 3}, {8, 79, 2}, {10, 77, 2}, {12, 75, 4},
                              {16, 72, 2}, {18, 70, 2}, {20, 72, 3}, {24, 67, 4}, {28, 70, 2}, {30, 72, 2}};
static const uint8_t CHORD[2][4] = {{60, 63, 67, 70}, {58, 62, 65, 68}};   /* Cm7, Bb7sus-ish */
static const uint8_t COMP[] = {0, 3, 6, 10, 12, 16, 19, 22, 26, 28};

static uint8_t held[128];
static uint32_t off_at[128];
/* (Stallion) the notes go in as the keys and MIDI play them (seq.c input_on / input_off): through the arpeggiator of a
 * sound that has one (UPSIDE RUN, STEP PLUCK, TURING PONY ...); trk_note_on alone held its chord, not the arp's notes */
#if CLIPPER_SYNTH
#define note_in input_on
#define note_out input_off
#else
#define note_in trk_note_on
#define note_out trk_note_off
#endif
static void play(uint32_t note, uint32_t vel, uint32_t off)
{
    note_in(&trk[0], note, vel);
    held[note] = 1;
    off_at[note] = off;
}

/* one preset measured: its phrase rendered into sg (the mix before the master's limiter, what comes out, the peak)
 * (Stallion 1.0) each in a process of its own, forked from main before any sound: every preset starts from the
 * state the firmware boots in, nothing of the one measured before it. In one process the part's free-running LFOs (LFO 1,
 * LFO 2 and LFO 3 FREE: their phases and S&H values), the MOD SEQ, the effects' buses and lines, the master, the voice
 * allocator and the engines' own state (arenas, layouts) all went on from the last preset: a slow LFO met the phrase
 * at another place, and leaving 32 presets out of the run (LOFI, 1.0) moved 8 others by 1.5 to 5.6 dB (PHYS DUSK PAD
 * after PHYS INIT: 8.8 dB under its level alone, from LFO 2's and LFO 3's FREE phases: LFO 3 > CUT, LFO 2 > POS) */
#define PL_NB ((FS * 9u / 2u) / CTL)                  /* the phrase, 4.5 s, then its tail, 0.25 s */
#define PL_TAIL ((FS / 4u) / CTL)
#define PL_FRAMES ((PL_NB + PL_TAIL) * CTL)
typedef struct {
    int32_t pk, gmin;                                 /* the peak before the limiter, the limiter's least gain */
    int32_t raw[PL_FRAMES];                           /* the mix before the limiter (mono) */
    int32_t wav[2 * PL_FRAMES];                       /* what comes out */
} seg_t;
static void measure(uint32_t e, uint32_t pi, int role, seg_t *sg)
{
    uint32_t b, n, i, fr = 0;
    const uint32_t step = FS * 60u / 100u / 4u, nb = PL_NB, tail = PL_TAIL;
    int32_t o[2 * CTL], pk = 0, gmin = 32768;
    host_tracks_init();
    song.g[G_BPM] = 100;
    host_preset(&trk[0], e, pi);
#if CLIPPER_SYNTH
    if (e != ENGI_MARE)                             /* as ui.c apply_preset_to: the factory sounds dry (MARE's: */
        trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;       /* the sends its presets name, a design exception) */
#endif
    song.sel = 0;
    memset(held, 0, sizeof held);
    for (b = 0; b < FS / CTL; b++)                    /* a second after the load, before the first note */
        mix_block(o, CTL);
    {                                                 /* each preset its own random draws (libc.c, voice.c's
                                                       * note-ons; as unison_level_test seed_for): a preset
                                                       * added or changed elsewhere no longer moves this one */
        uint32_t h = (e * 64u + pi) * 2654435761u ^ 0x9E3779B9u ^ pl_seed;   /* PL_SEED=n: other draws */
        rng_state = h | 1u;
        uni_ph_seed = h * 3u + 1u, note_seed = h * 5u + 3u, uni_seed = h * 7u + 5u, puni_seed = h * 11u + 7u;
        pend_seed = h * 13u + 9u;
        {                                             /* the voices' phases at random places of their own, as after
                                                       * playing */
            uint32_t v, k, g = h;
            memset(trk[0].v, 0, sizeof trk[0].v);
            for (v = 0; v < NVOICE; v++)
                for (k = 0; k < 3u; k++)
                    g = g * 1664525u + 1013904223u, trk[0].v[v].ph[k] = g | 1u;
        }
    }
    for (b = 0; b < nb + tail; b++) {
        uint32_t s0 = b * CTL, s1 = s0 + CTL, st;
        for (n = 0; n < 128u; n++)
            if (held[n] && (off_at[n] < s1 || b >= nb)) {
                note_out(&trk[0], n);
                held[n] = 0;
            }
        for (st = 0; st < 32u && b < nb; st++) {
            uint32_t at = st * step;
            if (at < s0 || at >= s1)
                continue;
            if (role == R_BASS || role == R_MELODY) {
                const ev_t *ev = role == R_BASS ? BASS : MELODY;
                uint32_t ne = role == R_BASS ? sizeof BASS / sizeof BASS[0] : sizeof MELODY / sizeof MELODY[0];
                for (i = 0; i < ne; i++)
                    if (ev[i].step == st)
                        play(ev[i].note, 100, at + ev[i].len * step - step / 4u);
            } else if (role == R_HELD || role == R_ONEHELD) {
                if (st == 0 || st == 16)
                    for (i = 0; i < (role == R_ONEHELD ? 1u : 4u); i++)
                        play(CHORD[st / 16u][i], 90, at + 15u * step);
            } else {
                for (n = 0; n < sizeof COMP; n++)
                    if (COMP[n] == st)
                        for (i = 0; i < (role == R_ONE ? 1u : 4u); i++)
                            play(CHORD[st / 16u][i], 95, at + 2u * step - step / 3u);
            }
        }
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {                   /* the master's peak before its limiter (fx.c lim_block:
                                                       * dl / dr hold this block's input), its gain */
            int32_t a = lim_dac.dl[i] < 0 ? -lim_dac.dl[i] : lim_dac.dl[i], c = lim_dac.dr[i] < 0 ? -lim_dac.dr[i] : lim_dac.dr[i];
            pk = a > pk ? a : pk, pk = c > pk ? c : pk;
        }
        gmin = lim_dac.g < gmin ? lim_dac.g : gmin;
        for (i = 0; i < CTL; i++) {                   /* raw: the mix before the limiter (Stallion), what the
                                                       * FM-1 plays below the top of MASTER: at the top the limiter
                                                       * turns the loud chords down (by up to 17 dB), which is not
                                                       * the sound's level; the WAV: what comes out */
            uint32_t k = (lim_dac.w + i) & (CTL - 1u);
            sg->raw[fr] = (int32_t)(((int64_t)lim_dac.dl[k] + lim_dac.dr[k]) / 2);
            sg->wav[2 * fr] = o[2 * i], sg->wav[2 * fr + 1] = o[2 * i + 1];
            fr++;
        }
    }
    sg->pk = pk, sg->gmin = gmin;
}

/* the presets measured, in this order: every engine's in turn; PL_REV=1 the other way round; PL_ONLY=e.p,e.p,.. only those
 * (each is measured in a process of its own: neither changes a preset's measure; PL_JOBS=n: n at once, default every core) */
static uint16_t ord[NENGINES * 32u];
static uint32_t order(void)
{
    const char *only = getenv("PL_ONLY");
    uint32_t e, pi, n = 0, i;
    for (e = 0; e < NENGINES; e++)
        for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
            if (only) {
                const char *q = only;
                int hit = 0;
                while (q && *q && !hit) {
                    char *end;
                    unsigned long qe = strtoul(q, &end, 10), qp = *end == '.' ? strtoul(end + 1, &end, 10) : ~0ul;
                    hit = qe == e && qp == pi;
                    q = strchr(q, ',');
                    q = q ? q + 1 : 0;
                }
                if (!hit)
                    continue;
            }
            ord[n++] = (uint16_t)(e * 32u + pi);
        }
    if (getenv("PL_REV") && atoi(getenv("PL_REV")))
        for (i = 0; i < n / 2u; i++) {
            uint16_t x = ord[i];
            ord[i] = ord[n - 1u - i], ord[n - 1u - i] = x;
        }
    return n;
}

int main(int argc, char **argv)
{
    FILE *w = fopen(argv[1], "wb"), *rep = fopen(argv[2], "w"), *raw = fopen(argv[3], "wb"), *pk_f = argc > 4 ? fopen(argv[4], "w") : 0;
    uint32_t frames = 0, nord, k, i, jobs = 8;
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    seg_t *slot;
    pid_t *pid;
    if (getenv("PL_SEED"))
        pl_seed = (uint32_t)atoi(getenv("PL_SEED")) * 0x85EBCA6Bu;
    if (ncpu > 0)
        jobs = (uint32_t)ncpu;
    if (getenv("PL_JOBS") && atoi(getenv("PL_JOBS")) > 0)
        jobs = (uint32_t)atoi(getenv("PL_JOBS"));
    if (!w || !rep || !raw)
        return 1;
    nord = order();
    for (k = 0; k < nord; k++) {
        uint32_t e = ord[k] / 32u, pi = ord[k] % 32u;
#if CLIPPER_SYNTH
        if (role_at(e, pi) < 0) {
            fprintf(stderr, "preset_level: engine %u (%u presets): ROLES[%u] has %u letters or a wrong one\n", e,
                    (unsigned)ENGINES[e]->npresets, e, (unsigned)strlen(ROLES[e]));
            return 2;
        }
#endif
    }
    slot = mmap(0, jobs * sizeof(seg_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);   /* the children's */
    pid = calloc(jobs, sizeof *pid);
    if (slot == MAP_FAILED || !pid)
        return 1;
    wav_hdr(w, 0);
    fflush(0);
    for (k = 0; k < nord + jobs; k++) {               /* up to jobs at once; written in order */
        if (k >= jobs) {
            uint32_t d = k - jobs, e = ord[d] / 32u, pi = ord[d] % 32u;
            seg_t *sg = &slot[d % jobs];
            int st = 0;
#if CLIPPER_SYNTH
            int role = role_at(e, pi);
#else
            int role = role_of(ENGINES[e]->presets[pi].name);
#endif
            if (waitpid(pid[d % jobs], &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st)) {
                fprintf(stderr, "preset_level: %u.%u: its measure failed\n", e, pi);
                return 3;
            }
            fprintf(rep, "%u %u %d %s\n", e, pi, role, ENGINES[e]->presets[pi].name);
            fwrite(sg->raw, 4, PL_FRAMES, raw);
            for (i = 0; i < PL_FRAMES; i++)
                wav_put(w, sg->wav[2 * i], sg->wav[2 * i + 1]);
            frames += PL_FRAMES;
            if (pk_f)                                 /* dBFS (32768), the limiter's most gain reduction, dB */
                fprintf(pk_f, "%u %u %.2f %.2f\n", e, pi, 20.0 * log10((sg->pk + 1) / 32768.0),
                        -20.0 * log10(sg->gmin / 32768.0));
        }
        if (k < nord) {
            uint32_t e = ord[k] / 32u, pi = ord[k] % 32u;
#if CLIPPER_SYNTH
            int role = role_at(e, pi);
#else
            int role = role_of(ENGINES[e]->presets[pi].name);
#endif
            pid_t c = fork();
            if (c < 0)
                return 1;
            if (!c) {                                 /* a fresh state: nothing rendered in main before this */
                measure(e, pi, role, &slot[k % jobs]);
                _exit(0);
            }
            pid[k % jobs] = c;
        }
    }
    fseek(w, 0, SEEK_SET);
    wav_hdr(w, frames);
    fclose(w);
    fclose(rep);
    fclose(raw);
    if (pk_f)
        fclose(pk_f);
    return 0;
}
