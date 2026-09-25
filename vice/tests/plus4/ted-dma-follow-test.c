/* A scroll change must not cancel an already requested second DMA line. */
#include <assert.h>
#include <stdio.h>
#include "../../src/plus4/ted-fetch.c"

ted_t ted;
static CLOCK stolen;
void dma_maincpu_steal_cycles(CLOCK start, CLOCK num, CLOCK sub)
{
    stolen += num;
}
void ted_delay_oldclk(CLOCK num)
{
}

int main(void)
{
    uint8_t matrix[1024];
    uint8_t colors[1024];
    int i;

    for (i = 0; i < 1024; i++) {
        matrix[i] = (uint8_t)i;
        colors[i] = (uint8_t)~i;
    }
    ted.screen_ptr = matrix;
    ted.color_ptr = colors;
    ted.allow_bad_lines = 1;
    ted.first_dma_line = 0;
    ted.last_dma_line = 203;
    ted.ted_raster_counter = 102;
    ted.memptr_col = 1020;
    ted.raster.ysmooth = 0; /* no longer matches the preceding line */
    ted.matrix_fetch_pending = 1;
    assert(do_matrix_fetch(0));
    assert(stolen == 86);
    for (i = 0; i < 40; i++) {
        assert(ted.vbuf[i] == (uint8_t)(1020 + i));
    }
    ted.memory_fetch_done = 0;
    ted.matrix_fetch_pending = 0;
    ted.raster.ysmooth = 5; /* a match now cannot invent the earlier request */
    stolen = 0;
    assert(!do_matrix_fetch(0));
    assert(stolen == 0);
    ted.memory_fetch_done = 0;
    ted.matrix_fetch_pending = 1;
    ted.raster.ysmooth = 6; /* simultaneous requests use the attribute address */
    assert(do_matrix_fetch(0));
    assert(stolen == 86);
    for (i = 0; i < 40; i++) {
        assert(ted.vbuf[i] == (uint8_t)~(1020 + i));
        assert(ted.cbuf_tmp[i] == ted.vbuf[i]);
    }
    puts("TED second DMA request tests passed");
    return 0;
}
