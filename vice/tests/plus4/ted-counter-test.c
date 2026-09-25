/* TED horizontal-event regressions.  Compile the production counter module;
   only CPU timing notification and alarm allocation are replaced here. */
#include "vice.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alarm.h"
#include "ted-counter.h"
#include "tedtypes.h"

ted_t ted;
CLOCK maincpu_clk;
static alarm_context_t context;
static alarm_t alarms[3];
static unsigned int resyncs;

void ted_delay_resync(void)
{
    resyncs++;
}

void alarm_log_too_many_alarms(void)
{
    abort();
}

static void setup(void)
{
    unsigned int i;

    memset(&ted, 0, sizeof(ted));
    memset(&context, 0, sizeof(context));
    memset(alarms, 0, sizeof(alarms));
    context.next_pending_alarm_clk = CLOCK_MAX;
    for (i = 0; i < 3; i++) {
        alarms[i].context = &context;
        alarms[i].pending_idx = -1;
    }
    ted.raster_draw_alarm = &alarms[0];
    ted.raster_fetch_alarm = &alarms[1];
    ted.raster_irq_alarm = &alarms[2];
    ted.cycles_per_line = 114;
    ted.draw_clk = 114;
    ted.fetch_clk = 4;
    ted.raster_irq_clk = CLOCK_MAX;
    ted.character_fetch_on = 1;
    ted.raster.ycounter = 6;
    maincpu_clk = 0;
    resyncs = 0;
}

static void advance(CLOCK count)
{
    maincpu_clk += count;
    ted_counter_update(maincpu_clk);
}

/* Keep the unoptimized, per-clock implementation as an equivalence oracle.
   The register/event assertions below remain the hardware contract tests. */
static void counter_update_reference(CLOCK clk)
{
    while (ted.counter_clk < clk) {
        unsigned int cycle;
        ted.counter_clk++;
        cycle = TED_RASTER_CYCLE(ted.counter_clk);
        if (cycle == 8 && ted.counter_clk >= ted.counter_overflow_until) {
            if (ted.counter_increment) {
                ted.memptr_col = (ted.mem_counter + 1) & 0x3ff;
            }
            ted.mem_counter = ted.memptr_col;
            ted.memptr = ted.chr_pos_reload;
            ted.chr_pos_count = ted.memptr;
            ted.counter_increment = ted.character_fetch_on;
        }
        /* The latch ends the increments (last one at cycle 88). */
        if (cycle == TED_POSITION_LATCH_CYCLE) {
            if (ted.character_fetch_on) {
                if (ted.raster.ycounter == 6) {
                    ted.memptr_col = ted.mem_counter;
                }
                if (ted.chr_pos_latch && !ted.idle_state) {
                    ted.chr_pos_reload = ted.chr_pos_count;
                }
            }
            ted.counter_increment = 0;
        }
        if (!(cycle & 1) && ted.counter_increment
            && (cycle != 8 || ted.counter_clk < ted.counter_overflow_until)) {
            ted.mem_counter = (ted.mem_counter + 1) & 0x3ff;
            if (!ted.idle_state) {
                ted.chr_pos_count = (ted.chr_pos_count + 1) & 0x3ff;
            }
        }
    }
}

static uint32_t random_state = 1;

static unsigned int next_random(void)
{
    random_state = random_state * 1664525U + 1013904223U;
    return random_state >> 8;
}

static void check_equivalence(CLOCK target)
{
    static ted_t before, expected;

    before = ted;
    counter_update_reference(target);
    expected = ted;
    ted = before;
    ted_counter_update(target);
    assert(memcmp(&ted, &expected, sizeof(ted)) == 0);
}

static void test_equivalence(void)
{
    unsigned int i;
    unsigned int phase;
    unsigned int value;

    /* Vary chunk length, wrap, idle/fetch enables, row and address overflow. */
    for (i = 0; i < 50000; i++) {
        setup();
        ted.last_emulate_line_clk = 1000;
        ted.counter_clk = 1000 + next_random() % 114;
        ted.counter_overflow_until = 1000 + next_random() % 130;
        ted.counter_increment = next_random() & 1;
        ted.character_fetch_on = next_random() & 1;
        ted.idle_state = next_random() & 1;
        ted.raster.ycounter = next_random() & 7;
        ted.chr_pos_latch = next_random() & 1;
        ted.mem_counter = next_random() & 0x3ff;
        ted.memptr_col = next_random() & 0x3ff;
        ted.memptr = next_random() & 0x3ff;
        ted.chr_pos_count = next_random() & 0x3ff;
        ted.chr_pos_reload = next_random() & 0x3ff;
        check_equivalence(ted.counter_clk + next_random() % 228);
    }
    /* Real register writes exercise overflow and the altered line origin. */
    for (phase = 0; phase < 2; phase++) {
        for (value = 0; value < 256; value++) {
            setup();
            advance(49 + phase);
            ted_counter_store((uint8_t)value);
            check_equivalence(maincpu_clk + 114);
        }
    }
}

