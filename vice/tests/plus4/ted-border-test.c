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
#include "../../src/plus4/ted-draw.c"

ted_t ted;
CLOCK maincpu_clk;

static void setup(uint8_t ff06, uint8_t ff07, int blank_enabled)
{
    memset(&ted.raster, 0, sizeof(ted.raster));
    ted.screen_leftborderwidth = 32;
    ted.screen_height = 312;
    ted.row_25_start_line = TED_PAL_25ROW_START_LINE;
    ted.row_25_stop_line = TED_PAL_25ROW_STOP_LINE;
    ted.row_24_start_line = TED_PAL_24ROW_START_LINE;
    ted.row_24_stop_line = TED_PAL_24ROW_STOP_LINE;
    ted.regs[0x06] = ff06;
    ted.regs[0x07] = ff07;
    ted.regs[0x15] = 2;
    ted.regs[0x19] = 6;
    ted.raster.blank_enabled = blank_enabled;
    maincpu_clk = 0;
    ted_draw_init();
}

static void change_width(int cycle, uint8_t value)
{
    maincpu_clk = cycle;
    ted_draw_store(7, value);
    ted.regs[7] = value;
    ted_draw_sync(114);
}

static void test_side_border(void)
{
    int cycle;

    for (cycle = 0; cycle < 114; cycle++) {
        setup(0x1b, 8, 0);
        change_width(cycle, 0);
        assert(!beam.border == (cycle == 94 || cycle == 95));
        setup(0x1b, 0, 0);
        change_width(cycle, 8);
        assert(beam.border);
        /* If both opening comparisons were missed, foreground never opens. */
        if (cycle == 16 || cycle == 17) {
            assert(beam.line[64] == beam.palette[4]);
        }
        setup(0x1b, 8, 1);
        change_width(cycle, 0);
        assert(beam.border);
    }
}

/* The vertical latch changes immediately.  The separate side-border
   latch consults it only at its opening comparisons.  Returns 1 if the
   vertical window is open after the write. */
static int vertical(uint8_t old_ff06, uint8_t new_ff06, uint8_t ff07,
                    unsigned int line, int cycle, int blank_enabled)
{
    setup(old_ff06, ff07, blank_enabled);
    check_lower_upper_border(new_ff06, line, cycle);
    return !ted.raster.blank_enabled;
}

static void test_vertical_window(void)
{
    int cycle;

    for (cycle = 0; cycle < 112; cycle++) {

        /* Opening: 25 rows on line 4, 24 rows on line 8. */
        assert(vertical(0x13, 0x1b, 0x08, 4, cycle, 1) == 1);
        assert(vertical(0x13, 0x1b, 0x00, 4, cycle, 1) == 1);
        assert(vertical(0x1b, 0x13, 0x08, 8, cycle, 1) == 1);
        assert(vertical(0x0b, 0x1b, 0x08, 4, cycle, 1) == 1);
        /* Wrong width or no DEN: no opening. */
        assert(vertical(0x1b, 0x13, 0x08, 4, cycle, 1) == 0);
        assert(vertical(0x13, 0x1b, 0x08, 8, cycle, 1) == 0);
        assert(vertical(0x13, 0x0b, 0x08, 4, cycle, 1) == 0);

        /* Closing: 24 rows on line 200, 25 rows on line 204. */
        assert(vertical(0x1b, 0x13, 0x08, 200, cycle, 0) == 0);
        assert(vertical(0x13, 0x1b, 0x08, 204, cycle, 0) == 0);
        assert(vertical(0x13, 0x1b, 0x00, 204, cycle, 0) == 0);

        /* Lines 199 and 203 have no test: switching there opens the
           border instead of closing it early. */
        assert(vertical(0x1b, 0x13, 0x08, 203, cycle, 0) == 1);
        assert(vertical(0x13, 0x1b, 0x08, 199, cycle, 0) == 1);
        assert(vertical(0x1b, 0x13, 0x08, 202, cycle, 0) == 1);
    }

    /* The line number for the tests is incremented at cycle 112. */
    for (cycle = 112; cycle < 114; cycle++) {
        assert(vertical(0x13, 0x1b, 0x08, 3, cycle, 1) == 1);
        assert(vertical(0x13, 0x1b, 0x08, 4, cycle, 1) == 0);
        assert(vertical(0x1b, 0x13, 0x08, 199, cycle, 0) == 0);
        assert(vertical(0x1b, 0x13, 0x08, 200, cycle, 0) == 1);
        assert(vertical(0x13, 0x1b, 0x08, 203, cycle, 0) == 0);
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
