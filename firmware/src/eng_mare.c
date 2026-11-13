/* SPDX-License-Identifier: GPL-3.0-only */
/* MARE (MARE on the panel): a paraphonic-style voice (the idea of a published instrument manual: VCOs whose WAVE
 * turns continuously from triangle through saw and square to a narrow pulse, hard sync and linear FM between them,
 * noise, two 4-pole ladder filters side by side, their cutoffs apart by SPACING, in STEREO, PARALLEL or SERIES). No
 * code, sample or preset of its maker's.
 * EDIT 1 (VCO)  WAVE  VCO2  DTUNE  SY/FM        EDIT 2 (VCF)  CUT  RES  SPACE  MODE
 *   WAVE  both VCOs: TRI (0) .. SAW (42) .. SQR (85) .. a ~5 % PLS (127), turned continuously (between TRI and SAW
 *         the two mixed, from SAW to SQR the saw less a part of itself half a cycle on, then the pulse narrowing).
 *         SHAPE moves it (LFO / ENV DEST SHP, MW / AT SHP, TIMBR, the matrix): a wave modulation, a PWM
 *         in the pulse half
 *   VCO2  VCO 2's interval, -24 .. +24 semitones
 *   DTUNE VCO 2 against VCO 1, 0 .. 40 cents (VCO 1 down, VCO 2 up, half each; squared: fine near 0)
 *   SY/FM right of 0 SYNC: VCO 2 hard-synced to VCO 1 (band-limited resets, as eng_palomino.c's), pitched up from its
 *         interval by up to 36 semitones (1: none), and SHAPE then moves VCO 2's pitch (up to +-32 semitones)
 *         instead of the wave: ENV DEST SHP sweeps the sync. Left of 0 FM: VCO 2 bends VCO 1's frequency linearly
 *         (linear FM), up to twice VCO 1's frequency either way (squared: fine near 0); bells and
 *         metal with VCO2 off the octave. The label follows the side (FM, SY/FM at 0, SYNC)
 *   CUT RES  the two ladders (Zavalishin's TPT ladder, as dsp.c ladder_tap, with a high-pass of the same stages):
 *         keyboard tracking about half; ENV DEST FLT, the LFO, VEL>F, MW / AT CUT, BRITE and the matrix move both.
 *         RES 127 sings, bounded by the ladder's tanh
 *   SPACE -64 .. 63 (as the instrument's SPACING): VCF 1 (the left low-pass, or the high-pass) alone off CUT, above 0
 *         up, below 0 down, VCF 2 (the right, the low-pass) at CUT; two thirds of a cutoff step a unit (63: ~3 octaves
 *         up)
 *   MODE  STER: two low-passes, the left one left, the right one right (SPACE 0: one filter for both, half the cost);
 *         PARA: a high-pass and a low-pass on the same VCOs, summed into both sides (the instrument's
 *         parallel out, centred): SPACE above 0 a notch, below
 *         0 the two overlapping; SERI: the high-pass then the low-pass, mono (SPACE the band's width: below 0 wider,
 *         above 0 narrower; its level made up x 2). +N: the same with noise (white, -3 dB under a VCO) in the mixer.
 *         SPLT: VCO 1 alone into the left low-pass, VCO 2 alone into the right (SPACE as STER's), each
 *         at x 0.72 (two VCOs apart: about the mix's level); SPL+N: the noise into both
 * EDIT 3 (MARE only; device-only values): the instrument's four VCOs
 *   VCO3 VCO4  (P_MFFM, P_MRING) VCO 3's and VCO 4's interval from the note, -24 .. +24, or OFF (the minimum: the
 *         two-VCO sound, at its cost). VCO 3 goes with VCO 1 (SPLT: the left; P>VCO 1&3), VCO 4 with VCO 2 (the right;
 *         2&4); every VCO at the mix's half, so four of them drive the ladders harder, as the instrument's mixer
 *   WAV34 (P_MW34) VCO 3 and 4's WAVE, as WAVE (SHAPE moves it too)
 *   (FILTER FM and RING: the matrix's FLTFM and RING alone, patch-only as on the instrument)
 * the instrument's character beyond its panel:
 *   - PARA / PARA+ (P_VOICE): one voice, the held keys (the keyboard's and the sequencer's) sharing its VCOs as the
 *     instrument's paraphonic unison, each VCO gliding to its key (mar_para); PARA's envelopes run from all keys up
 *     (MULTI TRIG off), PARA+'s restart on every key
 *   - each VCO drifts on its own (mar_drift: DTUNE x 2 + VINTAGE x 3), DTUNE spread over the four at unrelated shares
 *   - the square as loud as the saw; DIST into the ladders a soft saturator (mar_sat); GLMOD EXP (mare_block)
 *   - MODE ST/SP: STER with the VCAs split, the left (VCF 1) through ENV 2 (the filter EG), the right through ENV 1
 *   - the delay: the INSERT's STDLY / PPDLY (fx.c ins_sdly), its lines in the part's arena (mar_part_t)
 * The voice in stereo (its two filters left and right): the left and right filters' middle into the voice's output,
 * their side into the part's side (engines.c f2_sd, as FILTER 2's SPLIT: voice.c vrender takes it while this engine
 * plays, F2V_ESIDE), which the FX > FILTER, DIST and the level carry as SPREAD's (SPREAD adds its own). FILTER 2
 * (after the voice: engines.c f2_post) filters the middle only.
 * The reduction (the instrument it follows has a panel of values; MARE has EDIT 1..3 and the patch bay):
 *   - one WAVE for VCO 1 and 2, one for VCO 3 and 4; no VCO levels (equal); one RES knob (VCF 2's own: the matrix's
 *     RES2), the key follow fixed at about half (more: a KEY cable); the filter EG ENV 2 through cables
 *   - the envelopes, LFOs, sequencer and arpeggiator Clipper's own; no DRONE (a held note at SUS 127)
 *   - SLOPE and the MODEL of FX > FILTER are not this engine's (its ladders are its own, 24 dB)
 * Voice state: ph[0] VCO 1, ph[1] VCO 2, ph[2] the noise (an LCG: any value); VCO 3 and 4's phases in the part's
 * arena (mar_part_t ph, by the voice's number); s[0..3] the left (or high-pass) ladder,
 * s[4..7] the right (or low-pass) one; fs[0] the mixed sample waiting a sample (a sync reset corrects the one before
 * it: polyBLEP), fs[1] SPLT's VCO 2 sample waiting, fs[2..3] unused (voice.c clears fs from silence). Nothing shared: two voices may render on two cores
 * at once (voice.c vs_ok).
 * A block at a time: VCO 1, VCO 2 (each its wave's kind chosen once), the mix, the left ladder, the right one, the
 * output, each a tight loop (the FM-1's 16 registers hold a ladder's states and coefficients).
 * Cost: two VCOs (at most two polyBLEP saws each), one ladder (STER, SPACE 0) or two: on the FM-1 (fm1-emu, its
 * instructions a sample and voice, 8 voices held) ~190 .. 230 with one ladder, ~310 .. 375 with two, 307 for the mean
 * preset, against eng_palomino.c's 304 .. 343 (319) and eng_pony.c's 340 .. 395 (356); on the host tests/mare_test.c. */
enum { MTW_TRI = 0, MTW_SAW = 42, MTW_SQR = 85, MTW_PLS = 127 };
enum { MTM_ST, MTM_PAR, MTM_SER, MTM_STN, MTM_PARN, MTM_SERN, MTM_SPLT, MTM_SPLN,   /* (append-only: stored) */
       MTM_STSP };              /* ST/SP: STER, the VCAs split (VCF 1 / left through ENV 2, right ENV 1) */
enum { MTF_ST, MTF_PAR, MTF_SER, MTF_SPL };      /* the filter path a MODE takes (MTM_DEC) */
static const char *const N_MTWAVE[] = {"TRI", "T>SAW", "SAW", "S>SQR", "SQR", "S>PLS", "PLS", 0};
static const char *const N_MTM[] = {"STER", "PARA", "SERI", "STR+N", "PAR+N", "SER+N", "SPLT", "SPL+N", "ST/SP"};
/* MODE decoded: its filter path (MTF_*) and the noise (0 .. 5: mode % 3, and mode >= STR+N) */
static const struct { uint8_t fil, nz; } MTM_DEC[9] = {
    {MTF_ST, 0}, {MTF_PAR, 0}, {MTF_SER, 0}, {MTF_ST, 1}, {MTF_PAR, 1}, {MTF_SER, 1}, {MTF_SPL, 0}, {MTF_SPL, 1},
    {MTF_ST, 0},
};
static const param_desc_t MAR_FM = {"FM", F_INT, -64, 63, 0, 0, 0};      /* = edit[3], its label by its side */
static const param_desc_t MAR_SYFM = {"SY/FM", F_INT, -64, 63, 0, 0, 0};
static const param_desc_t MAR_SYNC = {"SYNC", F_INT, -64, 63, 0, 0, 0};

static const param_desc_t *mare_desc(const track_t *t, uint32_t k)
{
    return k == 3u ? (t->p[P_E3] < 0 ? &MAR_FM : t->p[P_E3] > 0 ? &MAR_SYNC : &MAR_SYFM) : 0;
}

/* the part's own state, in its engine arena (engines.c eng_arena_of: the pool, nothing more in RAM): VCO 3's
 * and VCO 4's phases of each voice, the part's block clock (the VCOs' drift), PARA's VCOs (mar_para) and the stereo
 * delay's lines (fx.c ins_sdly: the INSERT's STDLY / PPDLY on MARE) */
#if NVOICE > 8
#define MTD_N 1024u                              /* (the 16-voice build: VCO 3/4's phases of 16 voices fill the arena;
                                                  * MARE's delay lines as another engine's, 1024 stages) */
#else
#define MTD_N 2048u                              /* the stereo delay's stages a line on MARE (a power of 2) */
#endif
#define MAR_VOFF (-25)                            /* VCO3 / VCO4 (P_MFFM, P_MRING) OFF */
_Static_assert(CTL == 32, "MARE's balance slides over a block in 32nds");
typedef struct {
    uint32_t ph[NVOICE][2];                      /* VCO 3, VCO 4 */
    uint32_t clk;                                /* blocks */
    int32_t pq[4];                               /* PARA: each VCO's pitch now (1/4096 semitone), 0: none yet */
    int32_t gs[4];                               /* .. its glide's step (TIME: set as it starts) */
    uint8_t pn[4];                               /* .. the note it goes to */
    uint8_t pk, pnk, rr;                         /* .. the newest key, the keys held a block ago, ROUND ROBIN's turn */
    int8_t semi, sem34;                          /* the matrix's SEMI and SEM34 now: whole semitones (mar_semis) */
    int32_t bx[NVOICE];                          /* each voice's balance: its sides' cross energy, a leaky sum as theirs
                                                  * (v->fs[2], fs[3]: the sides' correlation over the same ~23 ms) */
    int32_t ac[NVOICE][3];                       /* each voice's analog path (mar_ac): the coupling's low state, the
                                                  * all-pass's last in and out */
    int16_t wp[NVOICE][4];                       /* each VCO's cycle wobble now: its pitch (1/4096 semitone), mar_mods */
    int16_t wa[NVOICE];                          /* .. the voice's level wobble (Q12 around 4096; 0: none yet) */
    int16_t bg[NVOICE][3];                       /* .. its balance's gains and mix last block (mar_bal_gains' c), the
                                                  * next block slides from them */
    int16_t line[2][MTD_N];                      /* the delay (fx.c), at a quarter */
} mar_part_t;
static mar_part_t *mar_arena(track_t *t) { return (mar_part_t *)eng_arena_of(t, ENGI_MARE); }

/* a VCO at interval iv (semitones) from VCO 1 in step with VCO 1 at phase p1 (mare_note_on) */
static uint32_t mar_instep(const voice_t *v, uint32_t p1, int32_t iv)
{
    iv = clamp(iv, -24, 24);
    return iv ? uni_turns(p1, v->pitch16, iv * 16) + 0x20000000u : p1;
}

