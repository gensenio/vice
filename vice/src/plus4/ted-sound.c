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
    uint32_t sample_ticks;
    uint32_t sample_remainder;
    uint32_t sample_rate;
    uint32_t partial_length;
    uint64_t partial_sum;
    uint32_t cycle_pending;
    /* Volume table index. */
    int16_t volume;
    /* Digital output?  */
    uint8_t digital;
    /* Noise generator active?  */
    uint8_t noise;
    uint8_t noise_shift_register;
    uint8_t noise_output;
};

static struct plus4_sound_s snd;
static int snapshot_loaded;
#ifdef SOUND_SYSTEM_FLOAT
static float *primary_buffer;
#endif

#define CTRL_VOICE0_ENABLE  0x10
#define CTRL_VOICE1_ENABLE  0x20
#define CTRL_NOISE_ENABLE   0x40
#define CTRL_DIGITAL_ENABLE 0x80

#define OSCRELOADVAL 0x400

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

static inline void clock_shift_register(void)
{
    snd.noise_shift_register = (snd.noise_shift_register << 1) |
                               (((snd.noise_shift_register >> 7) ^
                                 (snd.noise_shift_register >> 5) ^
                                 (snd.noise_shift_register >> 4) ^
                                 (snd.noise_shift_register >> 1)) & 1);
}

static inline void reset_shift_register(void)
{
    snd.noise_shift_register = 0xff;
    snd.noise_output = CTRL_VOICE1_ENABLE;
}

/* The sound counters run at one quarter of the single clock.  Account for
   every counter tick, including multiple transitions within an output sample.
   See TLC, "TED Sound Generation Internals", Plus/4 World article 500244.
   The existing noise sequence/clock is retained; the available descriptions
   do not settle its exact phase or rate. */
static void ted_sound_clock(void)
{
    /* Register 17 reloads the active counters at the voice increment time
       while its test bit is set (7360R0 data sheet, page 15). */
    if (snd.digital) {
        snd.voice0_accu = snd.voice0_reload;
        snd.voice1_accu = snd.voice1_reload;
        return;
    }
    if (snd.voice0_reload != 0x3ff) {
        if (++snd.voice0_accu >= OSCRELOADVAL) {
            snd.voice0_sign ^= CTRL_VOICE0_ENABLE;
            snd.voice0_accu = snd.voice0_reload;
            snd.voice0_cached_output = snd.volume |
                                      (snd.voice0_sign & snd.voice0_output_enabled);
        }
    }
    if (snd.voice1_reload != 0x3ff) {
        if (++snd.voice1_accu >= OSCRELOADVAL) {
            snd.voice1_sign ^= CTRL_VOICE1_ENABLE;
            snd.voice1_accu = snd.voice1_reload;
            snd.noise_output = (snd.noise_shift_register & 1) ? CTRL_VOICE1_ENABLE : 0;
            snd.voice1_cached_output = snd.volume |
                                      (snd.voice1_sign & snd.voice1_output_enabled) |
                                      (snd.noise_output & (snd.noise >> 1));
            clock_shift_register();
        }
    }
}

/* Integrate the piecewise constant output over the sample interval.  This
   reduces aliasing without a look-ahead buffer or a rounded oscillator step.
   Both sound backends use the same generator and amplitude scale. */
