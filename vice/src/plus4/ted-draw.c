/*
 * ted-draw.c - Rendering for the TED emulation.
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

#include "maincpu.h"
#include "raster-line.h"
#include "snapshot.h"
#include "ted-draw.h"
#include "ted-timing.h"
#include "tedtypes.h"
#include "viewport.h"

/* A line emits at most 456 output positions; ted_draw_line() fills the rest
   of the canvas with the border.  The buffer must hold the widest canvas,
   NTSC with debug borders.  Snapshots keep the first 512 positions. */
#define TED_DRAW_LINE_SIZE  (TED_SCREEN_XPIX + TED_SCREEN_NTSC_DEBUG_LEFTBORDERWIDTH \
                             + TED_SCREEN_NTSC_DEBUG_RIGHTBORDERWIDTH)
#define TED_DRAW_SNAPSHOT_LINE  512

/* A line is 114 double clocks of four dots.  The horizontal counter starts
   it at LINE_START_DOT, TED_DRAW_DISPLAY_START dots before the first display
   dot (0), and wraps from 455 to 0, or from 511 after a $ff1e write beyond
   the line.  Such a write also delays the end of the line: output
   positions stop at MAX_X, past every canvas.  */
#define LINE_CLOCKS         114
#define LINE_DOTS           (LINE_CLOCKS * 4)
#define LINE_START_DOT      (LINE_DOTS - TED_DRAW_DISPLAY_START)
#define COUNTER_DOTS        512
#define MAX_X               576

/* Dots at which the side border flip-flop tests CSEL ($ff07 bit 3), each
   only for its own width: the display opens at dot 0 (cycle 16) in 40
   column mode or 8 (cycle 18) in 38 column mode, if the vertical window is
   open, and closes at dot 312 (cycle 94) in 38 column mode or 320 (cycle
   96) in 40 column mode.  */
#define DOT_40COL_START     0
#define DOT_38COL_START     8
#define DOT_38COL_STOP      312
#define DOT_40COL_STOP      320

/* From two cells before the display the shifter loads the empty cells
   fetched outside the display.  */
#define EMPTY_LOAD_DOT      (LINE_DOTS - 16)

/* The dot counter and the TV's output position are separate.  A write to
   $ff1e changes the former after one dot, never pixels already emitted.
   CLOCK is the double CPU clock (four dots); sub is the dot within it.
   Runs stop at latch, shift-register load, border and register events.
   There is one rendering path, including lines without register writes. */
typedef struct {
    CLOCK clk;
    unsigned int sub, h, x;
    uint8_t control1, control2, scroll;
    uint8_t bits, attr, character, pair;
    uint8_t waiting_bits, waiting_attr, waiting_char;
    uint8_t palette[5];
    int border;
    int pending_color, pending_counter;
    uint8_t color_value, counter_value;
    unsigned int delay;
    uint8_t line[TED_DRAW_LINE_SIZE];
} ted_beam_t;

static ted_beam_t beam;

/* Zero while the raster does not show the current TV line: its pixels are
   not generated, only the latches advance.  */
static int line_shown = 1;


/* Dot at which the side border closes with the current width.  */
static unsigned int display_stop(void)
{
    return (ted.regs[7] & 8) ? DOT_40COL_STOP : DOT_38COL_STOP;
}

/* Non-zero if the cursor is in one of `count' cells from `first'.  Cell
   addresses wrap at 1024, like the video matrix counter.  */
static int cursor_in_cells(unsigned int first, unsigned int count)
{
    return ted.cursor_visible
           && (((unsigned int)(ted.crsrpos - ted.memptr) - first) & 0x3ff) < count;
}

/* Host-order byte masks.  They select colours, not precoloured characters:
   a palette write can split even the two dots of a multicolour pixel. */
static uint64_t mono[256], multi[4][256];
static int tables_ready;

static void build_tables(void)
{
    unsigned int b, x, c;
    uint8_t bytes[8];

    if (tables_ready) {
        return;
    }
    for (b = 0; b < 256; b++) {
        for (x = 0; x < 8; x++) {
            bytes[x] = (b & (0x80 >> x)) ? 255 : 0;
        }
        memcpy(&mono[b], bytes, 8);
        for (c = 0; c < 4; c++) {
            for (x = 0; x < 8; x++) {
                bytes[x] = ((b >> (6 - (x & ~1))) & 3) == c ? 255 : 0;
            }
            memcpy(&multi[c][b], bytes, 8);
        }
    }
    tables_ready = 1;
}

