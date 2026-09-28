/*
 * tedtypes.h - A cycle-exact event-driven TED emulation.
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

#ifndef VICE_TEDTYPES_H
#define VICE_TEDTYPES_H

#include "raster.h"
#include "types.h"

/* Screen constants.  */
#define TED_SCREEN_XPIX                 320
#define TED_SCREEN_YPIX                 200
#define TED_SCREEN_TEXTCOLS             40
#define TED_SCREEN_TEXTLINES            25

/* First and last lines of the 25 and 24 row display windows, in values of
   the TED raster counter.  */
#define TED_PAL_25ROW_START_LINE        4
#define TED_PAL_25ROW_STOP_LINE         0xcb
#define TED_PAL_24ROW_START_LINE        8
#define TED_PAL_24ROW_STOP_LINE         0xc7

/* NTSC mode uses the PAL values; not verified on hardware.  */
#define TED_NTSC_25ROW_START_LINE       4
#define TED_NTSC_25ROW_STOP_LINE        0xcb
#define TED_NTSC_24ROW_START_LINE       8
#define TED_NTSC_24ROW_STOP_LINE        0xc7

/* TED raster counter values */
#define TED_PAL_VSYNC_LINE              257
#define TED_NTSC_VSYNC_LINE             229

/* FIXME add negated colors as well */
#define TED_NUM_COLORS                  128


/* Video modes, numbered by ECM, BMM ($ff06 bits 6 and 5) and MCM ($ff07
   bit 4).  */
enum ted_video_mode_s {
    TED_NORMAL_TEXT_MODE,
    TED_MULTICOLOR_TEXT_MODE,
    TED_HIRES_BITMAP_MODE,
    TED_MULTICOLOR_BITMAP_MODE,
    TED_EXTENDED_TEXT_MODE,
    TED_ILLEGAL_TEXT_MODE,
    TED_ILLEGAL_BITMAP_MODE_1,
    TED_ILLEGAL_BITMAP_MODE_2,
    TED_NUM_VMODES
};

#define TED_IS_ILLEGAL_MODE(x)       ((x) >= TED_ILLEGAL_TEXT_MODE)

/* Note: we measure cycles from 0 to 113, not from 1 to 114.  */

/* Cycle # at which the TED takes the bus in a bad line (BA goes low).  */
#define TED_FETCH_CYCLE             4

/* Three single clocks of BA warning before TED owns the memory bus. */
#define TED_DMA_BUS_DELAY           6

/* Cycle # at which the CPU runs again after the DMA of a bad line.  */
#define TED_DMA_END_CYCLE           (TED_FETCH_CYCLE + TED_DMA_BUS_DELAY + TED_SCREEN_TEXTCOLS * 2)

/* Attribute and character bytes are fetched for character i at cycle
   TED_DMA_SLOT_CYCLE + 2 * i.  */
#define TED_DMA_SLOT_CYCLE          12

/* Cycle at which the DMA and bitmap positions are latched for the next
   character row (see ted-counter.c).  */
#define TED_POSITION_LATCH_CYCLE    90

/* Cycle at which the incremented raster line is latched for the vertical
   window tests.  */
#define TED_LINE_LATCH_CYCLE        112

/* The blink counter ($ff1f bits 3-6) is incremented once per frame on this
   line.  The data sheet lists "Increment Blink" at dot 336; FPGATED, whose
   horizontal counter is 0 at the start of the 40 column window (cycle 16),
   applies it at count 352, two single clocks later.  */
#define TED_BLINK_LINE              205
#define TED_BLINK_CYCLE             104

/* Cycle at which TED raises the raster interrupt, on every line including
   line 0.  YapeSDL and plus4emu raise it here, when the line counter has
   changed; FPGATED one cycle earlier.  The 7501's own delay is applied by
   `ted_delay_irq_clk()'.  */
#define TED_RASTER_IRQ_CYCLE        0

/* Current vertical position of the raster.  Unlike `ted.ted_raster_counter',
   which advances when the draw event of the line is served, this is correct
   at any clock.  */
#define TED_RASTER_Y(clk)           ((unsigned int)((ted.ted_raster_counter \
                                                     + (((clk) - ted.last_emulate_line_clk) \
                                                        >= 114 ? (ted.ted_raster_counter == (ted.screen_height - 1) \
                                                                  ? 1 - ted.screen_height : 1) : 0)) & 0x1ff))

