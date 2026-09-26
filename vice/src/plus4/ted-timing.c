/*
 * ted-timing.c - Timing related settings for the TED emulation.
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
 *  Tibor Biczo <crown@axelero.hu>
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

#include "interrupt.h"
#include "maincpu.h"
#include "machine.h"
#include "plus4.h"
#include "resources.h"
#include "ted-timing.h"
#include "ted.h"
#include "tedtypes.h"

static CLOCK old_maincpu_clk = 0;
static CLOCK old_cycle = 0;

/* Number of cycles per line.  */
#define TED_PAL_CYCLES_PER_LINE     PLUS4_PAL_CYCLES_PER_LINE
#define TED_NTSC_CYCLES_PER_LINE    PLUS4_NTSC_CYCLES_PER_LINE

/* Cycle # at which the current raster line is re-drawn.  It is set to
   `TED_CYCLES_PER_LINE', so this actually happens at the very beginning
   (i.e. cycle 0) of the next line.  */
#define TED_PAL_DRAW_CYCLE          TED_PAL_CYCLES_PER_LINE
#define TED_NTSC_DRAW_CYCLE         TED_NTSC_CYCLES_PER_LINE

void ted_timing_set(machine_timing_t *machine_timing, int border_mode)
{
    int mode;

    resources_get_int("MachineVideoStandard", &mode);

    switch (mode) {
        case MACHINE_SYNC_NTSC:
            ted.tv_height = TED_NTSC_SCREEN_HEIGHT;
            switch (border_mode) {
                default:
                case TED_NORMAL_BORDERS:
                    ted.first_displayed_line = TED_NTSC_NORMAL_FIRST_DISPLAYED_LINE;
                    ted.last_displayed_line = TED_NTSC_NORMAL_LAST_DISPLAYED_LINE;
                    ted.screen_rightborderwidth = TED_SCREEN_NTSC_NORMAL_LEFTBORDERWIDTH;
                    ted.screen_leftborderwidth = TED_SCREEN_NTSC_NORMAL_RIGHTBORDERWIDTH;
                    break;
                case TED_FULL_BORDERS:
                    ted.first_displayed_line = TED_NTSC_FULL_FIRST_DISPLAYED_LINE;
                    ted.last_displayed_line = TED_NTSC_FULL_LAST_DISPLAYED_LINE;
                    ted.screen_rightborderwidth = TED_SCREEN_NTSC_FULL_LEFTBORDERWIDTH;
                    ted.screen_leftborderwidth = TED_SCREEN_NTSC_FULL_RIGHTBORDERWIDTH;
                    break;
                case TED_DEBUG_BORDERS:
                    ted.first_displayed_line = TED_NTSC_DEBUG_FIRST_DISPLAYED_LINE;
                    ted.last_displayed_line = TED_NTSC_DEBUG_LAST_DISPLAYED_LINE;
                    ted.screen_rightborderwidth = TED_SCREEN_NTSC_DEBUG_LEFTBORDERWIDTH;
                    ted.screen_leftborderwidth = TED_SCREEN_NTSC_DEBUG_RIGHTBORDERWIDTH;
                    break;
                case TED_NO_BORDERS:
                    ted.first_displayed_line = TED_NTSC_NO_BORDER_FIRST_DISPLAYED_LINE;
                    ted.last_displayed_line = TED_NTSC_NO_BORDER_LAST_DISPLAYED_LINE;
                    ted.screen_rightborderwidth = 0;
                    ted.screen_leftborderwidth = 0;
                    break;
            }
            ted.row_25_start_line = TED_NTSC_25ROW_START_LINE;
            ted.row_25_stop_line = TED_NTSC_25ROW_STOP_LINE;
            ted.row_24_start_line = TED_NTSC_24ROW_START_LINE;
            ted.row_24_stop_line = TED_NTSC_24ROW_STOP_LINE;
            ted.cycles_per_line = TED_NTSC_CYCLES_PER_LINE;
            ted.draw_cycle = TED_NTSC_DRAW_CYCLE;
            ted.first_dma_line = TED_NTSC_FIRST_DMA_LINE;
            ted.last_dma_line = TED_NTSC_LAST_DMA_LINE;
            ted.offset = TED_NTSC_OFFSET;
            ted.tv_vsync_line = TED_NTSC_VSYNC_LINE;
            break;
        case MACHINE_SYNC_PAL:
        default:
            ted.tv_height = TED_PAL_SCREEN_HEIGHT;
            switch (border_mode) {
                default:
                case TED_NORMAL_BORDERS:
                    ted.first_displayed_line = TED_PAL_NORMAL_FIRST_DISPLAYED_LINE;
                    ted.last_displayed_line = TED_PAL_NORMAL_LAST_DISPLAYED_LINE;
                    ted.screen_rightborderwidth = TED_SCREEN_PAL_NORMAL_LEFTBORDERWIDTH;
                    ted.screen_leftborderwidth = TED_SCREEN_PAL_NORMAL_RIGHTBORDERWIDTH;
                    break;
                case TED_FULL_BORDERS:
                    ted.first_displayed_line = TED_PAL_FULL_FIRST_DISPLAYED_LINE;
                    ted.last_displayed_line = TED_PAL_FULL_LAST_DISPLAYED_LINE;
                    ted.screen_rightborderwidth = TED_SCREEN_PAL_FULL_LEFTBORDERWIDTH;
                    ted.screen_leftborderwidth = TED_SCREEN_PAL_FULL_RIGHTBORDERWIDTH;
                    break;
                case TED_DEBUG_BORDERS:
                    ted.first_displayed_line = TED_PAL_DEBUG_FIRST_DISPLAYED_LINE;
                    ted.last_displayed_line = TED_PAL_DEBUG_LAST_DISPLAYED_LINE;
                    ted.screen_rightborderwidth = TED_SCREEN_PAL_DEBUG_LEFTBORDERWIDTH;
                    ted.screen_leftborderwidth = TED_SCREEN_PAL_DEBUG_RIGHTBORDERWIDTH;
                    break;
                case TED_NO_BORDERS:
                    ted.first_displayed_line = TED_PAL_NO_BORDER_FIRST_DISPLAYED_LINE;
                    ted.last_displayed_line = TED_PAL_NO_BORDER_LAST_DISPLAYED_LINE;
                    ted.screen_rightborderwidth = 0;
                    ted.screen_leftborderwidth = 0;
                    break;
            }
            ted.row_25_start_line = TED_PAL_25ROW_START_LINE;
            ted.row_25_stop_line = TED_PAL_25ROW_STOP_LINE;
            ted.row_24_start_line = TED_PAL_24ROW_START_LINE;
            ted.row_24_stop_line = TED_PAL_24ROW_STOP_LINE;
            ted.cycles_per_line = TED_PAL_CYCLES_PER_LINE;
            ted.draw_cycle = TED_PAL_DRAW_CYCLE;
            ted.first_dma_line = TED_PAL_FIRST_DMA_LINE;
            ted.last_dma_line = TED_PAL_LAST_DMA_LINE;
            ted.offset = TED_PAL_OFFSET;
            ted.tv_vsync_line = TED_PAL_VSYNC_LINE;
            break;
    }
    ted_timing_set_mode((ted.regs[0x07] & 0x40) != 0);
}