static void mare_note_on(track_t *t, voice_t *v)
{
    uint32_t k;
    mar_part_t *A = mar_arena(t);
    if (v->env < (1 << 17)) {                        /* from silence, or a tail below -42 dB (as RODEO's: a REL 110
                                                      * tail kept VCO 2 where DTUNE had moved it, -5 .. +6 dB at random,
                                                      * tests/tail_level_test.c): the filters clear, VCO 2, 3 and 4 in
                                                      * step with VCO 1 (VCO 1 runs on: free-running) */
        for (k = 0; k < 8u; k++)
            v->s[k] = 0;
        v->fs[0] = v->fs[1] = 0;
        if (!v->ph[2]) {                             /* the first note: VCO 1 and the noise start anywhere */
            v->ph[2] = 0x6A09E667u + v->note * 2654435761u;
            v->ph[0] = v->ph[2] * 2246822519u;
        }
        /* (at the same pitch, DTUNE 0, two VCOs apart by chance would thin each note differently: a saw's fundamental
         * can cancel.) In step: as if each VCO had started at 0 with VCO 1, VCO 1's free phase x its own frequency
         * (unison: VCO 1's phase itself), then an eighth of its own cycle on when it is at another interval. The same
         * phase as VCO 1 was in step at the unison alone: an octave's (VCO2 12, VCO3 -12, VCO4 24) relation was
         * wherever free-running VCO 1 stood, its level each note's own (SPACE DRIFT note 48: -4.9 .. +2.2 dB of its
         * mean, 7 dB, the relation holding for seconds at DTUNE 24; HIGH SWITCH, WHEEL KEYS, FAT HUG PAD 4 .. 6 dB);
         * now within ~2 dB (most ~1). The eighth: all of them from 0 together was the loudest relation (+1 .. +2 dB
         * over the random one's mean), an eighth on is its mean (within +-0.6 dB) */
        v->ph[1] = mar_instep(v, v->ph[0], t->p[P_E3] > 0 ? 0 : t->p[P_E1]);   /* (SYNC: VCO 2 reset by VCO 1) */
        if (A) {                                     /* VCO 3 and 4 the same: in step with VCO 1, the
                                                      * drift and DTUNE move them apart */
            k = (uint32_t)(v - t->v) % NVOICE;
            A->ph[k][0] = mar_instep(v, v->ph[0], t->p[P_MFFM] > MAR_VOFF ? t->p[P_MFFM] : 0);
            A->ph[k][1] = mar_instep(v, v->ph[0], t->p[P_MRING] > MAR_VOFF ? t->p[P_MRING] : 0);
            A->ac[k][0] = A->ac[k][1] = A->ac[k][2] = 0;   /* (the analog path from rest) */
            A->wa[k] = 0, A->wp[k][0] = A->wp[k][1] = A->wp[k][2] = A->wp[k][3] = 0;
        }
    }
}

/* the wave at a WAVE position, a kind and its place f in it (Q10): TRI, TRI -> SAW (f the saw's part), SAW, SAW -> SQR
 * (f how much of the saw half a cycle on is taken off), SQR -> PLS (pw the pulse's width); g the level (Q10: the
 * square no louder than the saw). TRI, SAW and SQR at their points (0, 42, 85) */
enum { MWK_TRI, MWK_TS, MWK_SAW, MWK_SS, MWK_PLS };
typedef struct { uint32_t kind, pw; int32_t f, g; } mtw_t;
static inline void mtw_set(mtw_t *w, int32_t pos, uint32_t skew)   /* pos 0 .. 127 << 8; skew: the
                                                  * square's halves apart by that (a phase, 2^32 a cycle: VINTAGE's
                                                  * analog square, its even harmonics) */
{
    pos = clamp(pos, 0, 127 << 8);
    w->pw = 0x80000000u + skew;
    w->g = 1024;
    if (pos < MTW_SAW << 8) {
        w->f = pos * 1024 / (MTW_SAW << 8);
        w->kind = w->f ? MWK_TS : MWK_TRI;
    } else if (pos < MTW_SQR << 8) {
        w->f = (pos - (MTW_SAW << 8)) * 1024 / ((MTW_SQR - MTW_SAW) << 8);
        w->kind = w->f ? MWK_SS : MWK_SAW;           /* (the square as loud as the saw, the instrument's
                                                      * waves all the same swing; it was 0.6) */
    } else {
        w->f = (pos - (MTW_SQR << 8)) * 1024 / ((MTW_PLS - MTW_SQR) << 8);
        w->kind = MWK_PLS;
        w->pw = 0x80000000u + skew - (uint32_t)w->f * 1887437u;   /* 50 % .. ~5 % */
    }
}
static inline int32_t mtw_ts(uint32_t ph, uint32_t inc, int32_t f)   /* TRI -> SAW, f the saw's part (Q10) */
{
    int32_t b = osc_tri_bl(ph, inc);
    return b + (((osc_saw(ph, inc) - b) * f) >> 10);
}
static int32_t mtw_osc(const mtw_t *w, uint32_t ph, uint32_t inc)   /* one sample (the sync's pass) */
{
    switch (w->kind) {
    case MWK_TRI:
        return osc_tri_bl(ph, inc);
    case MWK_TS:
        return mtw_ts(ph, inc, w->f);
    case MWK_SAW:
        return osc_saw(ph, inc);
    case MWK_SS:
        return ((osc_saw(ph, inc) - ((osc_saw(ph + w->pw, inc) * w->f) >> 10)) * w->g) >> 10;
    default:
        return ((osc_saw(ph, inc) - osc_saw(ph + w->pw, inc)) * w->g) >> 10;
    }
}
static inline int32_t mtw_naive(const mtw_t *w, uint32_t ph)   /* (a sync step: without the polyBLEP) */
{
    int32_t a = (int32_t)(ph >> 16) - 32768, b;
    switch (w->kind) {
    case MWK_TRI:
    case MWK_TS:
        b = osc_tri(ph);
        return b + (((a - b) * w->f) >> 10);
    case MWK_SAW:
    case MWK_SS:
        return ((a - ((((int32_t)((ph + w->pw) >> 16) - 32768) * w->f) >> 10)) * w->g) >> 10;
    default:
        return ((a - ((int32_t)((ph + w->pw) >> 16) - 32768)) * w->g) >> 10;
    }
}
/* FM: VCO 1's increment bent by VCO 2's sample y (Q15) x fmd (up to 4096: twice its frequency either way) */
static inline uint32_t mar_fminc(uint32_t inc, int32_t y, int32_t fmd)
{
    int64_t d = (int64_t)inc + (((int64_t)inc * y * fmd) >> 26);
    return d < 0 ? 0u : d > 0x60000000 ? 0x60000000u : (uint32_t)d;
}
/* one VCO over the block into y[0 .. n), its phase *php, a block at a time (the wave's kind chosen once, the phase in
 * a register); fm: VCO 2's samples bending its increment (FM, VCO 1), else 0 */
#define MTV_LOOP(WAVE)                                                                                                \
    if (!fm)                                                                                                          \
        for (i = 0; i < n; i++) {                                                                                     \
            const uint32_t inc = inc0;                                                                                \
            ph += inc;                                                                                                \
            y[i] = (WAVE);                                                                                            \
        }                                                                                                             \
    else                                                                                                              \
        for (i = 0; i < n; i++) {                                                                                     \
            const uint32_t inc = mar_fminc(inc0, fm[i], fmd);                                                          \
            ph += inc;                                                                                                \
            y[i] = (WAVE);                                                                                            \
        }                                                                                                             \
    break
static void mar_vco(const mtw_t *w, uint32_t *php, uint32_t inc0, int32_t *y, uint32_t n, const int32_t *fm, int32_t fmd)
{
    uint32_t i, ph = *php, pw = w->pw;
    int32_t f = w->f, g = w->g;
    switch (w->kind) {
    case MWK_TRI:
        MTV_LOOP(osc_tri_bl(ph, inc));
    case MWK_TS:
        MTV_LOOP(mtw_ts(ph, inc, f));
    case MWK_SAW:
        MTV_LOOP(osc_saw(ph, inc));
    case MWK_SS:
        MTV_LOOP(((osc_saw(ph, inc) - ((osc_saw(ph + pw, inc) * f) >> 10)) * g) >> 10);
    default:
        MTV_LOOP(((osc_saw(ph, inc) - osc_saw(ph + pw, inc)) * g) >> 10);
    }
    *php = ph;
}
#undef MTV_LOOP
/* VCO 2 hard-synced to VCO 1 (ph1 its phase before the block, inc1 its increment): at each wrap of VCO 1, VCO 2
 * restarts, by the fraction (q12: the overshoot / the increment); its step h band-limited over that sample and the one
 * before (y[i - 1], or the mix's sample waiting, *prev, at its 1/2) */
static void mar_vco_sync(const mtw_t *w, uint32_t ph1, uint32_t inc1, uint32_t *php, uint32_t inc, int32_t *y,
                        uint32_t n, int32_t *prev)
{
    uint32_t i, ph = *php;
    for (i = 0; i < n; i++) {
        uint32_t old1 = ph1;
        int32_t corr = 0;
        ph1 += inc1;
        ph += inc;
        if (ph1 < old1 && inc1 >= 4096u) {
            uint32_t q12 = ph1 / (inc1 >> 12), at;
            int32_t h, tq, b;
            q12 = q12 > 4095u ? 4095u : q12;
            at = ph - (inc >> 12) * q12;
            ph = (inc >> 12) * q12;
            h = mtw_naive(w, 0) - mtw_naive(w, at);  /* the step: new - old */
            tq = (int32_t)(q12 << 3), b = 32768 - tq;
            corr = -((((b * b) >> 16) * h) >> 15);   /* this sample: - h (1 - t)^2 / 2 */
            if (i)                                   /* the one before: + h t^2 / 2 */
                y[i - 1] += (((tq * tq) >> 16) * h) >> 15;
            else
                *prev += (((tq * tq) >> 16) * h) >> 16;
        }
        y[i] = mtw_osc(w, ph, inc) + corr;
    }
    *php = ph;
}

/* the ladders, after dsp.c ladder_tap (Zavalishin's TPT ladder: four trapezoidal one-poles, the feedback solved in the
 * same sample, a tanh at the ladder's input), their signals Q11 so that every product is 32 x 32 bits (one multiply on
 * the FM-1; ladder_tap's 64-bit sums take three): the states within ~4e8, every sum under 2^31 (G <= 0.75,
 * ladder_coef). A block at a time (the states and coefficients stay in registers): x the input (full scale), y the
 * output (full scale, may be x), s the four states. mar_lp: the low-pass (stage 4; its passband made up,
 * x (1 + k / 8): most of the ladder's own loss as the resonance rises, as the instrument's (closed and resonant it was
 * 6 dB louder than open at RES 110; now about level, and the bass under a high resonance thinner. It was x (1 + k / 2),
 * as ladder_tap's; the side balance's correlation over its energies' window keeps the SPACE sweep within 1 dB)); mar_hp: the 4-pole high-pass of the same stages, the binomial mix u - 4 y1 + 6 y2 - 4 y3 + y4 of the
 * input after the feedback and the stages (the Xpander's way), its top kept (no make-up) */
typedef struct { int32_t G, H, G4, kinv, k; } mtl_t;   /* Q16; kinv k / (1 + k G^4) (Q16), k Q12 */
static inline void mtl_coef(mtl_t *c, int32_t cut, int32_t res)
{
    ladder_t l;
    ladder_coef(&l, cut, res);
    c->G = l.G, c->H = l.H, c->G4 = l.G4, c->k = l.k;
    c->kinv = (int32_t)(((uint32_t)l.k * (uint32_t)l.inv) >> 12);
}
static inline int32_t mar_mul16(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }
static inline int32_t mar_pole(int32_t G, int32_t x, int32_t *s)   /* a trapezoidal one-pole: its output */
{
    int32_t v = (int32_t)(((int64_t)G * (x - *s)) >> 16), y = v + *s;
    *s = y + v;
    return y;
}
/* the ladder's input after the feedback (its output solved: u = x - k (G^4 x + S) / (1 + k G^4), S what the states
 * give), through the tanh at 65536 (on u / 2: softclip's range; the bits below a unit added back) */
static inline int32_t mar_u(const mtl_t *c, int32_t in, const int32_t *s)
{
    int32_t S = mar_mul16(c->G, mar_mul16(c->G, mar_mul16(c->G, s[0]) + s[1]) + s[2]) + s[3], q;
    int64_t u = in - (((int64_t)(mar_mul16(c->G4, in) + mar_mul16(c->H, S)) * c->kinv) >> 16);
    q = (int32_t)(u >> 12);                          /* (u / 2 in units: within 32 bits) */
    if (q <= -65536 || q >= 65536)
        return softclip(q < 0 ? -65536 : 65536) << 12;
    return (softclip(q) << 12) + ((int32_t)u & 4095);
}
static void mar_lp(const mtl_t *c, const int32_t *x, int32_t *y, uint32_t n, int32_t *s)
{
    int32_t G = c->G, k = c->k, st[4] = {s[0], s[1], s[2], s[3]};
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t in = clamp(x[i], -131072, 131072) << 11;
        in += (int32_t)(((int64_t)in * k) >> 15);    /* x (1 + k / 8) */
        in = mar_pole(G, mar_pole(G, mar_pole(G, mar_u(c, in, st), st), st + 1), st + 2);
        y[i] = (mar_pole(G, in, st + 3) + 1024) >> 11;
    }
    s[0] = st[0], s[1] = st[1], s[2] = st[2], s[3] = st[3];
}
static void mar_hp(const mtl_t *c, const int32_t *x, int32_t *y, uint32_t n, int32_t *s)
{
    int32_t G = c->G, st[4] = {s[0], s[1], s[2], s[3]};
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t uq = mar_u(c, clamp(x[i], -131072, 131072) << 11, st), h = (uq + 1024) >> 11;
        uq = mar_pole(G, uq, st);
        h -= 4 * ((uq + 1024) >> 11);
        uq = mar_pole(G, uq, st + 1);
        h += 6 * ((uq + 1024) >> 11);
        uq = mar_pole(G, uq, st + 2);
        h -= 4 * ((uq + 1024) >> 11);
        y[i] = h + ((mar_pole(G, uq, st + 3) + 1024) >> 11);
    }
    s[0] = st[0], s[1] = st[1], s[2] = st[2], s[3] = st[3];
}

/* FILTER FM: the ladders' cutoff moved by VCO 2 each sample, cut + dev x VCO 2 (Q15). Their coefficients
 * exact at 9 points over cut - dev .. cut + dev (a block's), between them linear (G and k / (1 + k G^4); G^4 from G):
 * no divide a sample. (noinline: mare_render's own loops measured apart, tests/target_budget.py: FILTER FM's run only
 * while it is up) */
