/*
 * ted-snapshot.c - Snapshot functionality for the TED emulation.
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
 *  Ettore Perazzoli <ettore@comm2000.it>
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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "alarm.h"
#include "interrupt.h"
#include "log.h"
#include "plus4.h"
#include "snapshot.h"
#include "raster-snapshot.h"
#include "ted-irq.h"
#include "ted-draw.h"
#include "ted-snapshot.h"
#include "ted-sound.h"
#include "ted-timing.h"
#include "ted-timer.h"
#include "ted.h"
#include "tedtypes.h"
#include "types.h"

/* #define DEBUGTED */

#ifdef DEBUGTED
#define DBG(x) log_printf x
#else
#define DBG(x)
#endif

/* Make sure all the TED alarms are removed.  This just makes it easier to
   write functions for loading snapshot modules in other video chips without
   caring that the TED alarms are dispatched when they really shouldn't
   be.  */

void ted_snapshot_prepare(void)
{
    ted.fetch_clk = CLOCK_MAX;
    alarm_unset(ted.raster_fetch_alarm);
    ted.draw_clk = CLOCK_MAX;
    alarm_unset(ted.raster_draw_alarm);
    ted.raster_irq_clk = CLOCK_MAX;
    alarm_unset(ted.raster_irq_alarm);
}

/*
    TED snapshot module format.  Each version appends to the previous one;
    loading an older module keeps or derives the missing state.

    Type    | Name                  | Description
    ---------------------------------------------------------------------
    QWORD   | last_emulate_line_clk | clock at which the current line started
    BYTE    | AllowBadLines         | flag: DMA can happen in this frame
    BYTE    | BadLine               | unused, 0
    BYTE    | Blank                 | flag: vertical window closed
    40*BYTE | ColorBuf              | attribute buffer
    BYTE    | IdleState             | flag: idle state
    40*BYTE | MatrixBuf             | character buffer
    BYTE    | RasterCycle           | cycle within the line
    WORD    | RasterLine            | current raster line
    64*BYTE | Registers             | TED registers
    DWORD   | tv_current_line       | line of the TV frame
    DWORD   | screen_height         | lines of the TED frame
    DWORD   | first_displayed_line  |
    DWORD   | last_displayed_line   |
    DWORD   | ted_raster_counter    |
    WORD    | Vc                    | DMA counter
    BYTE    | VcInc                 | unused, 40
    WORD    | VcBase                | character position
    BYTE    | VideoInt              | interrupt status
    QWORD   | FetchEventTick        | clocks to the next DMA fetch event
            | raster                | common raster module data
    1.6:    | chr_pos_reload, chr_pos_count
    1.7:    | counter_clk, counter_overflow_until, counter_increment,
            | row_counter_active, memptr_col
    1.8:    | sound registers and oscillators (ted-sound.c)
    1.9:    | 80 bytes, unused, 0
    1.10:   | BYTE unused (row counter), BYTE row counter,
            | BYTE matrix_fetch_pending
    1.11:   | dma_line, chr_pos_latch
    1.12:   | timers (ted-timer.c), line_repeat, fetch and refresh clock holds
    1.13:   | sound voice and output state (ted-sound.c)
    1.14:   | pixel pipeline (ted-draw.c)
*/

static char snap_module_name[] = "TED";
#define SNAP_MAJOR 1
#define SNAP_MINOR 14

