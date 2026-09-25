/*
 * ted-counter.c - Horizontal counter and memory-position events for TED.
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

#include "alarm.h"
#include "maincpu.h"
#include "ted-counter.h"
#include "tedtypes.h"

/* Counter events are expressed in the existing raster coordinate system:
   dot zero is cycle 16.  The preliminary TED event table locates position
   initialization at dot 424, increments at 432..288 and the latch at 290.
   One VICE clock spans four dots. */

void ted_counter_update(CLOCK clk)
{
    while (ted.counter_clk < clk) {
        unsigned int cycle = TED_RASTER_CYCLE(ted.counter_clk + 1);
        CLOCK count = clk - ted.counter_clk;
        unsigned int increments;

        if (cycle == 8 && ted.counter_clk + 1 >= ted.counter_overflow_until) {
            /* Re-entering the reload event while the DMA counter is still
               running latches its next position.  Horizontal counter writes
               can reach this event without passing the increment stop. */
            if (ted.counter_increment) {
                ted.memptr_col = (ted.mem_counter + 1) & 0x3ff;
            }
            ted.mem_counter = ted.memptr_col;
            ted.memptr = ted.chr_pos_reload;
            ted.chr_pos_count = ted.memptr;
            ted.counter_increment = ted.character_fetch_on;
            ted.counter_clk++;
            continue;
        }
        if (cycle == 89) {
            if (ted.character_fetch_on) {
                if (ted.raster.ycounter == 6) {
                    ted.memptr_col = ted.mem_counter;
                }
                if (ted.raster.ycounter == 7 && !ted.idle_state) {
                    ted.chr_pos_reload = ted.chr_pos_count;
                }
            }
            ted.counter_increment = 0;
            ted.counter_clk++;
            continue;
        }

        /* Between reload, latch and line wrap, only even clocks increment
           the positions.  Count them together instead of visiting each
           clock.  During horizontal overflow, cycle 8 is an ordinary clock. */
        if (cycle < 8 && count > 8 - cycle) {
            count = 8 - cycle;
        } else if (cycle >= 8 && cycle < 89 && count > 89 - cycle) {
            count = 89 - cycle;
        } else if (cycle > 89 && cycle < 114 && count > 114 - cycle) {
            count = 114 - cycle;
        }
        if (ted.counter_increment) {
            increments = (unsigned int)((count + !(cycle & 1)) / 2);
            ted.mem_counter = (ted.mem_counter + increments) & 0x3ff;
            if (!ted.idle_state) {
                ted.chr_pos_count = (ted.chr_pos_count + increments) & 0x3ff;
            }
        }
        ted.counter_clk += count;
    }
}

void ted_counter_store(uint8_t value)
{
    unsigned int cycle = TED_RASTER_CYCLE(maincpu_clk);
    unsigned int column = ((~value & 0xfc) >> 1) | (cycle & 1);
    unsigned int next = column < 114 ? (column + 16) % 114 : column - 112;
    int delta = (int)cycle - (int)next;

    ted.counter_overflow_until = column < 114 ? 0 : maincpu_clk + 128 - column;
    ted.last_emulate_line_clk += delta;
    ted.draw_clk += delta;
    alarm_set(ted.raster_draw_alarm, ted.draw_clk);
    ted.fetch_clk = ted.last_emulate_line_clk + TED_FETCH_CYCLE;
    if (next >= TED_FETCH_CYCLE || column >= 114) {
        ted.fetch_clk += ted.cycles_per_line;
    }
    alarm_set(ted.raster_fetch_alarm, ted.fetch_clk);
    if (ted.raster_irq_clk != CLOCK_MAX) {
        ted.raster_irq_clk += delta;
        alarm_set(ted.raster_irq_alarm, ted.raster_irq_clk);
    }
    ted_delay_resync();
}

unsigned int ted_counter_read(void)
{
    unsigned int cycle = TED_RASTER_CYCLE(maincpu_clk);
    unsigned int column = cycle < 16 ? cycle + 98 : cycle - 16;
    if (maincpu_clk < ted.counter_overflow_until) {
        column += 14;
    }
    return (column << 1) & 0xfe;
}