/* Cycle # within the current line.  */
#define TED_RASTER_CYCLE(clk)       ((unsigned int)((clk) - ted.last_emulate_line_clk - (((clk) - ted.last_emulate_line_clk) >= 114 ? 114 : 0)))

/* Clock of the positions of the counters: while TED is frozen they stay
   at `ted.freeze_clk'.  */
#define TED_COUNTER_CLK             (ted.freeze ? ted.freeze_clk : maincpu_clk)

/* `clk' value for the beginning of the current line.  */
#define TED_LINE_START_CLK(clk)     ((CLOCK)(ted.last_emulate_line_clk + (((clk) - ted.last_emulate_line_clk) >= 114UL ? 114UL : 0UL)))

/* # of the previous raster line.  Handles wrap over.
   FIXME: after a counter write beyond the last line it can be 511.  */
#define TED_PREVIOUS_LINE(line)  (((line) > 0) ? (line) - 1 : ted.screen_height - 1)

/* DMA line range, in values of the TED raster counter.  */
#define TED_PAL_FIRST_DMA_LINE      0x0
#define TED_PAL_LAST_DMA_LINE       0xcb

/* NTSC mode uses the PAL values; not verified on hardware.  */
#define TED_NTSC_FIRST_DMA_LINE     0x0
#define TED_NTSC_LAST_DMA_LINE      0xcb

/* TED structures.  This is meant to be used by TED modules
   *exclusively*!  */

struct alarm_s;

struct ted_s {
    /* Flag: Are we initialized?  */
    int initialized;

    /* TED raster.  */
    raster_t raster;

    /* TED registers.  */
    uint8_t regs[64];

    /* Timer 1 reload latch */
    CLOCK t1_start;

    /* Timers running status */

    unsigned int timer_running[3];

    /* Interrupt register.  */
    int irq_status;

    /* Line for raster compare IRQ.  */
    unsigned int raster_irq_line;

    /* Video memory pointers.  */
    uint8_t *screen_ptr;
    uint8_t *chargen_ptr;
    uint8_t *bitmap_ptr;
    uint8_t *color_ptr;

    /* Screen memory buffers (chars and color).  */
    uint8_t vbuf[TED_SCREEN_TEXTCOLS];
    uint8_t cbuf[TED_SCREEN_TEXTCOLS];
    uint8_t cbuf_tmp[TED_SCREEN_TEXTCOLS];

    /* If this flag is set, bad lines (DMA's) can happen.  */
    int allow_bad_lines;

    /* Flag: are we in idle state? */
    int idle_state;

    /* Which display line is drawn? */
    unsigned int tv_current_line;
    unsigned int ted_raster_counter;
    /* Scanline latched for DMA; $ff1c/$ff1d only change the live counter. */
    unsigned int dma_line;

    /* Non-zero once the fetch cycle of the current line has passed; 2 if
       it also fetched character data, halting the CPU.  */
    int memory_fetch_done;

    /* Horizontal-event state.  Clocks use the CPU double-clock unit. */
    CLOCK counter_clk;
    CLOCK counter_overflow_until;
    /* A horizontal counter write skipped the vertical counter increment:
       the line ends with its number unchanged.  */
    int line_repeat;
    /* A horizontal counter write skipped the switch of a clock flip-flop,
       which keeps its state until the given clock; `clock_hold_end' is
       the later of the two.  */
    CLOCK fetch_clock_hold_end;
    int fetch_clock_hold;
    CLOCK refresh_clock_hold_end;
    int refresh_clock_hold;
    CLOCK clock_hold_end;
    int counter_increment;
    /* Enabled by the first attribute fetch, independently of bitmap fetch. */
    int row_counter_active;

    /* Internal memory pointer (VCBASE).  */
    int memptr;
    int memptr_col;

    /* Internal memory counter (VC).  */
    int mem_counter;
    /* Character-position reload register ($ff1a/$ff1b). */
    int chr_pos_reload;
    /* Current bitmap fetch position. */
    int chr_pos_count;

