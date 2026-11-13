/* SPDX-License-Identifier: GPL-3.0-only */
/* Stallion: the MOTION lanes (specs/touch R35-R38), included by seq.c (events_block runs motion_block once a block,
 * after the MIDI in and the sequencer's step, before the clock moves on). The synth has one sequencer (track 1), so
 * three lanes, of track 1 (they play on its part, part_ctl[0], and so on a LAYER / SPLIT layer with it):
 *   BEND    the bend and the lever as the voices hear them before the range (part_ctl bend + lever, Q15);
 *   RIBBON  the ribbon (part_ctl ribbon, Q15);
 *   PRESS   the part's shaped pressure (R11-R15 applied): the largest of its channel pressure (MIDI, the shaping's
 *           at_chan) and the poly pressure of its held voices (the keys' KEY TOUCH, MIDI poly, MPE).
 * ML_SPS values a step, one int8 each (BEND and RIBBON x 127 / 32767 rounded, 0 at rest, -127 / 127 the ends exactly;
 * PRESS 0..127 as it is), in ml_lane; a lane's bit in ml_mask once something was recorded into it. Each value is the
 * control as the playhead entered that quarter of the step, as heard (REC_LAT earlier, as the notes are recorded).
 * RECORD: track 1 recording live (REC, playing; not a free take) with GLO > TOUCH > MOTION REC ON. A control is the
 * hand's (bit in act) while it is off its rest or moved within ML_HOLD; then each quarter it enters takes its value
 * (and those a jump passed over), and once it is let go one quarter more takes the rest value it came back to. Where
 * the hand did not move the lane keeps what it had (an overdub replaces only where the hand moved, R36). A pass that
 * writes a lane is one undo with its notes (EDIT + OCT-: the steps and the lanes as before the pass: ml_undo).
 * PLAY: the transport running and the lane's bit set: the value at the playhead, straight between the quarters (a
 * sweep glides, no steps: a 1 s ribbon sweep over 12 semitones moves at most a few cents a block), into part_ctl
 * ml_bend / ml_rib / ml_at with its ML_ON_* bit (voice.c, eng_hybrid.c: pc_bsum, pc_rib, pc_at hear it in place of
 * the live control). A control the hand has (act) takes its lane's place while it moves and ML_HOLD after it rests;
 * then the lane plays again from where the playhead is. Stopped, or no lane: ml_on 0, the live controls as before
 * (with no lane recorded nothing is read and every sound renders bit for bit as before).
 * CLEAR (R36): EDIT held + KNOB 4 picks the lanes (BEND, RIBBON, PRESS, ALL; back to "--" for none), EDIT let go
 * clears them from the pattern (ui_layers.c ml_pick; KNOB 4 has nothing else under EDIT). A cleared pattern
 * (steps_clear: REC held, TOOLS > CLRSQ, NEW) clears them too; EDIT + OCT- brings them back with the steps.
 * PATTERN: LENGTH x2 copies them with the steps, SHIFT moves them with the steps (ui_layers.c); a section stored /
 * played (SAVE + key), the song mode, a project saved / loaded and the editor's backup carry them inside the project
 * (project.c ml_put / ml_get: in the bytes tracks 2..4's empty steps leave free; a project without them: no lane).
 * MIDI OUT (R38, GLO > SYSTEM > MIDI = SEQ, as the sequencer's notes): a lane playing goes out on track 1's channel
 * as the live gestures do (R34): BEND as pitch bend, RIBBON as CC 16 (64 at rest), PRESS as channel pressure; each
 * at most once every ML_TX_GAP and only on change; a lane that stops playing (STOP, cleared) sends its rest once,
 * unless the hand has the control (its live path sends it then). */
