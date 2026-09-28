/*
 * ted-draw.h - Rendering for the TED emulation.
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

#ifndef VICE_TED_DRAW_H
#define VICE_TED_DRAW_H

#include "types.h"

/* Output position of the first display dot: a line starts 64 dots earlier
   (cycle 0), so no border mode can show a wider left border. */
#define TED_DRAW_DISPLAY_START  64

void ted_draw_init(void);
void ted_draw_reset(void);
void ted_draw_sync(CLOCK clk);
void ted_draw_store(unsigned int addr, uint8_t value);
void ted_draw_begin_line(CLOCK clk, int shown);
void ted_draw_line(CLOCK clk, int visible);
void ted_draw_black_line(void);
void ted_draw_freeze(CLOCK delta);
void ted_draw_canvas_changed(void);
struct snapshot_module_s;
int ted_draw_snapshot_write(struct snapshot_module_s *m);
int ted_draw_snapshot_read(struct snapshot_module_s *m);
void ted_draw_snapshot_legacy(void);

#endif