/* Set the frame of the mode selected by $FF07 bit 6.  The crystal of the
   machine does not change it: TED divides the crystal by 10 in PAL mode
   and by 8 in NTSC mode, 114 clocks per line in both.  */
void ted_timing_set_mode(int ntsc)
{
    if (ntsc) {
        ted.screen_height = TED_NTSC_SCREEN_HEIGHT;
        ted.vsync_line = TED_NTSC_VSYNC_LINE;
    } else {
        ted.screen_height = TED_PAL_SCREEN_HEIGHT;
        ted.vsync_line = TED_PAL_VSYNC_LINE;
    }
}

void ted_delay_oldclk(CLOCK num)
{
    old_maincpu_clk += num;
    old_cycle += num;
}

/* A counter write changes the horizontal phase without running the CPU. */
void ted_delay_resync(void)
{
    old_maincpu_clk = maincpu_clk;
    old_cycle = TED_RASTER_CYCLE(maincpu_clk);
}

/* TED runs the CPU in single clock while its character fetch clock (on
   lines with fetches) or its DRAM refresh clock is on.  Both are
   flip-flops, switched when the horizontal counter reaches their first
   and last slots: the fetch clock from slot 4 to 91, the refresh clock
   from 92 to 100.  $FF13 bit 1 and the freeze ($FF07 bit 5) force single
   clock.  */
#define TED_FETCH_CLOCK_ON      4U
#define TED_FETCH_CLOCK_OFF     92U
#define TED_REFRESH_CLOCK_ON    92U
#define TED_REFRESH_CLOCK_OFF   101U

static int ted_fetch_clock(CLOCK clk, unsigned int cycle)
{
    if (clk < ted.fetch_clock_hold_end) {
        return ted.fetch_clock_hold;
    }
    return cycle >= TED_FETCH_CLOCK_ON && cycle < TED_FETCH_CLOCK_OFF;
}

