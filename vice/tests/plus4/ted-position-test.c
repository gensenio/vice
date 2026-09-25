/* Regression for TED registers 26/27.  The Commodore TED data sheet
   specifies a 10-bit character-position reload register, separate from
   the matrix DMA address.  Exercise the production register handlers. */
#include <assert.h>
#include <stdio.h>

#include "../../src/plus4/ted-mem.c"

ted_t ted;

int main(void)
{
    ted.mem_counter = 0x155;
    ted.memptr = 0x028;
    ted.chr_pos_count = 0x050;
    ted.chr_pos_reload = 0x2aa;

    ted1a1b_store(0x1b, 0x18);
    assert(ted.chr_pos_reload == 0x218);
    assert(ted1a1b_read(0x1a) == 0xfe);
    assert(ted1a1b_read(0x1b) == 0x18);

    ted1a1b_store(0x1a, 0xfd);
    assert(ted.chr_pos_reload == 0x118);
    assert(ted1a1b_read(0x1a) == 0xfd);
    assert(ted1a1b_read(0x1b) == 0x18);
    assert(ted.mem_counter == 0x155);
    assert(ted.memptr == 0x028);
    assert(ted.chr_pos_count == 0x050);

    /* Reads follow the hardware reload, not the last software write or
       the live DMA position. */
    ted.chr_pos_reload = 0x3ff;
    ted.mem_counter = 0;
    assert(ted1a1b_read(0x1a) == 0xff);
    assert(ted1a1b_read(0x1b) == 0xff);
    ted.chr_pos_reload = (ted.chr_pos_reload + 40) & 0x3ff;
    assert(ted1a1b_read(0x1a) == 0xfc);
    assert(ted1a1b_read(0x1b) == 39);

    puts("TED character-position register tests passed");
    return 0;
}