#define MAR_FFM_ST 10767                          /* 36 semitones in cutoff steps (30 Hz .. 16 kHz over 127), x 256 */
#define MAR_RINGK 10014                           /* RING: VCO 1 x VCO 2 x 2.44 (Q12), its level that of the plain mix
                                                  * (measured: tests/mare_test.c t_ffm_ring) */
typedef struct { int32_t G[9], kinv[9], k; } mtf_t;
__attribute__((noinline)) static void mtf_set(mtf_t *f, int32_t cut, int32_t res, int32_t dev)
{
    mtl_t c;
    int32_t j;
    for (j = 0; j < 9; j++) {
        mtl_coef(&c, cut + ((dev * (j - 4)) >> 2), res);
        f->G[j] = c.G, f->kinv[j] = c.kinv;
    }
    f->k = c.k;
}
static inline void mtf_at(const mtf_t *f, int32_t x, mtl_t *c)   /* the coefficients at VCO 2's sample x */
{
    uint32_t pos = (uint32_t)(clamp(x, -32768, 32767) + 32768) << 3, j = pos >> 16, fr = (pos & 0xFFFFu) >> 1;
    int32_t G = f->G[j] + (int32_t)(((int64_t)(f->G[j + 1] - f->G[j]) * (int32_t)fr) >> 15);
    uint32_t G2 = ((uint32_t)G * (uint32_t)G) >> 16;
    c->G = G, c->H = 65536 - G, c->G4 = (int32_t)((G2 * G2) >> 16), c->k = f->k;
    c->kinv = f->kinv[j] + (int32_t)(((int64_t)(f->kinv[j + 1] - f->kinv[j]) * (int32_t)fr) >> 15);
}
__attribute__((noinline)) static void mar_lp_fm(const mtf_t *f, const int16_t *fb, const int32_t *x, int32_t *y, uint32_t n, int32_t *s)
{
    int32_t st[4] = {s[0], s[1], s[2], s[3]};
    uint32_t i;
    mtl_t c;
    for (i = 0; i < n; i++) {
        int32_t in = clamp(x[i], -131072, 131072) << 11;
        mtf_at(f, fb[i], &c);
        in += (int32_t)(((int64_t)in * c.k) >> 15);
        in = mar_pole(c.G, mar_pole(c.G, mar_pole(c.G, mar_u(&c, in, st), st), st + 1), st + 2);
        y[i] = (mar_pole(c.G, in, st + 3) + 1024) >> 11;
    }
    s[0] = st[0], s[1] = st[1], s[2] = st[2], s[3] = st[3];
}
__attribute__((noinline)) static void mar_hp_fm(const mtf_t *f, const int16_t *fb, const int32_t *x, int32_t *y, uint32_t n, int32_t *s)
{
    int32_t st[4] = {s[0], s[1], s[2], s[3]};
    uint32_t i;
    mtl_t c;
    for (i = 0; i < n; i++) {
        int32_t uq, h;
        mtf_at(f, fb[i], &c);
        uq = mar_u(&c, clamp(x[i], -131072, 131072) << 11, st), h = (uq + 1024) >> 11;
        uq = mar_pole(c.G, uq, st);
        h -= 4 * ((uq + 1024) >> 11);
        uq = mar_pole(c.G, uq, st + 1);
        h += 6 * ((uq + 1024) >> 11);
        uq = mar_pole(c.G, uq, st + 2);
        h -= 4 * ((uq + 1024) >> 11);
        y[i] = h + ((mar_pole(c.G, uq, st + 3) + 1024) >> 11);
    }
    s[0] = st[0], s[1] = st[1], s[2] = st[2], s[3] = st[3];
}
/* RING: the mix (1 - r) (a + b) + r k a b, r Q14 */
static inline int32_t mar_ring(int32_t a, int32_t b, int32_t s, int32_t r)
{
    int32_t ab = (clamp(a, -32767, 32767) * clamp(b, -32767, 32767)) >> 15;
    return (s * (16384 - r) + ((ab * MAR_RINGK) >> 12) * r) >> 14;
}

/* the mixer's saturation: tanh with a slope of 1, toward +-2 full scale (softclip: 32767 tanh(x / 32768)) */
#define MAR_SATB 12000                            /* (the mixer's lean: its tanh off centre by ~0.37 full scale) */
static inline int32_t mar_sat(int32_t z)
{
    return (softclip(clamp((z + MAR_SATB) >> 1, -65536, 65536)) - softclip(MAR_SATB >> 1)) << 1;
}
/* the mixes of RING and SPLT (noinline: mare_render's own loops measured apart, tests/target_budget.py);
 * nzon: the noise in, *nz its state (moved on). RING: (a + b) as the plain mix, VCO 1 x VCO 2 in it */
__attribute__((noinline)) static void mar_mix_ring(int32_t *xb, const int32_t *yb, uint32_t n, int32_t ring, int nzon, uint32_t *nzp)
{
    uint32_t i, nz = *nzp;
    for (i = 0; i < n; i++) {
        int32_t z = 0;
        if (nzon) {
            nz = nz * 1664525u + 1013904223u;
            z = (((int32_t)(nz >> 16) - 32768) * 23) >> 5;
        }
        xb[i + 1] = (mar_ring(xb[i + 1], yb[i], xb[i + 1] + yb[i], ring) + z) >> 1;
    }
    *nzp = nz;
}
/* SPLT: VCO 1 (xb[1 ..]) left, VCO 2 (yb) right into zb[1 ..], each at half (as in the mix: a sync's corrections at
 * that scale); then xb[0 .. n), zb[0 .. n) (a sample late: xb[0], zb[0] the samples waiting) x 1.44 (two VCOs apart:
 * the level of the mix's), the noise into both at the mix's level, FX > DIST (as the mix's). xb[n], zb[n] (the next
 * samples waiting) are left before the gain */
__attribute__((noinline)) static void mar_mix_spl(int32_t *xb, const int32_t *yb, int32_t *zb, uint32_t n, int32_t ring,
                                                 int nzon, uint32_t *nzp, int32_t dist)
{
    uint32_t i, nz = *nzp;
    if (ring)                                        /* (RING: VCO 1 x VCO 2 into both) */
        for (i = 0; i < n; i++) {
            int32_t a = xb[i + 1], b = yb[i];
            xb[i + 1] = mar_ring(a, b, a, ring) >> 1, zb[i + 1] = mar_ring(a, b, b, ring) >> 1;
        }
    else
        for (i = 0; i < n; i++)
            xb[i + 1] >>= 1, zb[i + 1] = yb[i] >> 1;
    for (i = 0; i < n; i++) {
        int32_t z = 0;
        if (nzon) {
            nz = nz * 1664525u + 1013904223u;
            z = ((((int32_t)(nz >> 16) - 32768) * 23) >> 5) >> 1;
        }
        xb[i] = ((xb[i] * 23) >> 4) + z, zb[i] = ((zb[i] * 23) >> 4) + z;
    }
    if (dist) {
        int32_t dg = 256 + dist * 12;
        for (i = 0; i < n; i++)
            xb[i] = mar_sat((xb[i] * dg) >> 8), zb[i] = mar_sat((zb[i] * dg) >> 8);   /* (soft) */
    }
    *nzp = nz;
}

/* the balance (Q8) from the sides' energies: each side to the level of their mean power, never lifted by more than
 * 6 dB. Sides further apart than 7:1 in power (8.5 dB) are first mixed toward
 * each other, by sixteenths up to the middle (each side (1 - c) its own + c the other's), until they are within it: the
 * image centred, the timbres as far apart as that leaves them (the mix's energies from the sides' and their
 * correlation in this block). c[0], c[1]: the left's and the right's gains, c[2]: the mix, sixteenths (noinline: per block, measured apart from mare_render's per-sample loops, tests/target_budget.py) */
__attribute__((noinline)) static void mar_bal_gains(uint32_t fl, uint32_t fr, uint32_t pl, uint32_t pr, int32_t px,
                                                 int32_t *c)
{
    uint32_t j = 0, at, gl, gr;
    int64_t el = fl, er = fr;
    if (el > 7 * er || er > 7 * el) {            /* fx: the sides' cross energy, this block's correlation (Q15) on
                                                  * the leaky sums: a mix's energies exact */
        uint32_t sq = isqrt32(pl) * isqrt32(pr);
        int64_t rho;
        while (px >= 32768 || px <= -32768)      /* (32-bit divides only: the FM-1 has no 64-bit one) */
            px >>= 1, sq >>= 1;
        rho = sq ? clamp(px * 32768 / (int32_t)sq, -32768, 32768) : 0;
        int64_t fx = (rho * (int64_t)isqrt32(fl) * (int64_t)isqrt32(fr)) >> 15;
        for (j = 1; j < 8u; j++) {
            int64_t a = (int64_t)(16 - j) * (16 - j), b = (int64_t)j * j, x = 2 * (int64_t)j * (16 - j) * fx;
            el = (a * fl + b * fr + x) >> 8, er = (a * fr + b * fl + x) >> 8;
            el = el < 1 ? 1 : el, er = er < 1 ? 1 : er;
            if (el <= 7 * er && er <= 7 * el)
                break;
        }
        if (j == 8u)
            el = er = ((int64_t)fl + fr) >> 1;
    }
    at = isqrt32((uint32_t)((el >> 1) + (er >> 1))) << 8;
    gl = (uint32_t)clamp((int32_t)(at / (isqrt32((uint32_t)el) + 1u)), 1, 512);
    gr = (uint32_t)clamp((int32_t)(at / (isqrt32((uint32_t)er) + 1u)), 1, 512);
    c[0] = (int32_t)gl, c[1] = (int32_t)gr, c[2] = (int32_t)j;
}

/* ST/SP: the left's gain against the right's (Q12, 0 .. x 2): ENV 2 over ENV 1, so the left follows ENV 2
 * (the instrument's filter EG on VCA 1). ENV 2 runs while the matrix reads it (voice.c): a sound with no cable from ENV 2
 * has both sides on ENV 1 */
__attribute__((noinline)) static int32_t mar_split(const track_t *t, const voice_t *v)   /* (noinline: per block, its
                                                  * slots' scan measured apart, tests/target_budget.py) */
{
    uint32_t k;
    for (k = 0; k < MX_SLOTS; k++)
        if (t->p[mx_pid(k)] == MS_ENV2 && t->p[mx_pid(k) + 1u] != MT_OFF)
            break;
    if (k == MX_SLOTS)
        return 4096;
    return clamp((int32_t)(((uint32_t)(v->env2 >> 8) << 12) / (uint32_t)((v->env >> 8) + 1)), 0, 8192);
}
static inline int mar_is_para(int m) { return m == V_PARA || m == V_PARAT; }   /* PARA, PARA+ */
/* the VCOs a sound uses: VCO 1 and 2 always, VCO 3 and 4 unless OFF; *idx their numbers (0 .. 3) */
static uint32_t mar_vcos(const int16_t *p, uint8_t *idx)
{
    uint32_t n = 2;
    idx[0] = 0, idx[1] = 1;
    if (p[P_MFFM] > MAR_VOFF)
        idx[n++] = 2;
    if (p[P_MRING] > MAR_VOFF)
        idx[n++] = 3;
    return n;
}
/* PARA, PARA+ (P_VOICE V_PARA, V_PARAT): the part plays one voice (as LEG / MONO: voice.c),
 * the held keys (its note stack: the keyboard's and the sequencer's alike, oldest first) share its VCOs as the
 * instrument's paraphonic unison: the newest keys, as many as there are VCOs, each VCO to key j n / nv (four VCOs: one
 * key all four, two keys VCO 1+2 and 3+4, three 1+2, 3, 4, four one each; two VCOs: 1 the older, 2 the newer). A key let
 * go hands its VCOs to the keys still held; all up, each keeps its last note through the release. Each VCO glides to its
 * note as the voice would (GLIDE, GLMOD RATE / TIME / EXP); the first note jumps. The voice's own pitch (the filter's
 * key follow, the bend, LFO, VINTAGE) is the newest key's: mare_render plays each VCO at its pitch here less the
 * voice's (mar_render). Once a block (mare_block), on the part's core */
/* MENU > SEQ > PARA ASSIGN and PARA ROUND ROBIN: the instrument's, not the sound's (kept with the settings:
 * project.c side record). PLAYED (the default): the keys share the VCOs by the order they went down, as above. LOW: the
 * held keys by pitch, lowest first (VCO 1 and its share the lowest key; two keys VCO 1+2 the lower, 3+4 the upper;
 * three 1+2 the lowest, then 3, 4; four one each), whatever the order and the phrase before; more keys than VCOs: the
 * lowest ones. HIGH: the same, highest first (more keys than VCOs: the highest ones). ROUND ROBIN (PLAYED only; OFF the
 * default): each key down turns the shares one VCO on (1 2 3 4 1 ...), so a key struck again over held ones
 * lands on other VCOs, other intervals */
enum { PA_PLAYED, PA_LOW, PA_HIGH, PA_N };
static uint8_t para_assign, para_rr;
/* PARA NOTE-OFF:
 * KEEP (0, the default; the instrument's own, Update Unison on Note-Off off): a key let go leaves its VCOs on its note
 * under the shared envelope until a key goes down; HAND BACK (1): they go to the keys still held at once */