static uint64_t repeat_color(unsigned int c)
{
    return UINT64_C(0x0101010101010101) * c;
}

static unsigned int mode(void)
{
    return ((beam.control1 & 0x60) | (beam.control2 & 0x10)) >> 4;
}

static int multicolor(void)
{
    return (beam.control2 & 0x10)
           && ((beam.control1 & 0x60) || (beam.attr & 8));
}

/* Read once into the video latch, before loading the pixel shift register.
   CPU stores flush the pipeline first, so later RAM writes cannot alter a
   byte already being shifted.  Address and row changes affect later reads. */
static void fetch_cell(unsigned int cell)
{
    unsigned int index, address;

    beam.waiting_char = beam.waiting_attr = beam.waiting_bits = 0;
    if (cell >= TED_SCREEN_TEXTCOLS) {
        if (ted.idle_state) {
            beam.waiting_bits = (uint8_t)ted.idle_data;
        }
        return;
    }
    if (ted.idle_state || !ted.character_fetch_on) {
        /* No matrix/character fetch means no replacement colour attributes.
           Preserve them through the vertical border as well as the horizontal
           gap; otherwise the first scrolled display line acquires black dots. */
        beam.waiting_attr = beam.attr;
        beam.waiting_char = beam.character;
        if (ted.idle_state) {
            beam.waiting_bits = (uint8_t)ted.idle_data;
        }
        return;
    }
    beam.waiting_char = ted.vbuf[cell];
    beam.waiting_attr = ted.cbuf[cell];
    if (beam.control1 & 0x20) {
        address = (((ted.memptr + cell) << 3) | ted.raster.ycounter) & 0x1fff;
        if (ted.bitmap_ptr) {
            beam.waiting_bits = ted.bitmap_ptr[address];
        }
    } else {
        index = beam.waiting_char;
        if (beam.control1 & 0x40) {
            index &= 0x3f;
        } else if (!(beam.control2 & 0x80)) {
            index &= 0x7f;
        }
        if (ted.chargen_ptr) {
            beam.waiting_bits = ted.chargen_ptr[(index << 3) | ted.raster.ycounter];
        }
    }
    if (mode() == TED_NORMAL_TEXT_MODE && ted.cursor_visible
        && ((ted.memptr + cell) & 0x3ff) == (unsigned int)ted.crsrpos) {
        beam.waiting_bits ^= 0xff;
    }
}

static void dot_events(void)
{
    unsigned int h = beam.h;

    if ((h & 7) == 0) {
        beam.control1 = ted.regs[6];
        beam.control2 = ted.regs[7];
        beam.scroll = beam.control2 & 7;
        fetch_cell(h < TED_SCREEN_XPIX ? h >> 3 : TED_SCREEN_TEXTCOLS);
    }
    if (h == ((ted.regs[7] & 8) ? DOT_40COL_START : DOT_38COL_START)
        && !ted.raster.blank_enabled) {
        beam.border = 0;
    }
    if (h == display_stop()) {
        beam.border = 1;
    }
    if ((h < TED_SCREEN_XPIX || h >= EMPTY_LOAD_DOT) && (h & 7) == beam.scroll) {
        beam.bits = beam.waiting_bits;
        /* Empty pre-display shifts drain pixel data, not the colour
           attributes of the last displayed cell.  In hires bitmap mode
           those attributes still select the zero-bit colour until the
           first new cell is loaded, including a horizontal scroll gap. */
        if (h < TED_SCREEN_XPIX) {
            beam.attr = beam.waiting_attr;
            beam.character = beam.waiting_char;
        }
        beam.pair = 0;
    }
}