    /* The row counter was 6 when the current line began: the bitmap
       position is latched at TED_POSITION_LATCH_CYCLE.  */
    int chr_pos_latch;

    /* Is the cursor visible?  */
    int cursor_visible;

    /* Cursor interval counter.  */
    int cursor_phase;

    /* Cursor position.  */
    int crsrpos;

    /* Character DMA follows the attribute request on the preceding line. */
    int matrix_fetch_pending;

    /* Data to display in idle state: the byte at $ffff.  */
    int idle_data;

    /* TED keyboard read value.  */
    uint8_t kbdval;

    /* All the TED logging goes here.  */
    signed int log;

    /* TED alarms.  */
    struct alarm_s *raster_fetch_alarm;
    struct alarm_s *raster_draw_alarm;
    struct alarm_s *raster_irq_alarm;

    /* $FF07 bit 5 (freeze) stops the horizontal and vertical counters and
       the timers.  While `freeze' is set, the TED clocks after
       `freeze_clk' move forward in whole single clocks, so the counters
       stay where they stopped.  The TV, without sync, keeps drawing lines
       every `cycles_per_line' clocks from `tv_line_clk'.  */
    int freeze;
    CLOCK freeze_clk;
    CLOCK tv_line_clk;
    struct alarm_s *tv_line_alarm;

    /* Clock cycle for the next "raster fetch" alarm.  */
    CLOCK fetch_clk;

    /* Clock at which the last CPU write seen by TED ended.  A write whose
       previous access ended here follows another write (RMW, stack
       pushes), not a read.  */
    CLOCK cpu_write_end_clk;

    /* Clock cycle for the next "raster draw" alarm.  */
    CLOCK draw_clk;

    /* Clock value for raster compare IRQ.  */
    CLOCK raster_irq_clk;

    /* Clock at which the current line started.  */
    CLOCK last_emulate_line_clk;

    /* Geometry and timing parameters of the selected TED emulation.  */
    /* Lines of a frame in the mode selected by $FF07 bit 6.  */
    unsigned int screen_height;
    int first_displayed_line;
    int last_displayed_line;

    unsigned int row_25_start_line;
    unsigned int row_25_stop_line;
    unsigned int row_24_start_line;
    unsigned int row_24_stop_line;

    int screen_leftborderwidth;
    int screen_rightborderwidth;

    int cycles_per_line;
    int draw_cycle;

    unsigned int first_dma_line;
    unsigned int last_dma_line;

    /* Line starting the vertical sync in the mode of $FF07 bit 6.  */
    unsigned int vsync_line;

    /* Lines of a frame and vertical sync line of the video standard of the
       crystal, which the TV and the canvas follow.  */
    unsigned int tv_height;
    unsigned int tv_vsync_line;

    /* TED clock mode.  */
    unsigned int fastmode;

    int character_fetch_on;

    /* Last value read from TED (used for RMW access).  */
    uint8_t last_read;

    /* Video chip capabilities.  */
    struct video_chip_cap_s *video_chip_cap;

    /* Last (data) value read or written by the CPU */
    uint8_t last_cpu_val;

    unsigned int int_num;
};

typedef struct ted_s ted_t;

extern ted_t ted;

/* Private function calls, used by the other TED modules.  */
void ted_update_memory_ptrs(void);
void ted_raster_draw_alarm_handler(CLOCK offset, void *data);
void ted_delay_clk(void);
void ted_delay_oldclk(CLOCK num);
void ted_delay_resync(void);
void ted_delay_hold_clock(unsigned int from, unsigned int to);
void ted_freeze_update(void);
int ted_freeze_defers(const CLOCK *clk);
int ted_dma_halts_cpu(CLOCK clk, int after_write);
CLOCK ted_delay_irq_clk(CLOCK clk);

/* Debugging options.  */

/* #define TED_RASTER_DEBUG */
/* #define TED_REGISTERS_DEBUG */

#ifdef TED_RASTER_DEBUG
#define TED_DEBUG_RASTER(x) log_printf x
#else
#define TED_DEBUG_RASTER(x)
#endif

#ifdef TED_REGISTERS_DEBUG
#define TED_DEBUG_REGISTER(x) log_printf x
#else
#define TED_DEBUG_REGISTER(x)
#endif

#endif