static uint64_t ted_sound_integrate(uint32_t remaining)
{
    uint32_t length = remaining;
    uint32_t step;
    uint32_t phase, ticks, next;
    uint64_t sum = 0;

    /* The sample length is constant until the audio device is reopened.
       Split it at initialization, avoiding a division for every sample. */
    if (length == snd.sample_length) {
        phase = snd.tick_length - snd.tick_remaining + snd.sample_remainder;
        ticks = snd.sample_ticks;
    } else {
        phase = snd.tick_length - snd.tick_remaining + length % snd.tick_length;
        ticks = length / snd.tick_length;
    }
    if (phase >= snd.tick_length) {
        phase -= snd.tick_length;
        ticks++;
    }
    if (snd.digital) {
        if (ticks) {
            ted_sound_clock();
        }
        snd.tick_remaining = snd.tick_length - phase;
        return (uint64_t)snd.digital_cached_output * length;
    }
    /* Most audible tones have no transition in this sample.  Batch their
       counter ticks and avoid integrating a constant output. */
    if ((snd.voice0_reload == 0x3ff || snd.voice0_accu + ticks < OSCRELOADVAL) &&
        (snd.voice1_reload == 0x3ff || snd.voice1_accu + ticks < OSCRELOADVAL)) {
        if (snd.voice0_reload != 0x3ff) {
            snd.voice0_accu += ticks;
        }
        if (snd.voice1_reload != 0x3ff) {
            snd.voice1_accu += ticks;
        }
        snd.tick_remaining = snd.tick_length - phase;
        return (uint64_t)volumeTable[snd.voice0_cached_output | snd.voice1_cached_output] * length;
    }
    /* Between counter overflows the output is constant.  Integrate directly
       to the next overflow, retaining every noise and tone transition. */
    while (ticks) {
        next = ticks + 1;
        if (snd.voice0_reload != 0x3ff && OSCRELOADVAL - snd.voice0_accu < next) {
            next = OSCRELOADVAL - snd.voice0_accu;
        }
        if (snd.voice1_reload != 0x3ff && OSCRELOADVAL - snd.voice1_accu < next) {
            next = OSCRELOADVAL - snd.voice1_accu;
        }
        if (next > ticks) {
            break;
        }
        step = snd.tick_remaining + (next - 1) * snd.tick_length;
        sum += (uint64_t)volumeTable[snd.voice0_cached_output |
                                     snd.voice1_cached_output] * step;
        remaining -= step;
        ticks -= next;
        if (snd.voice0_reload != 0x3ff) {
            snd.voice0_accu += next - 1;
        }
        if (snd.voice1_reload != 0x3ff) {
            snd.voice1_accu += next - 1;
        }
        ted_sound_clock();
        snd.tick_remaining = snd.tick_length;
    }
    sum += (uint64_t)volumeTable[snd.voice0_cached_output |
                                 snd.voice1_cached_output] * remaining;
    if (snd.voice0_reload != 0x3ff) {
        snd.voice0_accu += ticks;
    }
    if (snd.voice1_reload != 0x3ff) {
        snd.voice1_accu += ticks;
    }
    snd.tick_remaining = snd.tick_length - phase;
    return sum;
}

static int16_t ted_sound_sample(void)
{
    return (int16_t)(ted_sound_integrate(snd.sample_length) / snd.sample_length);
}

/* With no SID cartridge, TED owns the cycle-to-sample conversion.  Keep the
   unfinished sample across calls so register writes divide its integral at
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

    snapshot_loaded = 0;
#ifdef SOUND_SYSTEM_FLOAT
    primary_buffer = pbuf;
#endif
    while (available && count < nr) {
        length = snd.sample_length - snd.partial_length;
        if (available < length) {
            length = (uint32_t)available;
        }
        snd.partial_sum += ted_sound_integrate(length);
        snd.partial_length += length;
        available -= length;
        if (snd.partial_length == snd.sample_length) {
            sample = (int16_t)(snd.partial_sum / snd.sample_length);
#ifdef SOUND_SYSTEM_FLOAT
            pbuf[count] = sample / 32767.0f;
#else
            pbuf[count * soc] = sample;
            if (soc == SOUND_OUTPUT_STEREO) {
                pbuf[count * soc + 1] = sample;
            }
#endif
            count++;
            snd.partial_sum = 0;
            snd.partial_length = 0;
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

    snapshot_loaded = 0;
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

    snapshot_loaded = 0;
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
    int restore = snapshot_loaded;

    DBG(("ted_sound_machine_init speed: %d cycles_per_sec: %d\n", speed, cycles_per_sec));
    memset(&snd, 0, sizeof(snd));
    snd.sample_length = cycles_per_sec;
    snd.sample_rate = speed;
    snd.tick_length = 8 * speed;
    snd.sample_ticks = snd.sample_length / snd.tick_length;
    snd.sample_remainder = snd.sample_length % snd.tick_length;
    snd.tick_remaining = snd.tick_length;
    reset_shift_register();

    /* Restore frequencies and cached output in the same units as stores.
       In particular, reopening audio must retain digital volume. */
    for (addr = 0x0e; addr <= 0x12; addr++) {
        if (addr != 0x11) {
            ted_sound_machine_store(psid, addr, plus4_sound_data[addr - 0x0e]);
        }
    }
    ted_sound_machine_store(psid, 0x11, plus4_sound_data[3]);
    if (restore) {
        if (saved.sample_rate != snd.sample_rate || saved.sample_length != snd.sample_length) {
            saved.tick_remaining = (uint32_t)(((uint64_t)saved.tick_remaining * snd.sample_rate
                                               + saved.sample_rate - 1) / saved.sample_rate);
            saved.sample_rate = snd.sample_rate;
            saved.sample_length = snd.sample_length;
            saved.tick_length = snd.tick_length;
            saved.sample_ticks = snd.sample_ticks;
            saved.sample_remainder = snd.sample_remainder;
            saved.partial_length = 0;
            saved.partial_sum = 0;
            saved.cycle_pending = 0;
        }
        snd = saved;
    }
    snapshot_loaded = 0;
    return 1;
}