int ted_snapshot_write_module(snapshot_t *s)
{
    int i;
    snapshot_module_t *m;

    /* FIXME: Dispatch all events?  */

    /* Save the clocks of a frozen TED as they stand now.  */
    ted_freeze_update();
    ted_draw_sync(maincpu_clk);

    m = snapshot_module_create (s, snap_module_name, SNAP_MAJOR, SNAP_MINOR);
    if (m == NULL) {
        return -1;
    }

    DBG(("TED write snapshot at clock: %"PRIu64" cycle: %u tedline: %u rasterline: %u",
         maincpu_clk, TED_RASTER_CYCLE(maincpu_clk), TED_RASTER_Y(maincpu_clk),
         ted.raster.current_line));

    if (0
        || SMW_CLOCK(m, ted.last_emulate_line_clk) < 0
        /* AllowBadLines */
        || SMW_B(m, (uint8_t)ted.allow_bad_lines) < 0
        /* BadLine */
        || SMW_B(m, 0) < 0
        /* Blank */
        || SMW_B(m, (uint8_t)ted.raster.blank_enabled) < 0
        /* ColorBuf */
        || SMW_BA(m, ted.cbuf, 40) < 0
        /* IdleState */
        || SMW_B(m, (uint8_t)ted.idle_state) < 0
        /* MatrixBuf */
        || SMW_BA(m, ted.vbuf, 40) < 0
        /* RasterCycle */
        || SMW_B(m, (uint8_t)TED_RASTER_CYCLE(maincpu_clk)) < 0
        /* RasterLine */
        || SMW_W(m, (uint16_t)(TED_RASTER_Y(maincpu_clk))) < 0
        ) {
        goto fail;
    }

    for (i = 0; i < 0x40; i++) {
        /* Registers */
        if (SMW_B(m, ted.regs[i]) < 0) {
            goto fail;
        }
    }

    if (0
        || SMW_DW(m, ted.tv_current_line) < 0
        || SMW_DW(m, ted.screen_height) < 0
        || SMW_DW(m, ted.first_displayed_line) < 0
        || SMW_DW(m, ted.last_displayed_line) < 0
        || SMW_DW(m, (uint32_t)ted.ted_raster_counter) < 0
        /* Vc */
        || SMW_W(m, (uint16_t)ted.mem_counter) < 0
        /* VcInc */
        || SMW_B(m, TED_SCREEN_TEXTCOLS) < 0
        /* VcBase */
        || SMW_W(m, (uint16_t)ted.memptr) < 0
        /* VideoInt */
        || SMW_B(m, (uint8_t)ted.irq_status) < 0
        ) {
        goto fail;
    }

    if (0
        /* FetchEventTick */
        || SMW_CLOCK(m, ted.fetch_clk - maincpu_clk) < 0
        ) {
        goto fail;
    }

    if (raster_snapshot_write(m, &ted.raster)) {
        goto fail;
    }

    /* Added in version 1.6: the reload and current bitmap position can
       differ following a write to $ff1a/$ff1b. */
    if (SMW_W(m, (uint16_t)ted.chr_pos_reload) < 0
        || SMW_W(m, (uint16_t)ted.chr_pos_count) < 0) {
        goto fail;
    }

    /* Version 1.7: preserve pending counter events across a snapshot. */
    if (SMW_CLOCK(m, ted.counter_clk) < 0
        || SMW_CLOCK(m, ted.counter_overflow_until) < 0
        || SMW_B(m, (uint8_t)ted.counter_increment) < 0
        || SMW_B(m, (uint8_t)ted.row_counter_active) < 0
        || SMW_W(m, (uint16_t)ted.memptr_col) < 0) {
        goto fail;
    }

    if (ted_sound_snapshot_write(m) < 0) {
        goto fail;
    }

    {
        static const uint8_t unused[2 * TED_SCREEN_TEXTCOLS];

        if (SMW_BA(m, unused, sizeof(unused)) < 0) {
            goto fail;
        }
    }

    if (SMW_B(m, (uint8_t)ted.raster.ycounter) < 0
        || SMW_B(m, (uint8_t)ted.raster.ycounter) < 0
        || SMW_B(m, (uint8_t)ted.matrix_fetch_pending) < 0) {
        goto fail;
    }

    if (SMW_W(m, (uint16_t)ted.dma_line) < 0
        || SMW_B(m, (uint8_t)ted.chr_pos_latch) < 0) {
        goto fail;
    }

    /* Version 1.12: timer reload value, counters and run state, a pending
       line repeat and a held CPU clock. */
    if (ted_timer_snapshot_write(m) < 0
        || SMW_B(m, (uint8_t)ted.line_repeat) < 0
        || SMW_CLOCK(m, ted.fetch_clock_hold_end > maincpu_clk
                        ? ted.fetch_clock_hold_end - maincpu_clk : 0) < 0
        || SMW_B(m, (uint8_t)ted.fetch_clock_hold) < 0
        || SMW_CLOCK(m, ted.refresh_clock_hold_end > maincpu_clk
                        ? ted.refresh_clock_hold_end - maincpu_clk : 0) < 0
        || SMW_B(m, (uint8_t)ted.refresh_clock_hold) < 0) {
        goto fail;
    }

    /* Version 1.13: time since the last state change of the sound voices
       and the sound output stage. */
    if (ted_sound_snapshot_write_state(m) < 0) {
        goto fail;
    }

    if (ted_draw_snapshot_write(m) < 0) {
        goto fail;
    }

    DBG(("TED snapshot written."));
    return snapshot_module_close(m);

fail:
    if (m != NULL) {
        snapshot_module_close(m);
    }
    DBG(("error writing TED snapshot."));
    return -1;
}

