/*
 * ted-sound.c
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
 *  Tibor Biczo <crown @ axelero . hu>
 *  Marco van den Heuvel <blackystardust68@yahoo.com>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "digiblaster.h"
#include "lib.h"
#include "maincpu.h"
#include "machine.h"
#include "plus4.h"
#include "plus4speech.h"
#include "sid.h"
#include "sidcart.h"
#include "sid-resources.h"
#include "sound.h"
#include "snapshot.h"
#include "ted-sound.h"

/* #define DEBUG_TEDSOUND */

#ifdef DEBUG_TEDSOUND
#pragma GCC diagnostic warning "-O3"
#pragma GCC diagnostic warning "-Wall"
#pragma GCC diagnostic warning "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#define DBG(x)     log_printf x
#else
#define DBG(x)
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------------- */

/* Some prototypes are needed */
static int ted_sound_machine_init(sound_t *psid, int speed, int cycles_per_sec);
static void ted_sound_machine_store(sound_t *psid, uint16_t addr, uint8_t val);
static uint8_t ted_sound_machine_read(sound_t *psid, uint16_t addr);

#ifdef SOUND_SYSTEM_FLOAT
static int ted_sound_machine_calculate_samples(sound_t **psid, float *pbuf, int nr, int sound_chip_channels, CLOCK *delta_t);
#else
static int ted_sound_machine_calculate_samples(sound_t **psid, int16_t *pbuf, int nr, int sound_output_channels, int sound_chip_channels, CLOCK *delta_t);
#endif

static int ted_sound_machine_cycle_based(void)
{
    return 0;   /* we are NOT cycle based */
}

static int ted_sound_machine_channels(void)
{
    return 1;
}

#ifdef SOUND_SYSTEM_FLOAT
/* stereo mixing placement of the TED sound */
static sound_chip_mixing_spec_t ted_sound_mixing_spec[SOUND_CHIP_CHANNELS_MAX] = {
    {
        100, /* left channel volume % in case of stereo output, default output to both */
        100  /* right channel volume % in case of stereo output, default output to both */
    }
};
#endif

/* TED sound device */
static sound_chip_t ted_sound_chip = {
    NULL,                                /* NO sound chip open function */
    ted_sound_machine_init,              /* sound chip init function */
    NULL,                                /* NO sound chip close function */
    ted_sound_machine_calculate_samples, /* sound chip calculate samples function */
    ted_sound_machine_store,             /* sound chip store function */
    ted_sound_machine_read,              /* sound chip read function */
    ted_sound_reset,                     /* sound chip reset function */
    ted_sound_machine_cycle_based,       /* sound chip 'is_cycle_based()' function, chip is NOT cycle based */
    ted_sound_machine_channels,          /* sound chip 'get_amount_of_channels()' function, sound chip has 1 channel */
#ifdef SOUND_SYSTEM_FLOAT
    ted_sound_mixing_spec,               /* stereo mixing placement specs */
#endif
    1                                    /* sound chip enabled flag, chip is always enabled */
};

static uint16_t ted_sound_chip_offset = 0;

void ted_sound_chip_init(void)
{
    ted_sound_chip_offset = sound_chip_register(&ted_sound_chip);
}

/* ------------------------------------------------------------------------- */

static uint8_t plus4_sound_data[5];
static uint8_t last_sound_read;

/* dummy function for now */
int machine_sid2_check_range(unsigned int sid_adr)
{
    return 0;
}

/* dummy function for now */
int machine_sid3_check_range(unsigned int sid_adr)
{
    return 0;
}

/* dummy function for now */
int machine_sid4_check_range(unsigned int sid_adr)
{
    return 0;
}

/* dummy function for now */
int machine_sid5_check_range(unsigned int sid_adr)
{
    return 0;
}

/* dummy function for now */
int machine_sid6_check_range(unsigned int sid_adr)
{
    return 0;
}

/* dummy function for now */
int machine_sid7_check_range(unsigned int sid_adr)
{
    return 0;
}

/* dummy function for now */
int machine_sid8_check_range(unsigned int sid_adr)
{
    return 0;
}

/* dummy function for now */
int machine_sid9_check_range(unsigned int sid_adr)
{
    return 0;
}

/* dummy function for now */
int machine_sid10_check_range(unsigned int sid_adr)
{
    return 0;
}

void machine_sid2_enable(int val)
{
}

/* Output stage.  The output level changes in steps at the ticks of the
   voices and at register writes.  Each step is spread over TED_SOUND_TAPS
   output samples by the step response of a low-pass filter at half the
   sample rate, so the tones and their harmonics above it do not alias.  The
   filter is the minimum phase version of a Kaiser windowed sinc: the same
   magnitude response, but a step rises within about three samples instead
   of half the taps.  The response is tabulated for TED_SOUND_PHASES
   positions within a sample.  */
#define TED_SOUND_TAPS      48
#define TED_SOUND_PHASES    64
#define TED_SOUND_RING      (TED_SOUND_TAPS + 2)
#define TED_SOUND_KAISER    8.0
#define TED_SOUND_FFT_SIZE  16384

/* The board couples SND to its audio amplifier through R10 (1 kOhm) and C18
   (10 uF) into R11 (12 kOhm) and R12 (10 kOhm): a high-pass filter with a
   time constant of (R10 + R11 || R12) * C18 (Plus/4 schematic 310164, sheet
   2).  Its low-pass pole at about 70 kHz (C19) is not modelled.  */
