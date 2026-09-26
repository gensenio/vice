/* $FF07 bit 5 (freeze) stops the horizontal and vertical counters and the
   timers, and forces single clock (TED preliminary data sheet, register 7;
   FPGATED `stop').  FPGATED latches the bit at the end of a single clock,
   so the counters stop for whole single clocks.  Events due before the
   freeze still happen; the later ones wait for its end.  Exercise the
   production register, draw and CPU clock handlers. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../src/plus4/ted.c"
#include "../../src/plus4/ted-timing.c"
#include "../../src/plus4/ted-mem.c"
#include "../../src/plus4/ted-counter.c"

CLOCK maincpu_clk;
int maincpu_rmw_flag;
uint8_t mem_ram[0x10000];
static interrupt_cpu_status_t int_status;
interrupt_cpu_status_t *maincpu_int_status = &int_status;
static alarm_context_t context;
static alarm_t draw_alarm, fetch_alarm, irq_alarm, tv_alarm;
static raster_changes_t background, foreground, border, sprites, next_line;
static raster_changes_all_t changes;
static geometry_t geometry;
static int timer_freeze = -1;
static CLOCK timer_run;
static unsigned int drawn_lines, vsyncs;

void alarm_log_too_many_alarms(void)
{
    abort();
}

void alarm_unset(alarm_t *alarm)
{
    alarm->pending_idx = -1;
}

void ted_timer_freeze(int freeze, CLOCK run)
{
    timer_freeze = freeze;
    timer_run = run;
}

void plus4_set_ted_ntsc_mode(int ntsc)
{
}

uint8_t *mem_get_tedmem_base(unsigned int segment)
{
    return mem_ram;
}

uint8_t *mem_get_open_space(void)
{
    return mem_ram;
}

void ted_fetch_store(uint16_t addr, uint8_t old_value, unsigned int ram_mask)
{
}

void raster_line_emulate(raster_t *raster)
{
    drawn_lines++;
}

void raster_canvas_handle_end_of_frame(raster_t *raster)
{
}

void vsync_do_end_of_line(void)
{
}

void vsync_do_vsync(struct video_canvas_s *canvas)
{
    vsyncs++;
}

static void init_alarm(alarm_t *alarm)
{
    memset(alarm, 0, sizeof(*alarm));
    alarm->context = &context;
    alarm->pending_idx = -1;
}

/* Line 220 (lower border, double clock) started at clock 1140.  */
static void setup(void)
{
    memset(&ted, 0, sizeof(ted));
    memset(&context, 0, sizeof(context));
    init_alarm(&draw_alarm);
    init_alarm(&fetch_alarm);
    init_alarm(&irq_alarm);
    init_alarm(&tv_alarm);
    memset(&changes, 0, sizeof(changes));
    changes.background = &background;
    changes.foreground = &foreground;
    changes.border = &border;
    changes.sprites = &sprites;
    changes.next_line = &next_line;
    geometry.text_size.width = TED_SCREEN_TEXTCOLS;
    ted.raster.changes = &changes;
    ted.raster.geometry = &geometry;
    ted.raster_draw_alarm = &draw_alarm;
    ted.raster_fetch_alarm = &fetch_alarm;
    ted.raster_irq_alarm = &irq_alarm;
    ted.tv_line_alarm = &tv_alarm;
    ted.cycles_per_line = 114;
    ted.fastmode = 1;
    ted.first_dma_line = TED_PAL_FIRST_DMA_LINE;
    ted.last_dma_line = TED_PAL_LAST_DMA_LINE;
    ted.tv_height = TED_PAL_SCREEN_HEIGHT;
    ted.tv_vsync_line = TED_PAL_VSYNC_LINE;
    ted_timing_set_mode(0);
    ted.ted_raster_counter = 220;
    ted.dma_line = 220;
    ted.tv_current_line = 10;
    ted.regs[0x07] = 0x08;
    ted.last_emulate_line_clk = 1140;
    ted.draw_clk = 1254;
    ted.fetch_clk = 1254 + 91 * 114 + TED_FETCH_CYCLE;
    ted.raster_irq_clk = 1254 + 2 * 114;
    ted.counter_clk = 1190;
    maincpu_clk = 1190;
    ted_delay_resync();
    timer_freeze = -1;
    drawn_lines = vsyncs = 0;
}

