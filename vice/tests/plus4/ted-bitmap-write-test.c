/* Preserve fetched bitmap bytes across CPU writes, including RAM mirrors. */
#include "vice.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ted-fetch.h"
#include "tedtypes.h"

ted_t ted;
CLOCK maincpu_clk;

static void setup(void)
{
    memset(&ted, 0, sizeof(ted));
    ted.regs[6] = 0x3b;
    ted.regs[0x12] = 8;
    ted.character_fetch_on = 1;
    ted.raster.ycounter = 2;
    ted.counter_clk = maincpu_clk = 80;
}

int main(void)
{
    setup();
    ted_fetch_store(0x2002, 0xff, 0xffff);
    assert(ted.bitmap_latched[0] && ted.bitmap_data[0] == 0xff);
    ted_fetch_store(0x2002, 0, 0xffff);
    assert(ted.bitmap_data[0] == 0xff); /* first fetched value survives */
    ted_fetch_store(0x200a, 0x55, 0xffff);
    assert(ted.bitmap_data[1] == 0x55);

    setup();
    ted.counter_clk = maincpu_clk = 4;
    ted_fetch_store(0x2002, 0xff, 0xffff);
    assert(!ted.bitmap_dirty); /* before bitmap fetching */
    setup();
    ted_fetch_store(0x2003, 0xff, 0xffff);
    ted_fetch_store(0x4002, 0xff, 0xffff);
    assert(!ted.bitmap_dirty); /* another row/bank */
    ted.regs[0x12] |= 4;
    ted_fetch_store(0x2002, 0xff, 0xffff);
    assert(!ted.bitmap_dirty); /* RAM write cannot change ROM fetch */

    setup();
    ted.regs[0x12] = 0x28; /* $a000 aliases $2000 with 16/32 KiB RAM */
    ted_fetch_store(0x2002, 0x33, 0x3fff);
    assert(ted.bitmap_latched[0] && ted.bitmap_data[0] == 0x33);
    setup();
    ted.regs[0x12] = 0x28;
    ted_fetch_store(0x2002, 0x66, 0x7fff);
    assert(ted.bitmap_latched[0] && ted.bitmap_data[0] == 0x66);

    setup();
    ted.memptr = 1020;
    ted_fetch_store(0x2002, 0xaa, 0xffff);
    assert(ted.bitmap_latched[4] && ted.bitmap_data[4] == 0xaa);
    puts("TED bitmap write preservation passed");
    return 0;
}