int main(void)
{
    CLOCK irq;
    unsigned int value;
    unsigned int phase;

    setup();
    ted.memptr_col = 56;
    ted.chr_pos_reload = 92;
    advance(7);
    assert(ted.mem_counter == 0);
    advance(1);
    assert(ted.mem_counter == 56 && ted.memptr == 92);
    advance(80);
    assert(ted.mem_counter == 96 && ted.chr_pos_count == 132);
    assert(ted.memptr_col == 56);
    advance(1);
    assert(ted.memptr_col == 56); /* latched at cycle 90 (FPGATED) */
    advance(1);
    assert(ted.memptr_col == 96);
    assert(ted.chr_pos_reload == 92);
    advance(24);
    assert(ted.mem_counter == 96);

    setup();
    ted.raster.ycounter = 7;
    ted.chr_pos_latch = 1;
    ted.chr_pos_reload = 1000;
    advance(90);
    assert(ted.chr_pos_count == 16);
    assert(ted.chr_pos_reload == 16);
    assert(ted.memptr == 1000); /* current row is not the next-row reload */

    /* The bitmap position latch follows the row at the start of the line
       (FPGATED CharPosLatch), not a $ff1f write during it. */
    setup();
    ted.raster.ycounter = 0; /* written after the line began with row 6 */
    ted.chr_pos_latch = 1;
    ted.chr_pos_reload = 1000;
    advance(90);
    assert(ted.chr_pos_reload == 16);
    setup();
    ted.raster.ycounter = 7; /* written after the line began with row 5 */
    ted.chr_pos_latch = 0;
    ted.chr_pos_reload = 1000;
    advance(90);
    assert(ted.chr_pos_reload == 1000);

    /* A forward jump skips increments; it must not count skipped columns. */
    setup();
    advance(49);
    assert(ted.mem_counter == 20);
    ted_counter_store(0x7f);
    assert(TED_RASTER_CYCLE(maincpu_clk) == 81);
    assert(ted_counter_read() == 0x82); /* inverted write, preserved phase */
    assert(ted.draw_clk - maincpu_clk == 33);
    assert(resyncs == 1);
    advance(9);
    assert(ted.mem_counter == 24 && ted.memptr_col == 24);
    /* Jumping back after stop cannot silently restart the incrementer. */
    ted_counter_store(0xd1);
    advance(50);
    assert(ted.mem_counter == 24);

    /* Reading cannot consume time or change counter state. */
    value = ted_counter_read();
    assert(ted_counter_read() == value);
    assert(ted.mem_counter == 24);

    /* HSP re-enters reload without crossing the increment stop.  The DMA
       position advances independently of the bitmap reload, even in idle. */
    setup();
    ted.idle_state = 1;
    ted.chr_pos_reload = 92;
    advance(30);
    assert(ted.mem_counter == 11);
    ted_counter_store(0x2f); /* back to cycle 6, before reload */
    advance(2);
    assert(ted.memptr_col == 12 && ted.mem_counter == 12);
    assert(ted.chr_pos_reload == 92 && ted.memptr == 92);

    setup();
    ted.memptr_col = 0x3ff - 11;
    advance(30);
    assert(ted.mem_counter == 0x3ff);
    ted_counter_store(0x2f);
    advance(2);
    assert(ted.memptr_col == 0 && ted.mem_counter == 0);

    /* No visible-fetch window: horizontal events must not advance memory. */
    setup();
    ted.character_fetch_on = 0;
    advance(113);
    assert(ted.mem_counter == 0 && ted.chr_pos_count == 0);

    /* Bitmap idle does not prevent the matrix position from advancing. */
    setup();
    ted.idle_state = 1;
    advance(90);
    assert(ted.mem_counter == 40 && ted.chr_pos_count == 0);

    /* Values above dot 455 must overflow at 512, not wrap at 456. */
    setup();
    advance(20);
    ted_counter_store(0x00);
    assert(ted_counter_read() == 0xfc);
    advance(1);
    assert(ted_counter_read() == 0xfe);
    advance(1);
    assert(ted_counter_read() == 0x00);

    /* Overflow aliases raster cycle 8, but must not reload the position. */
    setup();
    advance(20);
    assert(ted.mem_counter == 6);
    ted_counter_store(0x10); /* column 118, raster coordinate 6 */
    advance(2);
    assert(ted.mem_counter == 7);
    assert(ted.memptr_col == 0);

    /* Every write value preserves the low clock phase, including overflow. */
    for (phase = 0; phase < 2; phase++) {
        for (value = 0; value < 256; value++) {
            setup();
            advance(20 + phase);
            ted_counter_store((uint8_t)value);
            assert(ted_counter_read() == ((~value & 0xfc) | (phase << 1)));
            assert(ted.draw_clk > maincpu_clk);
            assert(ted.fetch_clk > maincpu_clk);
            assert(ted.raster_irq_clk == CLOCK_MAX);
        }
    }

    setup();
    advance(49);
    ted.raster_irq_clk = irq = 114 * 100;
    ted_counter_store(0x7f);
    assert(ted.raster_irq_clk == irq - 32);
    assert(alarms[2].pending_idx >= 0);

    test_equivalence();
    puts("TED horizontal-counter regressions passed (including batching equivalence)");
    return 0;
}
