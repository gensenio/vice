/* Regression for the TED blink counter ($ff1f bits 3-6).  It advances on
   line 205 at cycle 104 (FPGATED); the end-of-line handler applies the
   increment, so reads and writes later on that line must account for it.
   Exercise the production register handlers. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/plus4/ted-mem.c"

ted_t ted;
CLOCK maincpu_clk;

static raster_changes_t foreground, next_line;
static raster_changes_all_t changes;
static geometry_t geometry;

static void at(unsigned int line, unsigned int cycle)
{
    ted.ted_raster_counter = line;
    ted.last_emulate_line_clk = 1000;
    maincpu_clk = 1000 + cycle;
}

static int count(void)
{
    return (ted1f_read() >> 3) & 0x0f;
}

/* What the end-of-line handler of line 205 does. */
static void end_of_blink_line(void)
{
    ted.cursor_phase = (ted.cursor_phase + 1) & 0x1f;
    ted.cursor_visible = ted.cursor_phase & 0x10;
}

int main(void)
{
    memset(&foreground, 0, sizeof(foreground));
    memset(&next_line, 0, sizeof(next_line));
    changes.foreground = &foreground;
    changes.next_line = &next_line;
    ted.raster.changes = &changes;
    geometry.text_size.width = TED_SCREEN_TEXTCOLS;
    ted.raster.geometry = &geometry;
    ted.screen_height = 312;

    ted.cursor_phase = 0x03;
    at(205, 103);
    assert(count() == 3);
    at(205, 104);
    assert(count() == 4);
    at(205, 113);
    assert(count() == 4);
    at(204, 110);
    assert(count() == 3);

    /* A write after the increment is not incremented again. */
    at(205, 105);
    foreground.count = 0;
    ted1f_store(9 << 3);
    assert(count() == 9);
    end_of_blink_line();
    at(206, 0);
    assert(count() == 9);

    /* Before the increment, the written value is incremented. */
    at(205, 50);
    foreground.count = 0;
    ted1f_store(9 << 3);
    assert(count() == 9);
    end_of_blink_line();
    at(206, 0);
    assert(count() == 10);

    /* Wrapping from 15 toggles the flash state; writing 15 after the
       wrap does not toggle it back. */
    ted.cursor_phase = 0x0f;
    at(205, 104);
    assert(count() == 0);
    foreground.count = 0;
    ted1f_store(15 << 3);
    assert(count() == 15);
    assert(ted.cursor_visible);
    end_of_blink_line();
    at(206, 0);
    assert(count() == 15);
    assert(ted.cursor_phase == 0x1f);

    puts("TED blink counter tests passed");
    return 0;
}
