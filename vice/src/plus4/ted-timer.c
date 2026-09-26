/*
 * ted-timer.c - Timer implementation for the TED emulation.
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
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

#include "alarm.h"
#include "log.h"
#include "maincpu.h"
#include "snapshot.h"
#include "ted-irq.h"
#include "ted-timer.h"
#include "tedtypes.h"
#include "ted.h"
#include "types.h"


/*#define DEBUG_TIMER*/


static alarm_t *ted_t1_alarm = NULL;
static alarm_t *ted_t2_alarm = NULL;
static alarm_t *ted_t3_alarm = NULL;

static CLOCK t1_last_restart;
static CLOCK t2_last_restart;
static CLOCK t3_last_restart;

static CLOCK t1_value;
static CLOCK t2_value;
static CLOCK t3_value;

/*-----------------------------------------------------------------------*/

static void ted_t1_alarm_handler(CLOCK offset, void *data)
{
    alarm_set(ted_t1_alarm, maincpu_clk
              + (ted.t1_start == 0 ? 65536 : ted.t1_start) * 2 - offset);
    /* Keep the value at the restart clock; reads account for offset once. */
    t1_value = (ted.t1_start == 0 ? 65536 : ted.t1_start) * 2;
#ifdef DEBUG_TIMER
    log_debug(LOG_DEFAULT, "TI1 ALARM %x", maincpu_clk);
#endif
    ted_irq_timer1_set(maincpu_clk - offset);
    t1_last_restart = maincpu_clk - offset;
}

static void ted_t2_alarm_handler(CLOCK offset, void *data)
{
    alarm_set(ted_t2_alarm, maincpu_clk + 65536 * 2 - offset);
    t2_value = 65536 * 2;
#ifdef DEBUG_TIMER
    log_debug(LOG_DEFAULT, "TI2 ALARM %x", maincpu_clk);
#endif
    ted_irq_timer2_set(maincpu_clk - offset);
    t2_last_restart = maincpu_clk - offset;
}

static void ted_t3_alarm_handler(CLOCK offset, void *data)
{
    alarm_set(ted_t3_alarm, maincpu_clk + 65536 * 2 - offset);
    t3_value = 65536 * 2;
#ifdef DEBUG_TIMER
    log_debug(LOG_DEFAULT, "TI3 ALARM %x", maincpu_clk);
#endif
    ted_irq_timer3_set(maincpu_clk - offset);
    t3_last_restart = maincpu_clk - offset;
}

/*-----------------------------------------------------------------------*/

static void ted_timer_t1_store_low(uint8_t value)
{
    alarm_unset(ted_t1_alarm);
    if (ted.timer_running[0]) {
        t1_value -= maincpu_clk - t1_last_restart;
        t1_last_restart = maincpu_clk;
    }
    t1_value = (ted.t1_start = (ted.t1_start & 0xff00) | value) << 1;
    ted.timer_running[0] = 0;
}

static void ted_timer_t1_store_high(uint8_t value)
{
    alarm_unset(ted_t1_alarm);
    t1_value = (ted.t1_start = (ted.t1_start & 0x00ff) | (value << 8)) << 1;
    alarm_set(ted_t1_alarm, maincpu_clk
              + (ted.t1_start == 0 ? 65536 : ted.t1_start) * 2);
    t1_last_restart = maincpu_clk;
    ted.timer_running[0] = 1;
}

static void ted_timer_t2_store_low(uint8_t value)
{
    alarm_unset(ted_t2_alarm);
    /* Writes replace only one byte of the live counter. */
    if (ted.timer_running[1]) {
        t2_value -= maincpu_clk - t2_last_restart;
    }
    t2_value = (t2_value & 0x1fe00) | ((CLOCK)value << 1);
    ted.timer_running[1] = 0;
}

static void ted_timer_t2_store_high(uint8_t value)
{
    alarm_unset(ted_t2_alarm);
    if (ted.timer_running[1]) {
        t2_value -= maincpu_clk - t2_last_restart;
    }
    t2_value = (t2_value & 0x1fe) | ((CLOCK)value << 9);
    alarm_set(ted_t2_alarm, maincpu_clk
              + (t2_value == 0 ? 65536 * 2 : t2_value));
    t2_last_restart = maincpu_clk;
    ted.timer_running[1] = 1;
}