static uint64_t pixel_colors(void)
{
    unsigned int m = mode();
    unsigned int b = beam.bits;
    unsigned int c0 = beam.palette[0], c1, c2, c3;

    if (beam.border) {
        return repeat_color(beam.palette[4]);
    }
    if (TED_IS_ILLEGAL_MODE(m)) {
        return 0;
    }
    if (m == TED_HIRES_BITMAP_MODE) {
        c0 = (beam.character & 15) | (beam.attr & 0x70);
        c1 = (beam.character >> 4) | ((beam.attr & 7) << 4);
    } else if (m == TED_MULTICOLOR_BITMAP_MODE) {
        c1 = (beam.character >> 4) | ((beam.attr & 7) << 4);
        c2 = (beam.character & 15) | (beam.attr & 0x70);
        c3 = beam.palette[1];
        return (multi[0][b] & repeat_color(c0))
             | (multi[1][b] & repeat_color(c1))
             | (multi[2][b] & repeat_color(c2))
             | (multi[3][b] & repeat_color(c3));
    } else if (m == TED_MULTICOLOR_TEXT_MODE && (beam.attr & 8)) {
        return (multi[0][b] & repeat_color(c0))
             | (multi[1][b] & repeat_color(beam.palette[1]))
             | (multi[2][b] & repeat_color(beam.palette[2]))
             | (multi[3][b] & repeat_color(beam.attr & 0x77));
    } else {
        c1 = beam.attr & (m == TED_MULTICOLOR_TEXT_MODE ? 0x77 : 0x7f);
        if (m == TED_EXTENDED_TEXT_MODE) {
            c0 = beam.palette[beam.character >> 6];
        } else if (m == TED_NORMAL_TEXT_MODE) {
            if ((beam.attr & 0x80) && !ted.cursor_visible) {
                b = 0;
            }
            if (!(beam.control2 & 0x80) && (beam.character & 0x80)) {
                b ^= 0xff;
            }
        }
    }
    return (mono[b] & repeat_color(c1)) | (~mono[b] & repeat_color(c0));
}

/* Emit at most eight dots.  memcpy handles unaligned output on all hosts. */
static void emit(unsigned int n)
{
    uint64_t pixels = pixel_colors();
    unsigned int skip = multicolor() ? beam.pair : 0;
    unsigned int x = beam.x;
    int dest = (int)x + ted.screen_leftborderwidth - TED_DRAW_DISPLAY_START;
    unsigned int count = n;

    if (dest < 0) {
        unsigned int cut = (unsigned int)-dest;
        if (cut > count) {
            cut = count;
        }
        skip += cut;
        count -= cut;
        dest += cut;
    }
    if (dest >= 0 && dest < (int)sizeof(beam.line) && count) {
        if (count > sizeof(beam.line) - dest) {
            count = sizeof(beam.line) - dest;
        }
        memcpy(beam.line + dest, (uint8_t *)&pixels + skip, count);
    }
    if (multicolor()) {
        unsigned int shift = (n + beam.pair) & ~1U;
        beam.bits = shift >= 8 ? 0 : (uint8_t)(beam.bits << shift);
    } else {
        beam.bits = n >= 8 ? 0 : (uint8_t)(beam.bits << n);
    }
    beam.pair = (beam.pair + n) & 1;
    beam.x += n;
    if (beam.x > MAX_X) { beam.x = MAX_X; }
    beam.h += n;
    if (beam.h == LINE_DOTS || beam.h == COUNTER_DOTS) {
        beam.h = 0;
    }
    beam.sub += n;
    beam.clk += beam.sub >> 2;
    beam.sub &= 3;
}

/* No register or memory operation occurs inside a sync interval.  In its
   interior a complete shifter load can cross the next mode latch: that
   latch sees the same controls.  Fetch the following byte before leaving
   the interval, just as the dot event loop does. */
static int equal_bytes(const uint8_t *p, unsigned int count, uint8_t value)
{
    uint64_t word, repeated = repeat_color(value);

    while (count >= 8) {
        memcpy(&word, p, 8);
        if (word != repeated) { return 0; }
        p += 8;
        count -= 8;
    }
    while (count--) {
        if (*p++ != value) { return 0; }
    }
    return 1;
}