static uint8_t para_hand;                         /* (MENU > SEQ > PARA NOTE-OFF, both boards) */
/* x / nv as x MAR_RCP[nv] >> 8, exact for nv 2 .. 4 (mar_vcos) and x = q nk <= 12 (x 86 >> 8: x / 3 up to x 128) */
static const uint8_t MAR_RCP[5] = {0, 0, 128, 86, 64};
static void mar_para(track_t *t, mar_part_t *A)
{
    const int16_t *p = t->p;
    uint8_t idx[4], s[8];
    uint32_t nv = mar_vcos(p, idx), nk = t->nmono, j, r, m = para_assign % PA_N;
    int32_t g = p[P_GLIDE];
    uint32_t top = nk ? t->mono_stack[nk - 1u] : A->pk, down = nk > A->pnk || (nk == A->pnk && top != A->pk);
    A->pk = (uint8_t)top;
    if (nk && (para_hand || down || !A->pq[idx[0]])) {   /* (KEEP: until a key goes down, nothing moves) */
        uint32_t k0 = nk > nv ? nk - nv : 0u, flip = m == PA_HIGH ? 0xFFu : 0u;
        if (down && ++A->rr >= nv)                   /* a key down: ROUND ROBIN's next turn */
            A->rr = 0;
        r = m == PA_PLAYED && para_rr && A->rr < nv ? A->rr : 0u;
        if (m != PA_PLAYED)                          /* (by pitch: every key held, the order forgotten) */
            k0 = 0;
        for (j = 0; j < nk - k0; j++) {              /* the keys: by the order they went down, or by pitch (HIGH:
                                                      * flipped, highest first) */
            uint8_t x = (uint8_t)(t->mono_stack[k0 + j] ^ flip), *e = &s[j];
            if (m != PA_PLAYED)                      /* (the insertion: by pointer, the tightest inner loop) */
                for (; e > s && e[-1] > x; e--)
                    *e = e[-1];
            *e = x;
        }
        nk -= k0;
        nk = nk < nv ? nk : nv;                      /* (by pitch, more keys than VCOs: the lowest / highest ones) */
        int32_t tk = 1;                              /* (TIME: as voice.c glide_set, its ticks from GLIDE; once, not
                                                      * a VCO: the loop below divides by it alone) */
        if (p[P_GLMODE] == 1 && ENV_LIN[g & 127])
            tk = (int32_t)((1u << 24) / ENV_LIN[g & 127]);
        for (j = 0; j < nv; j++) {
            uint32_t c = idx[j], q = j + nv - r;     /* (ROUND ROBIN: share 0 on VCO r, and on round) */
            q -= q >= nv ? nv : 0u;
            uint32_t note = s[(q * nk * MAR_RCP[nv]) >> 8] ^ flip;   /* (q nk / nv: MAR_RCP) */
            if (A->pn[c] != note || !A->pq[c]) {
                int32_t d = ((int32_t)note << 12) - A->pq[c];
                A->pn[c] = (uint8_t)note;
                if (!A->pq[c] || !g)
                    A->pq[c] = (int32_t)note << 12, d = 0;
                d = d < 0 ? -d : d;
                if (p[P_GLMODE] == 1) {              /* (TIME: tk above) */
                    A->gs[c] = d / tk > 0 ? d / tk : 1;
                } else {
                    A->gs[c] = 0;
                }
            }
        }
    }
    A->pnk = t->nmono;
    for (j = 0; j < 4u; j++) {                       /* the glides, a control tick a block */
        int32_t q = A->pq[j], d = ((int32_t)A->pn[j] << 12) - q, st;
        if (!d || !q)
            continue;
        st = p[P_GLMODE] == 2 ? mulq16(d < 0 ? -d : d, ENV_EXP[g & 127]) :
             A->gs[j] ? A->gs[j] : (int32_t)((ENV_LIN[g & 127] * 3u) >> 10);
        st = st < 1 ? 1 : st;
        A->pq[j] = q + (d > 0 ? (d < st ? d : st) : (-d < st ? d : -st));
    }
}
/* the matrix's SEMI / SEM34: a MOD SEQ lane (SEQ1..SEQ4) as whole semitones, AMT / 16 a step (16: one
 * semitone a step, -16 down, 32 two), the lane's value now rounded to a semitone (its SMTH glide steps through them,
 * no drift between steps); SEMI every VCO, SEM34 VCO 3 and 4 alone (PARA: VCO 1 and 2 hold the key, the top moves).
 * From another source: nothing (the matrix's PITCH bends). Every voice of the part alike, once a block */
static void mar_semis(const track_t *t, mar_part_t *A)
{
    int32_t a = 0, b = 0;
    uint32_t k;
    for (k = 0; k < MX_SLOTS; k++) {
        const int16_t *q = &t->p[mx_pid(k)];
        int32_t num, r;
        if ((q[1] != MT_SEMI && q[1] != MT_SEM34) || q[0] < MS_SEQ1 || q[0] > MS_SEQ4 || !q[2])
            continue;
        num = t->ms_cur[q[0] - MS_SEQ1] * clamp(q[2], -64, 63);   /* (a step v: v x 4369, voice.c mseq_q15) */
        r = (num + (num < 0 ? -34952 : 34952)) / 69904;   /* (4369 x 16, rounded) */
        if (q[1] == MT_SEMI)
            a += r;
        else
            b += r;
    }
    A->semi = (int8_t)clamp(a, -48, 48), A->sem34 = (int8_t)clamp(b, -48, 48);
}
/* once a block and part (engine_t block): the arena claimed on the part's core before its voices (they
 * may render on both), the drift's clock, GLMOD EXP (each gliding voice's step a share of what is left: a one-pole
 * approach; voice.c glides by a step a tick), PARA's VCOs */
static void mare_block(track_t *t)
{
    mar_part_t *A = mar_arena(t);
    uint32_t i;
    if (!A)
        return;
    A->clk++;
    if (t->p[P_GLMODE] == 2 && t->p[P_GLIDE])
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &t->v[i];
            if (v->active && (v->pitch_cur != v->pitch16 || v->pitch_frac)) {
                int32_t d = (v->pitch16 << 8) - ((v->pitch_cur << 8) + v->pitch_frac);
                d = mulq16(d < 0 ? -d : d, ENV_EXP[t->p[P_GLIDE] & 127]);
                v->gstep = d > 1 ? d : 1;
            }
        }
    if (mar_is_para(t->p[P_VOICE]))
        mar_para(t, A);
    mar_semis(t, A);
}
/* each VCO's own slow wander (1/4096 semitone): two slow triangles a VCO at rates nothing else shares
 * (0.06 .. 0.33 Hz), from the part's block clock and the voice's number (the same on both cores, every run): its depth
 * DTUNE x 2 + VINTAGE x 3 (DTUNE 24: ~0.7 cents; DTUNE 64 and VINTAGE 64: ~4.9; at most ~9; both 0: none, exact). Free-running analog VCOs: the beating between them moves (the references: 0.3 .. 2.5 Hz,
 * never one rate) */
static const uint32_t MAR_DRA[4] = {221000, 352000, 277000, 427000}, MAR_DRB[4] = {717000, 966000, 592000, 842000};
static inline int32_t mar_tri(uint32_t ph) { return (int32_t)((ph & 0x80000000u ? ~ph : ph) >> 14) - 65536; }   /* +-65536 */
static int32_t mar_drift(uint32_t clk, uint32_t vi, uint32_t k, int32_t dd)
{
    uint32_t s = (vi * 4u + k) * 0x9E3779B9u;
    int32_t a = mar_tri(clk * MAR_DRA[k] + s), b = mar_tri(clk * MAR_DRB[k] + (s << 7));
    return ((a * 3 + b * 2) >> 7) * dd >> 12;       /* (+-65536 x 5 >> 7: +-2560; x dd (0 .. 600) >> 12: up to +-9 cents) */
}

/* the stages after the ladders driven too (the VCAs, the output): each side x (1 + DIST x 4 / 256) into
 * the mixer's leaning tanh (mar_sat), back down by as much (the level as it was, the peaks rounded). What a ladder
 * leaves above its cutoff then has the partials' products in it: the references' floor between partials at 1..3 kHz
 * (strung-out: -49 dB under the peak; MARE at the same brightness -61 dB, -56 with this at DIST 60). nl, nr: the left's
 * and right's samples (nr 0: one side, the mono modes). noinline: measured apart (tests/target_budget.py) */
__attribute__((noinline)) static void mar_post(int32_t *yl, int32_t *yr, uint32_t nr, uint32_t nl, int32_t dist)
{
    int32_t dg = 256 + dist * 4, inv = (int32_t)((256u << 16) / (uint32_t)dg);   /* (Q16: the way back; x 4: was x 12,
                                                  * which took ~2.5 dB of a saw's crest at DIST 60) */
    uint32_t i;
    for (i = 0; i < nl; i++)
        yl[i] = (int32_t)(((int64_t)mar_sat((yl[i] * dg) >> 8) * inv) >> 16);
    for (i = 0; i < nr; i++)
        yr[i] = (int32_t)(((int64_t)mar_sat((yr[i] * dg) >> 8) * inv) >> 16);
}
/* the VCOs' analog path: their outputs AC-coupled (a one-pole high-pass at ~20 Hz: the plateaus sag)
 * and through the coupling's phase (a first-order all-pass at ~500 Hz: the harmonics' phases turned, their sizes kept), so
 * a square's edges are no longer a chip's: on the original's held square (C2) the cycle's crest is 9.1 dB, the upper
 * plateau sags 16 %, the edges take ~15 % of the period, ~17 % overshoot; MARE's was a flat square, crest 0.4 dB. This
 * makes it ~8.5..9.5 dB from C1 to C5 (measured on the original, cycle by cycle). x[0 .. n), st its
 * three states (Q8 low, the all-pass's in and out). noinline: measured apart (tests/target_budget.py) */
#define MAR_AC_K 187                              /* (2 pi 20 Hz / 44.1 kHz, Q16) */
#define MAR_AP_C (-30515)                         /* ((tan(pi 500 / 44100) - 1) / (tan + 1), Q15) */
__attribute__((noinline)) static void mar_ac(int32_t *x, uint32_t n, int32_t *st, int32_t g)
{
    int32_t lp = st[0], x1 = st[1], y1 = st[2];
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t h, y;
        lp += (int32_t)((((int64_t)x[i] << 8) - lp) * MAR_AC_K >> 16);
        h = x[i] - (lp >> 8);
        y = (int32_t)(((int64_t)MAR_AP_C * (h - y1)) >> 15) + x1;
        x1 = h, y1 = y;
        x[i] = (y * g) >> 12;                    /* (g: the cycle wobble's level, Q12) */
    }
    st[0] = lp, st[1] = x1, st[2] = y1;
}
/* a voice's VCO offsets for a block (1/4096 semitone): dr each VCO's drift at depth dd (DTUNE and
 * VINTAGE: 0 none), po PARA's (each VCO's pitch less the voice's). noinline: per block, measured apart from
 * mare_render's per-sample loops (tests/target_budget.py) */
#define MAR_WOBP 400                              /* the cycle wobble: pitch, x VINTAGE x 2048 >> 17 (60: +-9 cents) */
#define MAR_WOBA 300                              /* .. level (60: +-0.6 dB) */
__attribute__((noinline)) static void mar_mods(mar_part_t *A, const voice_t *v, uint32_t vi, int32_t dd, int para,
                                             int32_t *po, int32_t *dr, int32_t vint, uint32_t inc)
{
    uint32_t k;
    if (dd)
        for (k = 0; k < 4u; k++)
            dr[k] = mar_drift(A->clk, vi, k, dd > 600 ? 600 : dd);
    if (vint) {                                  /* every cycle a VCO's own: a new pitch about once a
                                                  * cycle (the block's share of VCO 1's cycle against a random number),
                                                  * and the voice's level with VCO 1's: the original's held square
                                                  * varies cycle to cycle (VINTAGE 60, C2: period sd 4.2 cents and
                                                  * peaks 0.26 dB as measured; the original's 10.6 and 1.5 hold a
                                                  * second VCO's beating). Its mean 0: no drift in tune */
        uint32_t share = (uint32_t)(((uint64_t)inc * CTL) >> 16);   /* (a cycle 65536) */
        for (k = 0; k < 4u; k++) {
            uint32_t h = (A->clk * 4u + k) * 0x85EBCA77u ^ (vi + 7u) * 0xC2B2AE3Du;
            h ^= h >> 13, h *= 0x27D4EB2Fu, h ^= h >> 16;
            if ((h & 0xFFFFu) < share) {
                A->wp[vi][k] = (int16_t)((((int32_t)((h >> 16) & 0xFFFu) - 2048) * vint * MAR_WOBP) >> 17);
                if (!k)
                    A->wa[vi] = (int16_t)(4096 + ((((int32_t)(((h * 0x9E3779B1u) >> 20) & 0xFFFu) - 2048) * vint *
                                                   MAR_WOBA) >> 17));
            }
            dr[k] += A->wp[vi][k];
        }
    }
    if (vint)                                    /* each VCO's jitter: a new random offset a block, up to
                                                  * +-0.4 cent at VINTAGE 127: the partials' skirts, energy between them
                                                  * (the references' floor, ~20 dB over a clean VCO's) */
        for (k = 0; k < 4u; k++) {
            uint32_t h = (A->clk * 4u + k) * 0x9E3779B1u ^ (vi + 1u) * 0x85EBCA77u;
            h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 13;
            dr[k] += (((int32_t)(h & 0xFFFu) - 2048) * vint) >> 14;
        }
    if (para) {
        int32_t base = (v->pitch_cur << 8) + v->pitch_frac;
        for (k = 0; k < 4u; k++)
            po[k] = A->pq[k] ? A->pq[k] - base : 0;
    }
    if (A->semi | A->sem34)                      /* the matrix's SEMI, SEM34 (mar_semis) */
        for (k = 0; k < 4u; k++)
            po[k] += (A->semi + (k >= 2u ? A->sem34 : 0)) * 4096;
}
/* ST/SP's output: the left at the right's gain x rs (Q12), into out and the side (noinline: only ST/SP
 * runs it, measured apart, tests/target_budget.py) */