#if CLIPPER_SYNTH
#define ML_LANES 3u
enum { ML_BEND, ML_RIB, ML_PRESS };
#define ML_SPS 4u                                       /* values a step */
#define ML_N (NSTEP * ML_SPS)
#define ML_ALL ((1u << ML_LANES) - 1u)
#define ML_HOLD (FS / 10u)                              /* samples: a control moved within this is the hand's (100 ms) */
#define ML_TX_GAP (FS / 100u)                           /* samples between two messages of a lane on MIDI out (10 ms) */
static int8_t ml_lane[ML_LANES][ML_N] __attribute__((section(".pool")));
static uint8_t ml_mask;                                 /* the lanes holding a recording: bit per lane */
static struct {                                         /* the lanes as undo_mark found them (track 1's undo) */
    int8_t lane[ML_LANES][ML_N];
    uint8_t mask;
} ml_undo __attribute__((section(".pool")));
static int32_t ml_prev[ML_LANES];                       /* the live values of the last block */
static uint32_t ml_idle[ML_LANES] = {ML_HOLD, ML_HOLD, ML_HOLD};   /* samples since each last moved (or left its rest) */
static int16_t ml_w[ML_LANES] = {-1, -1, -1};           /* recording: the quarter the hand last wrote, -1 none */
static int8_t ml_wv[ML_LANES];                          /* .. its value */
static uint16_t ml_txv[ML_LANES] = {8192u, 64u, 0u};    /* MIDI out: the value last sent (the rests to start) */
static uint32_t ml_txt[ML_LANES] = {ML_TX_GAP, ML_TX_GAP, ML_TX_GAP};   /* .. samples since */
/* (R39) NOTE PRESS: each recorded note's own poly pressure, so a PRESS BEND recorded bends only its own note again.
 * A step's notes (np_notes: a NOTE's note[0..n-1], a TIE's the chain it carries, as seq_step sounds them) each have
 * NP_PPS 4-bit points a step (pressure x 15 / 127 rounded: 0 and 127 exact), the note's pressure as the playhead entered
 * that quarter (REC_LAT earlier, as heard), in np_cv: step, note k, two points a byte (the first in the low nibble).
 * RECORD (with the lanes: track 1 recording live, MOTION REC ON): each note of the step under the playhead that a key
 * (or MIDI) holds takes its voices' pressure (the most of them; LAYER / SPLIT: the layer's voices too) into the quarter;
 * a note not held keeps what it had (an overdub replaces only the notes played). np_on once a point above 0 was written:
 * until then nothing is kept (KEY TOUCH OFF records nothing). PLAY (the transport running, np_on): each sounding note
 * of the step under the playhead gives its voices (gated, not held by a key: the hand's then) its curve, straight between
 * the points (into the next step's first point when the note goes on there), as their poly pressure (voice_t.pat, the
 * shaping's state with it); a voice of any other note is not touched. Undo, clear (PRESS, ALL), LENGTH x2, SHIFT go
 * with the lanes; project.c np_put / np_get carry it after the lanes' stream */
#define NP_PPS 4u                                       /* points a step (= ML_SPS: its quarters) */
#define NP_B (NSTEP * 4u * NP_PPS / 2u)                 /* bytes: 64 steps x 4 notes x 4 nibbles = 512 */
static uint8_t np_cv[NP_B] __attribute__((section(".pool")));
static uint8_t np_on;                                   /* a curve was recorded */
static struct {
    uint8_t cv[NP_B];
    uint8_t on;
} np_undo __attribute__((section(".pool")));
static uint32_t np_wq = 0xFFFFFFFFu;                    /* recording: the quarter last written (pattern quarters) */
_Static_assert(NP_PPS == ML_SPS, "a curve's points are the lanes' quarters");