static void emit_cells(unsigned int count)
{
    uint64_t pixels = pixel_colors();
    unsigned int cell = beam.h >> 3;
    unsigned int m = mode();
    unsigned int i, b, attr, chr, c0, c1;
    unsigned int row = ted.raster.ycounter;
    uint8_t *p = beam.line + beam.x + ted.screen_leftborderwidth - TED_DRAW_DISPLAY_START;
    uint8_t *font = ted.chargen_ptr;
    uint8_t *bitmap = ted.bitmap_ptr;
    uint64_t bg = repeat_color(beam.palette[0]);
    memcpy(p, &pixels, 8);
    if (beam.border) {
        memset(p + 8, beam.palette[4], (count - 1) * 8);
    } else if (!(m & 2) && count >= 4 && !ted.idle_state && ted.character_fetch_on
               && !cursor_in_cells(cell + 1, count - 1)
               && equal_bytes(ted.vbuf + cell + 1, count - 1, ted.vbuf[cell + 1])
               && equal_bytes(ted.cbuf + cell + 1, count - 1, ted.cbuf[cell + 1])) {
        /* Repeated shifter loads, such as spaces and solid text rows, have
           identical pixels.  Decode once, while retaining the final latch. */
        fetch_cell(cell + 1);
        beam.bits = beam.waiting_bits;
        beam.attr = beam.waiting_attr;
        beam.character = beam.waiting_char;
        pixels = pixel_colors();
        if (pixels == repeat_color(*(uint8_t *)&pixels)) {
            memset(p + 8, *(uint8_t *)&pixels, (count - 1) * 8);
        } else {
            for (i = 1; i < count; i++) { memcpy(p + i * 8, &pixels, 8); }
        }
    } else if (m == TED_NORMAL_TEXT_MODE && !ted.idle_state && ted.character_fetch_on && font) {
        unsigned int mask = (beam.control2 & 0x80) ? 0xff : 0x7f;
        int cursor = ted.cursor_visible ? (ted.crsrpos - ted.memptr) & 0x3ff : -1;
        font += row;
        memset(p + 8, beam.palette[0], (count - 1) * 8);
        for (i = 1; i < count; i++) {
            chr = ted.vbuf[cell + i];
            b = font[(chr & mask) << 3];
            if (!b && ((mask & 0x80) || !(chr & 0x80)) && (int)(cell + i) != cursor) {
                continue;
            }
            attr = ted.cbuf[cell + i];
            if ((attr & 0x80) && !ted.cursor_visible) { b = 0; }
            if (!(mask & 0x80) && (chr & 0x80)) { b ^= 255; }
            if ((int)(cell + i) == cursor) { b ^= 255; }
            pixels = bg ^ (mono[b] & (bg ^ repeat_color(attr & 0x7f)));
            memcpy(p + i * 8, &pixels, 8);
        }
    } else if (m == TED_MULTICOLOR_TEXT_MODE && !ted.idle_state && ted.character_fetch_on && font) {
        unsigned int mask = (beam.control2 & 0x80) ? 0xff : 0x7f;
        uint64_t bg1 = bg ^ repeat_color(beam.palette[1]);
        uint64_t bg2 = bg ^ repeat_color(beam.palette[2]);
        font += row;
        for (i = 1; i < count; i++) {
            chr = ted.vbuf[cell + i];
            attr = ted.cbuf[cell + i];
            b = font[(chr & mask) << 3];
            pixels = bg ^ ((attr & 8)
                ? (multi[1][b] & bg1) ^ (multi[2][b] & bg2)
                    ^ (multi[3][b] & (bg ^ repeat_color(attr & 0x77)))
                : (mono[b] & (bg ^ repeat_color(attr & 0x77))));
            memcpy(p + i * 8, &pixels, 8);
        }
    } else if (m == TED_EXTENDED_TEXT_MODE && !ted.idle_state && ted.character_fetch_on && font) {
        uint64_t backgrounds[4];
        for (i = 0; i < 4; i++) { backgrounds[i] = repeat_color(beam.palette[i]); }
        font += row;
        for (i = 1; i < count; i++) {
            chr = ted.vbuf[cell + i];
            attr = ted.cbuf[cell + i];
            b = font[(chr & 0x3f) << 3];
            bg = backgrounds[chr >> 6];
            pixels = bg ^ (mono[b] & (bg ^ repeat_color(attr & 0x7f)));
            memcpy(p + i * 8, &pixels, 8);
        }
    } else if (m == TED_HIRES_BITMAP_MODE && !ted.idle_state && ted.character_fetch_on && bitmap) {
        unsigned int address = ((ted.memptr + cell + 1) << 3) | row;
        for (i = 1; i < count; i++, address += 8) {
            chr = ted.vbuf[cell + i];
            attr = ted.cbuf[cell + i];
            b = bitmap[address & 0x1fff];
            c0 = (chr & 15) | (attr & 0x70);
            c1 = (chr >> 4) | ((attr & 7) << 4);
            pixels = repeat_color(c0) ^ (mono[b] & repeat_color(c0 ^ c1));
            memcpy(p + i * 8, &pixels, 8);
        }
    } else if (m == TED_MULTICOLOR_BITMAP_MODE && !ted.idle_state && ted.character_fetch_on && bitmap) {
        unsigned int address = ((ted.memptr + cell + 1) << 3) | row;
        uint64_t bg3 = bg ^ repeat_color(beam.palette[1]);
        for (i = 1; i < count; i++, address += 8) {
            chr = ted.vbuf[cell + i];
            attr = ted.cbuf[cell + i];
            b = bitmap[address & 0x1fff];
            c1 = (chr >> 4) | ((attr & 7) << 4);
            c0 = (chr & 15) | (attr & 0x70);
            pixels = bg ^ (multi[1][b] & (bg ^ repeat_color(c1)))
                        ^ (multi[2][b] & (bg ^ repeat_color(c0)))
                        ^ (multi[3][b] & bg3);
            memcpy(p + i * 8, &pixels, 8);
        }
    } else {
        /* Modes with four colours and idle data share the same decoder as
           a partial shifter run.  The common two-colour modes above only
           hoist their invariant address and palette calculations. */
        for (i = 1; i < count; i++) {
            fetch_cell(cell + i);
            beam.bits = beam.waiting_bits;
            beam.attr = beam.waiting_attr;
            beam.character = beam.waiting_char;
            pixels = pixel_colors();
            memcpy(p + i * 8, &pixels, 8);
        }
    }
    fetch_cell(cell + count - 1);
    beam.attr = beam.waiting_attr;
    beam.character = beam.waiting_char;
    if (beam.scroll) { fetch_cell(cell + count); }
    beam.bits = 0;
    beam.h += count * 8;
    beam.x += count * 8;
    beam.clk += count * 2;
}

