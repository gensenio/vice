/* $FF07 bit 6 selects PAL or NTSC mode (TED preliminary data sheet,
   register 7): 312 or 262 lines per frame, vertical sync at line 257 or
   229, and the crystal divided by 10 or 8.  The raster interrupt and the
   matrix DMA scheduled across the end of the frame must follow the new
   frame; a counter beyond the last line counts up to 511.  Exercise the
   production register handler and draw handler. */
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
static alarm_context_t context;
static alarm_t draw_alarm, fetch_alarm, irq_alarm;
static raster_changes_t background, foreground, border, sprites, next_line;
static raster_changes_all_t changes;
static geometry_t geometry;
static int machine_mode_calls;
static int machine_mode;
static unsigned int vsyncs;

void alarm_log_too_many_alarms(void)
{
    abort();
}

void alarm_unset(alarm_t *alarm)
{
    alarm->pending_idx = -1;
}

void plus4_set_ted_ntsc_mode(int ntsc)
{
    machine_mode_calls++;
    machine_mode = ntsc;
}

void ted_timer_freeze(int freeze, CLOCK run)
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

void ted_counter_update(CLOCK clk)
{
}

void ted_fetch_store(uint16_t addr, uint8_t old_value, unsigned int ram_mask)
{
}

void raster_line_emulate(raster_t *raster)
{
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

/* Line `counter' started at clock 114 * 10; the next starts at 1254.  */
static void setup(unsigned int counter)
{
    memset(&ted, 0, sizeof(ted));
    memset(&context, 0, sizeof(context));
    init_alarm(&draw_alarm);
    init_alarm(&fetch_alarm);
    init_alarm(&irq_alarm);
    ted.raster_draw_alarm = &draw_alarm;
    ted.raster_fetch_alarm = &fetch_alarm;
    ted.raster_irq_alarm = &irq_alarm;
    memset(&changes, 0, sizeof(changes));
    memset(&background, 0, sizeof(background));
    memset(&foreground, 0, sizeof(foreground));
    memset(&border, 0, sizeof(border));
    memset(&sprites, 0, sizeof(sprites));
    memset(&next_line, 0, sizeof(next_line));
    changes.background = &background;
    changes.foreground = &foreground;
    changes.border = &border;
    changes.sprites = &sprites;
    changes.next_line = &next_line;
    geometry.text_size.width = TED_SCREEN_TEXTCOLS;
    ted.raster.changes = &changes;
    ted.raster.geometry = &geometry;
    ted.cycles_per_line = 114;
    ted.first_dma_line = TED_PAL_FIRST_DMA_LINE;
    ted.last_dma_line = TED_PAL_LAST_DMA_LINE;
    ted.tv_height = TED_PAL_SCREEN_HEIGHT;
    ted.tv_vsync_line = TED_PAL_VSYNC_LINE;
    ted_timing_set_mode(0);
    ted.ted_raster_counter = counter;
    ted.last_emulate_line_clk = 114 * 10;
    ted.draw_clk = ted.last_emulate_line_clk + 114;
    ted.fetch_clk = CLOCK_MAX;
    ted.raster_irq_clk = CLOCK_MAX;
    maincpu_clk = ted.last_emulate_line_clk + 50;
    machine_mode_calls = 0;
}

static void check_irq(unsigned int counter, unsigned int irq_line,
                      int ntsc, CLOCK expected)
{
    setup(counter);
    ted.raster_irq_line = irq_line;
    ted.raster_irq_clk = ted.draw_clk + 5 * 114;
    ted_set_ntsc_mode(ntsc);
    assert(ted.raster_irq_clk == expected);
    assert(machine_mode_calls == 1 && machine_mode == ntsc);
}

int main(void)
{
    CLOCK next;
    unsigned int line;

    setup(100);
    assert(ted.screen_height == 312 && ted.vsync_line == 257);
    ted_timing_set_mode(1);
    assert(ted.screen_height == 262 && ted.vsync_line == 229);

    next = 114 * 11;
    /* Line 50 after the end of the frame: 211 lines to line 312 in PAL
       mode, 161 to line 262 in NTSC mode. */
    check_irq(100, 50, 1, next + (161 + 50) * 114);
    check_irq(100, 50, 0, next + (211 + 50) * 114);
    /* Line 200 is ahead in both modes. */
    check_irq(100, 200, 1, next + 99 * 114);
    /* Line 280 is not reached in NTSC mode. */
    check_irq(100, 280, 1, CLOCK_MAX);
    check_irq(100, 280, 0, next + 179 * 114);
    /* Beyond the last line of the new mode the counter wraps after 511. */
    check_irq(280, 50, 1, next + (231 + 50) * 114);
    check_irq(280, 300, 1, next + 19 * 114);
    /* A repeated line ($FF1E write on column 97) delays everything. */
    setup(100);
    ted.line_repeat = 1;
    ted.raster_irq_line = 50;
    ted.raster_irq_clk = ted.draw_clk;
    ted_set_ntsc_mode(1);
    assert(ted.raster_irq_clk == next + (162 + 50) * 114);
    /* The interrupt of the current line is not moved. */
    setup(100);
    ted.raster_irq_line = 100;
    ted.raster_irq_clk = ted.last_emulate_line_clk;
    ted_set_ntsc_mode(1);
    assert(ted.raster_irq_clk == ted.last_emulate_line_clk);

    /* After the last DMA line the next matrix DMA is on line 0. */
    setup(250);
    ted.fetch_clk = next + 61 * 114 + TED_FETCH_CYCLE;
    ted_set_ntsc_mode(1);
    assert(ted.fetch_clk == next + 11 * 114 + TED_FETCH_CYCLE);
    /* On DMA lines it is on the next line in both modes. */
    setup(100);
    ted.fetch_clk = next + TED_FETCH_CYCLE;
    ted_set_ntsc_mode(1);
    assert(ted.fetch_clk == next + TED_FETCH_CYCLE);

    /* The register handler switches only on a change of bit 6. */
    setup(100);
    ted.regs[0x07] = 0x08;
    ted07_store(0x48);
    assert(machine_mode_calls == 1 && machine_mode == 1);
    assert(ted.screen_height == 262);
    ted07_store(0x58);
    assert(machine_mode_calls == 1);
    ted07_store(0x18);
    assert(machine_mode_calls == 2 && machine_mode == 0);
    assert(ted.screen_height == 312);

    /* A frame in NTSC mode: the counter wraps after line 261 and vertical
       sync starts at line 229, once per frame. */
    setup(0);
    ted.tv_current_line = 0;
    ted_timing_set_mode(1);
    vsyncs = 0;
    for (line = 0; line < 2 * 262; line++) {
        maincpu_clk = ted.draw_clk;
        ted_raster_draw_alarm_handler(0, NULL);
        if (ted.ted_raster_counter == 229) {
            assert(ted.tv_current_line == 0);
        }
    }
    assert(ted.ted_raster_counter == 0 && vsyncs == 2);

    puts("TED PAL/NTSC mode tests passed");
    return 0;
}
