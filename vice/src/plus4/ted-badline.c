/*
 * ted-badline.c - Bad line handling for the TED emulation.
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

#include <string.h>

#include "dma.h"
#include "maincpu.h"
#include "mem.h"
#include "ted-badline.h"
#include "ted-fetch.h"
#include "tedtypes.h"
#include "types.h"


inline static void line_becomes_good(int cycle)
{
    /* Bad line becomes good.  */
    ted.bad_line = 0;

    /* By changing the values in the registers, one can make the TED
       switch from idle to display state, but not from display to
       idle state.  So we are always in display state if this
       happens.  This is only true if the value changes in some
       cycle > 0, though; otherwise, the line never becomes bad.  */
    if (cycle > 0) {
        ted.raster.draw_idle_state = ted.idle_state = 0;
        ted.idle_data_location = IDLE_NONE;
        if ((cycle > (TED_FETCH_CYCLE + 2)) && !ted.ycounter_reset_checked) {
            /*ted.raster.ycounter = 0;*/
            ted.ycounter_reset_checked = 1;
        }
    }
}

/* The line becomes an attribute DMA line after the fetch cycle.  TED needs
   three more single clocks to take the bus from the CPU: the attribute
   slots in that time receive what the halted CPU keeps on the bus, the
   operand of the instruction after the store.  Later slots receive the
   attributes; earlier characters keep the previous request's data.  The
   CPU is halted until the end of the DMA window.  */
inline static void line_becomes_bad(int cycle, unsigned int line)
{
    int garbage, fresh;

    if (!ted.memory_fetch_done
        || line < ted.first_dma_line || line >= ted.last_dma_line) {
        /* Before the fetch cycle, the fetch alarm serves the request.  */
        return;
    }

    ted.bad_line = 1;
    ted.row_counter_active = 1;

    /* The following line fetches the character data (see the raster draw
       handler).  A character DMA on this line has kept the CPU halted.  */
    if (ted.memory_fetch_done == 2 || cycle >= TED_DMA_END_CYCLE) {
        return;
    }

    garbage = (cycle + 4 - TED_DMA_SLOT_CYCLE + 1) / 2;
    fresh = (cycle + 10 - TED_DMA_SLOT_CYCLE + 1) / 2;
    if (garbage < 0) {
        garbage = 0;
    }
    if (fresh > TED_SCREEN_TEXTCOLS) {
        fresh = TED_SCREEN_TEXTCOLS;
    }
    if (garbage < fresh) {
        memset(ted.cbuf_tmp + garbage,
               mem_bank_peek(0, (uint16_t)(reg_pc + 1), NULL),
               fresh - garbage);
    }
    if (fresh < TED_SCREEN_TEXTCOLS) {
        ted_fetch_color(fresh, TED_SCREEN_TEXTCOLS - fresh);
    }

    /* The CPU's second access after the store, at cycle + 4, is the first
       one halted; it reads at the end of the DMA window, like after a bad
       line requested at the fetch cycle.  */
    if (cycle + 4 < TED_DMA_END_CYCLE) {
        dma_maincpu_steal_cycles(maincpu_clk,
                                 (CLOCK)(TED_DMA_END_CYCLE - 4 - cycle), 0);
        ted_delay_oldclk((CLOCK)(TED_DMA_END_CYCLE - 4 - cycle));
    }
}

void ted_badline_check_state(uint8_t value, const int cycle,
                             const unsigned int line)
{
    int was_bad_line, now_bad_line;

    /* Check whether this line requests attribute DMA before and after the
       vertical scroll change.  */
    was_bad_line = (ted.allow_bad_lines
                    && (ted.raster.ysmooth == (int)(line & 7)));
    now_bad_line = (ted.allow_bad_lines
                    && ((int)(value & 7) == (int)(line & 7)));

    if (was_bad_line && !now_bad_line) {
        line_becomes_good(cycle);
    } else if (!was_bad_line && now_bad_line) {
        line_becomes_bad(cycle, line);
    }
}