/* Blank runs have no visible shifter output.  Stop before a graphics or
   border event, and never skip a pending register latch. */
static void emit_border(unsigned int n)
{
    int dest = (int)beam.x + ted.screen_leftborderwidth - TED_DRAW_DISPLAY_START;
    unsigned int count = n;

    if (dest < 0) {
        unsigned int cut = (unsigned int)-dest;
        if (cut > count) { cut = count; }
        count -= cut;
        dest += cut;
    }
    if (dest >= 0 && dest < (int)sizeof(beam.line) && count) {
        if (count > sizeof(beam.line) - dest) { count = sizeof(beam.line) - dest; }
        memset(beam.line + dest, beam.palette[4], count);
    }
    if ((beam.h & 7) == 0 || n > 8 - (beam.h & 7)) {
        beam.control1 = ted.regs[6];
        beam.control2 = ted.regs[7];
        beam.scroll = beam.control2 & 7;
        beam.waiting_bits = ted.idle_state ? (uint8_t)ted.idle_data : 0;
        beam.waiting_attr = beam.waiting_char = 0;
    }
    if (multicolor()) {
        unsigned int shift = (n + beam.pair) & ~1U;
        beam.bits = shift >= 8 ? 0 : (uint8_t)(beam.bits << shift);
    } else {
        beam.bits = n >= 8 ? 0 : (uint8_t)(beam.bits << n);
    }
    beam.pair = (beam.pair + n) & 1;
    beam.x += n;
    if (beam.x > MAX_X) { beam.x = MAX_X; }
    beam.h += n;
    if (beam.h == LINE_DOTS || beam.h == COUNTER_DOTS) { beam.h = 0; }
    beam.sub += n;
    beam.clk += beam.sub >> 2;
    beam.sub &= 3;
}