#define TED_SOUND_DC_TIME   ((1000.0 + 12000.0 * 10000.0 / 22000.0) * 10e-6)

struct plus4_sound_s {
    /* Voice 0 active sound counter (single clock / 4). */
    uint32_t voice0_accu;
    /* Voice 0 reload value after counter overflow. */
    uint32_t voice0_reload;
    /* Voice 0 sign of the square wave */
    int16_t voice0_sign;
    uint8_t voice0_output_enabled;

    /* Voice 1 active sound counter (single clock / 4). */
    uint32_t voice1_accu;
    /* Voice 1 reload value after counter overflow. */
    uint32_t voice1_reload;
    /* Voice 1 sign of the square wave */
    int16_t voice1_sign;
    uint8_t voice1_output_enabled;

    uint8_t voice0_cached_output;
    uint8_t voice1_cached_output;
    uint16_t digital_cached_output;

    /* Time is in CPU clocks multiplied by the output sample rate.
       One sound counter tick takes eight double-speed CPU clocks. */
    uint32_t tick_length;
    uint32_t tick_remaining;
    uint32_t sample_length;
    uint32_t sample_rate;
    uint32_t partial_length;
    uint32_t cycle_pending;
    /* Volume table index. */
    int16_t volume;
    /* Digital output?  */
    uint8_t digital;
    /* Noise generator active?  */
    uint8_t noise;
    uint8_t noise_shift_register;
    uint8_t noise_output;
    /* Counter ticks since each voice last changed its state.  */
    uint32_t voice0_hold;
    uint32_t voice1_hold;

    /* Output stage: the output level, the band-limited steps still to be
       added to the next samples (`steps[step_head]' is the current one),
       the band-limited output and the DC blocking stage of the board.  */
    int32_t level;
    unsigned int step_head;
    double steps[TED_SOUND_RING];
    double output;
    double dc_input;
    double dc_output;
    double dc_factor;
};

static struct plus4_sound_s snd;
#ifdef SOUND_SYSTEM_FLOAT
static float *primary_buffer;
#endif

#define CTRL_VOICE0_ENABLE  0x10
#define CTRL_VOICE1_ENABLE  0x20
#define CTRL_NOISE_ENABLE   0x40
#define CTRL_DIGITAL_ENABLE 0x80

#define OSCRELOADVAL 0x400

/* The voices keep their states in dynamic latches: without a state change
   a state clears, and the output goes high, after 188416 counter ticks
   (0.85 s), as in plus4emu (`soundDecayCycles') and FPGATED (`watchdog',
   whose voice 1 reset is not connected).  The noise register holds: TLC
   found the noise at $3fe settling high or low.  */
#define TED_SOUND_DECAY_TICKS 188416U

/* Mean output levels from TLC's measurements on a Plus/4, 2000-09-07:
   https://plus4world.powweb.com/ma/1550
   Subtract the measured idle level (31), average the two single voices,
   and scale the both-voices maximum (15674) to the existing peak (19976).
   This models the measured average output, not individual PWM pulses or
   a universal analogue transfer function for every TED/board revision.
   Bits 5 and 4 select the active voices; bits 3..0 select volume. */
static const int16_t volumeTable[4 * 16] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0299, 0x076b, 0x0c31, 0x1111, 0x1603, 0x1afb, 0x2009,
    0x2461, 0x2461, 0x2461, 0x2461, 0x2461, 0x2461, 0x2461, 0x2461,
    0x0000, 0x0299, 0x076b, 0x0c31, 0x1111, 0x1603, 0x1afb, 0x2009,
    0x2461, 0x2461, 0x2461, 0x2461, 0x2461, 0x2461, 0x2461, 0x2461,
    0x0000, 0x0558, 0x0f19, 0x18e7, 0x2323, 0x2d96, 0x3861, 0x43c1,
    0x4e08, 0x4e08, 0x4e08, 0x4e08, 0x4e08, 0x4e08, 0x4e08, 0x4e08
};

/* The noise generator is an 8 bit shift register clocked with voice 1's
   state; its new bit is the XNOR of bits 7, 5, 4 and 1 and is the noise
   output (FPGATED `noisegen').  Register 17 bit 7 clears it.  */
static inline void clock_shift_register(void)
{
    snd.noise_shift_register = (uint8_t)((snd.noise_shift_register << 1) |
                                         (~((snd.noise_shift_register >> 7) ^
                                            (snd.noise_shift_register >> 5) ^
                                            (snd.noise_shift_register >> 4) ^
                                            (snd.noise_shift_register >> 1)) & 1));
    snd.noise_output = (snd.noise_shift_register & 1) ? CTRL_VOICE1_ENABLE : 0;
}

static inline void reset_shift_register(void)
{
    snd.noise_shift_register = 0;
    snd.noise_output = 0;
}

/* A voice counter counts up to $3ff and then loads its frequency + 1; the
   voice state changes when the counter reaches $3ff (FPGATED `ch1count',
   `ch1stateclk').  With frequency $3fe it loads $3ff: the counter stays at
   $3ff and the state holds.  Return non-zero if the state changes.  */
static inline int ted_voice_tick(uint32_t *count, uint32_t reload)
{
    if (*count == 0x3ff) {
        *count = reload;
        return 0;
    }
    return ++(*count) == 0x3ff;
}

/* Return the counter ticks until the state of a voice changes.  */
static inline uint32_t ted_voice_ticks(uint32_t count, uint32_t reload)
{
    if (count != 0x3ff) {
        return 0x3ff - count;
    }
    return reload == 0x3ff ? UINT32_MAX : 0x400 - reload;
}

