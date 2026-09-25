/* Regression for the TED side and vertical border flip-flops.  CSEL is
   tested at cycle 16 (40 columns) or 18 (38 columns) to start the display
   and at cycle 94 (38 columns) or 96 (40 columns) to stop it; each test
   only fires for its own width.  The vertical window opens on line 4 or 8
   and closes on line 200 or 204, also when RSEL/DEN change on those lines.
   Exercise the production register handlers for every cycle. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/plus4/ted-mem.c"

ted_t ted;

static raster_changes_t next_line;
static raster_changes_all_t changes;

static void setup(uint8_t ff06, uint8_t ff07, int blank_enabled)
{
    memset(&ted.raster, 0, sizeof(ted.raster));
    memset(&next_line, 0, sizeof(next_line));
    changes.next_line = &next_line;
    ted.raster.changes = &changes;
    ted.screen_leftborderwidth = 32;
    ted.screen_height = 312;
    ted.row_25_start_line = TED_PAL_25ROW_START_LINE;
    ted.row_25_stop_line = TED_PAL_25ROW_STOP_LINE;
    ted.row_24_start_line = TED_PAL_24ROW_START_LINE;
    ted.row_24_stop_line = TED_PAL_24ROW_STOP_LINE;
    ted.regs[0x06] = ff06;
    ted.regs[0x07] = ff07;
    if (ff07 & 8) {
        ted.raster.display_xstart = TED_40COL_START_PIXEL;
        ted.raster.display_xstop = TED_40COL_STOP_PIXEL;
    } else {
        ted.raster.display_xstart = TED_38COL_START_PIXEL;
        ted.raster.display_xstop = TED_38COL_STOP_PIXEL;
    }
    ted.raster.blank_enabled = blank_enabled;
}

static void end_of_line(void)
{
    raster_changes_apply_all(&next_line);
    ted.raster.open_left_border = ted.raster.open_right_border;
    ted.raster.open_right_border = 0;
    ted.raster.blank_this_line = 0;
}

static void test_side_border(void)
{
    int cycle;

    assert(TED_38COL_START_PIXEL - TED_40COL_START_PIXEL == 8);
    assert(TED_40COL_STOP_PIXEL - TED_38COL_STOP_PIXEL == 8);

    for (cycle = 0; cycle < 114; cycle++) {
        /* 40 -> 38 columns. */
        setup(0x1b, 0x08, 0);
        check_lateral_border(0x00, cycle, &ted.raster);
        assert(ted.raster.display_xstart == (cycle < 16 ? TED_38COL_START_PIXEL
                                                         : TED_40COL_START_PIXEL));
        assert(ted.raster.display_xstop == (cycle < 94 ? TED_38COL_STOP_PIXEL
                                                        : TED_40COL_STOP_PIXEL));
        assert(ted.raster.open_right_border == (cycle == 94 || cycle == 95));
        assert(!ted.raster.blank_this_line);
        end_of_line();
        assert(ted.raster.display_xstart == TED_38COL_START_PIXEL);
        assert(ted.raster.display_xstop == TED_38COL_STOP_PIXEL);
        assert(ted.raster.open_left_border == (cycle == 94 || cycle == 95));

        /* 38 -> 40 columns. */
        setup(0x1b, 0x00, 0);
        check_lateral_border(0x08, cycle, &ted.raster);
        assert(ted.raster.display_xstart == (cycle < 16 ? TED_40COL_START_PIXEL
                                                         : TED_38COL_START_PIXEL));
        assert(ted.raster.display_xstop == (cycle < 94 ? TED_40COL_STOP_PIXEL
                                                        : TED_38COL_STOP_PIXEL));
        assert(ted.raster.blank_this_line == (cycle == 16 || cycle == 17));
        assert(!ted.raster.open_right_border);
        end_of_line();
        assert(ted.raster.display_xstart == TED_40COL_START_PIXEL);
        assert(ted.raster.display_xstop == TED_40COL_STOP_PIXEL);

        /* Without an active display there is nothing to keep open. */
        setup(0x1b, 0x08, 1);
        check_lateral_border(0x00, cycle, &ted.raster);
        assert(!ted.raster.open_right_border);

        /* Other $ff07 bits leave the border alone. */
        setup(0x1b, 0x08, 0);
        check_lateral_border(0x0f, cycle, &ted.raster);
        assert(ted.raster.display_xstart == TED_40COL_START_PIXEL);
        assert(next_line.count == 0);
    }

    /* A border kept open by the previous line stays open, even if this
       line's own start test did not fire. */
    setup(0x1b, 0x08, 0);
    ted.raster.open_left_border = 1;
    ted.raster.blank_this_line = 1;
    check_lateral_border(0x00, 95, &ted.raster);
    assert(ted.raster.open_right_border);
}

