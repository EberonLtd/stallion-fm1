/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* A factory preset played as a phrase, the whole instrument's stereo out (the part, its sends, the
 * buses, the master) to a 16-bit WAV: for listening to a preset and for measuring its sound (level, spectrum,
 * onset). Built like the host tests:
 *   cc -O2 -w -DCLIPPER_SYNTH=1 -Ibuild/gen -Ifirmware/src -Ifirmware/hal tools/mare_render.c -lm -o build/host/mare_render
 *   build/host/mare_render ENGINE PRESET PHRASE OUT.wav [SECS]
 * PRESET a name (or an index); PHRASE one of:
 *   held:N   key N (MIDI) held SECS - 1.5 s          chord   Cm9 voiced low, held 4.5 s, then a second chord
 *   melody   a slow line in C minor (legato)          bass    a low line, eighths and quarters
 *   lead     a line in the upper register             seq     the part's sequencer running a line, keys over it
 *   riff     a root held, a line over it (legato)  pedal   a bass held, a high line switching every 0.25 s
 *   pedal5   pedal, the bass alternating root and fifth every 0.5 s (F#1, C#2)
 *   notes:FILE  a note list (a .tsv file: start_s dur_s midi_note velocity a line,
 *            tab-separated, '#' comments): each key down and up at its own time, at the control block (CTL samples,
 *            0.7 ms: the firmware's own key timing); a key struck again while held is let go and struck again
 * env PSET=id:v,... sets track values after the preset (as hostsim); VEL=v the velocity (100); BPM=b the song tempo
 * (sequencer, ARP, MOD SEQ, synced delay; the harness' default 120); LAYER=preset (layered parts): part 2 that preset of the
 * same engine in LAYER mode, PSET2=id:v,... its values */
#define main hostsim_main
#include "../tests/hostsim.c"
#undef main

typedef struct { float t, len; uint8_t note; } ev_t;
/* (notes:FILE) a note list: key events sorted by time, a key up before a key down at the same time */
typedef struct { uint32_t at; uint8_t on, note, vel; } kev_t;
static kev_t *KEV;
static uint32_t NKEV;
static int kev_cmp(const void *a, const void *b)
{
    const kev_t *x = a, *y = b;
    return x->at != y->at ? (x->at < y->at ? -1 : 1) : (int)x->on - (int)y->on;
}
static int load_notes(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    float s, d;
    int n, v;
    if (!f)
        return 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || sscanf(line, "%f %f %d %d", &s, &d, &n, &v) != 4 || n < 0 || n > 127)
            continue;
        KEV = realloc(KEV, (NKEV + 2) * sizeof *KEV);
        KEV[NKEV++] = (kev_t){(uint32_t)(s * FS + 0.5f), 1, (uint8_t)n, (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v)};
        KEV[NKEV++] = (kev_t){(uint32_t)((s + d) * FS + 0.5f), 0, (uint8_t)n, 0};
    }
    fclose(f);
    qsort(KEV, NKEV, sizeof *KEV, kev_cmp);
    return NKEV > 0;
}
static const ev_t CHORD[] = {{0.2f, 4.3f, 48}, {0.2f, 4.3f, 55}, {0.2f, 4.3f, 58}, {0.2f, 4.3f, 62}, {0.2f, 4.3f, 63},
                             {4.6f, 2.6f, 44}, {4.6f, 2.6f, 51}, {4.6f, 2.6f, 55}, {4.6f, 2.6f, 60}, {4.6f, 2.6f, 62}};
static const ev_t MELODY[] = {{0.2f, 0.95f, 60}, {1.1f, 0.5f, 63}, {1.55f, 0.5f, 67}, {2.0f, 1.45f, 70},
                              {3.4f, 0.5f, 68}, {3.85f, 0.5f, 67}, {4.3f, 0.95f, 63}, {5.2f, 1.6f, 65}};
static const ev_t BASS[] = {{0.2f, 0.4f, 36}, {0.7f, 0.2f, 36}, {0.95f, 0.4f, 39}, {1.45f, 0.2f, 43},
                            {1.7f, 0.4f, 41}, {2.2f, 0.2f, 41}, {2.45f, 0.4f, 39}, {2.95f, 0.45f, 34},
                            {3.45f, 0.4f, 36}, {3.95f, 0.2f, 36}, {4.2f, 0.4f, 39}, {4.7f, 0.2f, 43},
                            {4.95f, 0.4f, 46}, {5.45f, 0.2f, 43}, {5.7f, 0.9f, 41}};
static const ev_t LEAD[] = {{0.2f, 0.35f, 72}, {0.6f, 0.35f, 75}, {1.0f, 1.0f, 79}, {2.05f, 0.3f, 77},
                            {2.4f, 0.3f, 75}, {2.75f, 1.2f, 74}, {4.0f, 0.35f, 72}, {4.4f, 0.35f, 70},
                            {4.8f, 1.6f, 72}};
static const ev_t RIFF[] = {{0.2f, 7.3f, 44}, {0.2f, 0.93f, 68}, {1.1f, 1.43f, 63}, {2.5f, 0.43f, 68}, {2.9f, 1.33f, 56},
                            {4.2f, 0.83f, 57}, {5.0f, 1.43f, 56}, {6.4f, 1.1f, 68}};   /* (a held root, a line over it) */
/* (dexba) a bass held (C#2, then F#2) while a high line switches every 0.25 s over it */
static const ev_t PEDAL[] = {{0.2f, 3.75f, 37}, {3.95f, 3.4f, 42}, {0.60f, 0.24f, 76}, {0.85f, 0.24f, 73}, {1.10f, 0.24f, 74}, {1.35f, 0.24f,
                             76}, {1.60f, 0.24f, 78}, {1.85f, 0.24f, 71}, {2.10f, 0.24f, 73}, {2.35f, 0.24f, 74}, {2.60f, 0.24f, 76}, {2.85f,
                             0.24f, 78}, {3.10f, 0.24f, 76}, {3.35f, 0.24f, 74}, {3.60f, 0.24f, 73}, {3.85f, 0.24f, 71}, {4.10f, 0.24f, 73},
                             {4.35f, 0.24f, 76}, {4.60f, 0.24f, 76}, {4.85f, 0.24f, 73}, {5.10f, 0.24f, 74}, {5.35f, 0.24f, 76}, {5.60f,
                             0.24f, 78}, {5.85f, 0.24f, 71}, {6.10f, 0.24f, 73}, {6.35f, 0.24f, 74}, {6.60f, 0.24f, 76}, {6.85f, 0.24f, 78}};
/* (radium) the bass alternating root and fifth (F#1, C#2) every 0.5 s, the high line switching every 0.25 s over it */
static const ev_t PEDAL5[] = {{0.20f, 0.49f, 30}, {0.70f, 0.49f, 37}, {1.20f, 0.49f, 30}, {1.70f, 0.49f, 37}, {2.20f, 0.49f, 30}, {2.70f,
                              0.49f, 37}, {3.20f, 0.49f, 30}, {3.70f, 0.49f, 37}, {4.20f, 0.49f, 30}, {4.70f, 0.49f, 37}, {5.20f, 0.49f,
                              30}, {5.70f, 0.49f, 37}, {6.20f, 0.49f, 30}, {6.70f, 0.49f, 37}, {0.60f, 0.24f, 71}, {0.85f, 0.24f, 73},
                              {1.10f, 0.24f, 74}, {1.35f, 0.24f, 76}, {1.60f, 0.24f, 78}, {1.85f, 0.24f, 76}, {2.10f, 0.24f, 74}, {2.35f,
                              0.24f, 73}, {2.60f, 0.24f, 74}, {2.85f, 0.24f, 76}, {3.10f, 0.24f, 78}, {3.35f, 0.24f, 74}, {3.60f, 0.24f,
                              73}, {3.85f, 0.24f, 71}, {4.10f, 0.24f, 73}, {4.35f, 0.24f, 74}, {4.60f, 0.24f, 71}, {4.85f, 0.24f, 73},
                              {5.10f, 0.24f, 74}, {5.35f, 0.24f, 76}, {5.60f, 0.24f, 78}, {5.85f, 0.24f, 76}, {6.10f, 0.24f, 74}, {6.35f,
                              0.24f, 73}, {6.60f, 0.24f, 74}, {6.85f, 0.24f, 76}};
static const ev_t OVER[] = {{1.0f, 1.8f, 67}, {3.0f, 0.9f, 70}, {4.0f, 2.0f, 72}};   /* (seq: keys over the line) */

int main(int argc, char **argv)
{
    uint32_t e = argc > 1 ? (uint32_t)atoi(argv[1]) : ENGI_MARE, pi, f, frames, k, n = 0;
    const char *ph = argc > 3 ? argv[3] : "chord";
    float secs = argc > 5 ? (float)atof(argv[5]) : 7.5f;
    const ev_t *ev = CHORD;
    ev_t held[1];
    int vel = getenv("VEL") ? atoi(getenv("VEL")) : 100, seq = 0;
    uint8_t on[128] = {0};
    FILE *w;
    if (argc < 5) {
        fprintf(stderr, "mare_render ENGINE PRESET PHRASE OUT.wav [SECS]\n");
        return 2;
    }
    pi = (argv[2][0] >= '0' && argv[2][0] <= '9') ? (uint32_t)atoi(argv[2]) : host_pi(e, argv[2]);
    host_tracks_init();
    if (getenv("BPM"))
        song.g[G_BPM] = (int16_t)atoi(getenv("BPM"));
    host_preset(&trk[0], e, pi);
    song.sel = 0;
    if (getenv("PSET")) {
        const char *s = getenv("PSET");
        int id, val, c;
        while (sscanf(s, "%d:%d%n", &id, &val, &c) == 2) {
            if (id >= 0 && id < P_COUNT)
                trk[0].p[id] = (int16_t)val;
            s += c;
            if (*s == ',')
                s++;
        }
    }
    if (getenv("LAYER")) {   /* (layered parts) LAYER=preset: part 2 that preset of the same engine, MODE LAYER (both
                              * parts play every key, each its own voices from the shared budget); PSET2 as PSET */
        const char *s = getenv("LAYER");
        uint32_t p2 = (s[0] >= '0' && s[0] <= '9') ? (uint32_t)atoi(s) : host_pi(e, s);
        int id, val, c;
        host_preset(&trk[1], e, p2);
        trk[1].p[P_LYMODE] = LY_LAYER;
        trk[1].p[P_LYOCT] = 2;
        for (s = getenv("PSET2") ? getenv("PSET2") : ""; sscanf(s, "%d:%d%n", &id, &val, &c) == 2;) {
            if (id >= 0 && id < P_COUNT)
                trk[1].p[id] = (int16_t)val;
            s += c;
            if (*s == ',')
                s++;
        }
    }
    if (!strncmp(ph, "notes:", 6)) {
        if (!load_notes(ph + 6)) {
            fprintf(stderr, "no notes in %s\n", ph + 6);
            return 2;
        }
        n = 0;
    } else if (!strncmp(ph, "held:", 5)) {
        held[0].t = 0.2f, held[0].len = secs - 1.7f, held[0].note = (uint8_t)atoi(ph + 5);
        ev = held, n = 1;
    } else if (!strcmp(ph, "chord"))
        ev = CHORD, n = NELEM(CHORD);
    else if (!strcmp(ph, "melody"))
        ev = MELODY, n = NELEM(MELODY);
    else if (!strcmp(ph, "bass"))
        ev = BASS, n = NELEM(BASS);
    else if (!strcmp(ph, "lead"))
        ev = LEAD, n = NELEM(LEAD);
    else if (!strcmp(ph, "riff"))
        ev = RIFF, n = NELEM(RIFF);
    else if (!strcmp(ph, "pedal"))
        ev = PEDAL, n = NELEM(PEDAL);
    else if (!strcmp(ph, "pedal5"))
        ev = PEDAL5, n = NELEM(PEDAL5);
    else if (!strcmp(ph, "seq")) {   /* a line on the part's own sequencer, keys played over it */
        static const uint8_t L[8] = {48, 55, 60, 63, 48, 55, 58, 62};
        for (k = 0; k < 8u; k++)
            put_step(&trk[0], k, 1u, &L[k], ST_NOTE, 0);
        trk[0].p[P_SLEN] = 8;
        transport_req = 1;
        ev = OVER, n = NELEM(OVER), seq = 1;
    } else {
        fprintf(stderr, "no phrase %s\n", ph);
        return 2;
    }
    (void)seq;
    frames = (uint32_t)(secs * FS) / CTL * CTL;
    w = fopen(argv[4], "wb");
    if (!w)
        return 1;
    wav_hdr(w, frames);
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        float t = (float)f / FS;
        uint32_t i;
        if (KEV) {   /* (notes:) every key event before this block's end, in order */
            static uint32_t next, down[128];
            while (next < NKEV && KEV[next].at < f + CTL) {
                kev_t *q = &KEV[next++];
                if (q->on) {
                    if (down[q->note])
                        input_off(&trk[0], q->note);
                    input_on(&trk[0], q->note, q->vel);
                    down[q->note]++;
                } else if (down[q->note] && !--down[q->note])
                    input_off(&trk[0], q->note);
            }
        }
        for (k = 0; k < n; k++) {
            int want = t >= ev[k].t && t < ev[k].t + ev[k].len;
            if (want && !(on[ev[k].note] & 1u)) {
                input_on(&trk[0], ev[k].note, (uint8_t)vel);
                on[ev[k].note] = 1;
            } else if (!want && on[ev[k].note] == 1 && t >= ev[k].t + ev[k].len) {
                uint32_t j, again = 0;   /* (a later event on the same key holds it on) */
                for (j = 0; j < n; j++)
                    again |= j != k && ev[j].note == ev[k].note && t >= ev[j].t && t < ev[j].t + ev[j].len;
                if (!again) {
                    input_off(&trk[0], ev[k].note);
                    on[ev[k].note] = 0;
                }
            }
        }
        mix_block(o, CTL);
        if (getenv("LAYER")) {   /* (layered parts) the most voices sounding at once, each part's */
            static uint32_t mx[2];
            uint32_t pp, c;
            for (pp = 0; pp < 2u; pp++) {
                for (c = 0, i = 0; i < NVOICE; i++)
                    c += trk[pp].v[i].stage != 0u;
                if (c > mx[pp])
                    mx[pp] = c;
            }
            if (f + CTL >= frames)
                fprintf(stderr, "layer: voices at most %u + %u of %u, voices given up %u\n", mx[0], mx[1], NVOICE, voice_kills);
        }
        for (i = 0; i < 2 * CTL; i++) {
            int16_t s = (int16_t)(o[i] > 32767 ? 32767 : o[i] < -32768 ? -32768 : o[i]);
            fwrite(&s, 2, 1, w);
        }
    }
    fclose(w);
    return 0;
}