/* Batch a complete, uninterrupted horizontal period.  The same shifter
   decoder is used as for partial runs.  There are no writes, RAM mutations,
   counter jumps or pending latches in this interval, so its fixed event
   sequence can be reduced to one graphics span and two border spans. */
static void emit_period(void)
{
    CLOCK start = beam.clk;
    unsigned int left = ted.screen_leftborderwidth;
    unsigned int end = left + LINE_START_DOT;
    unsigned int stop, gap;

    beam.control1 = ted.regs[6];
    beam.control2 = ted.regs[7];
    beam.scroll = beam.control2 & 7;
    if (end > sizeof(beam.line)) { end = sizeof(beam.line); }
    if (!ted.raster.blank_enabled && line_shown) {
        memset(beam.line, beam.palette[4], left);
        beam.border = 0;
        beam.bits = 0;
        {
            uint64_t pixels = pixel_colors();
            gap = *(uint8_t *)&pixels;
        }
        memset(beam.line + left, gap, beam.scroll);
        beam.h = beam.scroll;
        beam.x = TED_DRAW_DISPLAY_START + beam.scroll;
        beam.pair = 0;
        fetch_cell(0);
        beam.bits = beam.waiting_bits;
        beam.attr = beam.waiting_attr;
        beam.character = beam.waiting_char;
        emit_cells(TED_SCREEN_TEXTCOLS);
        if (!(ted.regs[7] & 8)) {
            memset(beam.line + left, beam.palette[4], DOT_38COL_START);
        }
        stop = left + display_stop();
        memset(beam.line + stop, beam.palette[4], end - stop);
    } else {
        /* Border or hidden line: only the latches advance.  */
        if (line_shown) {
            memset(beam.line, beam.palette[4], end);
        }
        fetch_cell(39);
        beam.attr = beam.waiting_attr;
        beam.character = beam.waiting_char;
    }
    beam.clk = start + LINE_CLOCKS;
    beam.h = LINE_START_DOT;
    beam.x = LINE_DOTS;
    beam.bits = 0;
    beam.pair = beam.scroll & 1;
    beam.border = 1;
    beam.waiting_bits = ted.idle_state ? (uint8_t)ted.idle_data : 0;
    beam.waiting_attr = beam.waiting_char = 0;
}

static void run_dots(uint64_t remaining)
{
    unsigned int n, load;

    if (remaining == LINE_DOTS && !beam.sub && beam.h == LINE_START_DOT && beam.x == 0
        && !beam.delay && beam.border
        && (!ted.idle_state || ted.raster.blank_enabled)
        && ted.screen_leftborderwidth >= 0 && ted.screen_leftborderwidth <= TED_DRAW_DISPLAY_START) {
        emit_period();
        return;
    }
    while (remaining) {
        if (!beam.delay && beam.border
            && beam.h >= DOT_40COL_STOP + 8 && beam.h < EMPTY_LOAD_DOT
            && beam.control1 == ted.regs[6] && beam.control2 == ted.regs[7]) {
            n = EMPTY_LOAD_DOT - beam.h;
            if (remaining < n) { n = (unsigned int)remaining; }
            emit_border(n);
            remaining -= n;
            continue;
        }
        dot_events();
        if (!beam.delay && remaining >= 8 && beam.h < display_stop()
            && (beam.h & 7) == beam.scroll && !beam.pair
            && beam.control1 == ted.regs[6] && beam.control2 == ted.regs[7]) {
            int dest = (int)beam.x + ted.screen_leftborderwidth - TED_DRAW_DISPLAY_START;
            /* Stop at the next border event.  */
            unsigned int stop = beam.h < DOT_38COL_START && !(ted.regs[7] & 8)
                                ? DOT_38COL_START : display_stop();

            n = (stop - beam.h) / 8;
            if (remaining / 8 < n) { n = (unsigned int)(remaining / 8); }
            if (n && dest >= 0 && dest + n * 8 <= sizeof(beam.line)) {
                emit_cells(n);
                remaining -= n * 8;
                continue;
            }
        }
        n = 8 - (beam.h & 7);
        load = (beam.scroll - beam.h) & 7;
        if (load && load < n) {
            n = load;
        }
        if (multicolor() && beam.pair && n == 8) {
            n = 7;
        }
        if (remaining < n) {
            n = (unsigned int)remaining;
        }
        if (beam.delay && beam.delay < n) {
            n = beam.delay;
        }
        emit(n);
        remaining -= n;
        if (beam.delay) {
            beam.delay -= n;
            if (!beam.delay) {
                if (beam.pending_color >= 0) {
                    beam.palette[beam.pending_color] = beam.color_value;
                    beam.pending_color = -1;
                }
                if (beam.pending_counter) {
                    beam.h = ((~beam.counter_value & 0xfc) << 1) | (beam.h & 7);
                    beam.pending_counter = 0;
                }
            }
        }
    }
}

