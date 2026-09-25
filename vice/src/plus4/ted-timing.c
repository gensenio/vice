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
            ted.screen_height = TED_NTSC_SCREEN_HEIGHT;
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
            ted.vsync_line = TED_NTSC_VSYNC_LINE;
            break;
        case MACHINE_SYNC_PAL:
        default:
            ted.screen_height = TED_PAL_SCREEN_HEIGHT;
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
            ted.vsync_line = TED_PAL_VSYNC_LINE;
            break;
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

/* Single clock slows the CPU clock down, it does not halt the CPU: BA is
   only asserted for DMA.  The CPU keeps sampling IRQ and NMI on each of
   its cycles, so these clocks must not be accounted as a DMA halt by
   `dma_maincpu_steal_cycles()'.  That would move an interrupt raised
   before the stretched clocks (e.g. the raster IRQ at the start of the
   line) to their end, delaying it by one more instruction.  */
static void ted_stretch_cpu_clk(CLOCK num)
{
    interrupt_cpu_status_t *cs = maincpu_int_status;

    if (num == 0) {
        return;
    }

    maincpu_clk += num;
    cs->irq_clk += num;
    cs->nmi_clk += num;
}

void ted_delay_clk(void)
{
    CLOCK diff;

    if (maincpu_clk == old_maincpu_clk) {
        return;
    }

    if (ted.fastmode == 0) {
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