static void ted_timer_t3_store_low(uint8_t value)
{
    alarm_unset(ted_t3_alarm);
    /* Writes replace only one byte of the live counter. */
    if (ted.timer_running[2]) {
        t3_value -= maincpu_clk - t3_last_restart;
    }
    t3_value = (t3_value & 0x1fe00) | ((CLOCK)value << 1);
    ted.timer_running[2] = 0;
}

static void ted_timer_t3_store_high(uint8_t value)
{
    alarm_unset(ted_t3_alarm);
    if (ted.timer_running[2]) {
        t3_value -= maincpu_clk - t3_last_restart;
    }
    t3_value = (t3_value & 0x1fe) | ((CLOCK)value << 9);
    alarm_set(ted_t3_alarm, maincpu_clk
              + (t3_value == 0 ? 65536 * 2 : t3_value));
    t3_last_restart = maincpu_clk;
    ted.timer_running[2] = 1;
}

static uint8_t ted_timer_t1_read_low(void)
{
#ifdef DEBUG_TIMER
    log_debug(LOG_DEFAULT, "TI1 READL %02x", t1_value & 0xff);
#endif
    if (ted.timer_running[0]) {
        t1_value -= maincpu_clk - t1_last_restart;
        t1_last_restart = maincpu_clk;
    }
    return (uint8_t)(t1_value >> 1);
}

static uint8_t ted_timer_t1_read_high(void)
{
#ifdef DEBUG_TIMER
    log_debug(LOG_DEFAULT, "TI1 READH %02x", t1_value >> 8);
#endif
    if (ted.timer_running[0]) {
        t1_value -= maincpu_clk - t1_last_restart;
        t1_last_restart = maincpu_clk;
    }
    return (uint8_t)(t1_value >> 9);
}

static uint8_t ted_timer_t2_read_low(void)
{
#ifdef DEBUG_TIMER
    log_debug(LOG_DEFAULT, "TI2 READL %02x", t2_value & 0xff);
#endif
    if (ted.timer_running[1]) {
        t2_value -= maincpu_clk - t2_last_restart;
        t2_last_restart = maincpu_clk;
    }
    return (uint8_t)(t2_value >> 1);
}

static uint8_t ted_timer_t2_read_high(void)
{
#ifdef DEBUG_TIMER
    log_debug(LOG_DEFAULT, "TI2 READH %02x", t2_value >> 8);
#endif
    if (ted.timer_running[1]) {
        t2_value -= maincpu_clk - t2_last_restart;
        t2_last_restart = maincpu_clk;
    }
    return (uint8_t)(t2_value >> 9);
}

static uint8_t ted_timer_t3_read_low(void)
{
#ifdef DEBUG_TIMER
    log_debug(LOG_DEFAULT, "TI3 READL %02x", t3_value & 0xff);
#endif
    if (ted.timer_running[2]) {
        t3_value -= maincpu_clk - t3_last_restart;
        t3_last_restart = maincpu_clk;
    }
    return (uint8_t)(t3_value >> 1);
}

static uint8_t ted_timer_t3_read_high(void)
{
#ifdef DEBUG_TIMER
    log_debug(LOG_DEFAULT, "TI3 READH %02x", t3_value >> 8);
#endif
    if (ted.timer_running[2]) {
        t3_value -= maincpu_clk - t3_last_restart;
        t3_last_restart = maincpu_clk;
    }
    return (uint8_t)(t3_value >> 9);
}

/*-----------------------------------------------------------------------*/

void ted_timer_store(uint16_t addr, uint8_t value)
{
#ifdef DEBUG_TIMER
    log_debug(LOG_DEFAULT, "TI STORE %02x %02x CLK %x", addr, value, maincpu_clk);
#endif
    switch (addr) {
        case 0:
            ted_timer_t1_store_low(value);
            break;
        case 1:
            ted_timer_t1_store_high(value);
            break;
        case 2:
            ted_timer_t2_store_low(value);
            break;
        case 3:
            ted_timer_t2_store_high(value);
            break;
        case 4:
            ted_timer_t3_store_low(value);
            break;
        case 5:
            ted_timer_t3_store_high(value);
            break;
    }
}

