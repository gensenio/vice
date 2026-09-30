/*
 * ted-video.c - TED output with a writable horizontal counter.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "vice.h"

#include <string.h>

#include "plus4mem.h"
#include "raster-cache.h"
#include "snapshot.h"
#include "ted-timing.h"
#include "ted-video.h"
#include "tedtypes.h"
#include "viewport.h"

/* A TV's beam does not jump when the CPU writes $ff1e. Counter events
   control the border, the fetch window and the shifter independently.
   Keep the output already produced before such a write. Ordinary lines
   still use the cached raster renderer. */

static uint8_t video_read(unsigned int addr)
{
    unsigned int rom = ted.regs[0x12] & 4;

    if (rom && addr < 0x8000) {
        return ted.last_cpu_val;
    }
    return mem_get_tedmem_base((addr >> 14) | rom)[addr & 0x3fff];
}

static void video_fetch(void)
{
    unsigned int addr, ch, attr;
    unsigned int column = ted.video_column;

    ch = column < TED_SCREEN_TEXTCOLS ? ted.vbuf[column] : 0;
    attr = column < TED_SCREEN_TEXTCOLS ? ted.cbuf[column] : 0;
    if (ted.video_shifting) {
        ted.video_wait_data = ted.video_next_data;
        ted.video_wait_char = ted.video_next_char;
        ted.video_wait_attr = ted.video_next_attr;
        ted.video_wait_cursor = ted.video_next_cursor;
    }
    ted.video_next_char = ch;
    ted.video_next_attr = attr;
    ted.video_next_cursor = ted.video_position
        == (unsigned int)(((ted.regs[0x0c] & 3) << 8) | ted.regs[0x0d]);

    if (ted.video_fetching) {
        if (ted.regs[0x06] & 0x20) {
            addr = ((ted.regs[0x12] & 0x38) << 10)
                   | ((ted.video_position & 0x3ff) << 3)
                   | ted.raster.ycounter;
        } else {
            unsigned int mask = (ted.regs[0x06] & 0x40) ? 0x3f
                                : (ted.regs[0x07] & 0x80) ? 0xff : 0x7f;
            addr = ((ted.regs[0x13] & (mask == 0x7f ? 0xfc : 0xf8)) << 8)
                   | ((ch & mask) << 3) | ted.raster.ycounter;
        }
        ted.video_next_data = video_read(addr);
        if (ted.video_counting) {
            ted.video_position = (ted.video_position + 1) & 0x3ff;
        }
    }
    if (column != TED_SCREEN_TEXTCOLS) {
        ted.video_column = column == 63 ? (ted.character_fetch_on ? 0 : 40)
                                       : column + 1;
    }
    if (!ted.character_fetch_on || ted.idle_state) {
        ted.video_next_data = mem_get_tedmem_base(3 | ((ted.regs[0x13] & 1) << 2))[0x3fff];
        ted.video_next_char = ted.video_next_attr = ted.video_next_cursor = 0;
    }
}

