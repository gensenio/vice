/* Register storage and PAL/NTSC/freeze hooks.  The pixel latch timing of
   reverse and mode changes is exercised by ted-pixel-test.c. */
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

static int ntsc_mode_calls;
static int ntsc_mode;

void ted_set_ntsc_mode(int ntsc)
{
    ntsc_mode_calls++;
    ntsc_mode = ntsc;
}

static int freeze_calls;
static int freeze;

void ted_set_freeze(int value)
{
    freeze_calls++;
    freeze = value;
}

static void setup(uint8_t ff07)
{
    memset(&ted.raster, 0, sizeof(ted.raster));
    memset(&foreground, 0, sizeof(foreground));
    memset(&next_line, 0, sizeof(next_line));
    memset(&changes, 0, sizeof(changes));
    changes.foreground = &foreground;
    changes.next_line = &next_line;
    geometry.text_size.width = TED_SCREEN_TEXTCOLS;
    ted.raster.changes = &changes;
    ted.raster.geometry = &geometry;
    ted.screen_leftborderwidth = 32;
    ted.regs[0x06] = 0x1b;
    ted.regs[0x07] = ff07;
    ted.reverse_mode = ff07 & 0x80;
    ted.raster.display_xstart = TED_40COL_START_PIXEL;
    ted.raster.display_xstop = TED_40COL_STOP_PIXEL;
}

int main(void)
{
    unsigned int cycle;
    int on;

    for (on = 0; on < 2; on++) {
        uint8_t from = on ? 0x88 : 0x08;
        uint8_t to = on ? 0x08 : 0x88;

        for (cycle = 0; cycle < 114; cycle++) {

            setup(from);
            maincpu_clk = cycle;
            ted07_store(to);
            assert(ted.reverse_mode == (to & 0x80));
            assert(foreground.count == 0 && next_line.count == 0);
        }
    }

    /* Writes that keep bit 7 queue no change of it. */
    setup(0x88);
    maincpu_clk = 100;
    ted07_store(0x98);
    assert(foreground.count == 0 && next_line.count == 0);
    assert(ntsc_mode_calls == 0);

    /* Bit 6 selects NTSC mode; only a change of it switches the mode. */
    setup(0x08);
    ted07_store(0x48);
    assert(ntsc_mode_calls == 1 && ntsc_mode);
    ted07_store(0x58);
    assert(ntsc_mode_calls == 1);
    ted07_store(0x18);
    assert(ntsc_mode_calls == 2 && !ntsc_mode);
    assert(freeze_calls == 0);

    /* Bit 5 freezes TED; only a change of it starts or ends the freeze. */
    setup(0x08);
    ted07_store(0x28);
    assert(freeze_calls == 1 && freeze);
    ted07_store(0x38);
    assert(freeze_calls == 1);
    ted07_store(0x18);
    assert(freeze_calls == 2 && !freeze);

    puts("TED reverse mode tests passed");
    return 0;
}
