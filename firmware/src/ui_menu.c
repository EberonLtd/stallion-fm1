/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLOOP menu (HOME held), in sections as the pages are (2.4): SCREEN (COLOR, ZOOM), LIGHTS (LIGHTS, KEYS, NOTES),
 * AUDIO (LOWCUT, USB AUDIO, USB SERIAL), SYSTEM (HARDWARE CALIBRATION, ABOUT). SELECT goes to the section
 * before / after (stopping at the ends), KNOB 1..3 set the section's settings in order (the knob's colour marks
 * its row), PRESETS moves the cursor; OCT+ steps the cursor's setting round or opens it (CALIBRATION, ABOUT),
 * OCT- closes (from ABOUT: back to the section). */
/* ------------------------------------------------------------ menu --- */
/* 0.7 (after SLOOP 2.7): SCREEN + KNOB ACCEL, AUDIO + USB RATE (four rows: the fourth takes the help's place), MIDI
 * (CLOCK OUT, MPE), SYS (2ND CORE, CALIBRATION, ABOUT). Clipper: SCREEN + BROWSE (what PRESETS and ALGORITHM travel) */
/* (Stallion) SYS + FACTORY RESET, last: the fifth row of a Clipper build (the rows scroll; no knob, PRESETS reaches it) */
#ifndef UI_SMALL
#define UI_SMALL                                   /* (gfx.c: minsize on the device; a test alone: nothing) */
#endif
#if CLIPPER_SYNTH && BOARD_SMK37                   /* Clipper Pro: the LIGHTS section is the pads': PAD LIGHTS (HIGH, MID, LOW,
                                                    * kept in the LIGHTS setting), PAD THRESHOLD (in the KEYS setting: the
                                                    * SMK-37's keys have no lights), NOTES */
enum { MI_COLOR, MI_ZOOM, MI_ACCEL, MI_BROWSE, MI_LIGHTS, MI_KEYS, MI_NOTES, MI_SCOPE, MI_LOWCUT, MI_USB, MI_SERIAL, MI_RATE, MI_ADV,
       MI_PASSIGN, MI_PRR, MI_PHAND, MI_CLKOUT, MI_MPE, MI_CPU1, MI_KSPEED, MI_PANEL, MI_ABOUT, MI_FRESET, MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {"COLOR", "ZOOM", "KNOB ACCEL", "BROWSE", "PAD LIGHTS", "PAD THRESHOLD", "NOTES",
                                              "PAD SCOPE", "SPEAKER LOWCUT", "USB AUDIO", "USB SERIAL", "USB RATE", "STEP ADVANCE",
                                              "PARA ASSIGN", "PARA ROUND ROBIN", "PARA NOTE-OFF",
                                              "CLOCK OUT (USB)", "MPE (CH 2-16)", "2ND CORE", "KNOB SPEED", "HARDWARE CALIBRATION",
                                              "ABOUT", "FACTORY RESET"};
static const char *const PADL_NAME[3] = {"HIGH", "MID", "LOW"};
static uint32_t padl_lvl(void) { return lights_lvl < 3u ? lights_lvl : 0u; }   /* 0 HIGH, 1 MID, 2 LOW */
/* PAD THRESHOLD (smk_input.h smk_pad_level), kept in the KEYS setting: 0 MID (the default), 1 LOW, 2 HIGH, 3 MAX; shared
 * with SLOOP Pro (the same settings) */
static const char *const PADT_NAME[4] = {"LOW", "MID", "HIGH", "MAX"};
static uint32_t padt_lvl(void) { return lights_keys == 0u ? 1u : lights_keys == 1u ? 0u : lights_keys & 3u; }
static void padt_set(uint32_t l) { lights_keys = (uint8_t)(l == 1u ? 0u : l == 0u ? 1u : l & 3u); }
#elif CLIPPER_SYNTH                                /* Clipper 0.4: MPE (MIDI: OFF or the member channels' bend range) */
enum { MI_COLOR, MI_ZOOM, MI_ACCEL, MI_BROWSE, MI_LIGHTS, MI_KEYS, MI_NOTES, MI_LOWCUT, MI_USB, MI_SERIAL, MI_RATE,
       MI_ADV, MI_PASSIGN, MI_PRR, MI_PHAND, MI_CLKOUT, MI_MPE, MI_CPU1, MI_KSPEED, MI_PANEL, MI_ABOUT, MI_FRESET, MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {"COLOR", "ZOOM", "KNOB ACCEL", "BROWSE", "LIGHTS", "KEYS", "NOTES",
                                              "SPEAKER LOWCUT", "USB AUDIO", "USB SERIAL", "USB RATE", "STEP ADVANCE",
                                              "PARA ASSIGN", "PARA ROUND ROBIN", "PARA NOTE-OFF", "CLOCK OUT (USB)", "MPE (CH 2-16)", "2ND CORE", "KNOB SPEED",
                                              "HARDWARE CALIBRATION", "ABOUT", "FACTORY RESET"};
#else
enum { MI_COLOR, MI_ZOOM, MI_ACCEL, MI_LIGHTS, MI_KEYS, MI_NOTES, MI_LOWCUT, MI_USB, MI_SERIAL, MI_RATE, MI_ADV, MI_CLKOUT,
       MI_CPU1, MI_PANEL, MI_ABOUT, MI_FRESET, MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {"COLOR", "ZOOM", "KNOB ACCEL", "LIGHTS", "KEYS", "NOTES", "SPEAKER LOWCUT",
                                              "USB AUDIO", "USB SERIAL", "USB RATE", "STEP ADVANCE", "CLOCK OUT (USB)",
                                              "2ND CORE", "HARDWARE CALIBRATION", "ABOUT", "FACTORY RESET"};
#endif
/* (1.0, after SLOOP 2.7.1) SEQ: STEP ADVANCE. (Clipper builds) then PARA ASSIGN, PARA ROUND ROBIN: how MARE's
 * PARA / PARA+ share the VCOs among the held keys (eng_mare.c mar_para; kept with the settings, project.c) */
enum { MS_SCREEN, MS_LIGHTS, MS_AUDIO, MS_SEQ, MS_MIDI, MS_SYSTEM, MS_COUNT };
#if CLIPPER_SYNTH && BOARD_SMK37
static const char *const MS_NAME[MS_COUNT] = {"SCREEN", "PADS", "AUDIO", "SEQ", "MIDI", "SYS"};   /* (the SMK-37: the
                                                 * LIGHTS section is the pads': PAD LIGHTS, PAD THRESHOLD, NOTES, PAD SCOPE) */
#else
static const char *const MS_NAME[MS_COUNT] = {"SCREEN", "LIGHTS", "AUDIO", "SEQ", "MIDI", "SYS"};   /* (AUDIO: and USB) */
#endif
static const uint8_t MS_FIRST[MS_COUNT + 1] = {MI_COLOR, MI_LIGHTS, MI_LOWCUT, MI_ADV, MI_CLKOUT, MI_CPU1, MI_COUNT};   /* rows of each */
UI_SMALL static int mi_cpu1_have(void)                      /* the second core is running (cpu1.c; the host tests have none) */
{
#if FELUCCA_CPU1
    return cpu1_state == CPU1_OK;
#else
    return 0;
#endif
}
static const char *const USB_RATE_NAME[3] = {"AUTO", "44.1 KHZ", "48 KHZ"};
#if CLIPPER_SYNTH
static const char *const PA_NAME[PA_N] = {"PLAYED", "LOW", "HIGH"};   /* PARA ASSIGN (eng_mare.c PA_*) */
#endif
static const char *const LIGHTS_NAME[LIGHTS_N] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8"};   /* every button lit,
                                                * the labels readable: brighter up the scale */
static const char *const KEYS_NAME[KEYS_N] = {"OFF", "C KEYS", "WHITE KEYS", "ALL KEYS"};      /* keys lit too, at the LIGHTS level */
#define MI_Y0 26                                   /* the first row, under the section tabs */
#define MI_DY 50                                   /* a row: the label, the value in large type */
#define MI_ROWS 4u                                 /* rows on the screen (KNOB 1..4); a longer section scrolls with the cursor */

/* (Stallion) SYS > FACTORY RESET (project.c factory_reset: every setting, sound, project and user preset erased, the
 * HARDWARE CALIBRATION panel table kept, then a restart into a fresh install's state). Two deliberate steps: OCT+ on the
 * row asks (ui.menu MENU_FRESET, the question in large type, the seconds left counting down); a second OCT+ at least
 * FR_MIN_MS later and within FR_WAIT_MS erases. Anything else cancels: another button, a HOME tap (ui_input.c), a key or
 * pad played, a knob, OCT-, a second OCT+ sooner (a double tap, a bounce), the question left alone. An erase the flash
 * refused: the keyboard plays on (the audio and USB back) and the same screen says ERASE FAILED - TRY AGAIN, nothing
 * saves until a retry or a switch off and on (fr_failed: the screen, until any key), and the row says ERASE FAILED as
 * long as the saves are held (project.c freset_held: after a warm restart too) */
#define MENU_FRESET 4                              /* ui.menu: the question (1 the list, 2 ABOUT, 3 PAD SCOPE) */
#define FR_WAIT_MS 5000u
#define FR_MIN_MS 600u
static uint32_t fr_t0;                             /* when the question came */
static uint8_t fr_failed;                          /* 1: the failure's screen (project.c factory_reset said so) */
UI_SMALL static void fr_cancel(void)               /* the question (or the failure's screen) closed: the list */
{
    ui.menu = 1;
    ui.force = 1;
}
/* the question's answer: 1 erase, -1 cancel, 0 still asking (now: fm1_ms; ok: OCT+ pressed; other: anything else) */
UI_SMALL static int fr_answer(uint32_t now, uint32_t ok, uint32_t other)
{
    uint32_t dt = now - fr_t0;
    if (other || dt >= FR_WAIT_MS)
        return -1;
    if (ok)
        return dt >= FR_MIN_MS ? 1 : -1;
    return 0;
}

UI_SMALL static uint32_t mi_sec(uint32_t i)                 /* the section of item i */
{
    uint32_t k = 0;
    while (k + 1u < MS_COUNT && i >= MS_FIRST[k + 1u])
        k++;
    return k;
}
UI_SMALL static uint32_t mi_row(uint32_t i) { return i - MS_FIRST[mi_sec(i)]; }   /* its row: KNOB 1 + row sets it */

/* item i's value as shown, and its colour */
UI_SMALL static const char *mi_value(uint32_t i, uint16_t *c)
{
    *c = C_HI;
    switch (i) {
    case MI_COLOR: return PALETTES[settings.palette].name;
    case MI_ZOOM: return settings.zoom ? "ON" : "OFF";
#if CLIPPER_SYNTH && BOARD_SMK37
    case MI_LIGHTS: return PADL_NAME[padl_lvl()];
#else
    case MI_LIGHTS: return LIGHTS_NAME[lights_lvl % LIGHTS_N];
#endif
    case MI_KEYS:
#if CLIPPER_SYNTH && BOARD_SMK37
        return PADT_NAME[padt_lvl()];
#else
        *c = lights_lvl ? C_HI : C_DIM;               /* (needs LIGHTS) */
        return KEYS_NAME[lights_keys % KEYS_N];
#endif
    case MI_NOTES: return lights_notes ? "ON" : "OFF";
    case MI_LOWCUT: return settings.lowcut ? "ON" : "OFF";
    case MI_USB: return usb_full ? "FULL" : "MASTER";
    case MI_SERIAL: return usb_serial ? "ON" : "OFF";
    case MI_ACCEL: return knob_accel_off ? "OFF" : "ON";   /* (0.7) the knobs speed up with a fast turn */
    case MI_RATE: return USB_RATE_NAME[usb_rate % 3u];     /* (0.7) for hosts that record one rate badly */
    case MI_CPU1: return cpu1_off ? "OFF" : "ON";          /* (0.7) the second core renders half the voices */
    case MI_CLKOUT: return clk_out ? "ON" : "OFF";         /* (0.7) the MIDI clock out on USB while SYNC is INT */
    case MI_ADV: return step_adv_off ? "OFF" : "ON";       /* (1.0) the NOTES page's cursor moves on when the keys are let go */
#if CLIPPER_SYNTH
    case MI_PASSIGN: return PA_NAME[para_assign % PA_N];  /* MARE PARA: the keys' order, or by pitch */
    case MI_PRR:
        *c = para_assign % PA_N == PA_PLAYED ? C_HI : C_DIM;   /* (acts with PLAYED only) */
        return para_rr ? "ON" : "OFF";
    case MI_PHAND: return para_hand ? "HAND BACK" : "KEEP";   /* a key let go: its VCOs keep its note */
#endif
#if CLIPPER_SYNTH
    case MI_MPE: return MPE_NAME[mpe_mode % MPE_MODES];
    case MI_BROWSE: return browse_mode == BR_ALGO ? "ALGORITHM" : "PRESET";   /* what the PRESETS knob travels */
    case MI_KSPEED: return KSPD_NAME[knob_speed_pos()];    /* (Stallion) values a detent on the number dials */
#endif
    case MI_FRESET:                                   /* (Stallion) large: OCT+ asks first; red only when it failed */
        if (freset_held()) {
            *c = TE_RED;
            return "ERASE FAILED";
        }
        return "OCT+ ASKS";
    default:
        *c = C_DIM;
        return "";                                    /* (an action: OCT+ opens it) */
    }
}

/* (stallion R1) "1.0 8V 2 CORES": the version (FELUCCA_VERSION less its name), the voices, the cores: ABOUT and
 * the boot screen, under the name */
UI_SMALL static void ver_line(char *b, uint32_t n)
{
    const char *v = FELUCCA_VERSION;
    if (*v < '0' || *v > '9') {                     /* "STALLION 1.0" -> "1.0" */
        while (*v && *v != ' ')
            v++;
        while (*v == ' ')
            v++;
    }
    str_cpy(b, v, n - 16u);
    str_cpy(b + str_len(b), " ", 2);
    fmt_int(b + str_len(b), NVOICE);
    str_cpy(b + str_len(b), "V ", 3);
#if FELUCCA_CPU1                                    /* (the host tests include this file without cpu1.c) */
    str_cpy(b + str_len(b), cpu1_label(), 8);       /* 2 CORES / 1 CORE / C1 OFF */
#else
    str_cpy(b + str_len(b), "1 CORE", 8);
#endif
}

/* (stallion R1) ABOUT's small longship by the name, in the logo's colours: stern post, hull, the stem and the
 * horse's head, the mast, the sail's five stripes (tools/gen_logo.py draws the full mark) */
static void about_ship(int32_t x, int32_t y)
{
    static const int8_t R[][5] = {{0, 12, 4, 14, 0}, {2, 22, 68, 6, 0}, {66, 8, 4, 18, 0}, {66, 4, 10, 5, 0},
                                  {36, 0, 2, 22, 2}, {16, 3, 8, 17, 2}, {24, 3, 8, 17, 1}, {32, 3, 8, 17, 2},
                                  {40, 3, 8, 17, 1}, {48, 3, 8, 17, 2}};   /* x, y, w, h, colour */
    static const uint16_t COL[3] = {RGB(217, 142, 47), RGB(184, 67, 42), RGB(243, 238, 226)};
    uint32_t i;
    for (i = 0; i < NELEM(R); i++)
        cv_rect(x + R[i][0], y + R[i][1], R[i][2], R[i][3], COL[R[i][4]]);
}

UI_SMALL static void draw_menu(void)
{
    uint32_t i, pass, sec = mi_sec(ui.menu_sel % MI_COUNT);
#if BOARD_SMK37
    if (ui.menu == 3) {                             /* PADS > PAD SCOPE: its own screen (ui_scope.c) */
        scope_draw();
        return;
    }
#endif
    uint32_t sig = ui.menu * 7u + ui.menu_sel * 131u + settings.palette * 1009u + settings.lowcut * 7919u +
                   (ui.menu == MENU_FRESET ? (fm1_ms - fr_t0) / 1000u * 613u : 0u) + (fr_failed + 2u * (uint32_t)freset_held()) * 2909u +   /* (FACTORY
                                                     * RESET: the seconds, the failure) */
                   settings.zoom * 104729u + lights_lvl * 1299709u + lights_keys * 15485863u +
                   lights_notes * 32452843u + usb_full * 49979687u + usb_serial * 86028121u +
                   knob_accel_off * 179424673u + usb_rate * 373587883u + cpu1_off * 674506111u + clk_out * 982451653u +
                   step_adv_off * 236887691u
#if CLIPPER_SYNTH
                   + mpe_mode * 2654435761u + browse_mode * 2246822519u + knob_speed * 3266489917u
                   + para_assign * 2166136261u + para_rr * 16777619u + para_hand * 40503u
#if BOARD_SMK37
                   + pick_alg * 69069u
#endif
                  
#endif
        ;
    if (!ui.force && sig == ui.menu_sig)
        return;
    ui.menu_sig = sig;
    if (ui.force)                                   /* head + rule + two bands cover rows 0..229 */
        lcd_fill(0, H_HEAD + 1 + 124 + 95, 240, 240 - (H_HEAD + 1 + 124 + 95), C_BLACK);
    cv_begin(240, H_HEAD, C_BLACK);
    if (ui.menu == MENU_FRESET)
        cv_text(4, 1, &FONT_S, "FACTORY RESET", C_WHITE);   /* (red only on the line that erases) */
    else
        cv_text(4, 1, &FONT_S, ui.menu == 2 ? "ABOUT" : "MENU", C_HI);
    if (ui.menu == 1) {                             /* the section and its number, as a page's "ENV DEST 2/2" */
        char t[16];
        str_cpy(t, MS_NAME[sec], sizeof t);
        str_cpy(t + str_len(t), " ", 2);
        fmt_int(t + str_len(t), (int32_t)sec + 1);
        str_cpy(t + str_len(t), "/", 2);
        fmt_int(t + str_len(t), MS_COUNT);
        cv_text(236 - text_w(&FONT_S, t), 1, &FONT_S, t, C_GRAY);
    }
    cv_blit(0, Y_HEAD);
    lcd_fill(0, H_HEAD, 240, 1, C_LINE);
    for (pass = 0; pass < 2u; pass++) {             /* the canvas holds 124 rows: draw in two bands */
        cv_begin(240, pass ? 95u : 124u, C_BLACK);
        cv_oy = pass ? -124 : 0;
        if (ui.menu == MENU_FRESET && fr_failed) { /* the erase failed: large, nothing saved until a retry */
            static const char *const F[7] = {"ERASE FAILED -", "TRY AGAIN", "", "STILL PLAYS,", "NOTHING SAVES",
                                             "UNTIL A RETRY", "OR OFF AND ON"};   /* (QUICKSTART.md says the same) */
            for (i = 0; i < 7u; i++)
                cv_text(4, (int32_t)i * 31, &FONT_L, F[i], i == 0u ? TE_RED : i == 1u ? C_WHITE : C_HI);
        } else if (ui.menu == MENU_FRESET) {        /* the question, large (32 high: 7 lines in the 219 rows) */
            static const char *const Q[4] = {"ERASE SETTINGS", "SOUNDS, USER", "PRESETS AND", "PROJECTS?"};
            char t[8];
            uint32_t left = (FR_WAIT_MS - (fm1_ms - fr_t0) + 999u) / 1000u;
            for (i = 0; i < 4u; i++)
                cv_text(4, (int32_t)i * 31, &FONT_L, Q[i], C_WHITE);
            cv_text(4, 124, &FONT_L, "OCT+ = ERASE", TE_RED);   /* (the one red line: it erases) */
            cv_text(4, 155, &FONT_L, "OTHER: CANCEL", C_HI);
            fmt_int(t, (int32_t)(left > FR_WAIT_MS / 1000u ? FR_WAIT_MS / 1000u : left));
            str_cpy(t + str_len(t), " S", 3);
            cv_text(4, 186, &FONT_L, t, C_WHITE);         /* the seconds left, on their own line */
        } else if (ui.menu == 2) {
            char vl[32];
            cv_text(4, 4, &FONT_L, "STALLION", C_WHITE);
            about_ship(156, 4);
            ver_line(vl, sizeof vl);                    /* (stallion R1) "1.0 8V 2 CORES" under the name */
            cv_text(4, 36, &FONT_S, vl, C_HI);
            cv_text(236 - text_w(&FONT_S, __DATE__), 36, &FONT_S, __DATE__, C_GRAY);   /* build date */
            cv_text(4, 49, &FONT_S, "COPYRIGHT (C) 2026", C_WHITE);   /* (Stallion's notice, LICENSING.md 7(b)) */
            cv_text(4, 62, &FONT_S, "EBERON LIMITED (JOHN GORMAN)", C_WHITE);
            cv_text(4, 75, &FONT_S, "FROM CLIPPER + SLOOP (ISOD89)", C_AMB);
            cv_text(cv_text(4, 88, &FONT_S, "LEO KUROSHITA", C_HI) + 8, 88, &FONT_S, "@KUROGEDELIC", C_AMB);
            cv_text(4, 101, &FONT_S, "H\xDCGELTON INSTRUMENTS", C_HI);   /* Latin-1 U-umlaut */
            cv_text(4, 114, &FONT_S, "HUGELTON.COM", C_AMB);
            cv_text(4, 127, &FONT_S, "GPL-3.0, NO WARRANTY", C_HI);
            cv_text(4, 140, &FONT_S, "SRC: EBERONLTD/STALLION-FM1", C_AMB);   /* (the source of this firmware: GitHub) */
            cv_text(4, 153, &FONT_S, "FONT: TERMINUS (OFL)", C_DIM);
#if CLIPPER_SYNTH                                            /* (no built-in samples: the DSP's sources) */
            cv_text(4, 166, &FONT_S, "LADDER: ZAVALISHIN", C_DIM);
            cv_text(4, 179, &FONT_S, "PLATE: DATTORRO", C_DIM);
#else
            cv_text(4, 166, &FONT_S, "SAMPLES: VERSILIAN (CC0)", C_DIM);
            cv_text(4, 179, &FONT_S, "+ SONIC PI (CC0)", C_DIM);
#endif
            cv_text(4, 192, &FONT_S, "PHASE: CRISPYZEBRA (GPL)", C_DIM);
            cv_text(4, 205, &FONT_S, "VOICE: REF. KLATTSCH (MIT)", C_DIM);
        } else {
            int32_t x = 2;
            for (i = 0; i < MS_COUNT; i++) {        /* the sections as tabs: this one lit (1.0: six, 4 apart: the
                                                     * last ends at 240) */
                int32_t w = text_w(&FONT_S, MS_NAME[i]);
                if (i == sec)
                    cv_rect(x - 2, 3, w + 4, 18, C_LINE);
                cv_text(x, 4, &FONT_S, MS_NAME[i], i == sec ? C_WHITE : C_DIM);
                x += w + 4;
            }
            uint32_t sr = ui.menu_sel - MS_FIRST[sec], top = sr >= MI_ROWS ? sr - (MI_ROWS - 1u) : 0u;   /* (scrolled:
                                                     * the cursor's row the last one shown) */
            for (i = MS_FIRST[sec] + top; i < MS_FIRST[sec + 1u] && i < MS_FIRST[sec] + top + MI_ROWS; i++) {
                uint32_t r = i - MS_FIRST[sec];
                int32_t y = MI_Y0 + (int32_t)(r - top) * MI_DY, xv;
                int cur = i == ui.menu_sel;
                uint16_t vc, kc = r < MI_ROWS ? TE_COL[r & 3u] : C_DIM;   /* (past KNOB 4: no knob, PRESETS reaches it) */
                const char *v = mi_value(i, &vc);
                char k[4] = {'K', (char)('1' + r), 0, 0};
                if (r >= MI_ROWS)
                    k[0] = 0;
                cv_rect(4, y + 2, 4, MI_DY - 8, cur ? C_WHITE : kc);   /* the knob's colour */
                xv = k[0] ? cv_text(14, y, &FONT_S, k, kc) : 8;
                cv_text(xv + 6, y, &FONT_S, MI_NAME[i], cur ? C_WHITE : C_GRAY);
                if (v[0] && text_w(&FONT_L, v) <= 222) {
                    xv = cv_text(14, y + 16, &FONT_L, v, cur && vc == C_HI ? C_WHITE : vc);
                } else if (v[0]) {                      /* (too wide for the large type: WHITE KEYS) */
                    xv = cv_text(14, y + 24, &FONT_S, v, cur && vc == C_HI ? C_WHITE : vc);
                } else {
                    xv = cv_text(14, y + 24, &FONT_S, "OCT+ OPENS", cur ? C_WHITE : C_DIM);
                }
                if (i == MI_COLOR) {                    /* the palette's colours */
                    uint32_t q;
                    for (q = 0; q < 5u; q++)
                        cv_rect(xv + 10 + (int32_t)q * 14, y + 28, 10, 10, pal[q]);
                }
                if (i == MI_SERIAL && usb_serial != usb_cdc_now())
                    cv_text(xv + 8, y + 30, &FONT_S, "RESTART", C_AMB);   /* (usb.c: at the next start) */
                if (i == MI_RATE && usb_rate != usb_rate_now())
                    cv_text(xv + 8, y + 30, &FONT_S, "RESTART", C_AMB);
                if (i == MI_CPU1 && !cpu1_off && !mi_cpu1_have())
                    cv_text(xv + 8, y + 30, &FONT_S, "NOT RUNNING", C_AMB);   /* (cpu1.c: ABOUT says 1 CORE) */
            }
            if (MS_FIRST[sec + 1u] - MS_FIRST[sec] <= 3u) {   /* (a section of four rows: its fourth takes the help's place) */
                cv_text(4, MI_Y0 + 3 * MI_DY + 6, &FONT_S, "SELECT SECTION  KNOB SETS", C_DIM);
                cv_text(4, MI_Y0 + 3 * MI_DY + 22, &FONT_S, "OCT+ OK   OCT- CLOSE", C_DIM);   /* (rows .. 218) */
            }
        }
        cv_oy = 0;
        cv_blit(0, H_HEAD + 1 + pass * 124u);
    }
}

UI_SMALL static void enc_drop(void)                             /* knob turns nobody takes */
{
    uint32_t k;
    for (k = 0; k < NE; k++)
        panel_enc(k);
}

UI_SMALL static void menu_close(void)
{
    if (song.playing || transport_req)
        settings_later = 1;                            /* (a flash write stops the audio: once stopped) */
    else
        settings_save();                               /* palette / panel table, if changed */
#if BOARD_SMK37
    scope_close();                                     /* (PAD SCOPE: no more recording) */
#endif
    ui.menu = 0;
    ui.force = 1;
    go_home();
}

/* item i: a knob turned s (> 0 right, < 0 left), or 0: OCT+ (steps round, toggles, or opens) */
UI_SMALL static void mi_set(uint32_t i, int32_t s)
{
    switch (i) {
    case MI_COLOR:                                     /* both ways round; OCT+ the next */
        settings.palette = (settings.palette + (s < 0 ? NPALETTES - 1u : 1u)) % NPALETTES;
        palette_set(settings.palette);
        break;
    case MI_ZOOM:
    case MI_LOWCUT: {                                  /* right ON, left OFF; OCT+ toggles */
        uint32_t *v = i == MI_LOWCUT ? &settings.lowcut : &settings.zoom;
        *v = s > 0 ? 1u : s < 0 ? 0u : !*v;
        fx_lowcut = (uint8_t)(settings.lowcut != 0);
        break;
    }
    case MI_USB:                                       /* right FULL, left MASTER */
        usb_full = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !usb_full);
        break;
    case MI_SERIAL:
        usb_serial = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !usb_serial);
        break;
    case MI_ACCEL:                                     /* (0.7) right ON, left OFF; OCT+ toggles */
        knob_accel_off = (uint8_t)(s > 0 ? 0u : s < 0 ? 1u : !knob_accel_off);
        break;
    case MI_RATE:                                      /* (0.7) AUTO, 44.1, 48 (stops at the ends); OCT+ steps round */
        if (s > 0 && usb_rate < 2u)
            usb_rate++;
        else if (s < 0 && usb_rate > 0u)
            usb_rate--;
        else if (!s)
            usb_rate = (uint8_t)((usb_rate + 1u) % 3u);
        break;
    case MI_ADV:                                       /* (1.0) right ON, left OFF; OCT+ toggles */
        step_adv_off = (uint8_t)(s > 0 ? 0u : s < 0 ? 1u : !step_adv_off);
        break;
#if CLIPPER_SYNTH
    case MI_PASSIGN:                                   /* PLAYED LOW HIGH (stops at the ends); OCT+ steps round */
        para_assign = (uint8_t)(s > 0 ? (para_assign + 1u < PA_N ? para_assign + 1u : PA_N - 1u) :
                                s < 0 ? (para_assign ? para_assign - 1u : 0u) : (para_assign + 1u) % PA_N);
        break;
    case MI_PRR:                                       /* right ON, left OFF; OCT+ toggles */
        para_rr = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !para_rr);
        break;
    case MI_PHAND:                                     /* right HAND BACK, left KEEP; OCT+ toggles */
        para_hand = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !para_hand);
        break;
#endif
    case MI_CLKOUT:                                    /* (0.7) right ON, left OFF; OCT+ toggles */
        clk_out = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !clk_out);
        break;
    case MI_CPU1:                                      /* (0.7) right ON, left OFF; OCT+ toggles: live (the next block) */
        cpu1_off = (uint8_t)(s > 0 ? 0u : s < 0 ? 1u : !cpu1_off);
        cpu1_units = cpu1_off ? 0 : -1;
        if (!cpu1_off && !cpu1_on())                   /* (core 1 starts at power-on only) */
            ui_message("AT THE NEXT POWER-ON");
        break;
#if CLIPPER_SYNTH
    case MI_MPE:                                       /* OFF, then the bend range; OCT+ steps round */
        if (s > 0 && mpe_mode + 1u < MPE_MODES)
            mpe_mode++;
        else if (s < 0 && mpe_mode > 0u)
            mpe_mode--;
        else if (!s)
            mpe_mode = (uint8_t)((mpe_mode + 1u) % MPE_MODES);
        mpe_reset();
        at_reset();                                    /* (Stallion: the pressure shaping's MPE states too) */
        break;
    case MI_BROWSE:                                    /* right ALGORITHM, left PRESET; OCT+ toggles */
        browse_mode = (uint8_t)(s > 0 ? BR_ALGO : s < 0 ? BR_PRESET : browse_mode == BR_ALGO ? BR_PRESET : BR_ALGO);
        break;
    case MI_KSPEED: {                                  /* (Stallion) X1 X2 X4: right faster, left finer; OCT+ steps round */
        uint32_t k = knob_speed_pos();
        k = s > 0 ? (k < 2u ? k + 1u : 2u) : s < 0 ? (k ? k - 1u : 0u) : (k + 1u) % 3u;
        knob_speed = (uint8_t)(k + 1u);
        break;
    }
#endif
    case MI_NOTES:
        lights_notes = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !lights_notes);
        break;