static uint8_t video_pixel(unsigned int phase)
{
    unsigned int data, ch, attr, color;
    int mode = ((ted.regs[0x06] & 0x60) >> 4)
               | ((ted.regs[0x07] & 0x10) >> 4);
    int multicolor;

    if (phase == (ted.regs[0x07] & 7)) {
        ted.video_shift_data = ted.video_wait_data;
        ted.video_pixel_char = ted.video_wait_char;
        ted.video_pixel_attr = ted.video_wait_attr;
        ted.video_pixel_cursor = ted.video_wait_cursor;
        ted.video_shift_phase = 0;
    }
    data = ted.video_shift_data;
    ch = ted.video_pixel_char;
    attr = ted.video_pixel_attr;
    multicolor = (mode == TED_MULTICOLOR_BITMAP_MODE)
                 || (mode == TED_MULTICOLOR_TEXT_MODE && (attr & 8));
    color = ted.regs[0x19] & 0x7f;
    if (ted.video_display && !ted.raster.blank_enabled) {
        switch (mode) {
            case TED_NORMAL_TEXT_MODE:
            case TED_MULTICOLOR_TEXT_MODE:
                if (multicolor) {
                    unsigned int colors[4] = { ted.regs[0x15], ted.regs[0x16],
                                                ted.regs[0x17], attr & 0x77 };
                    color = colors[data >> 6];
                } else {
                    unsigned int bit = data >> 7;
                    if (mode == TED_NORMAL_TEXT_MODE
                        && (attr & 0x80) && !ted.cursor_visible) {
                        bit = 0;
                    }
                    if (mode == TED_NORMAL_TEXT_MODE && !(ted.regs[0x07] & 0x80)) {
                        if (ch & 0x80) {
                            bit ^= 1;
                        }
                    }
                    if (mode == TED_NORMAL_TEXT_MODE && ted.cursor_visible
                        && ted.video_pixel_cursor) {
                        bit ^= 1;
                    }
                    color = bit ? attr : ted.regs[0x15];
                }
                break;
            case TED_HIRES_BITMAP_MODE:
                color = (data & 0x80) ? (ch >> 4) | ((attr & 7) << 4)
                                     : (ch & 0x0f) | (attr & 0x70);
                break;
            case TED_MULTICOLOR_BITMAP_MODE:
                switch (data >> 6) {
                    case 0: color = ted.regs[0x15]; break;
                    case 1: color = (ch >> 4) | ((attr & 7) << 4); break;
                    case 2: color = (ch & 0x0f) | (attr & 0x70); break;
                    case 3: color = ted.regs[0x16]; break;
                }
                break;
            case TED_EXTENDED_TEXT_MODE:
                color = (data & 0x80) ? attr : ted.regs[0x15 + (ch >> 6)];
                break;
            default:
                color = 0;
                break;
        }
    }
    if (!multicolor || (ted.video_shift_phase & 1)) {
        ted.video_shift_data = data << (multicolor ? 2 : 1);
    }
    ted.video_shift_phase ^= 1;
    if (ted.video_blank) {
        color = 0;
    }
    color &= 0x7f;

    /* The oscillator is fixed by the machine. Switching TED's divider to
       the other video standard produces an incompatible colour burst; the
       TV displays luminance only (e.g. NTSC mode on a PAL Plus/4). */
    if (((ted.regs[0x07] & 0x40) != 0)
        != (ted.tv_height == TED_NTSC_SCREEN_HEIGHT)) {
        color = (color & 0x0f) ? (color & 0x70) | 1 : 0;
    }
    return color;
}