/* Returns 1 or 0 for the window state used by the current line, and 2 if
   the current line is unchanged but the next line gets a new state. */
static int vertical(uint8_t old_ff06, uint8_t new_ff06, uint8_t ff07,
                    unsigned int line, int cycle, int blank_enabled)
{
    setup(old_ff06, ff07, blank_enabled);
    check_lower_upper_border(new_ff06, line, cycle);
    if (next_line.count) {
        assert(ted.raster.blank_enabled == blank_enabled);
        end_of_line();
        return ted.raster.blank_enabled == blank_enabled ? -1 : 2;
    }
    return !ted.raster.blank_enabled;
}

static void test_vertical_window(void)
{
    int cycle;

    for (cycle = 0; cycle < 112; cycle++) {
        int now40 = cycle < 16;
        int now38 = cycle < 18;

        /* Opening: 25 rows on line 4, 24 rows on line 8. */
        assert(vertical(0x13, 0x1b, 0x08, 4, cycle, 1) == (now40 ? 1 : 2));
        assert(vertical(0x13, 0x1b, 0x00, 4, cycle, 1) == (now38 ? 1 : 2));
        assert(vertical(0x1b, 0x13, 0x08, 8, cycle, 1) == (now40 ? 1 : 2));
        assert(vertical(0x0b, 0x1b, 0x08, 4, cycle, 1) == (now40 ? 1 : 2));
        /* Wrong width or no DEN: no opening. */
        assert(vertical(0x1b, 0x13, 0x08, 4, cycle, 1) == 0);
        assert(vertical(0x13, 0x1b, 0x08, 8, cycle, 1) == 0);
        assert(vertical(0x13, 0x0b, 0x08, 4, cycle, 1) == 0);

        /* Closing: 24 rows on line 200, 25 rows on line 204. */
        assert(vertical(0x1b, 0x13, 0x08, 200, cycle, 0) == (now40 ? 0 : 2));
        assert(vertical(0x13, 0x1b, 0x08, 204, cycle, 0) == (now40 ? 0 : 2));
        assert(vertical(0x13, 0x1b, 0x00, 204, cycle, 0) == (now38 ? 0 : 2));

        /* Lines 199 and 203 have no test: switching there opens the
           border instead of closing it early. */
        assert(vertical(0x1b, 0x13, 0x08, 203, cycle, 0) == 1);
        assert(vertical(0x13, 0x1b, 0x08, 199, cycle, 0) == 1);
        assert(vertical(0x1b, 0x13, 0x08, 202, cycle, 0) == 1);
    }

    /* The line number for the tests is incremented at cycle 112. */
    for (cycle = 112; cycle < 114; cycle++) {
        assert(vertical(0x13, 0x1b, 0x08, 3, cycle, 1) == 2);
        assert(vertical(0x13, 0x1b, 0x08, 4, cycle, 1) == 0);
        assert(vertical(0x1b, 0x13, 0x08, 199, cycle, 0) == 2);
        assert(vertical(0x1b, 0x13, 0x08, 200, cycle, 0) == 1);
        assert(vertical(0x13, 0x1b, 0x08, 203, cycle, 0) == 2);
        assert(vertical(0x13, 0x1b, 0x08, 311, cycle, 1) == 0);
    }
}

int main(void)
{
    test_side_border();
    test_vertical_window();
    puts("TED side and vertical border tests passed");
    return 0;
}