/* Return the counter ticks until a voice changes its state or decays.  */
static inline uint32_t ted_voice_event_ticks(uint32_t count, uint32_t reload,
                                             uint32_t hold)
{
    uint32_t ticks = ted_voice_ticks(count, reload);
    uint32_t decay = TED_SOUND_DECAY_TICKS - hold;

    return decay < ticks ? decay : ticks;
}

/* Advance a voice counter by `ticks', fewer than `ted_voice_ticks()'.  */
static inline void ted_voice_advance(uint32_t *count, uint32_t reload,
                                     uint32_t ticks)
{
    if (ticks == 0) {
        return;
    }
    if (*count == 0x3ff) {
        *count = reload;
        ticks--;
        if (reload == 0x3ff) {
            return;
        }
    }
    *count += ticks;
}

/* The sound counters run at one quarter of the single clock.  FPGATED
   clocks voice 1 two single clocks after voice 0 (`ch1clk', `ch2clk'):
   half a counter tick.  `snd.tick_remaining' is the time to the next tick
   of voice 0; return the time to the next tick of voice 1.  */
static inline uint32_t ted_voice1_remaining(void)
{
    uint32_t half = snd.tick_length / 2;

    return snd.tick_remaining > half ? snd.tick_remaining - half
                                     : snd.tick_remaining + half;
}

/* Return the number of ticks within `length' time units of a voice whose
   next tick is `remaining' units away.  */
static inline uint32_t ted_voice_ticks_within(uint32_t remaining,
                                              uint64_t length)
{
    if (length < remaining) {
        return 0;
    }
    return (uint32_t)(1 + (length - remaining) / snd.tick_length);
}

/* See TLC, "TED Sound Generation Internals", Plus/4 World article 500244,
   and FPGATED's sound generator.  Register 17 bit 7 loads the counters with
   the frequencies (7360R0 data sheet, page 15; FPGATED `damode').  */
static void ted_voice0_clock(void)
{
    if (snd.digital) {
        snd.voice0_accu = snd.voice0_reload;
        snd.voice0_hold = 0;
        return;
    }
    if (ted_voice_tick(&snd.voice0_accu, snd.voice0_reload)) {
        snd.voice0_sign ^= CTRL_VOICE0_ENABLE;
        snd.voice0_hold = 0;
    } else if (++snd.voice0_hold == TED_SOUND_DECAY_TICKS) {
        snd.voice0_sign = CTRL_VOICE0_ENABLE;
        snd.voice0_hold = 0;
    }
    snd.voice0_cached_output = snd.volume |
                              (snd.voice0_sign & snd.voice0_output_enabled);
}

static void ted_voice1_clock(void)
{
    if (snd.digital) {
        snd.voice1_accu = snd.voice1_reload;
        snd.voice1_hold = 0;
        return;
    }
    if (ted_voice_tick(&snd.voice1_accu, snd.voice1_reload)) {
        snd.voice1_sign ^= CTRL_VOICE1_ENABLE;
        snd.voice1_hold = 0;
        clock_shift_register();
    } else if (++snd.voice1_hold == TED_SOUND_DECAY_TICKS) {
        snd.voice1_sign = CTRL_VOICE1_ENABLE;
        snd.voice1_hold = 0;
    }
    snd.voice1_cached_output = snd.volume |
                              (snd.voice1_sign & snd.voice1_output_enabled) |
                              (snd.noise_output & (snd.noise >> 1));
}

/* Advance a voice by `ticks', fewer than `ted_voice_event_ticks()'.  In
   digital mode the counters stay at the reload value.  */
static inline void ted_voice0_advance(uint32_t ticks)
{
    if (snd.digital) {
        if (ticks) {
            ted_voice0_clock();
        }
        return;
    }
    snd.voice0_hold += ticks;
    ted_voice_advance(&snd.voice0_accu, snd.voice0_reload, ticks);
}

static inline void ted_voice1_advance(uint32_t ticks)
{
    if (snd.digital) {
        if (ticks) {
            ted_voice1_clock();
        }
        return;
    }
    snd.voice1_hold += ticks;
    ted_voice_advance(&snd.voice1_accu, snd.voice1_reload, ticks);
}

/* Increments of the band-limited unit step for a step at position
   p / TED_SOUND_PHASES of a sample, over the following TED_SOUND_TAPS
   samples.  Each row adds up to one.  */
static double step_table[TED_SOUND_PHASES + 1][TED_SOUND_TAPS];
static int step_table_ready = 0;

static double ted_sound_bessel_i0(double x)
{
    double sum = 1.0, term = 1.0;
    int k;

    for (k = 1; k < 50; k++) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1e-15) {
            break;
        }
    }
    return sum;
}

/* Impulse response of the filter at `u' samples from its centre.  */
static double ted_sound_impulse(double u)
{
    double half = TED_SOUND_TAPS / 2.0;
    double r = u / half;
    double sinc = u == 0.0 ? 1.0 : sin(M_PI * u) / (M_PI * u);

    if (r <= -1.0 || r >= 1.0) {
        return 0.0;
    }
    return sinc * ted_sound_bessel_i0(TED_SOUND_KAISER * sqrt(1.0 - r * r))
           / ted_sound_bessel_i0(TED_SOUND_KAISER);
}

/* In-place radix-2 transform of `n' complex values, `n' a power of two:
   forward with `sign' -1, inverse without scaling with `sign' 1.  */