void ted_video_update(CLOCK clk)
{
    while (ted.video_clk < clk) {
        unsigned int cycle;
        unsigned int i;
        unsigned int dot = (ted.regs[0x07] & 0x40) ? 4 : 5;
        unsigned int tv_dot = ted.tv_height == TED_NTSC_SCREEN_HEIGHT ? 4 : 5;

        ted.video_clk++;
        cycle = TED_RASTER_CYCLE(ted.video_clk);
        if (ted.video_clk >= ted.counter_overflow_until) {
            if (cycle == 8) {
                ted.video_blank = 0;
            } else if (cycle == 104) {
                ted.video_blank = 1;
            }
            if (cycle == 4 && ted.video_column == TED_SCREEN_TEXTCOLS) {
                ted.video_column = 60;
            }
            if ((cycle == TED_40COL_START_CYCLE && (ted.regs[0x07] & 8))
                || (cycle == TED_38COL_START_CYCLE && !(ted.regs[0x07] & 8))) {
                ted.video_display = 1;
            }
            if ((cycle == TED_40COL_STOP_CYCLE && (ted.regs[0x07] & 8))
                || (cycle == TED_38COL_STOP_CYCLE && !(ted.regs[0x07] & 8))) {
                ted.video_display = 0;
            }
            if (cycle == 8) {
                ted.video_position = ted.chr_pos_reload;
                ted.video_counting = ted.character_fetch_on && !ted.idle_state;
            }
            if (cycle == 12) {
                ted.video_fetching = ted.character_fetch_on && !ted.idle_state;
                ted.video_shifting = ted.character_fetch_on;
            }
            if (cycle == 92) {
                ted.video_fetching = 0;
            }
            if (cycle == TED_POSITION_LATCH_CYCLE) {
                ted.video_counting = 0;
            }
            if (cycle == 94) {
                ted.video_shifting = 0;
            }
        }
        /* PAL and NTSC divide the crystal differently. Measure the beam
           in crystal-relative units, so NTSC pixels on a PAL TV are 4/5
           as wide and PAL pixels on an NTSC TV are 5/4 as wide. */
        if (dot == tv_dot) {
            CLOCK start = ted.video_beam_position / tv_dot + ted.screen_leftborderwidth;
            for (i = 0; i < 4; i++, start++) {
                uint8_t color = video_pixel((cycle & 1) * 4 + i);
                if (start >= 64 && start < TED_VIDEO_LINE_SIZE + 64) {
                    ted.video_line[start - 64] = color;
                }
            }
            ted.video_beam_position += dot * 4;
        } else {
            for (i = 0; i < 4; i++) {
                CLOCK start = ted.video_beam_position / tv_dot;
                CLOCK end;
                uint8_t color = video_pixel((cycle & 1) * 4 + i);
                ted.video_beam_position += dot;
                end = ted.video_beam_position / tv_dot;
                while (start < end) {
                    if (start + ted.screen_leftborderwidth >= 64
                        && start + ted.screen_leftborderwidth < TED_VIDEO_LINE_SIZE + 64) {
                        ted.video_line[start + ted.screen_leftborderwidth - 64] = color;
                    }
                    start++;
                }
            }
        }
        if (cycle & 1) {
            video_fetch();
        }
    }
}

void ted_video_reset(CLOCK clk)
{
    ted.video_clk = ted.video_line_clk = clk;
    ted.video_beam_position = ted.tv_height == TED_NTSC_SCREEN_HEIGHT ? 16 : 20;
    ted.video_changed = 0;
    ted.video_display = 0;
    ted.video_fetching = ted.video_shifting = 0;
    ted.video_counting = 0;
    ted.video_blank = 1;
    ted.video_position = ted.chr_pos_reload;
    ted.video_column = TED_SCREEN_TEXTCOLS;
    ted.video_next_data = ted.video_wait_data = ted.video_shift_data = 0;
    ted.video_next_char = ted.video_wait_char = ted.video_pixel_char = 0;
    ted.video_next_attr = ted.video_wait_attr = ted.video_pixel_attr = 0;
    ted.video_next_cursor = ted.video_wait_cursor = ted.video_pixel_cursor = 0;
    ted.video_shift_phase = 0;
    memset(ted.video_line, 0, sizeof(ted.video_line));
}

void ted_video_end_line(CLOCK clk)
{
    ted.video_clk = ted.video_line_clk = clk;
    ted.video_beam_position = ted.tv_height == TED_NTSC_SCREEN_HEIGHT ? 16 : 20;
    ted.video_changed = 0;
    memset(ted.video_line, ted.regs[0x19] & 0x7f, sizeof(ted.video_line));
}

void ted_video_draw_line(raster_t *raster)
{
    unsigned int width = raster->geometry->screen_size.width;
    if (ted.video_changed || (((ted.regs[0x07] & 0x40) != 0)
                             != (ted.tv_height == TED_NTSC_SCREEN_HEIGHT))) {
        memcpy(raster->draw_buffer_ptr, ted.video_line, width);
        /* The cache contains the ordinary raster picture, so a following
           line without counter changes must repaint the physical output. */
        if (raster->cache != NULL) {
            raster->cache[raster->current_line].is_dirty = 1;
        }
    }
}

