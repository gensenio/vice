/*
 * ted-fetch.c - Phi2 data fetch for the TED emulation.
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
 *  Ettore Perazzoli <ettore@comm2000.it>
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

#include <string.h>

#include "alarm.h"
#include "dma.h"
#include "maincpu.h"
#include "ted-fetch.h"
#include "ted-draw.h"
#include "tedtypes.h"
#include "types.h"


/* Preserve the fetched pixel latch before a CPU store to video RAM. */
void ted_fetch_store(uint16_t addr, uint8_t old_value, unsigned int ram_mask)
{
    unsigned int base;
    unsigned int mask;

    if (!ted.character_fetch_on || (ted.regs[0x12] & 4)
        || (addr & 7) != ted.raster.ycounter) {
        return;
    }
    if (ted.regs[0x06] & 0x20) {
        base = (ted.regs[0x12] & 0x38) << 10;
        mask = 0xe000;
    } else {
        mask = ((ted.regs[6] & 0x40) || (ted.regs[7] & 0x80)) ? 0xf800 : 0xfc00;
        base = (ted.regs[0x13] << 8) & mask;
    }
    if (!((addr ^ base) & ram_mask & mask)) {
        ted_draw_sync(maincpu_clk);
    }
}

/* Emulate a matrix line fetch, `num' bytes starting from `offs'.  This takes
   care of the 10-bit counter wraparound.  */
void ted_fetch_matrix(int offs, int num)
{
    uint8_t *p;
    int start_char;
    int c;

    /* When the attribute request of this line overlaps the character
       request, both buffers receive attribute data.  */
    p = (ted.dma_line & 7) == (unsigned int)ted.raster.ysmooth
        ? ted.color_ptr : ted.screen_ptr;

    start_char = (ted.memptr_col + offs) & 0x3ff;
    c = 0x3ff - start_char + 1;

    if (c >= num) {
        memcpy(ted.vbuf + offs, p + start_char, num);
    } else {
        memcpy(ted.vbuf + offs, p + start_char, c);
        memcpy(ted.vbuf + offs + c, p, num - c);
    }
}

/* Emulate an attribute fetch into `cbuf_tmp', which becomes `cbuf' at the
   end of the line.  */
inline void ted_fetch_color(int offs, int num)
{
    int start_char;
    int c;

    start_char = (ted.memptr_col + offs) & 0x3ff;
    c = 0x3ff - start_char + 1;

    if (c >= num) {
        memcpy(ted.cbuf_tmp + offs, ted.color_ptr + start_char, num);
    } else {
        memcpy(ted.cbuf_tmp + offs, ted.color_ptr + start_char, c);
        memcpy(ted.cbuf_tmp + offs + c, ted.color_ptr, num - c);
    }
}

/* Serve the DMA requests of the current line at its fetch cycle: character
   data requested by the preceding line and attributes requested by this
   one.  Either halts the CPU for the DMA window, minus `sub' clocks.  */
inline static void do_matrix_fetch(CLOCK sub)
{
    int dma = 0;

    if (!ted.memory_fetch_done) {
        ted.memory_fetch_done = 1;

        if (ted.matrix_fetch_pending
            && ted.allow_bad_lines
            && ted.dma_line > ted.first_dma_line
            && ted.dma_line <= ted.last_dma_line) {
            ted_fetch_matrix(0, TED_SCREEN_TEXTCOLS);
            ted.idle_state = 0;
            ted.memory_fetch_done = 2;
            dma = 1;
        }

        if ((ted.dma_line & 7) == (unsigned int)ted.raster.ysmooth
            && ted.allow_bad_lines
            && ted.dma_line >= ted.first_dma_line
            && ted.dma_line < ted.last_dma_line) {
            ted.row_counter_active = 1;
            ted_fetch_color(0, TED_SCREEN_TEXTCOLS);
            dma = 1;
        }
    }

    if (dma) {
        /* Both DMA requests share one bus-ownership interval. */
        dma_maincpu_steal_cycles(ted.fetch_clk,
                                 (TED_SCREEN_TEXTCOLS + 3) * 2 - sub, 0);
        ted_delay_oldclk((TED_SCREEN_TEXTCOLS + 3) * 2 - sub);
    }
}

inline static void handle_fetch_matrix(CLOCK sub)
{
    do_matrix_fetch(sub);

    if ((ted.ted_raster_counter >= ted.first_dma_line) &&
        (ted.ted_raster_counter < ted.last_dma_line)) {
        ted.fetch_clk += ted.cycles_per_line;
    } else {
        ted.fetch_clk += (ted.screen_height - ted.ted_raster_counter) * ted.cycles_per_line;
    }

    alarm_set(ted.raster_fetch_alarm, ted.fetch_clk);
}

/* Handle matrix fetch events.  */
void ted_fetch_alarm_handler(CLOCK offset, void *data)
{
    CLOCK last_opcode_first_write_clk;
    CLOCK last_opcode_last_write_clk;
    CLOCK sub;

    if (ted_freeze_defers(&ted.fetch_clk)) {
        alarm_unset(ted.raster_fetch_alarm);
        return;
    }

    /* This kludgy thing is used to emulate the behavior of the 6510 when BA
       goes low.  When BA goes low, every read access stops the processor
       until BA is high again; write accesses happen as usual instead.  */

    if (offset > 0) {
        switch (OPINFO_NUMBER(last_opcode_info)) {
            case 0:
                /* In BRK, IRQ and NMI the 3rd, 4th and 5th cycles are write
                   accesses, while the 1st, 2nd, 6th and 7th are read accesses.  */
                last_opcode_first_write_clk = maincpu_clk - 10;
                last_opcode_last_write_clk = maincpu_clk - 6;
                break;

            case 0x20:
                /* In JSR, the 4th and 5th cycles are write accesses, while the
                   1st, 2nd, 3rd and 6th are read accesses.  */
                last_opcode_first_write_clk = maincpu_clk - 6;
                last_opcode_last_write_clk = maincpu_clk - 4;
                break;

            default:
                /* In all the other opcodes, all the write accesses are the last
                   ones.  */
                if (maincpu_num_write_cycles() != 0) {
                    last_opcode_last_write_clk = maincpu_clk - 2;
                    last_opcode_first_write_clk = maincpu_clk
                                                  - maincpu_num_write_cycles() * 2;
                } else {
                    last_opcode_first_write_clk = (CLOCK)0;
                    last_opcode_last_write_clk = last_opcode_first_write_clk;
                }
                break;
        }
    } else { /* offset <= 0, i.e. offset == 0 */
        /* If we are called with no offset, we don't have to care about write
           accesses.  */
        last_opcode_first_write_clk = last_opcode_last_write_clk = 0;
    }

    if (ted.fetch_clk < (last_opcode_first_write_clk - 1)
        || ted.fetch_clk > last_opcode_last_write_clk) {
        sub = 0;
    } else {
        sub = last_opcode_last_write_clk - ted.fetch_clk + 1;
    }

    handle_fetch_matrix(sub);

    if ((offset > 11) && (ted.fastmode)) {
        dma_maincpu_steal_cycles(ted.fetch_clk, -(((signed)offset - 11) / 2), 0);
        ted_delay_oldclk(-(((signed)offset - 11) / 2));
    }
}

void ted_fetch_init(void)
{
    ted.raster_fetch_alarm = alarm_new(maincpu_alarm_context, "TEDRasterFetch",
                                       ted_fetch_alarm_handler, NULL);
}