static int ted_refresh_clock(CLOCK clk, unsigned int cycle)
{
    if (clk < ted.refresh_clock_hold_end) {
        return ted.refresh_clock_hold;
    }
    return cycle >= TED_REFRESH_CLOCK_ON && cycle < TED_REFRESH_CLOCK_OFF;
}

/* Return non-zero if the CPU runs a cycle in clock slot `cycle' of the
   line at `clk'.  In single clock TED takes the even slots, see
   `ted_delay_clk()'.  */
static int ted_cpu_slot(CLOCK clk, unsigned int cycle)
{
    int single;

    single = ted.fastmode == 0 || ted.freeze
             || (ted.character_fetch_on && ted_fetch_clock(clk, cycle))
             || ted_refresh_clock(clk, cycle);
    return !single || (cycle & 1);
}

/* Keep a clock flip-flop in `state' after the counter moved to slot `to',
   until the counter reaches the next slot switching it.  */
static void ted_hold_flip_flop(int *hold, CLOCK *hold_end, int state,
                               int state_at_to, unsigned int to,
                               unsigned int on, unsigned int off)
{
    unsigned int to_on, to_off;

    *hold_end = 0;
    if (state == state_at_to) {
        return;
    }
    to_on = (on + ted.cycles_per_line - to) % ted.cycles_per_line;
    to_off = (off + ted.cycles_per_line - to) % ted.cycles_per_line;
    if (to_on == 0 || to_off == 0) {
        return;
    }
    *hold = state;
    *hold_end = maincpu_clk + (to_on < to_off ? to_on : to_off);
}

/* A `$FF1E' write moves the counter from slot `from' to slot `to'
   without passing the slots in between, so the clock flip-flops keep
   their state until the counter reaches their next switch.  */
void ted_delay_hold_clock(unsigned int from, unsigned int to)
{
    int fetch = ted_fetch_clock(maincpu_clk, from);
    int refresh = ted_refresh_clock(maincpu_clk, from);

    ted_hold_flip_flop(&ted.fetch_clock_hold, &ted.fetch_clock_hold_end,
                       fetch, ted_fetch_clock(CLOCK_MAX, to), to,
                       TED_FETCH_CLOCK_ON, TED_FETCH_CLOCK_OFF);
    ted_hold_flip_flop(&ted.refresh_clock_hold,
                       &ted.refresh_clock_hold_end, refresh,
                       ted_refresh_clock(CLOCK_MAX, to), to,
                       TED_REFRESH_CLOCK_ON, TED_REFRESH_CLOCK_OFF);
    ted.clock_hold_end = ted.fetch_clock_hold_end;
    if (ted.refresh_clock_hold_end > ted.clock_hold_end) {
        ted.clock_hold_end = ted.refresh_clock_hold_end;
    }
}

/* The 7501 takes an interrupt at an opcode fetch once the request has been
   seen during two of its cycles.  The CPU core counts `INTERRUPT_DELAY'
   clocks instead, which are only two CPU cycles at double clock.  Return
   the interrupt clock to pass to the core for a request raised at `clk',
   so that the core's delay ends after the second CPU cycle from `clk'.
   Horizontal counter overflow (`$FF1E' writes) is not taken into account.  */
CLOCK ted_delay_irq_clk(CLOCK clk)
{
    CLOCK line_clk = ted.last_emulate_line_clk;
    unsigned int cycle;
    int cpu_cycles;

    if (clk >= line_clk) {
        cycle = (unsigned int)((clk - line_clk) % ted.cycles_per_line);
    } else {
        cycle = (unsigned int)((ted.cycles_per_line
                                - (line_clk - clk) % ted.cycles_per_line)
                               % ted.cycles_per_line);
    }

    /* Find the slot of the second CPU cycle from `clk'.  */
    cpu_cycles = ted_cpu_slot(clk, cycle);
    while (cpu_cycles < 2) {
        clk++;
        cycle = (cycle + 1) % ted.cycles_per_line;
        cpu_cycles += ted_cpu_slot(clk, cycle);
    }

    return clk + 1 - INTERRUPT_DELAY;
}

/* Single clock slows the CPU clock down, it does not halt the CPU: BA is
   only asserted for DMA.  The CPU keeps sampling IRQ and NMI on each of
   its cycles, so these clocks must not be accounted as a DMA halt by
   `dma_maincpu_steal_cycles()'.  That would move an interrupt raised
   before the stretched clocks (e.g. the raster IRQ at the start of the
   line) to their end, delaying it by one more instruction.  Pending
   interrupt clocks are not moved either: `ted_delay_irq_clk()' already
   counts the CPU cycles from the request.  */