int main(void)
{
    CLOCK start, shift;

    /* Freeze on cycle 50 of line 220 and run 1001 clocks: the counters
       move by 1000 clocks, the odd clock is not a whole single clock. */
    setup();
    ted07_store(0x28);
    assert(ted.freeze && timer_freeze == 1 && timer_run == 0);
    assert(tv_alarm.pending_idx >= 0);
    maincpu_clk += 1001;
    ted_freeze_update();
    assert(ted.last_emulate_line_clk == 2140 && ted.draw_clk == 2254);
    assert(ted.fetch_clk == 2254 + 91 * 114 + TED_FETCH_CYCLE);
    assert(ted.raster_irq_clk == 2254 + 2 * 114);
    assert(ted.counter_clk == 2190 && ted.freeze_clk == 2190);
    assert(TED_RASTER_Y(maincpu_clk) == 220);
    assert(TED_RASTER_CYCLE(maincpu_clk) == 51);
    assert(ted_counter_read() == ((50 - 16) << 1));

    /* A draw event while frozen waits: the line does not end. */
    maincpu_clk = ted.draw_clk + 7;
    ted_raster_draw_alarm_handler(0, NULL);
    assert(ted.ted_raster_counter == 220 && drawn_lines == 0);
    assert(draw_alarm.pending_idx == -1);

    /* An event due before the freeze keeps its clock. */
    setup();
    ted.raster_irq_clk = 1189;
    ted07_store(0x28);
    maincpu_clk += 500;
    ted_freeze_update();
    assert(ted.raster_irq_clk == 1189 && ted.draw_clk == 1254 + 500);

    /* The end of the freeze starts the counters from where they stopped;
       the timers run the odd clock left. */
    setup();
    ted07_store(0x28);
    start = ted.draw_clk;
    maincpu_clk += 12345;
    ted07_store(0x08);
    assert(!ted.freeze && timer_freeze == 0 && timer_run == 1);
    assert(tv_alarm.pending_idx == -1);
    shift = ted.draw_clk - start;
    assert(shift == 12344);
    assert(draw_alarm.pending_idx >= 0 && fetch_alarm.pending_idx >= 0
           && irq_alarm.pending_idx >= 0);
    maincpu_clk = ted.draw_clk;
    ted_raster_draw_alarm_handler(0, NULL);
    assert(ted.ted_raster_counter == 221 && drawn_lines == 1);

    /* The CPU runs in single clock while frozen, as with $FF13 bit 1: ten
       CPU cycles in the border take about twenty clocks, not ten. */
    setup();
    maincpu_clk += 10;
    ted_delay_clk();
    assert(maincpu_clk == 1200);
    setup();
    ted.fastmode = 0;
    maincpu_clk += 10;
    ted_delay_clk();
    start = maincpu_clk;
    assert(start >= 1209);
    setup();
    ted07_store(0x28);
    maincpu_clk += 10;
    ted_delay_clk();
    assert(maincpu_clk == start);
    assert(ted_cpu_slot(maincpu_clk, 40) == 0 && ted_cpu_slot(maincpu_clk, 41));

    /* Without sync the TV keeps scanning lines and frames. */
    setup();
    ted07_store(0x28);
    ted.tv_current_line = ted.tv_height - 2;
    ted_tv_line_alarm_handler(0, NULL);
    ted_tv_line_alarm_handler(0, NULL);
    assert(drawn_lines == 2 && vsyncs == 1 && ted.tv_current_line == 0);
    assert(ted.tv_line_clk == 1254 + 2 * 114);

    puts("TED freeze tests passed");
    return 0;
}
