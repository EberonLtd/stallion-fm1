/* SPDX-License-Identifier: GPL-3.0-only */
/* The real firmware's editor protocol on stdin / stdout (tests/editor_host.c: editor.c, upreset.c, project.c, the UI and
 * the audio on the host, CLIPPER_SYNTH=1), for the web editor's own code to talk to (web/test_editor_p15.mjs):
 *   a line of hex bytes "F0 7D 46 4C .. F7"   fed as a USB SysEx; every frame the device sends (replies, pushes) comes
 *                                             back as a line of upper-case hex
 *   #tick N                                   N UI frames of 16 ms (the pushes go out)
 *   #p ID V                                   the selected track's value of protocol 15 id ID set on the FM-1 (a knob)
 *   #x ID V                                   a setting of scope 3 changed on the FM-1
 *   #note K V                                 a USB-MIDI note on (V 0: off), taken by one UI frame
 *   #hz N                                     N audio blocks rendered, one line "#hz F": the pitch heard (Hz)
 *   #cover                                    one JSON line: every protocol 15 id (label), every params.c PAGES entry
 *                                             (layer, scope, ids: protocol 15 ids, G_* or ui_synth.c X_*), the settings
 * and after each line a line "." (in step). specs/stallion R42..R45. */
#include "editor_host.c"

static void out_frame(const uint8_t *f, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        printf(i ? " %02X" : "%02X", f[i]);
    printf("\n");
}
static void jstr(const char *s)
{
    putchar('"');
    for (; s && *s; s++)
        if (*s == '"' || *s == '\\') printf("\\%c", *s);
        else if ((unsigned char)*s < 32 || (unsigned char)*s > 126) printf("\\u%04x", (unsigned char)*s);
        else putchar(*s);
    putchar('"');
}
static void cover(void)
{
    static const char *const FAM[FAM_COUNT] = {"HOME", "ENV", "LFO", "FX", "SEL", "EDIT", "GLO", "SAVE", "ARP", "SEQ", "TRK"};
    static const char *const SCO[] = {"track", "global", "engine", "step", "trk", "song", "drum", "syn"};
    uint32_t i, k;
    printf("{\"pall\":%u,\"pedn\":%u,\"pe0\":%u,\"params\":[", (unsigned)P_COUNT, (unsigned)P_EDN, (unsigned)ed15_of_p(P_E0));
    for (k = 0; k < P_COUNT; k++) {
        uint32_t pid = p_of_ed15(k);
        printf("%s{\"id\":%u,\"label\":", k ? "," : "", (unsigned)k);
        jstr(pid >= P_E0 ? ENGINES[0]->edit[pid - P_E0].label : TP[pid].label);
        printf("}");
    }
    printf("],\"pages\":[");
    for (i = 0; i < NPAGES; i++) {
        const page_t *pg = &PAGES[i];
        uint32_t first = 1;
        printf("%s{\"title\":", i ? "," : "");
        jstr(pg->title);
        printf(",\"layer\":\"%s\",\"scope\":\"%s\",\"ids\":[", FAM[pg->fam % FAM_COUNT], SCO[pg->scope % 8u]);
        for (k = 0; k < 4u; k++) {
            uint32_t id = pg->id[k];
            if (id == 0xFFu) continue;
            if (pg->scope == SC_TRACK || pg->scope == SC_ENGINE) id = ed15_of_p(id);
            printf("%s%u", first ? "" : ",", (unsigned)id);
            first = 0;
        }
        printf("]}");
    }
    printf("],\"settings\":[");
    for (i = 0; i < ED_NSET; i++)
        printf("%s%u", i ? "," : "", ED_SIDS[i]);
    printf("]}\n");
}

/* #hz N: N audio blocks rendered (a held note), one line "#hz F": the left channel's pitch in Hz from its rising zero
 * crossings (low-passed, less its mean) over the second half, the attack left out */
static void hz(int nb)
{
    static double y[44100 * 2];
    static int32_t o[CTL * 2];
    uint32_t n = 0, q, i, a, cnt = 0;
    double lp = 0, m = 0, first = -1, last = -1;
    for (q = 0; q < (uint32_t)nb; q++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL && n < NELEM(y); i++)
            y[n++] = lp += (o[2u * i] - lp) * 0.25;
    }
    a = n / 2u;
    for (i = a; i < n; i++)
        m += y[i];
    m /= n > a ? n - a : 1u;
    for (i = a + 1u; i < n; i++)
        if (y[i - 1] - m < 0 && y[i] - m >= 0) {
            double t = i - 1 + (m - y[i - 1]) / (y[i] - y[i - 1]);
            if (first < 0)
                first = t;
            last = t;
            cnt++;
        }
    printf("#hz %.3f\n", cnt > 1 ? (cnt - 1) * (double)FS / (last - first) : 0.0);
}

int main(void)
{
    static char line[4096];
    harness_init();
    rx_hook = out_frame;
    setvbuf(stdout, 0, _IOLBF, 0);
    while (fgets(line, sizeof line, stdin)) {
        if (!strncmp(line, "#tick", 5)) {
            int n = atoi(line + 5), i;
            for (i = 0; i < n; i++) { ui_frame(); rx_take(); }
        } else if (!strncmp(line, "#p", 2)) {
            unsigned id = 0; int v = 0;
            if (sscanf(line + 2, "%u %d", &id, &v) == 2 && id < P_COUNT) {
                uint32_t pid = p_of_ed15(id);
                const param_desc_t *d = ed_tdesc(TSEL, pid);
                TSEL->p[pid] = (int16_t)clamp(v, d->min, d->max);
                ui.force = 1;
            }
        } else if (!strncmp(line, "#x", 2)) {
            unsigned id = 0; int v = 0;
            if (sscanf(line + 2, "%u %d", &id, &v) == 2)
                ed_sput(id, v);
        } else if (!strncmp(line, "#cover", 6)) {
            cover();
        } else if (!strncmp(line, "#note", 5)) {
            unsigned k = 0, v = 0;
            if (sscanf(line + 5, "%u %u", &k, &v) == 2) {
                midi_in_event((v ? 0x09u : 0x08u) | (v ? 0x90u : 0x80u) << 8 | (k & 127u) << 16 | (v & 127u) << 24);
                ui_frame();                                /* (the note taken by the main loop) */
                rx_take();
            }
        } else if (!strncmp(line, "#hz", 3)) {
            hz(atoi(line + 3));
        } else {
            uint8_t f[700];
            uint32_t n = 0;
            char *p = line;
            for (;;) {
                char *e;
                unsigned long b = strtoul(p, &e, 16);
                if (e == p) break;
                if (b != 0xF0 && b != 0xF7 && n < sizeof f) f[n++] = (uint8_t)b;
                p = e;
            }
            if (n) {
                fm1_ms += 3;                               /* (ed_sync runs every 5 ms) */
                usb_sysex(f, n);
                rx_take();
            }
        }
        printf(".\n");
    }
    return 0;
}
