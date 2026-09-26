/* TED's BA warning: the read in the single clock in which BA falls
   completes, RDY stops the CPU at its next read, but writes that follow
   another write (RMW, stack pushes) complete during the three single clocks
   before TED owns the bus. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../src/plus4/ted.c"
#include "../../src/plus4/ted-timing.c"
#include "../../src/plus4/ted-mem.c"

CLOCK maincpu_clk;
int maincpu_rmw_flag;
uint8_t mem_ram[0x10000];
static interrupt_cpu_status_t int_status;
interrupt_cpu_status_t *maincpu_int_status = &int_status;
static unsigned int fetches;
static unsigned int drawn_lines;
static alarm_context_t context;
static alarm_t draw_alarm;

void alarm_log_too_many_alarms(void)
{
    abort();
}

void alarm_unset(alarm_t *alarm)
{
    alarm->pending_idx = -1;
}

void ted_counter_update(CLOCK clk)
{
}

void ted_fetch_store(uint16_t addr, uint8_t old_value, unsigned int ram_mask)
{
}

void ted_fetch_alarm_handler(CLOCK offset, void *data)
{
    fetches++;
    ted.fetch_clk = CLOCK_MAX;
}

void raster_line_emulate(raster_t *raster)
{
    drawn_lines++;
}

void raster_canvas_handle_end_of_frame(raster_t *raster)
{
    abort();
}

void vsync_do_end_of_line(void)
{
}

void vsync_do_vsync(struct video_canvas_s *canvas)
{
    abort();
}

uint8_t *mem_get_tedmem_base(unsigned int segment)
{
    abort();
}

static void setup(void)
{
    memset(&ted, 0, sizeof(ted));
    memset(&context, 0, sizeof(context));
    memset(&draw_alarm, 0, sizeof(draw_alarm));
    draw_alarm.context = &context;
    draw_alarm.pending_idx = -1;
    ted.raster_draw_alarm = &draw_alarm;
    fetches = 0;
    drawn_lines = 0;
    ted.fastmode = 1;
    ted.character_fetch_on = 1;
    ted.cycles_per_line = 114;
    ted.draw_clk = 114;
    ted.fetch_clk = TED_FETCH_CYCLE;
    maincpu_clk = 3;
    ted_delay_resync();
}

int main(void)
{
    void (*stores[])(uint16_t, uint8_t) = {
        ted_mem_vbank_store, ted_mem_vbank_store_32k, ted_mem_vbank_store_16k
    };
    unsigned int i;

    setup();
    /* An RMW whose last read precedes BA: the core issues the old-value
       write, followed by the final write, each with its own call.  Both
       complete during the BA warning; the next read waits for the DMA. */
    maincpu_clk += 1;
    ted_handle_pending_alarms(1);
    assert(maincpu_clk == 5);
    assert(fetches == 0);
    maincpu_clk++;
    ted_handle_pending_alarms(1);
    assert(maincpu_clk == 7);
    assert(fetches == 0);

    maincpu_clk++;
    ted_handle_pending_alarms(0);
    assert(fetches == 1);

    /* Reads stall even on the first warning clock. */
    setup();
    maincpu_clk++;
    ted_handle_pending_alarms(0);
    assert(maincpu_clk == 5);
    assert(fetches == 1);

    /* The read in the single clock in which BA falls completes, so does a
       store after it... */
    setup();
    maincpu_clk += 2;
    ted_handle_pending_alarms(1);
    assert(fetches == 0);

    /* ...but a later read waits: the write follows the DMA (Return to
       Promised Land's STA $FF07 after a forced bad line). */
    setup();
    maincpu_clk += 3;
    ted_handle_pending_alarms(1);
    assert(fetches == 1);

    for (i = 0; i < sizeof(stores) / sizeof(stores[0]); i++) {
        setup();
        maincpu_clk += 1;
        stores[i](0x1234, 0x40);
        assert(maincpu_clk == 5);
        assert(fetches == 0);
        maincpu_clk++;
        stores[i](0x1234, 0x41);
        assert(maincpu_clk == 7);
        assert(fetches == 0);
        assert(mem_ram[0x1234] == 0x41);
        maincpu_clk++;
        ted_handle_pending_alarms(0);
        assert(fetches == 1);

        /* The halt is decided on the bus slot of the write, after the
           single clock stretch, as for register writes: the read before
           this write is in slot 5, after BA. */
        setup();
        maincpu_clk += 3;
        stores[i](0x1234, 0x42);
        assert(fetches == 1);
        assert(mem_ram[0x1234] == 0x42);

        /* JSR, BRK and interrupts push two bytes at one core clock: the
           second push must not stretch the clock again. */
        setup();
        maincpu_clk += 1;
        stores[i](0x0100, 0x12);
        assert(maincpu_clk == 5);
        stores[i](0x01ff, 0x34);
        assert(maincpu_clk == 5);
        assert(mem_ram[0x0100] == 0x12 && mem_ram[0x01ff] == 0x34);
    }

    /* Resetting the live line after attribute DMA must still copy its
       colours and request character DMA at the line boundary. */
    setup();
    ted.screen_height = 312;
    ted.tv_height = 312;
    ted.first_dma_line = 0;
    ted.last_dma_line = 203;
    ted.allow_bad_lines = 1;
    ted.row_counter_active = 1;
    ted.raster.ysmooth = 3;
    ted.dma_line = 3;
    ted.ted_raster_counter = 2;
    memset(ted.cbuf_tmp, 0x71, sizeof(ted.cbuf_tmp));
    ted_raster_draw_alarm_handler(0, NULL);
    assert(drawn_lines == 1);
    assert(ted.matrix_fetch_pending);
    assert(ted.dma_line == 3);
    assert(memcmp(ted.cbuf, ted.cbuf_tmp, TED_SCREEN_TEXTCOLS) == 0);

    puts("TED DMA write warning tests passed");
    return 0;
}