static uint32_t np_notes(const track_t *t, uint32_t idx, uint8_t *out)   /* the notes step idx sounds, in their order */
{
    const step_t *s = &t->step[idx % NSTEP];
    if (s->time == ST_NOTE && s->n) {
        uint32_t n = s->n > 4u ? 4u : s->n;
        memcpy(out, s->note, n);
        return n;
    }
    return s->time == ST_TIE ? chain_notes(t, idx, out) : 0u;
}
static uint32_t np_get(uint32_t idx, uint32_t k, uint32_t q)   /* a point, 0..15 */
{
    uint32_t i = ((idx % NSTEP) * 4u + (k & 3u)) * (NP_PPS / 2u) + (q & 3u) / 2u;
    return (np_cv[i] >> (4u * (q & 1u))) & 15u;
}
static void np_put(uint32_t idx, uint32_t k, uint32_t q, uint32_t v)
{
    uint32_t i = ((idx % NSTEP) * 4u + (k & 3u)) * (NP_PPS / 2u) + (q & 3u) / 2u, sh = 4u * (q & 1u);
    np_cv[i] = (uint8_t)((np_cv[i] & ~(15u << sh)) | (v & 15u) << sh);
}
static uint32_t np_live1(const track_t *t, uint32_t note, uint32_t a)   /* the most pressure of t's voices of note */
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && t->v[i].note == note && t->v[i].pat > a)
            a = t->v[i].pat;
    return a;
}
static uint32_t np_live(uint32_t note)                  /* the pressure a held note of track 1 plays with */
{
    uint32_t r = note < 128u ? ly_route[note] : 0u, a = 0;
    if (!r || (r & 1u))
        a = np_live1(&trk[0], note, a);
    if (r & 2u)
        a = np_live1(&trk[1], ly_note[note], a);
    return a;
}
static void np_set1(track_t *t, uint32_t note, uint32_t x)   /* t's voices of note: poly pressure x */
{
    uint32_t p = (uint32_t)(t - trk), i;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        if (!v->active || !v->gate || v->note != note)
            continue;
        v->pat = (uint8_t)x;
        if (p < NPART) {                                /* (AT ATTACK / RELEASE: from here, should the hand take it) */
            at_va[p][i] = v->age;
            at_vt[p][i] = (uint8_t)x;
            at_vs[p][i] = x << 16;
        }
    }
}
static void np_set(uint32_t note, uint32_t x)
{
    uint32_t r = note < 128u ? ly_route[note] : 0u;
    if (!r || (r & 1u))
        np_set1(&trk[0], note, x);
    if (r & 2u)
        np_set1(&trk[1], ly_note[note], x);
}
static void np_rec(track_t *t, uint32_t qa, uint32_t nq)   /* recording: quarter qa of the pattern (nq quarters) */
{
    uint8_t nt[4];
    uint32_t idx = qa / NP_PPS, q = qa % NP_PPS, n = np_notes(t, idx, nt), k, j, q0;
    q0 = np_wq < nq && np_wq / NP_PPS == idx && np_wq % NP_PPS < q ? np_wq % NP_PPS + 1u : q;   /* (quarters a jump passed) */
    np_wq = qa;
    for (k = 0; k < n; k++) {
        uint32_t v;
        if (!live_has(&trk[0], nt[k]))
            continue;                                   /* (not played now: kept) */
        v = (np_live(nt[k]) * 15u + 63u) / 127u;
        if (!np_on) {
            if (!v)
                continue;
            undo_mark(t, UNDO_REC(t));
            memset(np_cv, 0, sizeof np_cv);             /* (a new curve: at rest everywhere else) */
            np_on = 1;
        }
        if (np_get(idx, k, q) != v)
            undo_mark(t, UNDO_REC(t));                  /* (the pass: one undo, with its notes; once) */
        for (j = q0; j <= q; j++)
            np_put(idx, k, j, v);
    }
}
static void np_play(track_t *t, uint32_t pos, uint32_t nq)   /* playing: pos Q16 quarters, nq quarters a pattern */
{
    uint8_t nt[4], nx[4];
    uint32_t qa = (pos >> 16) % nq, idx = qa / NP_PPS, q = qa % NP_PPS, n = np_notes(t, idx, nt), nn = 0, k, j;
    int32_t f = (int32_t)((pos & 0xFFFFu) >> 1);
    if (q + 1u == NP_PPS && n)
        nn = np_notes(t, (qa + 1u >= nq ? 0u : idx + 1u), nx);
    for (k = 0; k < n; k++) {
        int32_t a, b;
        if (live_has(&trk[0], nt[k]))
            continue;                                   /* (the hand's) */
        a = (int32_t)np_get(idx, k, q) * 127;
        b = a;
        if (q + 1u < NP_PPS) {
            b = (int32_t)np_get(idx, k, q + 1u) * 127;
        } else {
            for (j = 0; j < nn && nx[j] != nt[k]; j++)
                ;
            if (j < nn)                                 /* (it goes on into the next step: to its first point) */
                b = (int32_t)np_get(qa + 1u >= nq ? 0u : idx + 1u, j, 0) * 127;
        }
        np_set(nt[k], (uint32_t)((a + (((b - a) * f) >> 15) + 7) / 15));
    }
}