__attribute__((noinline)) static void mar_out_split(const int32_t *yb, const int32_t *xb, int32_t rs,
                                                  const vmod_t *m, int32_t *out, int32_t *sd, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t yl = yb[i], yr = xb[i], a = (amp_at(m, i) * VOICE_FS) >> 15;   /* (the balance's gains: in yb, xb) */
        int32_t l = (int32_t)(((int64_t)yl * a * rs) >> 26), r = (yr * a) >> 14;
        out[i] += l + r;
        if (sd)
            sd[i] += l - r;
    }
}

static void mare_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *ev = vm_ev(t, m);              /* the EDIT values: the voice's own or the part's */
    const int16_t *p = t->p;
    uint32_t mode = (uint32_t)clamp(ev[7], MTM_ST, MTM_STSP), i, ph1 = v->ph[0], ph2 = v->ph[1], nz = v->ph[2];
    uint32_t inc = m->inc, inc2, fil = MTM_DEC[mode].fil, nzon = MTM_DEC[mode].nz, lp2 = fil == MTF_ST || fil == MTF_SPL;
    int32_t dt = clamp(ev[2], 0, 127), sf = clamp(ev[3], -64, 63), sp = clamp(ev[6], -64, 63) * 84;
    int32_t off = (dt * dt * 52) >> 10, sync = sf > 0, fmd = sf < 0 ? sf * sf : 0, dev = m->shape - (64 << 8);
    int32_t cut = (ev[4] << 8) + m->cutoff + ((v->pitch16 - 60 * 16) << 3), res = clamp(ev[5], 0, 127), k;
    int32_t cutr = m->cutb_on ? cut - m->cutoff + m->cutb : cut;   /* filter 2's: CUTOFF 2
                                                      * patched, its own cables instead of CUTOFF 1's (voice.c) */
    int32_t res2 = clamp(res + m->res2, 0, 127);    /* VCF 2's resonance: RES + the matrix's RES2 */
    int one = fil == MTF_ST && !sp && !m->cutb_on && res2 == res;   /* (STER, SPACE 0: both sides the same filter) */
    if (one && (uint32_t)(v->s[3] - v->s[7] + 4) > 8u)   /* .. once the two ladders' outputs have met */
        one = 0;                                     /* (the side kept while they meet) */
    int32_t *sd = (t->f2v & F2V_ESIDE) && (fil == MTF_SPL || (fil == MTF_ST && (!one || mode == MTM_STSP))) ?
                  f2_sd[C1_IX(out)] : 0;
    int32_t xb[CTL + 1], yb[CTL];                    /* the mix (xb[0]: the sample waiting from the last block), the
                                                      * left side (0.7: on the stack, the voices render on two cores) */
    int32_t zb[CTL + 1];                             /* (SPLT: VCO 2 alone, the right ladder's; zb[0] waiting) */
    int32_t ffm = clamp(m->ffm, 0, 32767), fdev = (ffm * MAR_FFM_ST) >> 15;   /* (the matrix's FLTFM and RING
                                                      * alone; P_MFFM / P_MRING are VCO 3 / 4) */
    int32_t ring = clamp(m->ring, 0, 32767) >> 1;   /* (Q14) */
    int16_t fb[CTL];                                 /* (FILTER FM: VCO 2's samples, for the cutoffs) */
    int bal = !one && fil != MTF_SER && fil != MTF_PAR;   /* two sides apart: their levels matched (PARA
                                                      * is the instrument's parallel out: the high-pass and the
                                                      * low-pass summed, the same in both ears, never one a side) */
    int32_t bc[3];                                   /* (Q8 gains, the mix: mar_bal_gains; bal alone) */
    uint32_t pl = 0, pr = 0;
    int32_t px = 0;
    int32_t sl[4], sr[4];
    mtl_t cl, cr;
    mtw_t w, w34;
    mar_part_t *A = mar_arena(t);                      /* VCO 3 / 4, PARA, the drift */
    uint32_t vi = (uint32_t)(v - t->v) % NVOICE, inc3 = 0, inc4 = 0;
    int32_t v3 = p[P_MFFM], v4 = p[P_MRING], po[4] = {0, 0, 0, 0}, dr[4] = {0, 0, 0, 0}, rs = 4096;
    int split = mode == MTM_STSP;
    /* DIST eased off above C5, to a quarter at C7: a bright high note's drive products fold over the
     * Nyquist (C7, DIST 60: -17 dB of alias under the fundamental), the low notes' grit kept */
    int32_t dk = (p[P_DIST] * clamp(256 - ((v->pitch16 - 72 * 16) >> 1), 64, 256)) >> 8;
    if (n > CTL)
        n = CTL;
    for (k = 0; k < 4; k++) {                        /* (a voice another engine held: in range) */
        sl[k] = clamp(v->s[k], -500000000, 500000000);
        sr[k] = clamp(v->s[4 + k], -500000000, 500000000);
    }
    mtw_set(&w, (clamp(ev[0], 0, 127) << 8) + (sync ? 0 : dev), (uint32_t)clamp(p[P_VINT], 0, 127) * 253640u);   /* (VINTAGE
                                                      * 127: the square 50.75 / 49.25 %, its evens ~-33 dB: the
                                                      * original's -32 .. -46 at h4 .. h16) */
    if (A) {                                         /* each VCO's drift; PARA: each VCO at its key's
                                                      * pitch, against the voice's (mar_para); the matrix's SEMI, SEM34 */
        mar_mods(A, v, vi, dt * 2 + clamp(p[P_VINT], 0, 127) * 3, mar_is_para(p[P_VOICE]), po, dr, clamp(p[P_VINT], 0, 127), inc);
    } else
        v3 = v4 = MAR_VOFF;
    {   /* P>VCO, the instrument's PITCH MOD ASSIGN: the LFO's and ENV's pitch (m->pmod) on VCO 1 or
         * VCO 2 alone (VCO2 with SYNC: the sync swept, VCO 1 in tune), as PALOMINO's; VCO 3 with VCO 1,
         * VCO 4 with VCO 2 (the instrument's 1&3 / 2&4). DTUNE over four VCOs at unrelated shares (1 down, 2 up,
         * 3 down 0.29, 4 up 0.57): every pair beats at its own rate */
        uint32_t pt = (uint32_t)clamp(p[P_PTGT], PT_BOTH, PT_VCO2);
        int32_t pm1 = pt == PT_VCO2 ? m->pmod : 0, pm2 = pt == PT_VCO1 ? m->pmod : 0;
        if (off != m->vo1 || pm1 || po[0] || dr[0])  /* (vo1, vo2: each VCO its own pitch) */
            inc = hy_inc(m->pitch16, m->fine, m->vo1 - off - pm1 + po[0] + dr[0]);
        inc2 = hy_inc(m->pitch16 + clamp(ev[1], -24, 24) * 16 + (sync ? (sf - 1) * 36 * 16 / 62 + (dev >> 5) : 0),
                      m->fine, off + m->vo2 - pm2 + po[1] + dr[1]);
        if (v3 > MAR_VOFF)
            inc3 = hy_inc(m->pitch16 + clamp(v3, -24, 24) * 16, m->fine, m->vo1 - pm1 - ((off * 297) >> 10) + po[2] + dr[2]);
        if (v4 > MAR_VOFF)
            inc4 = hy_inc(m->pitch16 + clamp(v4, -24, 24) * 16, m->fine, m->vo2 - pm2 + ((off * 584) >> 10) + po[3] + dr[3]);
        if (inc3 | inc4)
            mtw_set(&w34, (clamp(p[P_MW34], 0, 127) << 8) + (sync ? 0 : dev), 0u - (uint32_t)clamp(p[P_VINT], 0, 127) * 190000u);
    }
    xb[0] = v->fs[0];                                /* the VCOs: VCO 1 into xb[1 ..], VCO 2 into yb */
    if (fmd) {                                       /* FM: VCO 2 first, its samples bend VCO 1 */
        mar_vco(&w, &ph2, inc2, yb, n, 0, 0);
        mar_vco(&w, &ph1, inc, xb + 1, n, yb, fmd);
    } else if (sync) {
        if (fil == MTF_SPL)
            zb[0] = v->fs[1];
        /* (the reset's polyBLEP corrects VCO 2's own term only: RING's VCO 1 x VCO 2 and SPLT's ring term go
         * uncorrected, aliasing accepted) */
        mar_vco_sync(&w, ph1, inc, &ph2, inc2, yb, n, fil == MTF_SPL ? &zb[0] : &xb[0]);
        mar_vco(&w, &ph1, inc, xb + 1, n, 0, 0);
    } else {
        mar_vco(&w, &ph1, inc, xb + 1, n, 0, 0);
        mar_vco(&w, &ph2, inc2, yb, n, 0, 0);
    }
    if (fdev)
        for (i = 0; i < n; i++)
            fb[i] = (int16_t)clamp(yb[i], -32767, 32767);
    if (inc3) {                                      /* VCO 3 with VCO 1 (SPLT: the left), VCO 4 with
                                                      * VCO 2 (the right), each at the mix's half as they are; zb[1 ..]
                                                      * free until SPLT's mix */
        mar_vco(&w34, &A->ph[vi][0], inc3, zb + 1, n, 0, 0);
        for (i = 0; i < n; i++)
            xb[i + 1] += zb[i + 1];
    }
    if (inc4) {
        mar_vco(&w34, &A->ph[vi][1], inc4, zb + 1, n, 0, 0);
        for (i = 0; i < n; i++)
            yb[i] += zb[i + 1];
    }
    if (fil == MTF_SPL) {                            /* SPLT: VCO 1 left, VCO 2 right, each at half (as in
                                                      * the mix: a sync's corrections at that scale) a sample late,
                                                      * then x 1.44 (two VCOs apart: the level of the mix's) and the
                                                      * noise into both at the mix's level */
        if (!sync)
            zb[0] = v->fs[1];
        mar_mix_spl(xb, yb, zb, n, ring, (int)nzon, &nz, dk);
        v->fs[0] = xb[n], v->fs[1] = zb[n];
    } else if (ring)                                 /* RING: the plain VCOs down as VCO 1 x VCO 2 comes */
        mar_mix_ring(xb, yb, n, ring, (int)nzon, &nz);
    else if (nzon)                                   /* the mix, each VCO at half (a loud mix warms the ladder's
                                                      * tanh); the noise -3 dB under a VCO */
        for (i = 0; i < n; i++) {
            nz = nz * 1664525u + 1013904223u;
            xb[i + 1] = (xb[i + 1] + yb[i] + ((((int32_t)(nz >> 16) - 32768) * 23) >> 5)) >> 1;
        }
    else
        for (i = 0; i < n; i++)
            xb[i + 1] = (xb[i + 1] + yb[i]) >> 1;
    if (A && fil != MTF_SPL)                         /* the VCOs' analog path (mar_ac), before the drive and
                                                      * the ladders (SPLT: its sides as they were) */
        mar_ac(xb + 1, n, A->ac[vi], p[P_VINT] > 0 && A->wa[vi] ? A->wa[vi] : 4096);
    if (fil != MTF_SPL)
        v->fs[0] = xb[n];
    if (dk && fil != MTF_SPL) {                      /* FX > DIST here: into the ladders, as an overdriven mixer drives
                                                      * them (fx.c leaves MARE's out): up to x 4, into a
                                                      * soft saturator (mar_sat: the mixer's, no hard edge) */
        int32_t dg = 256 + dk * 12;
        for (i = 0; i < n; i++)                      /* (xb[0 .. n), the ladders' samples, as SPLT's
                                                      * mar_mix_spl: xb[1 .. n] left the sample waiting, xb[0], undriven,
                                                      * one sample a block: a step at the block rate, its lines at
                                                      * k f0 +- 1378 Hz ~-45 dB under the note) */
            xb[i] = mar_sat((xb[i] * dg) >> 8);
    }
    if (fdev) {                                      /* FILTER FM: the same filters, VCO 2 on the cutoffs */
        mtf_t fl, fr;
        mtf_set(&fl, cut + 2 * sp, res, fdev);
        if (!one)
            mtf_set(&fr, cutr, res2, fdev);
        if (lp2)
            mar_lp_fm(&fl, fb, xb, yb, n, sl);
        else
            mar_hp_fm(&fl, fb, xb, yb, n, sl);
        if (fil == MTF_SPL)
            mar_lp_fm(&fr, fb, zb, xb, n, sr);
        else if (fil == MTF_SER)
            mar_lp_fm(&fr, fb, yb, yb, n, sr);
        else if (!one)
            mar_lp_fm(&fr, fb, xb, xb, n, sr);
    } else {
        mtl_coef(&cl, cut + 2 * sp, res);            /* (SPACE moves VCF 1, the left or the high-pass, alone, as the
                                                      * instrument's SPACING) the filters, on xb[0 .. n) (a sync may correct a sample
                                                      * after it: one late): the left (or high-pass) into yb, the
                                                      * right into xb */
        if (!one)
            mtl_coef(&cr, cutr, res2);
        if (lp2)
            mar_lp(&cl, xb, yb, n, sl);
        else
            mar_hp(&cl, xb, yb, n, sl);
        if (fil == MTF_SPL)                              /* (SPLT: the right ladder on VCO 2 alone) */
            mar_lp(&cr, zb, xb, n, sr);
        else if (fil == MTF_SER)
            mar_lp(&cr, yb, yb, n, sr);
        else if (!one)
            mar_lp(&cr, xb, xb, n, sr);
    }
    if (dk && fil != MTF_SPL)                        /* the VCAs' and the output's drive (mar_post) */
        mar_post(yb, xb, one || fil == MTF_SER ? 0 : n, n, dk);
    for (i = 0; i < n; i++) {                        /* the sides (SERI: x 2, its band's level made up: two 4-pole
                                                      * slopes cross -12 dB down each; mono) */
        int32_t yl = soft_knee(fil == MTF_SER ? yb[i] : yb[i] >> 1, 16000);
        int32_t yr = one || fil == MTF_SER ? yl : soft_knee(xb[i] >> 1, 16000);
        yb[i] = yl, xb[i] = yr;
        if (bal)                                     /* (the balance: the sides' energies) */
            pl += (uint32_t)(yl * yl) >> 5, pr += (uint32_t)(yr * yr) >> 5, px += (yl * yr) >> 5;
    }
    if (bal) {                                       /* the balance, two sides apart (STER, SPLT with
                                                      * their filters apart): each side to the level of the two sides'
                                                      * mean power, the voice's power kept (the brighter side down 3 dB
                                                      * at most, the darker up 6 dB at most; further apart, the sides
                                                      * mixed toward the middle first: mar_bal_gains): the timbres apart,
                                                      * the levels even. Each side's energy a leaky sum over ~32 blocks
                                                      * (~23 ms: a low note's cycle or two), this block's included */
        uint32_t fl = (uint32_t)v->fs[2], fr = (uint32_t)v->fs[3];
        pl >>= 4, pr >>= 4, px >>= 4;                /* (< 2^26: the sums < 2^31) */
        int fresh = !fl && !fr;
        if (fresh)                                   /* (from silence: this block's at once) */
            fl = pl << 5, fr = pr << 5;
        fl += pl - (fl >> 5), fr += pr - (fr >> 5);
        fl = fl < pl << 3 ? pl << 3 : fl, fr = fr < pr << 3 ? pr << 3 : fr;   /* (a quarter of this block's at least:
                                                      * a side opening by more than 6 dB at once, its gain no more
                                                      * than 6 dB over; a low note's swings in a cycle stay under) */
        v->fs[2] = (int32_t)fl, v->fs[3] = (int32_t)fr;
        if (A) {                                     /* the correlation over the energies' own window: one
                                                      * block's (a cycle or less of a low note) swung the mix toward the
                                                      * middle block to block, a side lifted too far (SPACE -40, RES 64:
                                                      * +1.2 dB) */
            int32_t bx = A->bx[vi];
            bx = fresh ? px << 5 : bx + px - (bx >> 5);
            A->bx[vi] = bx;
            mar_bal_gains(fl, fr, fl, fr, bx, bc);
        } else
            mar_bal_gains(fl, fr, pl, pr, px, bc);
        {   /* the sides mixed toward each other (bc[2] sixteenths) and their gains, slid from the last block's to
             * these over the block (32 samples: in 32nds). The output below at x 1; steady gains (and no arena):
             * every sample at bc exactly */
            static int16_t gz[3];                    /* (no arena: host tests alone) */
            int16_t *g = A ? A->bg[vi] : gz;
            int32_t o0 = g[0], o1 = g[1], o2 = g[2];   /* (a new note: from the voice's last ones, under its attack;
                                                      * an arena just claimed: from 0, a block's fade in) */
            for (i = 0; i < n; i++) {
                int32_t k = (int32_t)i + 1, d = ((xb[i] - yb[i]) * ((o2 << 5) + (bc[2] - o2) * k)) >> 9;
                yb[i] = ((yb[i] + d) * ((o0 << 5) + (bc[0] - o0) * k)) >> 13;
                xb[i] = ((xb[i] - d) * ((o1 << 5) + (bc[1] - o1) * k)) >> 13;
            }
            g[0] = (int16_t)bc[0], g[1] = (int16_t)bc[1], g[2] = (int16_t)bc[2];
        }
    }
    if (split)                                       /* ST/SP: VCF 1 / the left through ENV 2 */
        rs = mar_split(t, v);
    if (rs != 4096)
        mar_out_split(yb, xb, rs, m, out, sd, n);
    else
    for (i = 0; i < n; i++) {                        /* (bc: 1.0 but in the balance) */
        int32_t yl = yb[i], yr = xb[i], a = (amp_at(m, i) * VOICE_FS) >> 15;   /* (the balance's gains: in yb, xb) */
        out[i] += ((yl + yr) * a) >> 14;
        if (sd)                                      /* the side: (left - right) / 2, the left on the left */
            sd[i] += ((yl - yr) * a) >> 14;
    }
    v->ph[0] = ph1;
    v->ph[1] = ph2;
    v->ph[2] = nz;
    for (k = 0; k < 4; k++) {
        v->s[k] = sl[k];
        v->s[4 + k] = one ? sl[k] : sr[k];          /* (one: the right as the left, for SPACE off 0) */
    }
}

