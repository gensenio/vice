/* The $ff07 register handler.  Bit 7 (reverse or 256 characters), like
   ECM, aligns the character set to 2 KB: a change of it updates the video
   memory pointers.  Bit 6 selects PAL or NTSC mode and bit 5 freezes TED.
   Only a change of each bit has an effect.  The pixel pipeline latches the
   bits (see ted-pixel-test.c). */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/plus4/ted-mem.c"

ted_t ted;
CLOCK maincpu_clk;

static int memory_ptr_updates;

void ted_update_memory_ptrs(void)
{
    memory_ptr_updates++;
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
    ted.regs[0x06] = 0x1b;
    ted.regs[0x07] = ff07;
    memory_ptr_updates = 0;
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
            assert(ted.regs[0x07] == to);
            assert(memory_ptr_updates == 1);
        }
    }

    /* Scroll, width and multicolour bits do not move the pointers. */
    setup(0x88);
    maincpu_clk = 100;
    ted07_store(0x9f);
    assert(memory_ptr_updates == 0);
    assert(ntsc_mode_calls == 0 && freeze_calls == 0);

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

    puts("TED $ff07 register tests passed");
    return 0;
}