static void ted_sound_machine_store(sound_t *psid, uint16_t addr, uint8_t val)
{
    unsigned int freq;
    snapshot_loaded = 0;
    switch (addr) {
        case 0x0e: /* voice0 freq lo */
            plus4_sound_data[0] = val;
            freq = plus4_sound_data[0] | (plus4_sound_data[4] << 8);
            if (freq == 0x3fe) {
                snd.voice0_sign = CTRL_VOICE0_ENABLE;
                snd.voice0_cached_output = snd.volume | snd.voice0_output_enabled;
            }
            snd.voice0_reload = (freq + 1) & 0x3ff;
            break;
        case 0x0f: /* voice1 freq lo */
            plus4_sound_data[1] = val;
            freq = plus4_sound_data[1] | (plus4_sound_data[2] << 8);
            if (freq == 0x3fe) {
                snd.voice1_sign = CTRL_VOICE1_ENABLE;
                snd.voice1_cached_output = snd.volume | snd.voice1_output_enabled |
                                           (snd.noise_output & (snd.noise >> 1));
            }
            snd.voice1_reload = (freq + 1) & 0x3ff;
            break;
        case 0x10: /* voice1 freq hi */
            plus4_sound_data[2] = val & 3;
            freq = plus4_sound_data[1] | (plus4_sound_data[2] << 8);
            if (freq == 0x3fe) {
                snd.voice1_sign = CTRL_VOICE1_ENABLE;
                snd.voice1_cached_output = snd.volume | snd.voice1_output_enabled |
                                           (snd.noise_output & (snd.noise >> 1));
            }
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
            if (freq == 0x3fe) {
                snd.voice0_sign = CTRL_VOICE0_ENABLE;
                snd.voice0_cached_output = snd.volume | snd.voice0_output_enabled;
            }
            snd.voice0_reload = (freq + 1) & 0x3ff;
            break;
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

    snd.voice0_sign = 0;
    snd.voice1_sign = 0;
    snd.voice0_accu = 0;
    snd.voice1_accu = 0;
    reset_shift_register();
    snd.digital = 0;
    snd.partial_length = 0;
    snd.partial_sum = 0;
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
        || SMW_QW(m, snd.partial_sum) < 0
        || SMW_DW(m, snd.cycle_pending) < 0 ? -1 : 0;
}

void ted_sound_snapshot_legacy(const uint8_t *regs)
{
    memcpy(plus4_sound_data, regs, 5);
    plus4_sound_data[2] &= 3;
    plus4_sound_data[4] &= 3;
    snapshot_loaded = 0;
    if (snd.sample_rate) {
        ted_sound_machine_init(NULL, snd.sample_rate, snd.sample_length);
    }
}

int ted_sound_snapshot_read(snapshot_module_t *m)
{
    struct plus4_sound_s saved;
    uint8_t regs[5], sign0, sign1;
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
        || SMR_QW(m, &saved.partial_sum) < 0
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
            || saved.partial_sum > (uint64_t)19976 * saved.partial_length
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
        snd.partial_sum = saved.partial_sum;
        snd.cycle_pending = saved.cycle_pending;
        snd.voice0_cached_output = snd.volume | (sign0 & snd.voice0_output_enabled);
        snd.voice1_cached_output = snd.volume | (sign1 & snd.voice1_output_enabled)
                                  | (snd.noise_output & (snd.noise >> 1));
        snapshot_loaded = 1;
        if (current_rate && (current_rate != snd.sample_rate || current_clock != snd.sample_length)) {
            ted_sound_machine_init(NULL, current_rate, current_clock);
            snapshot_loaded = 1;
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