/* the matrix's names for MARE's own EDIT values, for its presets' cables (core.h CB) */
enum { MT_MWAVE = MT_E1, MT_MRES = MT_E1 + 5, MT_MSPACE = MT_E1 + 6 };
/* (1.0) VCO3, VCO4 and WAV34 as a preset's extras: their ids are past 127, so the pair's id byte holds
 * them cast (params.c preset_pairs reads it unsigned) */
#define MAR_XV3 ((int8_t)(P_MFFM + 1))
#define MAR_XV4 ((int8_t)(P_MRING + 1))
#define MAR_XW34 ((int8_t)(P_MW34 + 1))
/* (1.0) the INSERT types MARE's sounds use (fx.c IT_SDLY, IT_PDLY: N_ITYPE's STDLY, PPDLY) */
enum { MAR_ISDLY = 17, MAR_IPDLY = 18 };
/* (1.0) the bank for the four VCOs, PARA, the drift, the drive and the stereo delay
 * after the instrument's patch books (routings and panel settings
 * written anew, no one's text or data) and sounds measured on the original: the octave-and-fifth paraphonic
 * stack whose old notes hang under the new while each VCO glides, the four VCOs brought in, the filter opened, the
 * delay's repeats building, SPACING moved, the wide "hug", the square and its PWM, keys over the running sequencer.
 * INIT is the instrument with nothing patched; every other sound moves (its cables in MARE_PATCHES).
 * MARE's exceptions: the DLY / REV sends, its stereo delay INSERT and its drive (DIST into the ladders).
 * {WAVE, VCO2, DTUNE, SY/FM, CUT, RES, SPACE, MODE}, {A D S R}, ENV DEST FLT, mono; FX(DIST, CHORUS, DELAY, REVERB);
 * extras (VCO3 / VCO4 are P_MFFM / P_MRING, MAR_VOFF their default; WAV34 P_MW34, SAW its default) */
