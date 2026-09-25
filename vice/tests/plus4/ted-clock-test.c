/* Compare TED's batched CPU clock conversion with individual clock slots.
   The renderer is replaced; the production clock conversion is included. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/plus4/ted-timing.c"

ted_t ted;
CLOCK maincpu_clk;

void dma_maincpu_steal_cycles(CLOCK start, CLOCK count, CLOCK sub)
{
    assert(count <= 16);
    maincpu_clk += count;
}

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
                    ted.fastmode = fast;
                    ted.character_fetch_on = fetch;
                    maincpu_clk = phase;
                    ted_delay_resync();
                    expected = clock_slots(maincpu_clk, count, fast, fetch);
                    maincpu_clk += count;
                    ted_delay_clk();
                    if (maincpu_clk != expected) {
                        fprintf(stderr, "fast %d fetch %d phase %u count %u: %llu != %llu\n",
                                fast, fetch, phase, count,
                                (unsigned long long)maincpu_clk, (unsigned long long)expected);
                        return 1;
                    }
                }
            }
        }
    }
    puts("TED CPU clock slots passed (all phases, single/double clock, display/border)");
    return 0;
}
