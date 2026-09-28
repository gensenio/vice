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

static unsigned int flushes;
void ted_draw_sync(CLOCK clk)
{
    assert(clk == maincpu_clk);
    flushes++;
}

int main(void)
{
    setup();
    ted_fetch_store(0x2002, 0xff, 0xffff);
    assert(flushes == 1);
    ted_fetch_store(0x4002, 0xff, 0xffff);
    assert(flushes == 1);
    ted.regs[0x12] |= 4;
    ted_fetch_store(0x2002, 0xff, 0xffff);
    assert(flushes == 1);
    setup();
    ted.regs[0x12] = 0x28;
    ted_fetch_store(0x2002, 0x33, 0x3fff);
    ted_fetch_store(0x2002, 0x66, 0x7fff);
    assert(flushes == 3);
    setup();
    ted.regs[6] = 0x1b;
    ted.regs[0x13] = 0x30;
    ted_fetch_store(0x3002, 0x33, 0xffff);
    ted_fetch_store(0x3802, 0x33, 0xffff);
    assert(flushes == 4);
    puts("TED video RAM writes flush the pixel pipeline (bitmap, charset, mirrors)");
    return 0;
}