#if CLIPPER_SYNTH && BOARD_SMK37
    case MI_LIGHTS: {                                  /* PAD LIGHTS: right brighter, left dimmer; OCT+ steps round */
        uint32_t l = padl_lvl();
        l = s > 0 ? (l ? l - 1u : 0u) : s < 0 ? (l < 2u ? l + 1u : 2u) : (l + 1u) % 3u;
        lights_lvl = (uint8_t)l;
        break;
    }
    case MI_KEYS: {                                    /* PAD THRESHOLD: right higher, left lower; OCT+ steps round */
        uint32_t l = padt_lvl();
        padt_set(s > 0 ? (l < 3u ? l + 1u : 3u) : s < 0 ? (l ? l - 1u : 0u) : (l + 1u) % 4u);
        break;
    }
#else
    case MI_LIGHTS:
    case MI_KEYS: {                                    /* brighter / dimmer (stops at the ends); OCT+ steps round */
        uint8_t *v = i == MI_LIGHTS ? &lights_lvl : &lights_keys;
        uint32_t n = i == MI_LIGHTS ? LIGHTS_N : KEYS_N;
        if (s > 0 && *v + 1u < n)
            (*v)++;
        else if (s < 0 && *v > 0u)
            (*v)--;
        else if (!s)
            *v = (uint8_t)((*v + 1u) % n);
        if (i == MI_KEYS && lights_keys && !lights_lvl)
            lights_lvl = LIGHTS_LOW;                   /* keys lit need a level: the lowest */
        break;
    }