void ted_draw_sync(CLOCK clk)
{
    if (ted.freeze || clk <= beam.clk) {
        return;
    }
    run_dots((uint64_t)(clk - beam.clk) * 4 - beam.sub);
}

void ted_draw_store(unsigned int addr, uint8_t value)
{
    /* Timers, audio, keyboard and IRQ registers do not feed the pixel
       pipeline.  They must not split otherwise uninterrupted video runs. */
    if (addr < 6 || (addr >= 8 && addr <= 0x0b)
        || (addr >= 0x0e && addr <= 0x11) || (addr >= 0x20 && addr < 0x3e)) {
        return;
    }
    ted_draw_sync(maincpu_clk);
    if (addr >= 0x15 && addr <= 0x19) {
        if (beam.pending_color >= 0) {
            beam.palette[beam.pending_color] = beam.color_value;
        }
        beam.pending_color = (int)addr - 0x15;
        beam.palette[beam.pending_color] = 0x7f;
        beam.color_value = value & 0x7f;
        beam.delay = 1;
    } else if (addr == 0x1e) {
        beam.pending_counter = 1;
        beam.counter_value = value;
        beam.delay = 1;
    } else if (!ted.freeze && beam.clk == maincpu_clk && beam.sub == 0) {
        /* The horizontal comparisons at the bus edge see the old control
           bits.  New mode/scroll bits enter the next single-clock latch. */
        run_dots(1);
    }
}

/* Start a TV line at `clk'.  `shown' is zero if the raster does not show
   it.  */
void ted_draw_begin_line(CLOCK clk, int shown)
{
    beam.clk = clk;
    beam.sub = 0;
    beam.h = LINE_START_DOT;
    beam.x = 0;
    line_shown = shown;
}

void ted_draw_reset(void)
{
    unsigned int i;

    memset(&beam, 0, sizeof(beam));
    beam.pending_color = -1;
    beam.border = 1;
    for (i = 0; i < 5; i++) {
        beam.palette[i] = ted.regs[0x15 + i] & 0x7f;
    }
    ted_draw_begin_line(ted.last_emulate_line_clk, 1);
}

void ted_draw_init(void)
{
    build_tables();
    ted_draw_reset();
}

void ted_draw_line(CLOCK clk, int visible)
{
    ted_draw_sync(clk);
    if (visible) {
        int end = (int)beam.x + ted.screen_leftborderwidth - TED_DRAW_DISPLAY_START;
        unsigned int width = ted.raster.geometry->screen_size.width;
        if (end < 0) { end = 0; }
        if ((unsigned int)end < width) {
            memset(beam.line + end, beam.palette[4], width - end);
        }
        raster_line_emulate_pixels(&ted.raster, beam.line);
    }
}

void ted_draw_black_line(void)
{
    static const uint8_t black[TED_DRAW_LINE_SIZE];
    raster_line_emulate_pixels(&ted.raster, black);
}

void ted_draw_freeze(CLOCK delta)
{
    beam.clk += delta;
    /* The TV has scanned other lines meanwhile.  */
    line_shown = 1;
}

/* The canvas geometry changed during the current line.  */
void ted_draw_canvas_changed(void)
{
    line_shown = 1;
}

