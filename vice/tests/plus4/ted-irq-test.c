/* TED IRQ flags are latched independently of the CPU interrupt mask.
   Exercise the production comparator and register handlers. */
#include "vice.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "interrupt.h"

static int irq_line;
static CLOCK irq_clk;
static void test_irq(interrupt_cpu_status_t *cs, unsigned int int_num,
                     int value, CLOCK clk)
{
    irq_line = value;
    irq_clk = clk;
}
#define interrupt_set_irq test_irq
#include "../../src/plus4/ted-irq.c"
#include "../../src/plus4/ted-mem.c"
#include "../../src/plus4/ted-timing.c"

ted_t ted;
CLOCK maincpu_clk;
int maincpu_rmw_flag;
interrupt_cpu_status_t *maincpu_int_status;
static alarm_context_t context;
static alarm_t raster_alarm;

void alarm_log_too_many_alarms(void) { abort(); }
void alarm_unset(alarm_t *a)
{
    alarm_context_t *ctx = a->context;
    if (a->pending_idx >= 0) {
        unsigned int last = --ctx->num_pending_alarms;
        ctx->pending_alarms[a->pending_idx] = ctx->pending_alarms[last];
        ctx->pending_alarms[a->pending_idx].alarm->pending_idx = a->pending_idx;
        a->pending_idx = -1;
        alarm_context_update_next_pending(ctx);
    }
}

static void setup(unsigned int line, unsigned int compare)
{
    memset(&ted, 0, sizeof(ted));
    memset(&context, 0, sizeof(context));
    memset(&raster_alarm, 0, sizeof(raster_alarm));
    raster_alarm.context = &context;
    raster_alarm.pending_idx = -1;
    context.next_pending_alarm_clk = CLOCK_MAX;
    ted.raster_irq_alarm = &raster_alarm;
    ted.cycles_per_line = 114;
    ted.fastmode = 1;
    ted.screen_height = 312;
    ted.ted_raster_counter = line;
    ted.raster_irq_line = compare;
    ted.raster_irq_clk = CLOCK_MAX;
    ted.regs[0x0a] = compare >> 8;
    maincpu_clk = 30;
    maincpu_rmw_flag = 0;
    irq_line = 0;
}

int main(void)
{
    unsigned int high, enabled;

    for (high = 0; high < 2; high++) {
        for (enabled = 0; enabled < 2; enabled++) {
            setup(high ? 0x12a : 0x2a, high ? 0x2a : 0x29);
            ted.regs[0x0a] |= enabled * 2;
            if (high) {
                ted0a_store(1 | (enabled * 2));
            } else {
                ted0b_store(0x2a);
            }
            assert((ted.irq_status & 2) == 2);
            assert(irq_line == (int)enabled);
            assert(irq_clk == maincpu_clk);
            assert(!!(ted09_read() & 0x80) == enabled);

            /* Enabling a pending source asserts IRQ without a new match. */
            ted0a_store((high ? 1 : 0) | 2);
            assert(irq_line == 1);
            assert((ted.irq_status & 0x82) == 0x82);
            ted09_store(2);
            assert(irq_line == 0);
            assert((ted.irq_status & 0x82) == 0);

            /* Rewriting the same compare does not create another edge. */
            ted0b_store(0x2a);
            assert((ted.irq_status & 2) == 0);
        }
    }

    setup(42, 41);
    ted0b_store(43);
    assert((ted.irq_status & 2) == 0);
    ted_irq_raster_set(maincpu_clk);
    assert((ted.irq_status & 0x82) == 2);
    ted_irq_timer1_set(maincpu_clk);
    ted0a_store(8);
    assert(irq_line == 1);
    ted09_store(2);
    assert(irq_line == 1);
    assert((ted.irq_status & 0x8a) == 0x88);
    ted09_store(8);
    assert(irq_line == 0);

    /* In single clock the CPU core's two-clock delay must end after the
       second CPU cycle (slots 23 and 25), not after two clocks. */
    setup(42, 41);
    ted.character_fetch_on = 1;
    ted.regs[0x0a] = 2;
    ted_irq_raster_set(22);
    assert(irq_line == 1);
    assert(irq_clk == 24);

    puts("TED raster IRQ mask tests passed");
    return 0;
}
