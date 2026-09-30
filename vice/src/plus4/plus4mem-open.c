/*
 * plus4mem-open.c - Fallback storage for unconnected TED video memory.
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
 *  Tibor Biczo <crown@axelero.hu>
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

#include "plus4mem.h"

/* HACK: the following is an ugly hack, which is needed because of how the non-sc
         architecture works. Much of the TED emulation works on pointers that are
         not reassigned/updated on ever access, which would be required to wrap to
         the above function as needed. */

/* NOTE: fortunately only the TED fetching is affected, so the difference made by
         the pattern below is only on the visual result, and can not be detected
         by the CPU in any way - that probably means that "close" is "good enough". */

static uint8_t open_space[64 * 1024];
static int open_space_initialized = 0;

uint8_t *mem_get_open_space(void)
{
    /* FIXME: this is even lesser than less correct :) */

    int addr;
    if (!open_space_initialized) {
        for (addr = 0; addr < 0x10000; addr++) {
            /* HACK: we can't really produce a "correct" pattern here. So this
                     is just randomly tweaked a little to produce somewhat not
                     completely stupid results in the tests */
            open_space[addr] = ((addr >> 8) ^ 0xaa) ^ (addr ^ 0x55);
            if ((addr & 7) == 0) {
                if ((addr % (40 * 8 * 2)) < (40 * 8)) { open_space[addr] = 0x00; }
                if ((addr % (40 * 8 * 2)) >= (40 * 8)) { open_space[addr] = 0xff; }
            }
        }
        open_space_initialized = 1;
    }
    return open_space;
}