uint8_t ted_timer_read(uint16_t addr)
{
    switch (addr) {
        case 0:
            return ted_timer_t1_read_low();
        case 1:
            return ted_timer_t1_read_high();
        case 2:
            return ted_timer_t2_read_low();
        case 3:
            return ted_timer_t2_read_high();
        case 4:
            return ted_timer_t3_read_low();
        case 5:
            return ted_timer_t3_read_high();
    }
    return 0;
}

/* Return the clocks left until the next underflow of a timer: the value
   at its last restart minus the clocks elapsed since, as the reads do.  A
   running timer whose alarm is due but not yet served has none left.  */
static CLOCK ted_timer_remaining(CLOCK value, CLOCK last_restart, int running)
{
    CLOCK elapsed;

    if (!running) {
        return value;
    }
    elapsed = maincpu_clk - last_restart;
    return (elapsed < value) ? value - elapsed : 0;
}

static void ted_timer_restart(alarm_t *alarm, CLOCK *value,
                              CLOCK *last_restart, int running)
{
    alarm_unset(alarm);
    *last_restart = maincpu_clk;
    if (running) {
        alarm_set(alarm, maincpu_clk + *value);
    }
}

/* The counters are saved as the clocks left until their next underflow
   (two per single clock), timer 1 also saves its reload value.  */
int ted_timer_snapshot_write(snapshot_module_t *m)
{
    return SMW_W(m, (uint16_t)ted.t1_start) < 0
        || SMW_B(m, (uint8_t)ted.timer_running[0]) < 0
        || SMW_B(m, (uint8_t)ted.timer_running[1]) < 0
        || SMW_B(m, (uint8_t)ted.timer_running[2]) < 0
        || SMW_DW(m, (uint32_t)ted_timer_remaining(t1_value, t1_last_restart,
                                                   ted.timer_running[0])) < 0
        || SMW_DW(m, (uint32_t)ted_timer_remaining(t2_value, t2_last_restart,
                                                   ted.timer_running[1])) < 0
        || SMW_DW(m, (uint32_t)ted_timer_remaining(t3_value, t3_last_restart,
                                                   ted.timer_running[2])) < 0
        ? -1 : 0;
}

int ted_timer_snapshot_read(snapshot_module_t *m)
{
    uint16_t start;
    uint8_t running[3];
    uint32_t value[3];
    int i;

    if (SMR_W(m, &start) < 0
        || SMR_B(m, &running[0]) < 0
        || SMR_B(m, &running[1]) < 0
        || SMR_B(m, &running[2]) < 0
        || SMR_DW(m, &value[0]) < 0
        || SMR_DW(m, &value[1]) < 0
        || SMR_DW(m, &value[2]) < 0) {
        return -1;
    }
    for (i = 0; i < 3; i++) {
        if (running[i] > 1 || value[i] > 65536 * 2) {
            return -1;
        }
        ted.timer_running[i] = running[i];
    }
    ted.t1_start = start;
    t1_value = value[0];
    t2_value = value[1];
    t3_value = value[2];
    ted_timer_restart(ted_t1_alarm, &t1_value, &t1_last_restart,
                      ted.timer_running[0]);
    ted_timer_restart(ted_t2_alarm, &t2_value, &t2_last_restart,
                      ted.timer_running[1]);
    ted_timer_restart(ted_t3_alarm, &t3_value, &t3_last_restart,
                      ted.timer_running[2]);
    return 0;
}

void ted_timer_init(void)
{
    ted.t1_start = 0;
    ted_t1_alarm = alarm_new(maincpu_alarm_context, "TED T1", ted_t1_alarm_handler, NULL);
    ted_t2_alarm = alarm_new(maincpu_alarm_context, "TED T2", ted_t2_alarm_handler, NULL);
    ted_t3_alarm = alarm_new(maincpu_alarm_context, "TED T3", ted_t3_alarm_handler, NULL);
}

void ted_timer_reset(void)
{
    alarm_unset(ted_t1_alarm);
    ted.t1_start = 0;
    alarm_unset(ted_t2_alarm);
    alarm_unset(ted_t3_alarm);
    t1_value = t2_value = t3_value = 0;
    t1_last_restart = t2_last_restart = t3_last_restart = maincpu_clk;
    ted.timer_running[0] = ted.timer_running[1] = ted.timer_running[2] = 0;
}
