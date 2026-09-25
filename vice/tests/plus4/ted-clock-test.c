/* Compare TED's batched CPU clock conversion with individual clock slots.
   The renderer is replaced; the production clock conversion is included. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/plus4/ted-timing.c"

ted_t ted;
CLOCK maincpu_clk;
static interrupt_cpu_status_t int_status;
interrupt_cpu_status_t *maincpu_int_status = &int_status;

void ted_raster_draw_alarm_handler(CLOCK offset, void *data)
{
    ted.last_emulate_line_clk += 114;
}

static CLOCK clock_slots(CLOCK clock, unsigned int count, int fast, int fetch)
{
    unsigned int phase;

    while (count--) {
        clock++;
        phase = clock % 114;
        if (!(phase & 1) && (!fast || (phase >= (fetch ? 4 : 92) && phase <= 100))) {
            clock++;
        }
    }
    return clock;
}

int main(void)
{
    unsigned int phase, count;
    int fast, fetch;
    CLOCK expected;

    for (fast = 0; fast < 2; fast++) {
        for (fetch = 0; fetch < 2; fetch++) {
            for (phase = 0; phase < 114; phase++) {
                for (count = 0; count <= 8; count++) {
                    memset(&ted, 0, sizeof(ted));
                    memset(&int_status, 0, sizeof(int_status));
                    ted.fastmode = fast;
                    ted.character_fetch_on = fetch;
                    maincpu_clk = phase;
                    ted_delay_resync();
                    expected = clock_slots(maincpu_clk, count, fast, fetch);
                    int_status.irq_clk = maincpu_clk;
                    int_status.nmi_clk = maincpu_clk;
                    maincpu_clk += count;
                    ted_delay_clk();
                    if (maincpu_clk != expected) {
                        fprintf(stderr, "fast %d fetch %d phase %u count %u: %llu != %llu\n",
                                fast, fetch, phase, count,
                                (unsigned long long)maincpu_clk, (unsigned long long)expected);
                        return 1;
                    }
                    /* The CPU is slowed, not halted: interrupts raised
                       before the stretched clocks keep their clock, and no
                       DMA is recorded that would delay them. */
                    assert(int_status.irq_clk == phase);
                    assert(int_status.nmi_clk == phase);
                    assert(int_status.last_stolen_cycles_clk == 0);
                    assert(int_status.num_dma_per_opcode == 0);
                }
            }
        }
    }

    /* The interrupt clock passed to the CPU core ends its delay after the
       second CPU cycle from the request. */
    memset(&ted, 0, sizeof(ted));
    ted.cycles_per_line = 114;
    ted.fastmode = 1;
    assert(ted_delay_irq_clk(30) == 30);        /* double clock */
    ted.character_fetch_on = 1;
    assert(ted_delay_irq_clk(0) == 0);          /* line start: slots 0, 1 */
    assert(ted_delay_irq_clk(3) == 4);          /* slots 3, 5 */
    assert(ted_delay_irq_clk(21) == 22);        /* single clock: 21, 23 */
    assert(ted_delay_irq_clk(22) == 24);        /* TED slot: 23, 25 */
    assert(ted_delay_irq_clk(113) == 113);      /* slots 113, 0 */
    ted.last_emulate_line_clk = 114;
    assert(ted_delay_irq_clk(113) == 113);      /* request before the line */

    for (fast = 0; fast < 2; fast++) {
        for (fetch = 0; fetch < 2; fetch++) {
            for (phase = 0; phase < 228; phase++) {
                CLOCK slot;
                unsigned int cpu_cycles = 0;

                memset(&ted, 0, sizeof(ted));
                ted.cycles_per_line = 114;
                ted.last_emulate_line_clk = 114;
                ted.fastmode = fast;
                ted.character_fetch_on = fetch;
                for (slot = phase; cpu_cycles < 2; slot++) {
                    unsigned int p = slot % 114;

                    if ((p & 1) || (fast && (p < (fetch ? 4U : 92U) || p > 100))) {
                        cpu_cycles++;
                    }
                }
                /* `slot' is one past the second CPU cycle. */
                assert(ted_delay_irq_clk(phase) == slot - 2);
            }
        }
    }
    puts("TED CPU clock slots passed (all phases, single/double clock, display/border)");
    return 0;
}
