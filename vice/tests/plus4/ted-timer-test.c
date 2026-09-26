/* TED timer regression tests. Link the production timer and alarm code.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "vice.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alarm.h"
#include "maincpu.h"
#include "tedtypes.h"
#include "ted-timer.h"
#include "ted-irq.h"
#include "snapshot.h"

CLOCK maincpu_clk;
static alarm_context_t context;
alarm_context_t *maincpu_alarm_context = &context;
ted_t ted;
static unsigned int irqs[3];
static CLOCK irq_clock[3];

void ted_irq_timer1_set(CLOCK clk) { irqs[0]++; irq_clock[0] = clk; }
void ted_irq_timer2_set(CLOCK clk) { irqs[1]++; irq_clock[1] = clk; }
void ted_irq_timer3_set(CLOCK clk) { irqs[2]++; irq_clock[2] = clk; }

/* Minimal alarm allocation; scheduling and dispatch use alarm.h unchanged. */
alarm_t *alarm_new(alarm_context_t *ctx, const char *name,
                   alarm_callback_t callback, void *data)
{
    static alarm_t alarms[3];
    static unsigned int allocated;
    alarm_t *a;
    assert(allocated < 3);
    a = &alarms[allocated++];
    (void)name;
    a->context = ctx;
    a->callback = callback;
    a->data = data;
    a->pending_idx = -1;
    return a;
}
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
/* In-memory snapshot module for the timer state round trip. */
struct snapshot_module_s {
    uint8_t data[64];
    unsigned int size;
    unsigned int pos;
};
int snapshot_module_write_byte(snapshot_module_t *m, uint8_t data)
{
    assert(m->size < sizeof(m->data));
    m->data[m->size++] = data;
    return 0;
}
int snapshot_module_write_word(snapshot_module_t *m, uint16_t data)
{
    snapshot_module_write_byte(m, (uint8_t)data);
    return snapshot_module_write_byte(m, (uint8_t)(data >> 8));
}
int snapshot_module_write_dword(snapshot_module_t *m, uint32_t data)
{
    snapshot_module_write_word(m, (uint16_t)data);
    return snapshot_module_write_word(m, (uint16_t)(data >> 16));
}
int snapshot_module_read_byte(snapshot_module_t *m, uint8_t *b_return)
{
    if (m->pos >= m->size) {
        return -1;
    }
    *b_return = m->data[m->pos++];
    return 0;
}
int snapshot_module_read_word(snapshot_module_t *m, uint16_t *w_return)
{
    uint8_t lo, hi;
    if (snapshot_module_read_byte(m, &lo) < 0
        || snapshot_module_read_byte(m, &hi) < 0) {
        return -1;
    }
    *w_return = (uint16_t)(lo | (hi << 8));
    return 0;
}
int snapshot_module_read_dword(snapshot_module_t *m, uint32_t *dw_return)
{
    uint16_t lo, hi;
    if (snapshot_module_read_word(m, &lo) < 0
        || snapshot_module_read_word(m, &hi) < 0) {
        return -1;
    }
    *dw_return = lo | ((uint32_t)hi << 16);
    return 0;
}
static unsigned int count(unsigned int timer)
{
    return ted_timer_read(timer * 2) | (ted_timer_read(timer * 2 + 1) << 8);
}
static void start(unsigned int timer, unsigned int value)
{
    ted_timer_store(timer * 2, value & 255);
    ted_timer_store(timer * 2 + 1, value >> 8);
}
static void advance(CLOCK cycles)
{
    maincpu_clk += cycles;
    while (context.next_pending_alarm_clk <= maincpu_clk) {
        alarm_context_dispatch(&context, maincpu_clk);
    }
}
int main(void)
{
    unsigned int i;
    context.next_pending_alarm_clk = CLOCK_MAX;
    ted_timer_init();
    for (i = 1; i < 3; i++) {
        /* A low-byte write stops the LIVE count and preserves its high byte. */
        start(i, 0x1234);
        advance(0x40 * 2);
        assert(count(i) == 0x11f4);
        ted_timer_store(i * 2, 0x56);
        assert(count(i) == 0x1156);
        advance(20);
        assert(count(i) == 0x1156);
        ted_timer_store(i * 2 + 1, 0x23);
        assert(count(i) == 0x2356);
        advance(20);
        /* A high-byte-only write must retain the live low byte. */
        ted_timer_store(i * 2 + 1, 0x34);
        assert(count(i) == 0x344c);
        ted_timer_store(i * 2, 0);
    }
    ted_timer_reset();
    for (i = 0; i < 3; i++) {
        unsigned int expected = i == 0 ? 97 : 65533;
        start(i, 100);
        /* The callback is serviced three TED clocks after expiry. */
        advance(206);
        assert(count(i) == expected);
        assert(irqs[i] == 1);
        assert(irq_clock[i] == maincpu_clk - 6);
        assert(count(i) == expected); /* repeated reads cannot consume time */
        ted_timer_store(i * 2, 0);
    }
    ted_timer_reset();
    for (i = 1; i < 3; i++) {
        start(i, 0x1234);
        advance(0x40 * 2);
        /* No intervening read to synchronize the counter. */
        ted_timer_store(i * 2, 0x78);
        assert(count(i) == 0x1178);
    }
    /* Timer 1 is programmed using the documented low/high sequence.
       Its stopped live count must not advance before the high-byte write. */
    ted_timer_reset();
    ted_timer_store(0, 0x34);
    assert(!ted.timer_running[0]);
    advance(20);
    ted_timer_store(1, 0x12);
    assert(count(0) == 0x1234);
    advance(0x1234 * 2);
    assert(count(0) == 0x1234);
    /* Multiple overdue reloads, and the zero encoding of 65536 clocks. */
    ted_timer_reset();
    start(0, 10);
    advance(66);
    assert(count(0) == 7);
    ted_timer_reset();
    for (i = 0; i < 3; i++) {
        start(i, 0);
        advance(2);
        assert(count(i) == 65535);
        ted_timer_store(i * 2, 0);
    }
    start(0, 0x1234);
    start(1, 0x5678);
    start(2, 0x9abc);
    ted_timer_reset();
    advance(1000);
    for (i = 0; i < 3; i++) {
        assert(!ted.timer_running[i]);
        assert(count(i) == 0);
    }
    assert(context.next_pending_alarm_clk == CLOCK_MAX);
    /* A snapshot keeps the timers in phase: Alpharay starts timer 1 once
       with a four line period and relies on it for its HUD split.  A
       restore over a different timer state must continue exactly as the
       machine that saved it. */
    {
        snapshot_module_t m;
        unsigned int saved[3];
        unsigned int after[3];
        unsigned int irqs_after[3];
        CLOCK irq_after[3];
        CLOCK snap_clk;

        memset(&m, 0, sizeof(m));
        ted_timer_reset();
        start(0, 228);
        start(1, 0x1000);
        start(2, 0x2000);
        advance(1001);
        ted_timer_store(4, 0x55);
        advance(333);
        snap_clk = maincpu_clk;
        assert(ted_timer_snapshot_write(&m) == 0);
        for (i = 0; i < 3; i++) {
            saved[i] = count(i);
            irqs[i] = 0;
        }
        advance(20000);
        for (i = 0; i < 3; i++) {
            after[i] = count(i);
            irqs_after[i] = irqs[i];
            irq_after[i] = irq_clock[i];
        }
        ted_timer_reset();
        maincpu_clk = snap_clk;
        start(0, 0x39f1);
        assert(ted_timer_snapshot_read(&m) == 0);
        assert(ted.t1_start == 228);
        assert(ted.timer_running[0] && ted.timer_running[1]
               && !ted.timer_running[2]);
        for (i = 0; i < 3; i++) {
            assert(count(i) == saved[i]);
            irqs[i] = 0;
        }
        advance(20000);
        for (i = 0; i < 3; i++) {
            assert(count(i) == after[i]);
            assert(irqs[i] == irqs_after[i]);
            assert(irqs[i] == 0 || irq_clock[i] == irq_after[i]);
        }
        assert(irqs_after[0] > 0);
        m.pos = 0;
        m.size = 3;
        assert(ted_timer_snapshot_read(&m) < 0);
    }
    /* $FF07 bit 5 stops the counting: the counts stay during the freeze,
       also of a timer started while frozen, and count again after it. */
    ted_timer_reset();
    {
        unsigned int before[3];
        CLOCK resume;
        snapshot_module_t m = { { 0 }, 0, 0 };

        for (i = 0; i < 3; i++) {
            start(i, 1000);
            before[i] = irqs[i];
        }
        advance(200);
        ted_timer_freeze(1, 0);
        advance(5001);
        for (i = 0; i < 3; i++) {
            assert(count(i) == 900);
            assert(irqs[i] == before[i]);
        }
        /* The frozen counts survive a snapshot round trip. */
        assert(ted_timer_snapshot_write(&m) == 0);
        assert(ted_timer_snapshot_read(&m) == 0);
        advance(1000);
        for (i = 0; i < 3; i++) {
            assert(count(i) == 900);
        }
        /* One clock runs after the last whole single clock. */
        ted_timer_freeze(0, 1);
        resume = maincpu_clk - 1;
        assert(count(0) == 899);
        advance(1799);
        for (i = 0; i < 3; i++) {
            assert(irqs[i] == before[i] + 1);
            assert(irq_clock[i] == resume + 1800);
        }
        /* An underflow due at the freeze reloads the counter, which then
           waits: it counted one clock before the freeze. */
        ted_timer_reset();
        before[1] = irqs[1];
        start(1, 10);
        maincpu_clk += 21;
        ted_timer_freeze(1, 0);
        advance(0);
        assert(irqs[1] == before[1] + 1 && irq_clock[1] == maincpu_clk - 1);
        advance(3000);
        assert(count(1) == 0xffff);
        assert(irqs[1] == before[1] + 1);
        ted_timer_freeze(0, 0);
        advance(4);
        assert(count(1) == 0xfffd);
    }

    puts("TED timer regression tests passed");
    return 0;
}