static const preset_t MARE_PRESETS[] = {
    /* 0: the Initialized Patch: four saws at 8', STEREO, cutoff at noon, the filter EG opening both ladders, the
     * mixer at noon (a touch of drive), the delay's mix at 0; nothing patched */
    {"INIT", {72, 0, 10, 0, 66, 25, 4, MTM_ST}, {21, 80, 127, 70}, 0, 0, FX(0, 0, 0, 0),
     XP(P_VOICE + 1, V_PARAT, P_VINT + 1, 10)},
    /* the riffs: the octave-and-fifth stack in PARA (no retrigger, each VCO gliding 150 ms, the old note hanging
     * under the new); the same through the stereo delay at 428 ms, its repeats building; a PARA+ voice the sequence
     * and the keys share */
    {"HANGING RIFF", {MTW_SAW, 12, 24, 0, 48, 25, 6, MTM_ST}, {21, 80, 127, 85}, 12, 0, FX(10, 0, 0, 0),
     XP(P_VOICE + 1, V_PARA, P_GLIDE + 1, 69, P_GLMODE + 1, 2, MAR_XV3, -12)},
    {"RIFF ECHOES", {MTW_SAW, 12, 24, 0, 66, 20, 8, MTM_ST}, {21, 80, 127, 70}, 0, 0, FX(10, 0, 0, 0),
     XP(P_VOICE + 1, V_PARA, P_GLIDE + 1, 69, P_GLMODE + 1, 2, MAR_XV3, 0)},
    {"PEDAL SEQ", {MTW_SAW, 12, 24, 0, 50, 60, 8, MTM_ST}, {21, 80, 127, 70}, 0, 0, FX(10, 0, 20, 0),
     XP(P_VOICE + 1, V_PARA, MAR_XV3, 12, MAR_XV4, 24, P_VINT + 1, 10)},
    /* pads and strings: the wide fat four-saw pad; SPACING moved on a dark para drone; para strings gliding; PWM
     * strings; the split stereo envelopes; the travelling para pad; the slow build */
    {"FAT HUG PAD", {MTW_SAW, 12, 24, 0, 50, 60, 4, MTM_ST}, {21, 80, 127, 116}, 14, 0, FX(10, 0, 0, 0),
     XP(MAR_XV3, -12, MAR_XV4, 24, P_VINT + 1, 15, P_ITYPE + 1, 0)},
        /* the paraphonic pedal sounds (the instrument's signature sound): a bass held on VCO 1 + 2
     * while a high line hops on VCO 3 + 4 (PARA, no retrigger), the dark ladders opening in slow waves, SPACING lifting
     * the left (VCF 1) over the right */
    {"SPACE DRIFT", {MTW_SAW, 12, 24, 0, 45, 60, 8, MTM_ST}, {21, 80, 127, 116}, 0, 0, FX(10, 0, 20, 0),
     XP(P_VOICE + 1, V_PARA, P_GLIDE + 1, 100, P_GLMODE + 1, 2, MAR_XV3, -12)},
    {"GLIDE STRING", {MTW_SAW, 12, 30, 0, 56, 95, 0, MTM_ST}, {92, 100, 110, 116}, 0, 0, FX(25, 0, 0, 12),
     XP(P_VOICE + 1, V_PARA, P_GLIDE + 1, 80, P_GLMODE + 1, 2, MAR_XV3, -12)},
        {"HIGH SWITCH", {MTW_SAW, 12, 24, 0, 50, 60, 8, MTM_ST}, {21, 80, 127, 70}, 0, 0, FX(10, 0, 24, 0),
     XP(P_VOICE + 1, V_PARA, P_GLIDE + 1, 33, P_GLMODE + 1, 2, MAR_XV3, -12)},
    {"SPLIT CHORDS", {MTW_SAW, 12, 24, 0, 29, 30, 2, MTM_STSP}, {68, 97, 108, 116}, 0, 0, FX(10, 0, 0, 0),
     XP(P_VOICE + 1, V_PARAT, P_VINT + 1, 10)},
    {"TRAVEL PARA", {MTW_SAW, 12, 24, 0, 50, 60, 8, MTM_ST}, {21, 80, 127, 116}, 0, 0, FX(10, 0, 0, 0),
     XP(P_VOICE + 1, V_PARAT, MAR_XV3, -12, MAR_XV4, 24, P_L2RATE + 1, 43)},
    {"SLOW BLOOM", {MTW_SAW, 12, 24, 0, 50, 60, 8, MTM_ST}, {60, 100, 127, 116}, 0, 0, FX(10, 0, 0, 0),
     XP(P_VOICE + 1, V_PARAT, MAR_XV4, 24, P_ITYPE + 1, MAR_ISDLY, P_E2REL + 1, 116)},
    /* leads: the square and its PWM; a square with an LFO on each ladder; the four-saw legato glide; sync swept by
     * the filter EG; VCO 2 and 4 charging up; the duophonic para lead */
    {"PWM LEAD", {100, 0, 30, 0, 60, 45, 6, MTM_ST}, {39, 80, 120, 45}, 0, 1, FX(10, 0, 0, 0),
     XP(P_VINT + 1, 10, P_GLIDE + 1, 0, P_GLMODE + 1, 2, P_L2RATE + 1, 30)},
    {"SQUARE LEAD", {66, 0, 30, 0, 60, 45, 6, MTM_ST}, {0, 57, 127, 68}, 0, 1, FX(10, 0, 0, 0),
     XP(P_VINT + 1, 10, P_GLIDE + 1, 0, P_GLMODE + 1, 2, P_LRATE + 1, 70)},
    {"SPACE SEQ", {MTW_SAW, -12, 24, 0, 50, 45, 8, MTM_ST}, {21, 80, 127, 70}, 0, 0, FX(10, 0, 0, 0),
     XP(P_VOICE + 1, V_PARA, MAR_XV3, 0, MAR_XV4, 12, P_VINT + 1, 15)},
    {"SHRED LEAD", {MTW_SAW, 12, 40, 30, 66, 45, 4, MTM_ST}, {2, 70, 110, 24}, 0, 1, FX(22, 0, 0, 0),
     XP(MAR_XV3, -12, MAR_XV4, 24, MAR_XW34, MTW_TRI, P_GLIDE + 1, 15)},
    {"SWITCH SEQ", {MTW_SAW, 12, 24, 0, 50, 60, 8, MTM_ST}, {21, 80, 127, 70}, 0, 0, FX(10, 0, 0, 0),
     XP(P_VOICE + 1, V_PARA, MAR_XV3, 12, MAR_XV4, 24, P_VINT + 1, 15)},
        {"FIFTH PEDAL", {MTW_SAW, 7, 24, 0, 54, 45, 8, MTM_ST}, {21, 80, 127, 116}, 0, 0, FX(10, 0, 0, 0),
     XP(P_VOICE + 1, V_PARAT, P_GLIDE + 1, 0, P_GLMODE + 1, 2, MAR_XV3, -12)},
    /* basses: the rubber bass on four real VCOs; squares with the fifth in PARALLEL; the four VCOs of the demo, a
     * square pair under two saws; the resonant sweep under short repeats; the para bass (a root held under the
     * moving notes); the synced bass growling from FILTER FM */
    {"RUBBER BASS", {64, 0, 30, 0, 56, 44, 10, MTM_ST}, {0, 33, 127, 62}, 0, 1, FX(10, 0, 0, 0),
     XP(P_VOICE + 1, V_PUNI2, P_MWDST + 1, MD_SHP, P_GLIDE + 1, 12, P_VELF + 1, 10)},
    {"PIPE BASS", {MTW_SAW, 0, 24, 0, 34, 35, 4, MTM_ST}, {0, 80, 127, 116}, 0, 1, FX(10, 0, 0, 0),
     XP(MAR_XV3, -12, MAR_XW34, MTW_SAW, P_VINT + 1, 15)},
    {"STACK BASS", {MTW_SAW, 12, 24, 0, 54, 60, 8, MTM_ST}, {0, 80, 120, 57}, 0, 1, FX(10, 0, 0, 0),
     XP(P_VINT + 1, 10)},
    {"SWEEP BASS", {MTW_SAW, 12, 24, 0, 36, 30, 8, MTM_ST}, {0, 64, 110, 116}, 0, 1, FX(5, 0, 0, 0),
     XP(P_E2ATK + 1, 110, P_VINT + 1, 8)},
    {"PARA BASS", {MTW_SAW, 12, 24, 0, 40, 60, 4, MTM_ST}, {0, 80, 127, 116}, 22, 0, FX(10, 0, 0, 0),
     XP(P_VOICE + 1, V_PARAT, P_GLIDE + 1, 63, P_GLMODE + 1, 2, MAR_XV3, -12)},
    {"FIFTH SEQ", {MTW_SAW, 12, 24, 0, 40, 60, 8, MTM_ST}, {0, 80, 127, 57}, 16, 1, FX(10, 0, 0, 0),
     XP(MAR_XV3, -12, MAR_XV4, MAR_VOFF, MAR_XW34, MTW_SAW, P_VINT + 1, 20)},
    /* brass: the filter EG slower than the amp's (the manual's tip); the IO brass's growl; para brass re-blatting
     * every chord; velocity and pressure brass */
    {"BRASS SWELL", {MTW_SAW, 12, 24, 0, 30, 22, 4, MTM_ST}, {96, 90, 115, 116}, 0, 0, FX(10, 0, 25, 0),
     XP(P_VOICE + 1, V_PARAT, MAR_XV3, -12, MAR_XV4, 24, P_E2ATK + 1, 104)},
    {"BERLIN SEQ", {MTW_SAW, 12, 24, 0, 74, 40, 8, MTM_ST}, {0, 80, 127, 127}, 0, 1, FX(10, 0, 0, 0),
     XP(MAR_XV3, -12, MAR_XW34, MTW_SAW, P_VINT + 1, 10, P_ITYPE + 1, MAR_ISDLY)},
    {"PARA BRASS", {MTW_SAW, 0, 24, 0, 57, 15, 5, MTM_ST}, {0, 70, 110, 45}, 0, 0, FX(30, 0, 0, 0),
     XP(P_VOICE + 1, V_PARA, MAR_XV3, -12, MAR_XV4, 24, P_VINT + 1, 15)},
        {"WIDE PEDAL", {MTW_SAW, 0, 40, 0, 56, 40, 4, MTM_SPLT}, {21, 80, 127, 116}, 0, 0, FX(10, 0, 0, 0),
     XP(P_VOICE + 1, V_PARA, P_GLIDE + 1, 45, P_GLMODE + 1, 2, MAR_XV3, -12)},
    /* keys, organ and plucks: triangle keys opened by the wheel; the square organ's three footages; the triangle
     * counterpoint pluck over three octaves; the synced pluck through the ping-pong; chewy para pulse chords */
    {"WHEEL KEYS", {MTW_SAW, 12, 24, 0, 38, 35, 6, MTM_ST}, {15, 90, 50, 116}, 20, 0, FX(10, 0, 0, 0),
     XP(P_VOICE + 1, V_PARAT, MAR_XV3, -12, MAR_XV4, 24, P_LRATE + 1, 81)},
    {"BENDY ORGAN", {MTW_SAW, 12, 24, 0, 54, 50, 6, MTM_ST}, {0, 40, 127, 60}, 0, 0, FX(10, 0, 30, 0),
     XP(MAR_XV3, -12, MAR_XV4, 24, P_VINT + 1, 15)},
    {"SPACE STRING", {MTW_SAW, 0, 24, 0, 46, 50, 4, MTM_SPLT}, {68, 92, 70, 116}, 0, 0, FX(0, 0, 60, 0),
     XP(P_TRANS + 1, 12, P_L2RATE + 1, 70, P_E2ATK + 1, 92, P_E2DEC + 1, 103)},
    {"VOYAGE PLUCK", {MTW_SAW, 0, 24, 0, 48, 45, 6, MTM_ST}, {0, 94, 20, 80}, 32, 0, FX(10, 0, 0, 0),
     XP(P_VOICE + 1, V_PARAT, MAR_XV3, -12, MAR_XV4, 12, P_VINT + 1, 10)},
    {"DARK CHORDS", {MTW_SAW, 12, 24, 0, 46, 30, -6, MTM_ST}, {0, 21, 127, 116}, 0, 0, FX(20, 0, 0, 0),
     XP(P_VOICE + 1, V_PARA, MAR_XV3, -12, MAR_XV4, 24, P_L2RATE + 1, 43)},
};

/* the presets' cables (core.h preset_patch_t: slot, source, destination, amount) and up to 4 more extras. CUT moves
 * both filters until CUTB is patched, then CUT the left (VCF 1) alone. CUT a step an amount unit at full source,
 * SPACE / XMOD two units, PITCH squared (+-24 at 63); E1 + 2 is DTUNE */
static const pcable_t MARC_HANG[] = {CB(1, ENV2, CUT, 8), CB(2, LFO3, CUT, 3), CB(3, AT, CUT, 14), CB(4, VEL, CUT, 6),
                                    CB(5, MW, CUT, 20), CB(6, MAC2, MSPACE, 14), CB(7, KEY, CUT, -32)};
static const pcable_t MARC_ECHO[] = {CB(1, LFO3, CUT, 3), CB(2, AT, CUT, 12), CB(3, VEL, CUT, 8), CB(4, MAC2, MSPACE, 14), CB(5, KEY, CUT, -32)};
static const pcable_t MARC_PSEQ[] = {CB(1, SEQ1, SEM34, 16), CB(2, SEQ2, CUT, 3), CB(3, SEQ3, MSPACE, 2), CB(4, SEQ4, TRIG2, 63),
                                    CB(5, ENV2, CUT, 5), CB(7, AT, CUT, 12)};
static const pcable_t MARC_HUG[] = {CB(1, LFO3, CUT, 5), CB(2, LFO2, PIT2, 2), CB(3, VEL, CUT, 6), CB(4, AT, CUT, 10),
                                   CB(5, MW, CUT, 16), CB(6, MAC2, E1 + 2, 20), CB(7, KEY, CUT, -32)};
static const pcable_t MARC_DRIFT[] = {CB(1, LFO3, CUT, 30), CB(2, ENV2, CUT, 14), CB(3, AT, CUT, 16),
                                     CB(4, MW, MSPACE, 24), CB(5, LFO2, MSPACE, 14), CB(6, VEL, CUT, 6),
                                     CB(7, MAC2, MSPACE, 16)};
static const pcable_t MARC_GSTR[] = {CB(1, LFO2, PIT1, 3), CB(2, LFO3, PIT2, 3), CB(3, LFO3, CUT, 4), CB(4, AT, CUT, 12),
                                    CB(5, VEL, CUT, 6), CB(6, MW, CUT, 18), CB(7, KEY, CUT, -32)};
static const pcable_t MARC_HSW[] = {CB(1, LFO3, CUT, 22), CB(2, ENV2, CUT, 10), CB(3, AT, CUT, 16),
                                   CB(4, LFO3, MSPACE, 4), CB(5, MW, MSPACE, 24), CB(6, VEL, CUT, 6)};
static const pcable_t MARC_SPLIT[] = {CB(1, ENV2, CUT, 10), CB(3, VEL, CUT, 6), CB(4, AT, CUT, 12), CB(5, MAC2, MSPACE, 14), CB(6, KEY, CUT, -32)};
static const pcable_t MARC_TRAVEL[] = {CB(1, ENV2, CUT, 28), CB(2, ENV2, L2RATE, 30), CB(3, LFO2, CUT, 10),
                                      CB(4, VEL, CUT, 6), CB(5, AT, CUT, 10), CB(6, MAC2, MSPACE, -16), CB(7, KEY, CUT, -32)};
static const pcable_t MARC_BLOOM[] = {CB(4, AT, CUT, 12), CB(5, VEL, CUT, 6), CB(6, MAC2, MSPACE, 14), CB(7, KEY, CUT, -32)};
static const pcable_t MARC_PWM[] = {CB(1, LFO2, SHAPE, 24), CB(2, MW, SHAPE, 10), CB(3, VEL, CUT, 6), CB(4, AT, CUT, 8)};
static const pcable_t MARC_SQR[] = {CB(1, VEL, CUT, 6), CB(2, AT, CUT, 8)};
static const pcable_t MARC_GLEAD[] = {CB(8, DC, RES2, 40), CB(1, SEQ1, SEM34, 16), CB(2, SEQ2, MSPACE, 2), CB(3, SEQ3, CUT, 3), CB(4, SEQ4, TRIG2, 63),
                                     CB(5, ENV2, CUT, 5), CB(6, KEY, CUT, -32), CB(7, AT, CUT, 12)};
static const pcable_t MARC_SHRED[] = {CB(1, ENV2, SHAPE, 30), CB(2, MW, SHAPE, 24), CB(3, AT, CUT, 12), CB(4, VEL, CUT, 8), CB(5, KEY, CUT, -32)};
static const pcable_t MARC_CHARGE[] = {CB(1, SEQ1, SEMI, 16), CB(2, SEQ2, SEM34, 16), CB(3, SEQ3, CUT, 1), CB(4, SEQ4, TRIG2, 63),
                                      CB(5, KEY, CUT, -32), CB(6, AT, CUT, 12), CB(7, ENV2, CUT, 6), CB(8, LFO3, MSPACE, 4)};
static const pcable_t MARC_FPED[] = {CB(1, LFO3, CUT, 18), CB(2, ENV2, CUT, 14), CB(3, AT, CUT, 16),
                                    CB(4, MW, MSPACE, 24), CB(5, LFO2, MSPACE, 4), CB(6, VEL, CUT, 6), CB(7, KEY, CUT, -32)};
static const pcable_t MARC_FIFTH[] = {CB(1, ENV2, CUT, 22), CB(2, VEL, CUT, 8), CB(3, AT, CUT, 8), CB(4, MAC2, E1 + 2, 16)};
static const pcable_t MARC_STACK[] = {CB(2, VEL, CUT, 10), CB(3, AT, CUT, 12), CB(5, MAC2, E1 + 2, 20), CB(6, KEY, CUT, -32)};
static const pcable_t MARC_SWEEP[] = {CB(1, MW, CUT, 30), CB(2, ENV2, CUT, 40), CB(4, ENV2, AMP, 30), CB(3, VEL, CUT, 12), CB(5, MAC2, MSPACE, 14), CB(6, KEY, CUT, -32)};
static const pcable_t MARC_PBASS[] = {CB(1, ENV2, CUT, 28), CB(2, ENV2, L2RATE, 30), CB(3, LFO2, CUT, 10),
                                      CB(4, VEL, CUT, 6), CB(5, AT, CUT, 10), CB(6, MAC2, MSPACE, -16), CB(7, KEY, CUT, -32)};
static const pcable_t MARC_WIDEFM[] = {CB(1, SEQ1, SEMI, 16), CB(2, SEQ2, TRIG1, 63), CB(3, KEY, CUT, -32), CB(4, LFO3, CUT, 3),
                                      CB(5, VEL, CUT, 4), CB(6, ENV2, CUT, 14)};