static void ted_stretch_cpu_clk(CLOCK num)
{
    maincpu_clk += num;
}

/* Place the CPU cycles run since the last call on the slots of a held
   clock (see `ted_delay_hold_clock()'), stepping one clock at a time like
   the position based code: each CPU cycle takes the next CPU slot.  Return
   non-zero when they all fit in the hold; otherwise leave the remaining
   cycles, from the end of the hold, to the position based code.  */
static int ted_delay_clk_held(void)
{
    CLOCK remaining = maincpu_clk - old_maincpu_clk;
    CLOCK clk = old_maincpu_clk;
    unsigned int cycle = (unsigned int)old_cycle;

    while (remaining > 0 && clk + 1 < ted.clock_hold_end) {
        clk++;
        cycle = (cycle + 1) % ted.cycles_per_line;
        if (ted_cpu_slot(clk, cycle)) {
            remaining--;
        }
    }
    old_maincpu_clk = clk;
    old_cycle = cycle;
    maincpu_clk = clk + remaining;
    return remaining == 0;
}

void ted_delay_clk(void)
{
    CLOCK diff;

    /* The CPU core issues the stack pushes of JSR, BRK and interrupts at
       one clock, so a write rewound by `ted_handle_pending_alarms()' can be
       before the last stretch.  Its cycles are already accounted for.  */
    if (maincpu_clk <= old_maincpu_clk) {
        return;
    }
    /* While frozen the CPU runs in single clock; the counters stay.  */
    ted_freeze_update();
    if (old_maincpu_clk < ted.clock_hold_end && ted_delay_clk_held()) {
        return;
    }

    if (ted.fastmode == 0 || ted.freeze) {
        diff = maincpu_clk - old_maincpu_clk - ((old_cycle & 1) ^ 1);
        ted_stretch_cpu_clk(diff);
    } else {
fastloop:
        diff = maincpu_clk - old_maincpu_clk;

        if (ted.character_fetch_on) {
            /* Fast mode, with character fetches,
               every even cycle is stolen from cycle 4 till cycle 100
               this covers 5 RAM refresh, 40 graphic fetch, and 4 idle fetch
               before the window.
            */
            if ((old_cycle < 101) && (old_cycle + diff >= 4)) {
                CLOCK max = 49;
                if (old_cycle > 3) {
                    max = (101 - old_cycle) / 2;
                    diff -= !(old_cycle & 1);
                } else {
                    diff -= 3 - old_cycle;
                }
                if (diff > max) {
                    diff = max;
                }
                ted_stretch_cpu_clk(diff);
            } else if (old_cycle + diff >= 118) {
                /* Instruction crosses into next line, and potentially
                   crossing into area where clocking changes.
                   Call draw alarm, and check if clocking changed.
                */
                /* Resume at the last fast clock before the next line's
                   single-clock window.  The remaining CPU clocks must not
                   include the fast clocks already consumed. */
                old_maincpu_clk += 117 - old_cycle;
                old_cycle = 3;
                ted_raster_draw_alarm_handler(0, NULL);
                goto fastloop;
            }
        } else {
            /* Fast mode, no character fetches,
               we only have to deal with 5 RAM refresh cycles
               the following cycles are stolen 92,94,96,98,100.
            */
            if ((old_cycle < 101) && (old_cycle + diff >= 92)) {
                CLOCK max = 5;
                if (old_cycle > 91) {
                    max = (101 - old_cycle) / 2;
                    diff -= !(old_cycle & 1);
                } else {
                    diff -= 91 - old_cycle;
                }
                if (diff > max) {
                    diff = max;
                }
                ted_stretch_cpu_clk(diff);
            } else if (old_cycle + diff >= 118) {
                /* Instruction crosses into next line, and potentially
                   crossing into area where clocking changes.
                   Call draw alarm, and check if clocking changed.
                */
                /* Resume at the last fast clock before the next line's
                   single-clock window.  The remaining CPU clocks must not
                   include the fast clocks already consumed. */
                old_maincpu_clk += 117 - old_cycle;
                old_cycle = 3;
                ted_raster_draw_alarm_handler(0, NULL);
                goto fastloop;
            }
        }
    }

    old_maincpu_clk = maincpu_clk;
    old_cycle = TED_RASTER_CYCLE(maincpu_clk) % 114;

    return;
}