static void ted_sound_fft(double *re, double *im, unsigned int n, int sign)
{
    unsigned int i, j, k, bit, len;
    double angle, w_re, w_im, t_re, t_im;

    for (i = 1, j = 0; i < n; i++) {
        for (bit = n >> 1; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            t_re = re[i];
            re[i] = re[j];
            re[j] = t_re;
            t_im = im[i];
            im[i] = im[j];
            im[j] = t_im;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        angle = sign * 2.0 * M_PI / len;
        for (k = 0; k < len / 2; k++) {
            w_re = cos(angle * k);
            w_im = sin(angle * k);
            for (i = k; i < n; i += len) {
                j = i + len / 2;
                t_re = re[j] * w_re - im[j] * w_im;
                t_im = re[j] * w_im + im[j] * w_re;
                re[j] = re[i] - t_re;
                im[j] = im[i] - t_im;
                re[i] += t_re;
                im[i] += t_im;
            }
        }
    }
}

/* Replace the impulse response `h' of `points' values by its minimum phase
   version, through the real cepstrum: the logarithm of the magnitude,
   folded onto positive times, then exponentiated.  */
static void ted_sound_minimum_phase(double *h, unsigned int points)
{
    const unsigned int n = TED_SOUND_FFT_SIZE;
    double *re = lib_malloc(n * sizeof(double));
    double *im = lib_malloc(n * sizeof(double));
    double magnitude, peak = 0.0, e;
    unsigned int k;

    for (k = 0; k < n; k++) {
        re[k] = k < points ? h[k] : 0.0;
        im[k] = 0.0;
    }
    ted_sound_fft(re, im, n, -1);
    for (k = 0; k < n; k++) {
        magnitude = sqrt(re[k] * re[k] + im[k] * im[k]);
        re[k] = magnitude;
        if (magnitude > peak) {
            peak = magnitude;
        }
    }
    for (k = 0; k < n; k++) {
        re[k] = log(re[k] > peak * 1e-10 ? re[k] : peak * 1e-10);
        im[k] = 0.0;
    }
    ted_sound_fft(re, im, n, 1);
    for (k = 0; k < n; k++) {
        re[k] /= n;
        if (k > 0 && k < n / 2) {
            re[k] *= 2.0;
        } else if (k > n / 2) {
            re[k] = 0.0;
        }
        im[k] = 0.0;
    }
    ted_sound_fft(re, im, n, -1);
    for (k = 0; k < n; k++) {
        e = exp(re[k]);
        re[k] = e * cos(im[k]);
        im[k] = e * sin(im[k]);
    }
    ted_sound_fft(re, im, n, 1);
    for (k = 0; k < points; k++) {
        h[k] = re[k] / n;
    }
    lib_free(re);
    lib_free(im);
}

/* Sample the impulse response on a grid of TED_SOUND_PHASES points per
   sample, make it minimum phase, integrate it into the step response and
   take its increments.  */
static void ted_sound_build_step_table(void)
{
    static double impulse[TED_SOUND_TAPS * TED_SOUND_PHASES + 1];
    static double response[TED_SOUND_TAPS * TED_SOUND_PHASES + 1];
    const int points = TED_SOUND_TAPS * TED_SOUND_PHASES;
    double half = TED_SOUND_TAPS / 2.0, sum;
    int g, p, j, hi, lo;

    if (step_table_ready) {
        return;
    }
    for (g = 0; g <= points; g++) {
        impulse[g] = ted_sound_impulse((double)g / TED_SOUND_PHASES - half);
    }
    ted_sound_minimum_phase(impulse, points + 1);
    response[0] = 0.0;
    for (g = 1; g <= points; g++) {
        response[g] = response[g - 1] + (impulse[g - 1] + impulse[g]) / 2.0;
    }
    for (g = 1; g <= points; g++) {
        response[g] /= response[points];
    }
    for (p = 0; p <= TED_SOUND_PHASES; p++) {
        sum = 0.0;
        for (j = 0; j < TED_SOUND_TAPS; j++) {
            hi = (j + 1) * TED_SOUND_PHASES - p;
            lo = j * TED_SOUND_PHASES - p;
            step_table[p][j] = response[hi] - (lo < 0 ? 0.0 : response[lo]);
            sum += step_table[p][j];
        }
        /* The response is cut after the taps: keep each step exactly one.  */
        step_table[p][TED_SOUND_TAPS - 1] += 1.0 - sum;
    }
    step_table_ready = 1;
}

/* Add a step of `delta' at the current time to the output.  */
static void ted_sound_step(double delta)
{
    unsigned int slot = snd.step_head;
    double x = (double)snd.partial_length * TED_SOUND_PHASES / snd.sample_length;
    const double *a, *b;
    double t;
    int p, j;

    if (snd.partial_length >= snd.sample_length) {
        /* At the end of the current sample: the start of the next one.  */
        slot = slot + 1 == TED_SOUND_RING ? 0 : slot + 1;
        x = 0.0;
    }
    p = (int)x;
    if (p >= TED_SOUND_PHASES) {
        p = TED_SOUND_PHASES - 1;
    }
    t = x - p;
    a = step_table[p];
    b = step_table[p + 1];
    for (j = 0; j < TED_SOUND_TAPS; j++) {
        slot = slot + 1 == TED_SOUND_RING ? 0 : slot + 1;
        snd.steps[slot] += delta * (a[j] + t * (b[j] - a[j]));
    }
}

/* Return the output level of the voices or of the digital mode.  */
static inline int32_t ted_sound_level(void)
{
    if (snd.digital) {
        return snd.digital_cached_output;
    }
    return volumeTable[snd.voice0_cached_output | snd.voice1_cached_output];
}

/* Put a change of the output level at the current time.  */
static void ted_sound_level_changed(void)
{
    int32_t level = ted_sound_level();

    if (level != snd.level) {
        ted_sound_step((double)(level - snd.level));
        snd.level = level;
    }
}

/* Start the output stage at the current level, without pending steps.  */
static void ted_sound_output_reset(void)
{
    ted_sound_build_step_table();
    memset(snd.steps, 0, sizeof(snd.steps));
    snd.step_head = 0;
    snd.level = ted_sound_level();
    snd.output = snd.level;
    snd.dc_input = snd.level;
}

/* Complete the current output sample.  */
static int16_t ted_sound_emit(void)
{
    double value;

    snd.step_head = snd.step_head + 1 == TED_SOUND_RING ? 0 : snd.step_head + 1;
    snd.output += snd.steps[snd.step_head];
    snd.steps[snd.step_head] = 0.0;
    snd.dc_output = snd.dc_factor * (snd.dc_output + snd.output - snd.dc_input);
    snd.dc_input = snd.output;
    snd.partial_length = 0;
    value = floor(snd.dc_output + 0.5);
    if (value > 32767.0) {
        return 32767;
    }
    if (value < -32768.0) {
        return -32768;
    }
    return (int16_t)value;
}

/* Return the time to the `ticks'th tick of a voice, `ticks' at least one,
   or UINT64_MAX.  */
static inline uint64_t ted_voice_event_time(uint32_t remaining, uint32_t ticks)
{
    if (ticks == UINT32_MAX) {
        return UINT64_MAX;
    }
    return remaining + (uint64_t)(ticks - 1) * snd.tick_length;
}

/* Move the tick phase `length' time units on.  */
static inline void ted_sound_pass_time(uint64_t length)
{
    if (length < snd.tick_remaining) {
        snd.tick_remaining -= (uint32_t)length;
    } else {
        snd.tick_remaining = snd.tick_length
                             - (uint32_t)((length - snd.tick_remaining) % snd.tick_length);
    }
}

/* Run the voices for `length' time units, not beyond the end of the
   current sample, putting every change of the output level at its tick.
   Between the changes, count the ticks together.  The ticks of the two
   voices are half a tick apart and never coincide.  */
static void ted_sound_run(uint32_t length)
{
    uint64_t time0, time1, until;
    uint32_t remaining1, ticks0, ticks1;

    while (length) {
        remaining1 = ted_voice1_remaining();
        if (snd.digital) {
            time0 = time1 = UINT64_MAX;
        } else {
            time0 = ted_voice_event_time(snd.tick_remaining,
                                         ted_voice_event_ticks(snd.voice0_accu,
                                                               snd.voice0_reload,
                                                               snd.voice0_hold));
            time1 = ted_voice_event_time(remaining1,
                                         ted_voice_event_ticks(snd.voice1_accu,
                                                               snd.voice1_reload,
                                                               snd.voice1_hold));
        }
        until = time0 < time1 ? time0 : time1;
        if (until > length) {
            ted_voice0_advance(ted_voice_ticks_within(snd.tick_remaining, length));
            ted_voice1_advance(ted_voice_ticks_within(remaining1, length));
            ted_sound_pass_time(length);
            snd.partial_length += length;
            return;
        }
        ticks0 = ted_voice_ticks_within(snd.tick_remaining, until);
        ticks1 = ted_voice_ticks_within(remaining1, until);
        if (until == time0) {
            ted_voice0_advance(ticks0 - 1);
            ted_voice0_clock();
            ted_voice1_advance(ticks1);
        } else {
            ted_voice0_advance(ticks0);
            ted_voice1_advance(ticks1 - 1);
            ted_voice1_clock();
        }
        ted_sound_pass_time(until);
        snd.partial_length += (uint32_t)until;
        length -= (uint32_t)until;
        ted_sound_level_changed();
    }
}

static int16_t ted_sound_sample(void)
{
    ted_sound_run(snd.sample_length - snd.partial_length);
    return ted_sound_emit();
}

/* With no SID cartridge, TED owns the cycle-to-sample conversion.  Keep the
   unfinished sample across calls so register writes change the output at
   the actual CPU cycle, rather than at the next sample boundary. */
#ifdef SOUND_SYSTEM_FLOAT
int ted_sound_calculate_samples(sound_t **psid, float *pbuf, int nr, int scc, CLOCK *delta_t)
#else
int ted_sound_calculate_samples(sound_t **psid, int16_t *pbuf, int nr, int soc, int scc, CLOCK *delta_t)
#endif
{
    uint64_t available = (uint64_t)*delta_t * snd.sample_rate + snd.cycle_pending;
    uint32_t length;
    int count = 0;
    int16_t sample;

#ifdef SOUND_SYSTEM_FLOAT
    primary_buffer = pbuf;
#endif
    while (available && count < nr) {
        length = snd.sample_length - snd.partial_length;
        if (available < length) {
            length = (uint32_t)available;
        }
        ted_sound_run(length);
        available -= length;
        if (snd.partial_length == snd.sample_length) {
            sample = ted_sound_emit();
#ifdef SOUND_SYSTEM_FLOAT
            pbuf[count] = sample / 32767.0f;
#else
            pbuf[count * soc] = sample;
            if (soc == SOUND_OUTPUT_STEREO) {
                pbuf[count * soc + 1] = sample;
            }
#endif
            count++;
        }
    }
    *delta_t = (CLOCK)(available / snd.sample_rate);
    snd.cycle_pending = (uint32_t)(available % snd.sample_rate);
    return count;
}

#ifdef SOUND_SYSTEM_FLOAT
static int ted_sound_machine_calculate_samples(sound_t **psid, float *pbuf, int nr, int scc, CLOCK *delta_t)
{
    int i;

    if (delta_t && !sidcart_enabled()) {
        memcpy(pbuf, primary_buffer, nr * sizeof(*pbuf));
        return nr;
    }
    for (i = 0; i < nr; i++) {
        pbuf[i] = ted_sound_sample() / 32767.0f;
    }
    return nr;
}
#else
static int ted_sound_machine_calculate_samples(sound_t **psid, int16_t *pbuf, int nr, int soc, int scc, CLOCK *delta_t)
{
    int i;
    int16_t volume;

    if (delta_t && !sidcart_enabled()) {
        return nr;
    }
    for (i = 0; i < nr; i++) {
        volume = ted_sound_sample();
        pbuf[i * soc] = sound_audio_mix(pbuf[i * soc], volume);
        if (soc == SOUND_OUTPUT_STEREO) {
            pbuf[i * soc + 1] = sound_audio_mix(pbuf[i * soc + 1], volume);
        }
    }
    return nr;
}
#endif

static int ted_sound_machine_init(sound_t *psid, int speed, int cycles_per_sec)
{
    uint16_t addr;
    struct plus4_sound_s saved = snd;
    /* Reopening the sound output or a new clock rate ($FF07 bit 6) does not
       affect the chip: keep the state of a running chip.  */
    int restore = saved.sample_rate != 0;

    DBG(("ted_sound_machine_init speed: %d cycles_per_sec: %d\n", speed, cycles_per_sec));
    ted_sound_build_step_table();
    memset(&snd, 0, sizeof(snd));
    snd.sample_length = cycles_per_sec;
    snd.sample_rate = speed;
    snd.tick_length = 8 * speed;
    snd.tick_remaining = snd.tick_length;
    snd.dc_factor = exp(-1.0 / (TED_SOUND_DC_TIME * speed));
    reset_shift_register();

    /* Restore frequencies and cached output in the same units as stores.
       In particular, reopening audio must retain digital volume. */
    for (addr = 0x0e; addr <= 0x12; addr++) {
        if (addr != 0x11) {
            ted_sound_machine_store(psid, addr, plus4_sound_data[addr - 0x0e]);
        }
    }
    ted_sound_machine_store(psid, 0x11, plus4_sound_data[3]);
    ted_sound_output_reset();
    if (restore) {
        if (saved.sample_rate != snd.sample_rate || saved.sample_length != snd.sample_length) {
            saved.tick_remaining = (uint32_t)(((uint64_t)saved.tick_remaining * snd.sample_rate
                                               + saved.sample_rate - 1) / saved.sample_rate);
            if (saved.sample_rate != snd.sample_rate) {
                /* The pending steps are in samples of the old rate.  */
                memset(saved.steps, 0, sizeof(saved.steps));
                saved.output = saved.level;
                saved.dc_input = saved.level;
            }
            saved.sample_rate = snd.sample_rate;
            saved.sample_length = snd.sample_length;
            saved.tick_length = snd.tick_length;
            saved.dc_factor = snd.dc_factor;
            saved.partial_length = 0;
            saved.cycle_pending = 0;
        }
        snd = saved;
    }
    return 1;
}

static void ted_sound_machine_store(sound_t *psid, uint16_t addr, uint8_t val)
{
    unsigned int freq;
    switch (addr) {
        case 0x0e: /* voice0 freq lo */
            plus4_sound_data[0] = val;
            freq = plus4_sound_data[0] | (plus4_sound_data[4] << 8);
            snd.voice0_reload = (freq + 1) & 0x3ff;
            break;
        case 0x0f: /* voice1 freq lo */
            plus4_sound_data[1] = val;
            freq = plus4_sound_data[1] | (plus4_sound_data[2] << 8);
            snd.voice1_reload = (freq + 1) & 0x3ff;
            break;
        case 0x10: /* voice1 freq hi */
            plus4_sound_data[2] = val & 3;
            freq = plus4_sound_data[1] | (plus4_sound_data[2] << 8);
            snd.voice1_reload = (freq + 1) & 0x3ff;
            break;
        case 0x11:
            /* bit 0-3  volume
                   4    voice 0 enable
                   5    voice 1 enable
                   6    noise enable
                   7    digital mode enabled
             */
            snd.volume = val & 0x0f;
            snd.voice0_output_enabled = (val & CTRL_VOICE0_ENABLE);
            snd.voice1_output_enabled = (val & CTRL_VOICE1_ENABLE);
            snd.noise = ((val & 0x60) == CTRL_NOISE_ENABLE) ? CTRL_NOISE_ENABLE : 0;
            snd.digital = val & CTRL_DIGITAL_ENABLE;
            if (snd.digital) {
                snd.voice0_sign = CTRL_VOICE0_ENABLE;
                snd.voice1_sign = CTRL_VOICE1_ENABLE;
                reset_shift_register();
                snd.digital_cached_output = volumeTable[val & 0x3f];
            }
            snd.voice0_cached_output = snd.volume |
                                       (snd.voice0_sign & snd.voice0_output_enabled);
            snd.voice1_cached_output = snd.volume |
                                       (snd.voice1_sign & snd.voice1_output_enabled) |
                                       (snd.noise_output & (snd.noise >> 1));
            plus4_sound_data[3] = val;
            break;
        case 0x12: /* voice0 freq hi */
            plus4_sound_data[4] = val & 3;
            freq = plus4_sound_data[0] | (plus4_sound_data[4] << 8);
            snd.voice0_reload = (freq + 1) & 0x3ff;
            break;
    }
    if (snd.sample_rate) {
        ted_sound_level_changed();
    }
#if 0
    DBG(("freq0:%04x freq1:%04x ctrl:%02x\n",
            plus4_sound_data[0] | (plus4_sound_data[4] << 8),
            plus4_sound_data[1] | (plus4_sound_data[2] << 8),
            plus4_sound_data[3]));
#endif
}

static uint8_t ted_sound_machine_read(sound_t *psid, uint16_t addr)
{
    switch (addr) {
        case 0x0e:
            return plus4_sound_data[0];
        case 0x0f:
            return plus4_sound_data[1];
        case 0x10:
            return (plus4_sound_data[2] & 0x7f) | 0x7c;
        case 0x11:
            return plus4_sound_data[3];
        case 0x12:
            return plus4_sound_data[4];
    }

    return 0;
}

void ted_sound_reset(sound_t *psid, CLOCK cpu_clk)
{
    uint16_t i;

    /* Cleared states, as after register 17 bit 7: the outputs are high.  */
    snd.voice0_sign = CTRL_VOICE0_ENABLE;
    snd.voice1_sign = CTRL_VOICE1_ENABLE;
    snd.voice0_accu = 0;
    snd.voice1_accu = 0;
    reset_shift_register();
    snd.digital = 0;
    snd.cycle_pending = 0;
    snd.tick_remaining = snd.tick_length;
    snd.voice0_cached_output = 0;
    snd.voice1_cached_output = 0;
    snd.digital_cached_output = 0;

    /* FIXME: this is is almost certainly not correct */
    for (i = 0x0e; i <= 0x12; i++) {
        ted_sound_store(i, 0);
    }
}

/* TED module 1.8 appends sound registers, oscillator state and the unfinished
   output sample.  Do not write native structs: padding and host byte order
   are not part of the snapshot format. */
int ted_sound_snapshot_write(snapshot_module_t *m)
{
    return SMW_BA(m, plus4_sound_data, 5) < 0
        || SMW_DW(m, snd.voice0_accu) < 0
        || SMW_DW(m, snd.voice1_accu) < 0
        || SMW_B(m, (uint8_t)snd.voice0_sign) < 0
        || SMW_B(m, (uint8_t)snd.voice1_sign) < 0
        || SMW_B(m, snd.noise_shift_register) < 0
        || SMW_B(m, snd.noise_output) < 0
        || SMW_DW(m, snd.sample_rate) < 0
        || SMW_DW(m, snd.sample_length) < 0
        || SMW_DW(m, snd.tick_remaining) < 0
        || SMW_DW(m, snd.partial_length) < 0
        || SMW_QW(m, 0) < 0 /* Integral of the unfinished sample until 1.12 */
        || SMW_DW(m, snd.cycle_pending) < 0 ? -1 : 0;
}

void ted_sound_snapshot_legacy(const uint8_t *regs)
{
    uint32_t sample_rate = snd.sample_rate;
    uint32_t sample_length = snd.sample_length;

    memcpy(plus4_sound_data, regs, 5);
    plus4_sound_data[2] &= 3;
    plus4_sound_data[4] &= 3;
    /* Older modules contain no oscillator state.  */
    memset(&snd, 0, sizeof(snd));
    if (sample_rate) {
        ted_sound_machine_init(NULL, sample_rate, sample_length);
    }
}

/* TED module 1.13 appends the ticks since the last state change of each
   voice and the output stage.  Older modules restart them; the pending
   steps apply only at the sample rate they were saved at.  */
int ted_sound_snapshot_write_state(snapshot_module_t *m)
{
    unsigned int i;

    if (SMW_DW(m, snd.voice0_hold) < 0
        || SMW_DW(m, snd.voice1_hold) < 0
        || SMW_DW(m, snd.sample_rate) < 0
        || SMW_DW(m, (uint32_t)snd.level) < 0
        || SMW_B(m, (uint8_t)snd.step_head) < 0
        || SMW_DB(m, snd.output) < 0
        || SMW_DB(m, snd.dc_input) < 0
        || SMW_DB(m, snd.dc_output) < 0) {
        return -1;
    }
    for (i = 0; i < TED_SOUND_RING; i++) {
        if (SMW_DB(m, snd.steps[i]) < 0) {
            return -1;
        }
    }
    return 0;
}

int ted_sound_snapshot_read_state(snapshot_module_t *m)
{
    uint32_t hold0, hold1, rate, level;
    uint8_t head;
    double output, dc_input, dc_output, steps[TED_SOUND_RING];
    unsigned int i;

    if (SMR_DW(m, &hold0) < 0
        || SMR_DW(m, &hold1) < 0
        || SMR_DW(m, &rate) < 0
        || SMR_DW(m, &level) < 0
        || SMR_B(m, &head) < 0
        || SMR_DB(m, &output) < 0
        || SMR_DB(m, &dc_input) < 0
        || SMR_DB(m, &dc_output) < 0) {
        return -1;
    }
    for (i = 0; i < TED_SOUND_RING; i++) {
        if (SMR_DB(m, &steps[i]) < 0) {
            return -1;
        }
        if (!(fabs(steps[i]) < 1e9)) {
            snapshot_set_error(SNAPSHOT_MODULE_INCOMPATIBLE);
            return -1;
        }
    }
    if (hold0 >= TED_SOUND_DECAY_TICKS || hold1 >= TED_SOUND_DECAY_TICKS
        || head >= TED_SOUND_RING || level > 0x7fff
        || !(fabs(output) < 1e9) || !(fabs(dc_input) < 1e9)
        || !(fabs(dc_output) < 1e9)) {
        snapshot_set_error(SNAPSHOT_MODULE_INCOMPATIBLE);
        return -1;
    }
    snd.voice0_hold = hold0;
    snd.voice1_hold = hold1;
    snd.dc_output = dc_output;
    if (rate == snd.sample_rate && (int32_t)level == ted_sound_level()) {
        snd.level = (int32_t)level;
        snd.step_head = head;
        snd.output = output;
        snd.dc_input = dc_input;
        memcpy(snd.steps, steps, sizeof(snd.steps));
    }
    return 0;
}

int ted_sound_snapshot_read(snapshot_module_t *m)
{
    struct plus4_sound_s saved;
    uint8_t regs[5], sign0, sign1;
    uint64_t partial_sum;
    uint32_t current_rate = snd.sample_rate;
    uint32_t current_clock = snd.sample_length;

    memset(&saved, 0, sizeof(saved));
    if (SMR_BA(m, regs, 5) < 0
        || SMR_DW(m, &saved.voice0_accu) < 0
        || SMR_DW(m, &saved.voice1_accu) < 0
        || SMR_B(m, &sign0) < 0
        || SMR_B(m, &sign1) < 0
        || SMR_B(m, &saved.noise_shift_register) < 0
        || SMR_B(m, &saved.noise_output) < 0
        || SMR_DW(m, &saved.sample_rate) < 0
        || SMR_DW(m, &saved.sample_length) < 0
        || SMR_DW(m, &saved.tick_remaining) < 0
        || SMR_DW(m, &saved.partial_length) < 0
        || SMR_QW(m, &partial_sum) < 0
        || SMR_DW(m, &saved.cycle_pending) < 0) {
        return -1;
    }
    if (saved.voice0_accu >= OSCRELOADVAL || saved.voice1_accu >= OSCRELOADVAL
        || (sign0 != 0 && sign0 != CTRL_VOICE0_ENABLE)
        || (sign1 != 0 && sign1 != CTRL_VOICE1_ENABLE)
        || (saved.noise_output != 0 && saved.noise_output != CTRL_VOICE1_ENABLE)
        || regs[2] > 3 || regs[4] > 3
        || (saved.sample_rate && (saved.sample_rate > UINT32_MAX / 16
            || !saved.sample_length || saved.sample_length > 4000000
            || !saved.tick_remaining || saved.tick_remaining > 8 * saved.sample_rate
            || saved.partial_length >= saved.sample_length
            || partial_sum > (uint64_t)19976 * saved.partial_length
            || saved.cycle_pending >= saved.sample_rate))) {
        snapshot_set_error(SNAPSHOT_MODULE_INCOMPATIBLE);
        return -1;
    }
    ted_sound_snapshot_legacy(regs);
    if (saved.sample_rate) {
        /* Rebuild values derived from registers, then restore running state. */
        ted_sound_machine_init(NULL, saved.sample_rate, saved.sample_length);
        snd.voice0_accu = saved.voice0_accu;
        snd.voice1_accu = saved.voice1_accu;
        snd.voice0_sign = sign0;
        snd.voice1_sign = sign1;
        snd.noise_shift_register = saved.noise_shift_register;
        snd.noise_output = saved.noise_output;
        snd.tick_remaining = saved.tick_remaining;
        snd.partial_length = saved.partial_length;
        snd.cycle_pending = saved.cycle_pending;
        snd.voice0_cached_output = snd.volume | (sign0 & snd.voice0_output_enabled);
        snd.voice1_cached_output = snd.volume | (sign1 & snd.voice1_output_enabled)
                                  | (snd.noise_output & (snd.noise >> 1));
        /* TED module 1.13 also restores the output stage.  */
        ted_sound_output_reset();
        if (current_rate && (current_rate != snd.sample_rate || current_clock != snd.sample_length)) {
            ted_sound_machine_init(NULL, current_rate, current_clock);
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------*/

void ted_sound_store(uint16_t addr, uint8_t value)
{
    /* The CPU core combines the two writes of an RMW instruction.  Restore
       the write of the unmodified value on the preceding bus cycle. */
    if (maincpu_rmw_flag) {
        maincpu_clk--;
        sound_store((uint16_t)(ted_sound_chip_offset | addr), last_sound_read, 0);
        maincpu_clk++;
    }
    sound_store((uint16_t)(ted_sound_chip_offset | addr), value, 0);
}

uint8_t ted_sound_read(uint16_t addr)
{
    uint8_t value;

    value = sound_read((uint16_t)(ted_sound_chip_offset | addr), 0);

    if (addr == 0x12) {
        value &= 3;
    }

    last_sound_read = value;
    return value;
}

char *sound_machine_dump_state(sound_t *psid)
{
    return sid_sound_machine_dump_state(psid);
}

void sound_machine_enable(int enable)
{
    sid_sound_machine_enable(enable);
}