#endif
    case MI_PANEL:                                     /* actions: OCT+ only */
        if (!s) {
            panel_setup();
            ui.force = 1;
        }
        break;
    case MI_ABOUT:
        if (!s) {
            ui.menu = 2;
            ui.force = 1;
        }
        break;
    case MI_FRESET:                                    /* OCT+ only: the question first */
        if (!s) {
            ui.menu = MENU_FRESET;
            fr_t0 = fm1_ms;
            fr_failed = 0;
            ui.force = 1;
        }
        break;
#if CLIPPER_SYNTH && BOARD_SMK37
    case MI_SCOPE:                                     /* PAD SCOPE: OCT+ opens it (ui_scope.c) */
        if (!s) {
            scope_open();
            ui.menu = 3;
            ui.force = 1;
        }
        break;
#endif
    default:
        break;
    }
}

/* menu: SELECT the section, KNOB 1..3 its settings, PRESETS the cursor, OCT+ ok, OCT- close (ABOUT -> the section) */
UI_SMALL static void menu_input(uint32_t pressed)
{
    int32_t s;
    uint32_t k, ok = (pressed >> panel.btn[B_OCTUP]) & 1u, back = (pressed >> panel.btn[B_OCTDN]) & 1u;
    uint32_t sel = ui.menu_sel % MI_COUNT, sec = mi_sec(sel), n = MS_FIRST[sec + 1u] - MS_FIRST[sec];
    if (ui.menu == MENU_FRESET) {                      /* FACTORY RESET's question: OCT+ again, or anything cancels */
        uint32_t turned = 0;
        int a;
        for (k = 0; k < NE; k++)
            turned |= panel_enc(k) != 0;
        if (fr_failed) {                               /* (the failure's screen: any key or knob, the list) */
            if (pressed || turned)
                fr_cancel();
            return;
        }
        a = fr_answer(fm1_ms, ok, turned || (pressed & ~(1u << panel.btn[B_OCTUP])));
        if (a > 0 && factory_reset()) {                /* (restarts; back here only when the flash refused) */
            fr_failed = 1;
            ui.force = 1;
            return;
        }
        if (a)
            fr_cancel();
        return;
    }
    if (back) {
#if BOARD_SMK37
        if (ui.menu == 3)
            scope_close();
        if (ui.menu >= 2)
#else
        if (ui.menu == 2)
#endif
            ui.menu = 1, ui.force = 1;
        else
            menu_close();
        return;
    }
#if BOARD_SMK37
    if (ui.menu == 3 && ok) {                          /* PAD SCOPE: OCT+ records again */
        smk_scope_rearm();
        ui.force = 1;
    }
#endif
    if (ui.menu != 1) {
        enc_drop();
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0) {             /* the section before / after (stops at the ends) */
        int32_t to = clamp((int32_t)sec + (s > 0 ? 1 : -1), 0, MS_COUNT - 1);
        if ((uint32_t)to != sec) {
            ui.menu_sel = MS_FIRST[to];
            ui.force = 1;
        }
        enc_drop();
        return;
    }
    if ((s = panel_enc(EN_PRESET)) != 0)               /* the cursor, round the section's rows */
        ui.menu_sel = (uint8_t)(MS_FIRST[sec] + (sel - MS_FIRST[sec] + (s > 0 ? 1u : n - 1u)) % n);
    for (k = 0; k < 4u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0 && k < n) {
            ui.menu_sel = (uint8_t)(MS_FIRST[sec] + k);   /* (the cursor follows the knob turned) */
            mi_set(MS_FIRST[sec] + k, s);
        }
    if (ok)
        mi_set(ui.menu_sel % MI_COUNT, 0);
    enc_drop();                                        /* swallow the rest while the menu is up */
}