static int8_t ml_enc(uint32_t l, int32_t v)            /* a live value -> a lane's byte */
{
    if (l == ML_PRESS)
        return (int8_t)clamp(v, 0, 127);
    v = clamp(v, -32767, 32767);
    return (int8_t)((v * 127 + (v >= 0 ? 16383 : -16383)) / 32767);
}
static int32_t ml_dec(int32_t s) { return s * 32767 / 127; }   /* a BEND / RIBBON byte -> Q15 (0, the ends exact) */
static int32_t ml_live(uint32_t l)                      /* the part's control now, as the lane keeps it */
{
    const part_ctl_t *c = &part_ctl[0];
    uint32_t i, a;
    if (l == ML_BEND)
        return clamp(c->bend + c->lever, -32767, 32767);
    if (l == ML_RIB)
        return c->ribbon;
    a = c->at;
    for (i = 0; i < NVOICE; i++)
        if (trk[0].v[i].active && trk[0].v[i].gate && trk[0].v[i].pat > a)
            a = trk[0].v[i].pat;
    return (int32_t)a;
}
/* the playhead of t lat units back, in quarters of a step (Q16) inside the pattern: 0 .. len x ML_SPS */
static uint32_t ml_pos(const track_t *t, uint32_t lat)
{
    uint32_t into, slen, len = trk_len(t), abs = trk_grid(t, &into, &slen), idx, s = 0, q;
    if (len > NSTEP)
        len = NSTEP;
    idx = abs % len;
    if (lat > into) {                                   /* (in the step before: as long as this one) */
        idx = idx ? idx - 1u : len - 1u;
        into += slen > lat ? slen - lat : 0u;
    } else {
        into -= lat;
    }
    while ((slen >> s) >= 4096u)                        /* (a step's fraction to 1/4096, in 32 bits) */
        s++;
    q = ((into >> s) << 18) / ((slen >> s) ? (slen >> s) : 1u);
    if (q >= ML_SPS << 16)
        q = (ML_SPS << 16) - 1u;
    return idx * (ML_SPS << 16) + q;
}
/* lane l at pos (Q16 quarters) of a pattern nq quarters long, straight between them: Q15 (PRESS: 0..127 << 8) */
static int32_t ml_read(uint32_t l, uint32_t pos, uint32_t nq)
{
    uint32_t k = (pos >> 16) % nq, k2 = k + 1u >= nq ? 0u : k + 1u;
    int32_t a = ml_lane[l][k], b = ml_lane[l][k2], f = (int32_t)((pos & 0xFFFFu) >> 1);
    if (l == ML_PRESS)
        a <<= 8, b <<= 8;
    else
        a = ml_dec(a), b = ml_dec(b);
    return a + (((b - a) * f) >> 15);                   /* (|b - a| <= 65534, f < 2^15: within 31 bits) */
}
static void ml_undo_mark(void)                          /* (seq.c undo_mark, track 1) */
{
    memcpy(ml_undo.lane, ml_lane, sizeof ml_undo.lane);
    ml_undo.mask = ml_mask;
    memcpy(np_undo.cv, np_cv, sizeof np_undo.cv);       /* (R39: the notes' curves too) */
    np_undo.on = np_on;
}
static void ml_undo_swap(void)                          /* (ui.c undo_swap, track 1: IRQ off) */
{
    uint32_t l, k;
    uint8_t m = ml_mask;
    for (l = 0; l < ML_LANES; l++)
        for (k = 0; k < ML_N; k++) {
            int8_t x = ml_lane[l][k];
            ml_lane[l][k] = ml_undo.lane[l][k];
            ml_undo.lane[l][k] = x;
        }
    ml_mask = ml_undo.mask;
    ml_undo.mask = m;
    for (k = 0; k < NP_B; k++) {
        uint8_t x = np_cv[k];
        np_cv[k] = np_undo.cv[k];
        np_undo.cv[k] = x;
    }
    m = np_on;
    np_on = np_undo.on;
    np_undo.on = m;
}
static void ml_clear(uint32_t mask)                     /* lanes gone from the pattern (bit per lane) */
{
    ml_mask &= (uint8_t)~mask;
    if (mask & (1u << ML_PRESS))
        np_on = 0;                                      /* (PRESS, ALL: the notes' curves with it) */
}
static void ml_double(uint32_t len)                     /* LENGTH x2: the first len steps again after them */
{
    uint32_t l;
    if (2u * len <= NSTEP)
        for (l = 0; l < ML_LANES; l++)
            memcpy(&ml_lane[l][len * ML_SPS], ml_lane[l], len * ML_SPS);
    if (2u * len <= NSTEP)
        memcpy(&np_cv[len * NP_B / NSTEP], np_cv, len * NP_B / NSTEP);
}
static void ml_rotate(uint32_t len, int32_t d)          /* SHIFT: every step one later (d > 0) / earlier, in len */
{
    uint32_t l, k, n = len * ML_SPS;
    if (!n || n > ML_N)
        return;
    for (l = 0; l < ML_LANES; l++) {
        int8_t keep[ML_SPS];
        int8_t *a = ml_lane[l];
        if (d > 0) {
            memcpy(keep, &a[n - ML_SPS], ML_SPS);
            for (k = n - 1u; k >= ML_SPS; k--)
                a[k] = a[k - ML_SPS];
            memcpy(a, keep, ML_SPS);
        } else {
            memcpy(keep, a, ML_SPS);
            for (k = 0; k + ML_SPS < n; k++)
                a[k] = a[k + ML_SPS];
            memcpy(&a[n - ML_SPS], keep, ML_SPS);
        }
    }
    {                                                   /* (the notes' curves: a step's NP_B / NSTEP bytes) */
        uint8_t keep[NP_B / NSTEP];
        uint32_t w = NP_B / NSTEP, m = len * w;
        if (d > 0) {
            memcpy(keep, &np_cv[m - w], w);
            for (k = m - 1u; k >= w; k--)
                np_cv[k] = np_cv[k - w];
            memcpy(np_cv, keep, w);
        } else {
            memcpy(keep, np_cv, w);
            for (k = 0; k + w < m; k++)
                np_cv[k] = np_cv[k + w];
            memcpy(&np_cv[m - w], keep, w);
        }
    }
}
/* MIDI out of lane l: raw (pitch bend 0..16383, CC 16 0..127, pressure 0..127), sent once its gap is over */
static void ml_tx(uint32_t l, uint32_t raw)
{
    uint32_t ch = trk_midi_ch(0);
    if (raw == ml_txv[l] || ml_txt[l] < ML_TX_GAP)
        return;
    ml_txv[l] = (uint16_t)raw;
    ml_txt[l] = 0;
    if (l == ML_BEND)
        midi_out_event(0x0Eu | (0xE0u | ch) << 8 | (raw & 127u) << 16 | (raw >> 7) << 24);
    else if (l == ML_RIB)
        midi_out_event(0x0Bu | (0xB0u | ch) << 8 | 16u << 16 | raw << 24);
    else
        midi_out_event(0x0Du | (0xD0u | ch) << 8 | raw << 16);
}
static uint32_t ml_raw(uint32_t l, int32_t v)           /* a playing value (Q15; PRESS 0..127) -> its MIDI value */
{
    if (l == ML_BEND)
        return (uint32_t)clamp(8192 + (v >> 2), 0, 16383);
    if (l == ML_RIB)
        return (uint32_t)clamp(v >= 0 ? 64 + (v * 63 + 16383) / 32767 : 64 + (v * 64 - 16383) / 32768, 0, 127);
    return (uint32_t)clamp(v, 0, 127);
}
static const uint16_t ML_REST[ML_LANES] = {8192u, 64u, 0u};