static const pcable_t MARC_SWELL[] = {CB(1, ENV2, CUT, 40), CB(2, VEL, CUT, 12), CB(3, AT, CUT, 12), CB(6, MAC2, MSPACE, 14), CB(7, KEY, CUT, -32)};
static const pcable_t MARC_IOBRASS[] = {CB(1, SEQ1, SEMI, 16), CB(2, SEQ2, CUT, 1), CB(3, SEQ3, TRIG1, 63), CB(4, SEQ3, TRIG2, 63),
                                       CB(5, SEQ4, MRES, 1), CB(6, ENV2, CUT, 6)};
static const pcable_t MARC_PBRASS[] = {CB(1, ENV2, CUT, 56), CB(2, VEL, CUT, 12), CB(3, AT, CUT, 12), CB(5, MAC2, MSPACE, 14), CB(6, KEY, CUT, -32)};
static const pcable_t MARC_WPED[] = {CB(1, LFO3, CUT, 16), CB(2, ENV2, CUT, 12), CB(3, AT, CUT, 16),
                                    CB(4, LFO3, MSPACE, 4), CB(5, MW, MSPACE, 20), CB(6, VEL, CUT, 6),
                                    CB(7, MAC2, E1 + 2, 16), CB(8, KEY, CUT, -32), CB(10, LFO3, PIT2, 3)};
static const pcable_t MARC_WHEEL[] = {CB(1, MW, CUT, 30), CB(2, MW, DLY, 40), CB(4, VEL, CUT, 12), CB(6, MAC2, MSPACE, 20), CB(7, KEY, CUT, -16)};
static const pcable_t MARC_ORGAN[] = {CB(1, ENV2, CUT, 10), CB(2, LFO2, CUT, 3), CB(3, LFO2, CUTB, -3), CB(4, ENV2, CUTB, 10), CB(7, MAC2, MSPACE, 20), CB(10, LFO3, PIT2, 3)};
static const pcable_t MARC_STRING[] = {CB(1, ENV2, CUT, 24), CB(2, LFO2, SHAPE, 10), CB(3, MW, MSPACE, 20), CB(4, VEL, CUT, 8), CB(5, MAC2, MSPACE, 20)};
static const pcable_t MARC_VOYAGE[] = {CB(4, VEL, CUT, 10), CB(5, ENV2, CUT, 34), CB(6, MAC2, SHAPE, 20), CB(7, KEY, CUT, -16)};
static const pcable_t MARC_DARK[] = {CB(1, LFO2, CUT, 6), CB(3, MW, E1AMT, 40), CB(4, ENV2, CUT, 24), CB(5, VEL, CUT, 8), CB(6, MAC2, MSPACE, 24), CB(7, KEY, CUT, -32)};
/* the presets' own MOD SEQ steps (core.h PMSEQ; params.c MSEQ_TABLES): a sequence preset's lanes, for
 * the matrix's SEMI / SEM34 (whole semitones) or any other cable. (The entry with no preset: none yet, every preset with
 * mseq_init's shapes) */
static const preset_mseq_t MARE_MSEQ[] = {
    PMSEQ(&MARE_PRESETS[3], MSL(0, -2, -4, -2, 0, -7, -5, -4, -2, 0, -2, -4, -5, -7, -5, -2), MSL(-6, -5, -4, -2, 0, 2, 4, 6, 7, 5, 3, 1, -1, -3, -5, -6), MSL(0, 1, 2, 4, 6, 7, 6, 4, 2, 1, 0, 0, 1, 2, 1, 0), MSL(7, 0, 0, 0, 7, 0, 0, 0, 7, 0, 0, 7, 0, 0, 7, 0)),   /* PEDAL SEQ */
    PMSEQ(&MARE_PRESETS[13], MSL(0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 5, 5, 7, 7, 7, 7), MSL(0, 1, 2, 3, 4, 3, 2, 1, 0, -1, -2, -3, -4, -3, -2, -1), MSL(-2, 0, 2, 4, 2, 0, -2, -4, -2, 0, 2, 4, 2, 0, -2, -4), MSL(7, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0)),   /* SPACE SEQ */
    PMSEQ(&MARE_PRESETS[15], MSL(0, 0, 0, 0, -5, -5, -5, -5, 0, 0, 0, 0, -5, -5, -5, -5), MSL(0, 0, 3, 3, 7, 7, 3, 3, 0, 0, -2, -2, -5, -5, -2, -2), MSL(-4, -2, 0, 2, 4, 6, 7, 6, 4, 2, 0, -2, -4, -6, -8, -6), MSL(7, 0, 7, 0, 7, 0, 7, 0, 7, 0, 7, 0, 7, 0, 7, 0)),   /* SWITCH SEQ */
    PMSEQ(&MARE_PRESETS[22], MSL(0, 0, 7, 0, -5, 0, 7, -2, 0, 0, 7, 0, -5, -5, 3, 5), MSL(7, 0, 7, 0, 7, 0, 7, 7, 7, 0, 7, 0, 7, 0, 7, 7), MSL(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0), MSL(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)),   /* FIFTH SEQ */
    PMSEQ(&MARE_PRESETS[24], MSL(0, -5, 0, 3, 0, -2, 0, 7, 0, -5, 0, 3, 5, 3, 0, -2), MSL(7, -4, 2, -6, 5, -8, 0, -3, 6, -2, 3, -7, 4, -5, 1, -1), MSL(7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7), MSL(0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 3, 3, 2, 2, 1, 1)),   /* BERLIN SEQ */
};
static const preset_patch_t MARE_PATCHES[] = {
    PPATCH(&MARE_PRESETS[1], MARC_HANG, MAR_XV4, 7, P_VINT + 1, 20, P_E2ATK + 1, 100, P_E2SUS + 1, 96),
    PPATCH(&MARE_PRESETS[2], MARC_ECHO, MAR_XW34, MTW_SAW, P_ITYPE + 1, MAR_ISDLY, P_IA + 1, 100, P_IB + 1, 84),
    PPMORE(&MARE_PRESETS[2], P_IMIX + 1, 40, P_VINT + 1, 10, MAR_XV4, 24, P_IC + 1, 64),
    PPATCH(&MARE_PRESETS[3], MARC_PSEQ, P_E2DEC + 1, 60, P_E2SUS + 1, 30),
    PPMORE(&MARE_PRESETS[3], MAR_XW34, MTW_SAW),
    PPATCH(&MARE_PRESETS[4], MARC_HUG, P_IA + 1, 0, P_IB + 1, 90, P_IC + 1, 88, P_IMIX + 1, 16),
    PPMORE(&MARE_PRESETS[4], MAR_XW34, MTW_SAW, P_E2REL + 1, 116),
    PPATCH(&MARE_PRESETS[5], MARC_DRIFT, MAR_XV4, 24, P_VINT + 1, 10, P_E2ATK + 1, 115, P_E2SUS + 1, 90),
    PPMORE(&MARE_PRESETS[5], MAR_XW34, MTW_SAW, P_L2RATE + 1, 24, P_L3RATE + 1, 18, P_ITYPE + 1, MAR_ISDLY),
    PPMORE(&MARE_PRESETS[5], P_IA + 1, 115, P_IB + 1, 60, P_IC + 1, 90, P_IMIX + 1, 25),
    PPMORE(&MARE_PRESETS[5], P_E2REL + 1, 116),
    PPATCH(&MARE_PRESETS[6], MARC_GSTR, MAR_XV4, 7, P_VINT + 1, 70, P_E2REL + 1, 116),
    PPATCH(&MARE_PRESETS[7], MARC_HSW, MAR_XV4, 24, P_VINT + 1, 10, P_E2ATK + 1, 100, P_E2SUS + 1, 80),
    PPMORE(&MARE_PRESETS[7], MAR_XW34, MTW_SAW),
    PPATCH(&MARE_PRESETS[8], MARC_SPLIT, P_E2ATK + 1, 85, P_E2DEC + 1, 100, P_E2SUS + 1, 106, P_E2REL + 1, 116),
    PPATCH(&MARE_PRESETS[9], MARC_TRAVEL, P_E2ATK + 1, 45, P_E2DEC + 1, 68, P_E2SUS + 1, 51, P_E2REL + 1, 116),
    PPATCH(&MARE_PRESETS[10], MARC_BLOOM, P_IA + 1, 100, P_IB + 1, 64, P_IC + 1, 64, P_IMIX + 1, 30),
    PPATCH(&MARE_PRESETS[11], MARC_PWM, P_MWDST + 1, MD_OFF),
    PPMORE(&MARE_PRESETS[11], MAR_XV3, -12, MAR_XW34, 100),
    PPATCH(&MARE_PRESETS[12], MARC_SQR, P_ATDST + 1, MD_VIB),
    PPMORE(&MARE_PRESETS[12], MAR_XV3, -12, MAR_XW34, 66),
    PPATCH(&MARE_PRESETS[13], MARC_GLEAD, P_MSRATE + 1, 4, P_E2DEC + 1, 80, P_E2SUS + 1, 40),
    PPMORE(&MARE_PRESETS[13], MAR_XW34, MTW_SAW),
    PPATCH(&MARE_PRESETS[14], MARC_SHRED, P_GLMODE + 1, 2, P_E2ATK + 1, 0, P_E2DEC + 1, 85, P_E2SUS + 1, 30),
    PPATCH(&MARE_PRESETS[15], MARC_CHARGE, P_E2DEC + 1, 45, P_E2SUS + 1, 0),
    PPMORE(&MARE_PRESETS[15], MAR_XW34, MTW_SAW),
    PPATCH(&MARE_PRESETS[16], MARC_FPED, MAR_XV4, 19, P_VINT + 1, 15, P_E2ATK + 1, 97, P_E2SUS + 1, 90),
    PPMORE(&MARE_PRESETS[16], MAR_XW34, MTW_SAW, P_E2REL + 1, 116),
    PPATCH(&MARE_PRESETS[18], MARC_FIFTH, P_GLIDE + 1, 12, P_E2DEC + 1, 60),
    PPMORE(&MARE_PRESETS[18], MAR_XV4, MAR_VOFF),
    PPATCH(&MARE_PRESETS[19], MARC_STACK),
    PPATCH(&MARE_PRESETS[20], MARC_SWEEP),
    PPATCH(&MARE_PRESETS[21], MARC_PBASS, MAR_XV4, MAR_VOFF, P_VINT + 1, 20),
    PPMORE(&MARE_PRESETS[21], MAR_XW34, MTW_SAW),
    PPATCH(&MARE_PRESETS[22], MARC_WIDEFM, P_E2DEC + 1, 57),
    PPATCH(&MARE_PRESETS[23], MARC_SWELL, P_E2DEC + 1, 100, P_E2SUS + 1, 90, P_E2REL + 1, 116),
    PPATCH(&MARE_PRESETS[24], MARC_IOBRASS, P_IA + 1, 100, P_IB + 1, 72, P_IMIX + 1, 35, P_E2DEC + 1, 57),
    PPMORE(&MARE_PRESETS[24], MAR_XV4, MAR_VOFF),
    PPATCH(&MARE_PRESETS[25], MARC_PBRASS, P_E2ATK + 1, 0, P_E2DEC + 1, 82, P_E2SUS + 1, 30, P_E2REL + 1, 50),
    PPATCH(&MARE_PRESETS[26], MARC_WPED, MAR_XV4, -12, P_VINT + 1, 20, P_E2ATK + 1, 0, P_E2SUS + 1, 20),
    PPMORE(&MARE_PRESETS[26], MAR_XW34, MTW_SAW, P_ITYPE + 1, MAR_IPDLY, P_IA + 1, 90, P_IB + 1, 50),
    PPMORE(&MARE_PRESETS[26], P_IC + 1, 127, P_IMIX + 1, 22, P_E2REL + 1, 116),
    PPATCH(&MARE_PRESETS[27], MARC_WHEEL, P_MWDST + 1, MD_OFF, P_E2REL + 1, 116),
    PPATCH(&MARE_PRESETS[28], MARC_ORGAN, P_LRATE + 1, 62, P_E2DEC + 1, 15, P_E2SUS + 1, 70, P_E2REL + 1, 39),
    PPATCH(&MARE_PRESETS[29], MARC_STRING, P_E2SUS + 1, 57, P_E2REL + 1, 116, P_VINT + 1, 5),
    PPATCH(&MARE_PRESETS[30], MARC_VOYAGE, P_E2DEC + 1, 75, P_E2SUS + 1, 0),
    PPATCH(&MARE_PRESETS[31], MARC_DARK, P_LRATE + 1, 62, P_VINT + 1, 20, P_E2DEC + 1, 30, P_E2SUS + 1, 0),
};

static const engine_t ENG_MARE = {
    "MARE", {"VCO", "VCF"},
    {
        {"WAVE", F_INT, 0, 127, MTW_SAW, N_MTWAVE, 0},
        {"VCO2", F_SEMI, -24, 24, 0, 0, 0},
        {"DTUNE", F_PCT, 0, 127, 40, 0, 0},
        {"SY/FM", F_INT, -64, 63, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 80, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"SPACE", F_INT, -64, 63, 0, 0, 0},
        {"MODE", F_ENUM, 0, 8, MTM_ST, N_MTM, 0},
    },
    MARE_PRESETS, sizeof(MARE_PRESETS) / sizeof(MARE_PRESETS[0]), 1, mare_note_on, mare_render,
    0xC4A6, {P_E4, P_E5, P_E6, P_E0}, 0, 0, mare_desc, mare_block, 0, 0,
    1, 4, 0,                                         /* (two VCOs; the matrix's XMOD: SY/FM) */
};