/* Snapshots store the unfinished line and every latch, never native structs. */
int ted_draw_snapshot_write(snapshot_module_t *m)
{
    if (SMW_CLOCK(m, beam.clk) < 0
        || SMW_DW(m, beam.sub) < 0
        || SMW_DW(m, beam.h) < 0
        || SMW_DW(m, beam.x) < 0
        || SMW_B(m, beam.control1) < 0
        || SMW_B(m, beam.control2) < 0
        || SMW_B(m, beam.scroll) < 0
        || SMW_B(m, beam.bits) < 0
        || SMW_B(m, beam.attr) < 0
        || SMW_B(m, beam.character) < 0
        || SMW_B(m, beam.pair) < 0
        || SMW_B(m, beam.waiting_bits) < 0
        || SMW_B(m, beam.waiting_attr) < 0
        || SMW_B(m, beam.waiting_char) < 0
        || SMW_B(m, beam.color_value) < 0
        || SMW_B(m, beam.counter_value) < 0
        || SMW_BA(m, beam.palette, 5) < 0
        || SMW_B(m, (uint8_t)beam.border) < 0
        || SMW_B(m, (uint8_t)(beam.pending_color + 1)) < 0
        || SMW_B(m, (uint8_t)beam.pending_counter) < 0
        || SMW_B(m, (uint8_t)beam.delay) < 0
        || SMW_BA(m, beam.line, TED_DRAW_SNAPSHOT_LINE) < 0) {
        return -1;
    }
    return 0;
}

int ted_draw_snapshot_read(snapshot_module_t *m)
{
    ted_beam_t saved;
    uint8_t border, color, counter, delay;
    unsigned int i;

    memset(&saved, 0, sizeof(saved));
    if (SMR_CLOCK(m, &saved.clk) < 0
        || SMR_DW(m, &saved.sub) < 0
        || SMR_DW(m, &saved.h) < 0
        || SMR_DW(m, &saved.x) < 0
        || SMR_B(m, &saved.control1) < 0
        || SMR_B(m, &saved.control2) < 0
        || SMR_B(m, &saved.scroll) < 0
        || SMR_B(m, &saved.bits) < 0
        || SMR_B(m, &saved.attr) < 0
        || SMR_B(m, &saved.character) < 0
        || SMR_B(m, &saved.pair) < 0
        || SMR_B(m, &saved.waiting_bits) < 0
        || SMR_B(m, &saved.waiting_attr) < 0
        || SMR_B(m, &saved.waiting_char) < 0
        || SMR_B(m, &saved.color_value) < 0
        || SMR_B(m, &saved.counter_value) < 0
        || SMR_BA(m, saved.palette, 5) < 0
        || SMR_B(m, &border) < 0
        || SMR_B(m, &color) < 0
        || SMR_B(m, &counter) < 0
        || SMR_B(m, &delay) < 0
        || SMR_BA(m, saved.line, TED_DRAW_SNAPSHOT_LINE) < 0) {
        return -1;
    }
    if (saved.clk > maincpu_clk || saved.sub > 3 || saved.h >= COUNTER_DOTS || saved.x > MAX_X
        || saved.scroll > 7 || saved.pair > 1 || border > 1
        || color > 5 || counter > 1 || delay > 1
        || ((color || counter) && !delay) || saved.color_value > 127) {
        snapshot_set_error(SNAPSHOT_MODULE_INCOMPATIBLE);
        return -1;
    }
    for (i = 0; i < 5; i++) {
        if (saved.palette[i] > 127) {
            snapshot_set_error(SNAPSHOT_MODULE_INCOMPATIBLE);
            return -1;
        }
    }
    for (i = 0; i < TED_DRAW_SNAPSHOT_LINE; i++) {
        if (saved.line[i] > 127) {
            snapshot_set_error(SNAPSHOT_MODULE_INCOMPATIBLE);
            return -1;
        }
    }
    saved.border = border;
    saved.pending_color = (int)color - 1;
    saved.pending_counter = counter;
    saved.delay = delay;
    beam = saved;
    line_shown = 1;
    build_tables();
    return 0;
}

void ted_draw_snapshot_legacy(void)
{
    unsigned int cycle = TED_RASTER_CYCLE(maincpu_clk);

    ted_draw_reset();
    beam.clk = maincpu_clk;
    beam.h = (LINE_START_DOT + 4 * cycle) % LINE_DOTS;
    beam.x = 4 * cycle;
    beam.border = ted.raster.blank_enabled || beam.h >= DOT_40COL_STOP;
}
