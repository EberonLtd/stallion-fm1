/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Changes: Copyright (C) 2026 Eberon Limited (John Gorman) */
/* I2S output (ALNK0 -> external codec) and the audio ISR: each half buffer is
 * rendered in blocks of CTL samples by mix_block (fx.c: events -> each synth part
 * -> dist -> level / pan -> sends -> drums -> buses -> master), then scaled to 24-bit stereo. */
/* registers: hal/fm1_audio.h */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define OUT_SHIFT OUT_FRAC        /* Q15 -> 24-bit, -6 dBFS ceiling (fx.c: out_lo, the bits under it) */

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

/* diagnostics, kept across resets and UBOOT entry: read with `fm1t memr` */
#define DBG_MAGIC 0x44424731u                       /* "DBG1" */
struct felucca_dbg {
    uint32_t magic, halves, max_us, nested;
    volatile uint32_t in_audio;           /* TIMER5 reads it to decide what may nest in the render */
    uint32_t late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots;
    uint32_t stage, page, home;           /* where the main loop is (breadcrumbs) */
    uint32_t prev_stage, prev_page, prev_home, prev_rst, prev_frames;   /* as found at boot */
} felucca_dbg __attribute__((section(".noinit")));
static volatile uint32_t audio_halves, audio_max_us;
static volatile uint32_t t5_nested_ticks;              /* TIMER4 ticks TIMER5 spent nested in this ISR (main.c) */
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N], scope_bufr[SCOPE_N];   /* left, right (the visualiser): the mix at full level */
static uint32_t scope_w;

/* noinline: the ISR calls it a block at a time, as it did up to Clipper 1.1 (1.2's FM-1 image, the drum machine left
 * out, made the mix small enough for the compiler to inline it, in a loop: tests/target_budget.py measures the ISR and
 * the mix apart, fm1_alnk0_irq and audio_block) */
static __attribute__((noinline)) void audio_block(int32_t *out, uint32_t n)   /* mix (fx.c), then Q15 and its low bits -> 24 bit */
{
    uint32_t i;
    mix_block(out, n);
#if FELUCCA_UAC
    uac_tap(usb_full_now ? usb_out : out, n);           /* the USB audio input: the master output, or at full level */
#endif
    for (i = 0; i < n; i++) {
        if (i & 1u)
        {
            scope_bufr[scope_w & (SCOPE_N - 1u)] = vis_tap[2u * i + 1u];   /* (fx.c: MASTER all the way up) */
            scope_buf[scope_w++ & (SCOPE_N - 1u)] = vis_tap[2u * i];
        }
        out[2u * i] = (out[2u * i] << OUT_SHIFT) + out_lo[0][i];
        out[2u * i + 1u] = (out[2u * i + 1u] << OUT_SHIFT) + out_lo[1][i];
    }
}

/* overload: shed_voice (voice.c) */
static volatile uint8_t shed_req;
static uint8_t shed_over;                      /* bit k: the half k halves ago was over 85 % (shed_judge) */
static uint8_t shed_late;                      /* the last half took longer than its time */

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    uint32_t t0;
    felucca_dbg.in_audio = 1;                   /* first: TIMER5 nests from here on (main.c) */
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, us;
        int32_t *o = &abuf[half * HALF_WORDS];
        int shed = SHED_NONE;
        if (shed_req) {
            shed_req = 0;
            shed = shed_voice(shed_late);               /* (after a late half: a fast fade, as before) */
        }
#if FELUCCA_UAC
        uac_render_start();
        usb_full_now = (uint8_t)(usb_full && uac.feed);  /* menu USB AUDIO = FULL, while the computer records */
#endif
        for (b = 0; b < HALF_FRAMES; b += CTL)
            HOT_FAR(audio_block)(o + 2u * b, CTL);      /* (Stallion: in RAM, hal/fm1_hot.h) */
        fm1_audio_ack_half();
        audio_halves++;
        {   /* the deadline sees the render and TIMER5 nested in it (the USB audio stream); the load
             * figures the render alone (after Felucca 1.0) */
            uint32_t all = fm1_ticks() - t0;
            us = (all - t5_nested_ticks) / FM1_TICKS_PER_US;
            {
                uint32_t pct = all / FM1_TICKS_PER_US * 100u / (HALF_FRAMES * 1000000u / FS);
                if (pct > diag_peak)
                    diag_peak = pct;
                shed_late = pct >= 100u;
            }
            if (shed_judge(&shed_over, shed,      /* (a half that gave a voice up slowly: left out) */
                           all / FM1_TICKS_PER_US * 100u > (HALF_FRAMES * 1000000u / FS) * 85u))
                shed_req = 1;                           /* two in a row */
        }
#if CLIPPER_SYNTH
        vc_half(us);                                    /* (voice.c) the load prediction's correction */
#endif
        if (us > audio_max_us)
            audio_max_us = us;
        song.cpu_q8 = (song.cpu_q8 * 15u + (us * 256u) / (HALF_FRAMES * 1000000u / FS)) / 16u;
        if (fm1_audio_free_half() != half)
            felucca_dbg.late++, diag_late++;            /* the DMA moved on while we rendered */
        felucca_dbg.halves++;
        felucca_dbg.last_us = us;
        if (us > felucca_dbg.max_us)
            felucca_dbg.max_us = us;
        felucca_dbg.cpu_q8 = song.cpu_q8;
    }
    felucca_dbg.in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)                   /* hal/fm1_audio.h */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}
