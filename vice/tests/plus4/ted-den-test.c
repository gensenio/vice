/* Regression for a $ff06 write with DEN set on raster line 0.  If the
   display was enabled at the start of the frame, the write must not
   initialize it again: the row counter keeps its start-of-frame value.  If
   the display was disabled, the write enables it and the row counter starts
   at 7, as at the start of a frame, because attributes precede character
   data by one line.  Exercise the production register handler for every
   cycle of line 0. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/plus4/ted-mem.c"

ted_t ted;
CLOCK maincpu_clk;

static raster_changes_t foreground;
static raster_changes_t next_line;
static raster_changes_all_t changes;
static geometry_t geometry;

void ted_update_memory_ptrs(unsigned int cycle)
{
}

void ted_update_video_mode(unsigned int cycle)
{
}

void ted_badline_check_state(uint8_t value, const int cycle,
                             const unsigned int line)
{
    /* The vertical scroll does not change in these writes. */
    assert(0);
}

static void setup(int enabled)
{
    memset(&ted, 0, sizeof(ted));
    memset(&foreground, 0, sizeof(foreground));
    memset(&next_line, 0, sizeof(next_line));
    memset(&changes, 0, sizeof(changes));
    changes.foreground = &foreground;
    changes.next_line = &next_line;
    geometry.text_size.width = TED_SCREEN_TEXTCOLS;
    ted.raster.changes = &changes;
    ted.raster.geometry = &geometry;
    ted.screen_height = 312;
    ted.first_dma_line = TED_PAL_FIRST_DMA_LINE;
    ted.last_dma_line = TED_PAL_LAST_DMA_LINE;
    ted.row_25_start_line = TED_PAL_25ROW_START_LINE;
    ted.row_25_stop_line = TED_PAL_25ROW_STOP_LINE;
    ted.row_24_start_line = TED_PAL_24ROW_START_LINE;
    ted.row_24_stop_line = TED_PAL_24ROW_STOP_LINE;
    ted.dma_line = 0;
    ted.ted_raster_counter = 0;
    /* State left by the start of the frame (ted_raster_draw_alarm_handler). */
    ted.regs[0x06] = enabled ? 0x1b : 0x0b;
    ted.raster.ysmooth = 3;
    ted.raster.blank = !enabled;
    ted.allow_bad_lines = enabled;
    ted.character_fetch_on = enabled;
    ted.raster.ycounter = enabled ? 7 : 0;
    ted.draw_ycounter = ted.raster.ycounter;
}

int main(void)
{
    unsigned int cycle;

    for (cycle = 0; cycle < 114; cycle++) {
        /* Already enabled: e.g. Return to Promised Land rewrites $ff06 on
           line 0 of every frame.  */
        setup(1);
        maincpu_clk = cycle;
        ted06_store(0x3b);
        assert(ted.raster.ycounter == 7);
        assert(ted.draw_ycounter == 7);
        assert(ted.allow_bad_lines && ted.character_fetch_on);

        /* Enabled during line 0.  */
        setup(0);
        maincpu_clk = cycle;
        ted06_store(0x1b);
        assert(ted.raster.ycounter == 7);
        assert(ted.draw_ycounter == 7);
        assert(ted.allow_bad_lines && ted.character_fetch_on);
        assert(!ted.raster.blank);
    }

    puts("TED display enable tests passed");
    return 0;
}