int ted_video_snapshot_write(snapshot_module_t *m)
{
    return SMW_CLOCK(m, ted.video_clk) < 0
        || SMW_CLOCK(m, ted.video_line_clk) < 0
        || SMW_CLOCK(m, ted.video_beam_position) < 0
        || SMW_BA(m, ted.video_line, TED_VIDEO_LINE_SIZE) < 0
        || SMW_B(m, ted.video_changed) < 0
        || SMW_B(m, ted.video_display) < 0
        || SMW_B(m, ted.video_fetching) < 0
        || SMW_B(m, ted.video_counting) < 0
        || SMW_B(m, ted.video_shifting) < 0
        || SMW_B(m, ted.video_blank) < 0
        || SMW_W(m, ted.video_position) < 0
        || SMW_B(m, ted.video_column) < 0
        || SMW_B(m, ted.video_next_data) < 0
        || SMW_B(m, ted.video_wait_data) < 0
        || SMW_B(m, ted.video_shift_data) < 0
        || SMW_B(m, ted.video_next_char) < 0
        || SMW_B(m, ted.video_wait_char) < 0
        || SMW_B(m, ted.video_pixel_char) < 0
        || SMW_B(m, ted.video_next_attr) < 0
        || SMW_B(m, ted.video_wait_attr) < 0
        || SMW_B(m, ted.video_pixel_attr) < 0
        || SMW_B(m, ted.video_next_cursor) < 0
        || SMW_B(m, ted.video_wait_cursor) < 0
        || SMW_B(m, ted.video_pixel_cursor) < 0
        || SMW_B(m, ted.video_shift_phase) < 0 ? -1 : 0;
}

int ted_video_snapshot_read(snapshot_module_t *m)
{
    if (SMR_CLOCK(m, &ted.video_clk) < 0
        || SMR_CLOCK(m, &ted.video_line_clk) < 0
        || SMR_CLOCK(m, &ted.video_beam_position) < 0
        || SMR_BA(m, ted.video_line, TED_VIDEO_LINE_SIZE) < 0
        || SMR_B_INT(m, &ted.video_changed) < 0
        || SMR_B_INT(m, &ted.video_display) < 0
        || SMR_B_INT(m, &ted.video_fetching) < 0
        || SMR_B_INT(m, &ted.video_counting) < 0
        || SMR_B_INT(m, &ted.video_shifting) < 0
        || SMR_B_INT(m, &ted.video_blank) < 0
        || SMR_W_UINT(m, &ted.video_position) < 0
        || SMR_B_UINT(m, &ted.video_column) < 0
        || SMR_B(m, &ted.video_next_data) < 0
        || SMR_B(m, &ted.video_wait_data) < 0
        || SMR_B(m, &ted.video_shift_data) < 0
        || SMR_B(m, &ted.video_next_char) < 0
        || SMR_B(m, &ted.video_wait_char) < 0
        || SMR_B(m, &ted.video_pixel_char) < 0
        || SMR_B(m, &ted.video_next_attr) < 0
        || SMR_B(m, &ted.video_wait_attr) < 0
        || SMR_B(m, &ted.video_pixel_attr) < 0
        || SMR_B(m, &ted.video_next_cursor) < 0
        || SMR_B(m, &ted.video_wait_cursor) < 0
        || SMR_B(m, &ted.video_pixel_cursor) < 0
        || SMR_B_UINT(m, &ted.video_shift_phase) < 0) {
        return -1;
    }
    if (ted.video_clk < ted.video_line_clk || ted.video_changed > 1
        || ted.video_display > 1 || ted.video_fetching > 1
        || ted.video_counting > 1 || ted.video_shifting > 1 || ted.video_blank > 1
        || ted.video_position > 0x3ff || ted.video_column > 63
        || ted.video_next_cursor > 1 || ted.video_wait_cursor > 1
        || ted.video_pixel_cursor > 1 || ted.video_shift_phase > 1) {
        return -1;
    }
    return 0;
}