int ted_snapshot_read_module(snapshot_t *s)
{
    uint8_t major_version, minor_version;
    int i;
    uint16_t RasterLine;
    uint8_t RasterCycle;
    uint8_t unused;
    snapshot_module_t *m;

    m = snapshot_module_open(s, snap_module_name,
                             &major_version, &minor_version);
    if (m == NULL) {
        return -1;
    }

    if (snapshot_version_is_bigger(major_version, minor_version, SNAP_MAJOR, SNAP_MINOR)) {
        log_error(ted.log,
                  "Snapshot module version (%d.%d) newer than %d.%d.",
                  major_version, minor_version,
                  SNAP_MAJOR, SNAP_MINOR);
        goto fail;
    }

    /* A freeze of the running machine ends; $FF07 bit 5 of the snapshot
       freezes again once everything is restored.  */
    ted.freeze = 0;
    alarm_unset(ted.tv_line_alarm);
    ted_timer_freeze(0, 0);

    if (0
        || SMR_CLOCK(m, &ted.last_emulate_line_clk) < 0
        /* AllowBadLines */
        || SMR_B_INT(m, &ted.allow_bad_lines) < 0
        /* BadLine */
        || SMR_B(m, &unused) < 0
        /* Blank */
        || SMR_B_INT(m, &ted.raster.blank_enabled) < 0
        /* ColorBuf */
        || SMR_BA(m, ted.cbuf, 40) < 0
        /* IdleState */
        || SMR_B_INT(m, &ted.idle_state) < 0
        /* MatrixBuf */
        || SMR_BA(m, ted.vbuf, 40) < 0
        || SMR_B(m, &RasterCycle) < 0
        || SMR_W(m, &RasterLine) < 0
        ) {
        goto fail;
    }

    for (i = 0; i < 0x40; i++) {
        if (SMR_B(m, &ted.regs[i]) < 0 /* Registers */) {
            goto fail;
        }
    }

    if (0
        || SMR_DW(m, &ted.tv_current_line) < 0
        || SMR_DW(m, &ted.screen_height) < 0
        || SMR_DW_INT(m, &ted.first_displayed_line) < 0
        || SMR_DW_INT(m, &ted.last_displayed_line) < 0
        || SMR_DW_INT(m, (int*)&ted.ted_raster_counter) < 0
        /* Vc */
        || SMR_W_INT(m, &ted.mem_counter) < 0
        /* VcInc */
        || SMR_B(m, &unused) < 0
        /* VcBase */
        || SMR_W_INT(m, &ted.memptr) < 0
        /* VideoInt */
        || SMR_B_INT(m, &ted.irq_status) < 0) {
        goto fail;
    }

    /* The frame and the clock rate follow the mode of $FF07 bit 6.  */
    ted_timing_set_mode((ted.regs[0x07] & 0x40) != 0);
    plus4_set_ted_ntsc_mode((ted.regs[0x07] & 0x40) != 0);

    /* Sanity check the current raster line and the current raster cycle */
    DBG(("TED read snapshot at clock: %d cycle: %d (%d) tedline: %d (%d) rasterline: %d",
         maincpu_clk, TED_RASTER_CYCLE(maincpu_clk), RasterCycle, TED_RASTER_Y(maincpu_clk),
         RasterLine, ted.raster.current_line));

    if (RasterCycle != (uint8_t)TED_RASTER_CYCLE(maincpu_clk)) {
        log_error(ted.log,
                  "Not matching raster cycle (%d) in snapshot; should be %u.",
                  RasterCycle, TED_RASTER_CYCLE(maincpu_clk));
        goto fail;
    }

    if (RasterLine != (uint16_t)TED_RASTER_Y(maincpu_clk)) {
        log_error(ted.log,
                  "Not matching raster line (%d) in snapshot; should be %u.",
                  RasterLine, TED_RASTER_Y(maincpu_clk));
        goto fail;
    }

    /* Recalculate the alarms and the state derived from the registers.  */
    ted_irq_set_raster_line(ted.regs[0x0b] | ((ted.regs[0x0a] & 1) << 8));

    ted_update_memory_ptrs();

    ted.raster.ysmooth = ted.regs[0x06] & 0x7;
    ted.raster.blank = !(ted.regs[0x06] & 0x10);

    /* FIXME: not saved; after loading, the current line behaves as if its
       fetch cycle had not passed yet.  */
    ted.memory_fetch_done = 0;

    ted.draw_clk = maincpu_clk + (ted.draw_cycle - TED_RASTER_CYCLE(maincpu_clk));
    ted.last_emulate_line_clk = ted.draw_clk - ted.cycles_per_line;
    alarm_set(ted.raster_draw_alarm, ted.draw_clk);

    {
        CLOCK qw;

        if (SMR_CLOCK(m, &qw) < 0) {  /* FetchEventTick */
            goto fail;
        }

        ted.fetch_clk = maincpu_clk + qw;

        alarm_set(ted.raster_fetch_alarm, ted.fetch_clk);
    }

    if (ted.irq_status & 0x80) {
        interrupt_restore_irq(maincpu_int_status, ted.int_num, 1);
    }


    if (raster_snapshot_read(m, &ted.raster)) {
        goto fail;
    }

    if (snapshot_version_is_bigger(major_version, minor_version, 1, 5)) {
        if (SMR_W_INT(m, &ted.chr_pos_reload) < 0
            || SMR_W_INT(m, &ted.chr_pos_count) < 0) {
            goto fail;
        }
    } else {
        ted.chr_pos_reload = ted.memptr;
        ted.chr_pos_count = ted.memptr;
    }

    if (snapshot_version_is_bigger(major_version, minor_version, 1, 6)) {
        if (SMR_CLOCK(m, &ted.counter_clk) < 0
            || SMR_CLOCK(m, &ted.counter_overflow_until) < 0
            || SMR_B_INT(m, &ted.counter_increment) < 0
            || SMR_B_INT(m, &ted.row_counter_active) < 0
            || SMR_W_INT(m, &ted.memptr_col) < 0) {
            goto fail;
        }
    } else {
        /* Older modules contain neither pending events nor a DMA reload. */
        ted.counter_clk = maincpu_clk;
        ted.counter_overflow_until = 0;
        ted.counter_increment = ted.character_fetch_on
                               && RasterCycle >= 8
                               && RasterCycle < TED_POSITION_LATCH_CYCLE;
        ted.row_counter_active = !ted.idle_state;
        ted.memptr_col = ted.mem_counter;
    }
    if (snapshot_version_is_bigger(major_version, minor_version, 1, 7)) {
        if (ted_sound_snapshot_read(m) < 0) {
            goto fail;
        }
    } else {
        ted_sound_snapshot_legacy(ted.regs + 0x0e);
    }
    if (snapshot_version_is_bigger(major_version, minor_version, 1, 8)) {
        /* Versions 1.9-1.13 of the removed line renderer: CPU-overwritten
           bitmap flags (0 or 1) and bytes.  */
        uint8_t latched[TED_SCREEN_TEXTCOLS], data[TED_SCREEN_TEXTCOLS];

        if (SMR_BA(m, latched, TED_SCREEN_TEXTCOLS) < 0
            || SMR_BA(m, data, TED_SCREEN_TEXTCOLS) < 0) {
            goto fail;
        }
        for (i = 0; i < TED_SCREEN_TEXTCOLS; i++) {
            if (latched[i] > 1) {
                goto fail;
            }
        }
    }
    ted.matrix_fetch_pending = ted.allow_bad_lines
        && ((ted.ted_raster_counter - 1) & 7) == (unsigned int)ted.raster.ysmooth;
    if (snapshot_version_is_bigger(major_version, minor_version, 1, 9)) {
        /* The first byte is the row counter of the removed line renderer. */
        if (SMR_B(m, &unused) < 0
            || SMR_B_UINT(m, &ted.raster.ycounter) < 0
            || SMR_B_INT(m, &ted.matrix_fetch_pending) < 0
            || unused > 7 || ted.raster.ycounter > 7
            || ted.matrix_fetch_pending > 1) {
            goto fail;
        }
    }
    ted.dma_line = ted.ted_raster_counter;
    ted.chr_pos_latch = ted.raster.ycounter == 7;
    if (snapshot_version_is_bigger(major_version, minor_version, 1, 10)) {
        if (SMR_W_UINT(m, &ted.dma_line) < 0
            || SMR_B_INT(m, &ted.chr_pos_latch) < 0
            || ted.dma_line > 511 || ted.chr_pos_latch > 1) {
            goto fail;
        }
    }
    /* Older modules do not contain the timers, which keep their state.  */
    ted.line_repeat = 0;
    ted.fetch_clock_hold_end = 0;
    ted.refresh_clock_hold_end = 0;
    if (snapshot_version_is_bigger(major_version, minor_version, 1, 11)) {
        CLOCK fetch_hold, refresh_hold;

        if (ted_timer_snapshot_read(m) < 0
            || SMR_B_INT(m, &ted.line_repeat) < 0
            || SMR_CLOCK(m, &fetch_hold) < 0
            || SMR_B_INT(m, &ted.fetch_clock_hold) < 0
            || SMR_CLOCK(m, &refresh_hold) < 0
            || SMR_B_INT(m, &ted.refresh_clock_hold) < 0
            || ted.line_repeat > 1 || ted.fetch_clock_hold > 1
            || ted.refresh_clock_hold > 1
            || fetch_hold > ted.cycles_per_line
            || refresh_hold > ted.cycles_per_line) {
            goto fail;
        }
        ted.fetch_clock_hold_end = fetch_hold ? maincpu_clk + fetch_hold : 0;
        ted.refresh_clock_hold_end = refresh_hold ? maincpu_clk + refresh_hold : 0;
    }
    if (snapshot_version_is_bigger(major_version, minor_version, 1, 12)) {
        if (ted_sound_snapshot_read_state(m) < 0) {
            goto fail;
        }
    }
    if (snapshot_version_is_bigger(major_version, minor_version, 1, 13)) {
        if (ted_draw_snapshot_read(m) < 0) {
            goto fail;
        }
    } else {
        ted_draw_snapshot_legacy();
    }
    ted.clock_hold_end = ted.fetch_clock_hold_end;
    if (ted.refresh_clock_hold_end > ted.clock_hold_end) {
        ted.clock_hold_end = ted.refresh_clock_hold_end;
    }
    ted_delay_resync();
    if (ted.regs[0x07] & 0x20) {
        ted_set_freeze(1);
    }

    raster_force_repaint(&ted.raster);
    DBG(("TED: snapshot loaded."));
    return snapshot_module_close(m);

fail:
    if (m != NULL) {
        snapshot_module_close(m);
    }
    log_error(ted.log, "could not load TED snapshot.");
    return -1;
}
