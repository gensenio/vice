/* Raster writes must not cancel the current line's pending DMA. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/plus4/ted-mem.c"
#include "../../src/plus4/ted-fetch.c"

ted_t ted;
CLOCK maincpu_clk;
static CLOCK stolen;
static alarm_context_t context;
static alarm_t fetch_alarm, irq_alarm;

void alarm_log_too_many_alarms(void)
{
    abort();
}

void alarm_unset(alarm_t *alarm)
{
    alarm->pending_idx = -1;
}

void dma_maincpu_steal_cycles(CLOCK start, CLOCK num, CLOCK sub)
{
    stolen += num;
}

void ted_delay_oldclk(CLOCK num)
{
}

static void setup(void)
{
    memset(&ted, 0, sizeof(ted));
    memset(&context, 0, sizeof(context));
    memset(&fetch_alarm, 0, sizeof(fetch_alarm));
    memset(&irq_alarm, 0, sizeof(irq_alarm));
    fetch_alarm.context = irq_alarm.context = &context;
    fetch_alarm.pending_idx = irq_alarm.pending_idx = -1;
    ted.raster_fetch_alarm = &fetch_alarm;
    ted.raster_irq_alarm = &irq_alarm;
    ted.screen_height = 312;
    ted.cycles_per_line = 114;
    ted.first_dma_line = 0;
    ted.last_dma_line = 203;
    ted.raster_irq_line = 200;
    ted.ted_raster_counter = 4;
    ted.dma_line = 4;
    ted.fetch_clk = TED_FETCH_CYCLE;
    maincpu_clk = 2;
    stolen = 0;
}

int main(void)
{
    uint8_t matrix[1024], colors[1024];
    int i;

    for (i = 0; i < 1024; i++) {
        matrix[i] = (uint8_t)i;
        colors[i] = (uint8_t)~i;
    }

    setup();
    ted1c1d_store(0x1d, 2);
    assert(ted1c1d_read(0x1d) == 2);
    assert(ted.fetch_clk == TED_FETCH_CYCLE);
    assert(ted.dma_line == 4);
    ted1c1d_store(0x1c, 1);
    assert(ted1c1d_read(0x1c) == 0xff);
    assert(ted.dma_line == 4);
    assert(ted.fetch_clk == TED_FETCH_CYCLE);
    ted1c1d_store(0x1c, 0);
    ted.screen_ptr = matrix;
    ted.color_ptr = colors;
    ted.allow_bad_lines = 1;
    ted.raster.ysmooth = 3;
    ted.matrix_fetch_pending = 1;
    assert(do_matrix_fetch(0));
    assert(stolen == 86);
    assert(memcmp(ted.vbuf, matrix, 40) == 0);

    /* A live match must not change a character DMA to attribute addresses. */
    ted1c1d_store(0x1d, 3);
    ted.memory_fetch_done = 0;
    assert(do_matrix_fetch(0));
    assert(memcmp(ted.vbuf, matrix, 40) == 0);

    /* Repeated latched attribute lines fill both buffers, even when the
       live counter has already been changed to prepare the next line. */
    ted.dma_line = 3;
    ted1c1d_store(0x1d, 2);
    ted.memory_fetch_done = 0;
    assert(do_matrix_fetch(0));
    assert(memcmp(ted.vbuf, colors, 40) == 0);
    assert(memcmp(ted.cbuf_tmp, colors, 40) == 0);

    /* Neither byte of the live counter changes the current DMA window. */
    ted1c1d_store(0x1c, 1);
    assert(ted1c1d_read(0x1c) == 0xff);
    assert(ted.dma_line == 3);
    ted.memory_fetch_done = 0;
    assert(do_matrix_fetch(0));

    /* After this line's event, the new counter schedules future fetches. */
    maincpu_clk = 20;
    ted.memory_fetch_done = 2;
    ted1c1d_store(0x1c, 0);
    assert(ted.fetch_clk == 114 + TED_FETCH_CYCLE);
    ted1c1d_store(0x1d, 250);
    assert(ted.fetch_clk == (312 - 250) * 114 + TED_FETCH_CYCLE);

    /* A write on the BA warning clocks can leave the alarm overdue.  It
       still belongs to this scanline until the next CPU read serves it. */
    setup();
    maincpu_clk = TED_FETCH_CYCLE + 1;
    ted1c1d_store(0x1d, 2);
    assert(ted.fetch_clk == TED_FETCH_CYCLE);

    puts("TED DMA scanline latch tests passed");
    return 0;
}