static __attribute__((noinline)) void motion_block(uint32_t n)
{
    track_t *t = &trk[0];
    part_ctl_t *c = &part_ctl[0];
    uint32_t l, on = 0, act = 0, nq, play = song.playing && t->seq_abs != SEQ_NONE;
    uint32_t rec = play && (song.rec & 1u) && touch_cfg.motion_rec && !ft_on;
    int32_t val[ML_LANES];
    for (l = 0; l < ML_LANES; l++)
        if (ml_txt[l] < ML_TX_GAP)
            ml_txt[l] += n;
    if (!ml_mask && !np_on && !rec && ml_txv[0] == ML_REST[0] && ml_txv[1] == ML_REST[1] && ml_txv[2] == ML_REST[2]) {
        c->ml_on = 0;                                   /* (no lane, not recording: nothing to do) */
        return;
    }
    nq = trk_len(t) * ML_SPS;
    if (nq > ML_N)
        nq = ML_N;
    for (l = 0; l < ML_LANES; l++) {                    /* the hand's: off its rest, or moved within ML_HOLD */
        int32_t v = ml_live(l);
        if (v || v != ml_prev[l])
            ml_idle[l] = 0;
        else if (ml_idle[l] < ML_HOLD)
            ml_idle[l] += n;
        ml_prev[l] = v;
        val[l] = v;
        if (ml_idle[l] < ML_HOLD)
            act |= 1u << l;
    }
    if (rec) {
        uint32_t q = (ml_pos(t, REC_LAT * (uint32_t)song.g[G_BPM]) >> 16) % nq;
        for (l = 0; l < ML_LANES; l++) {
            int8_t e = ml_enc(l, val[l]);
            if (!((act >> l) & 1u) && (ml_w[l] < 0 || ml_wv[l] == e)) {
                ml_w[l] = -1;                           /* (let go, and its rest written: the lane is kept from here) */
                continue;
            }
            if (ml_w[l] == (int16_t)q)
                continue;                               /* (this quarter has its value) */
            undo_mark(t, UNDO_REC(t));                  /* (the pass: one undo, with its notes; once) */
            if (!((ml_mask >> l) & 1u)) {
                memset(ml_lane[l], 0, sizeof ml_lane[l]);   /* a new lane: at rest everywhere else */
                ml_mask |= (uint8_t)(1u << l);
            }
            {
                uint32_t k = ml_w[l] >= 0 ? ((uint32_t)ml_w[l] + 1u) % nq : q, g = 0;
                while (k != q && g++ < 2u * ML_SPS) {   /* (the quarters a jump passed over: this value too) */
                    ml_lane[l][k] = e;
                    k = (k + 1u) % nq;
                }
                ml_lane[l][q] = e;
            }
            ml_w[l] = (int16_t)q;
            ml_wv[l] = e;
        }
        np_rec(t, q, nq);                               /* (R39: the held notes' own pressure) */
    } else {
        ml_w[0] = ml_w[1] = ml_w[2] = -1;
        np_wq = 0xFFFFFFFFu;
    }
    if (play && np_on)
        np_play(t, ml_pos(t, 0), nq);                   /* (R39: each recorded note's pressure on its own voices) */
    if (play && ml_mask) {
        uint32_t pos = ml_pos(t, 0);
        on = ml_mask & ~act;
        if (on & (1u << ML_BEND))
            c->ml_bend = (int16_t)clamp(ml_read(ML_BEND, pos, nq), -32767, 32767);
        if (on & (1u << ML_RIB))
            c->ml_rib = (int16_t)clamp(ml_read(ML_RIB, pos, nq), -32767, 32767);
        if (on & (1u << ML_PRESS))
            c->ml_at = (uint8_t)clamp((ml_read(ML_PRESS, pos, nq) + 128) >> 8, 0, 127);
    }
    c->ml_on = (uint8_t)on;                             /* (ML_ON_BEND .. are 1 << ML_BEND ..) */
    if (song.g[G_MIDI])                                 /* R38: as the sequencer's notes go out */
        for (l = 0; l < ML_LANES; l++) {
            if ((on >> l) & 1u)
                ml_tx(l, ml_raw(l, l == ML_BEND ? c->ml_bend : l == ML_RIB ? c->ml_rib : c->ml_at));
            else if (!((act >> l) & 1u))
                ml_tx(l, ML_REST[l]);                   /* (stopped playing: its rest, once) */
        }
    else
        ml_txv[0] = ML_REST[0], ml_txv[1] = ML_REST[1], ml_txv[2] = ML_REST[2];
}
_Static_assert(ML_ON_BEND == 1u << ML_BEND && ML_ON_RIB == 1u << ML_RIB && ML_ON_AT == 1u << ML_PRESS, "ml_on's bits");
#else
static void ml_undo_mark(void) {}
static void ml_clear(uint32_t mask) { (void)mask; }
static void motion_block(uint32_t n) { (void)n; }
#endif
