/* Exercise the production TED shifter and FF1E handler. The RAM bus and
   snapshot byte transport are substituted; no game or ROM is required. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alarm.h"
#include "maincpu.h"
#include "snapshot.h"
#include "ted-counter.h"
#include "ted-timing.h"
#include "ted-video.h"
#include "tedtypes.h"

ted_t ted;
CLOCK maincpu_clk;
static uint8_t ram[0x10000];
static alarm_context_t context;
static alarm_t draw_alarm, fetch_alarm, irq_alarm;

uint8_t *mem_get_tedmem_base(unsigned int segment)
{
    return ram + (segment & 3) * 0x4000;
}

void alarm_unset(alarm_t *alarm)
{
    alarm->pending_idx = -1;
}

void alarm_log_too_many_alarms(void)
{
    abort();
}

void ted_delay_hold_clock(unsigned int from, unsigned int to)
{
}

void ted_delay_resync(void)
{
}

struct snapshot_module_s {
    uint8_t bytes[2048];
    unsigned int pos, size;
};

int snapshot_module_write_byte_array(snapshot_module_t *m, const uint8_t *p,
                                     unsigned int size)
{
    assert(m->pos + size <= sizeof(m->bytes));
    memcpy(m->bytes + m->pos, p, size);
    m->pos += size;
    m->size = m->pos;
    return 0;
}

int snapshot_module_read_byte_array(snapshot_module_t *m, uint8_t *p,
                                    unsigned int size)
{
    if (m->pos + size > m->size) {
        return -1;
    }
    memcpy(p, m->bytes + m->pos, size);
    m->pos += size;
    return 0;
}

#define TRANSPORT(name, type) \
    int snapshot_module_write_##name(snapshot_module_t *m, type value) \
    { return snapshot_module_write_byte_array(m, (uint8_t *)&value, sizeof(value)); } \
    int snapshot_module_read_##name(snapshot_module_t *m, type *value) \
    { return snapshot_module_read_byte_array(m, (uint8_t *)value, sizeof(*value)); }
TRANSPORT(byte, uint8_t)
TRANSPORT(word, uint16_t)
TRANSPORT(qword, uint64_t)

int snapshot_module_read_byte_into_int(snapshot_module_t *m, int *value)
{
    uint8_t b;
    if (snapshot_module_read_byte(m, &b) < 0) { return -1; }
    *value = b;
    return 0;
}

int snapshot_module_read_byte_into_uint(snapshot_module_t *m, unsigned int *value)
{
    uint8_t b;
    if (snapshot_module_read_byte(m, &b) < 0) { return -1; }
    *value = b;
    return 0;
}

int snapshot_module_read_word_into_uint(snapshot_module_t *m, unsigned int *value)
{
    uint16_t w;
    if (snapshot_module_read_word(m, &w) < 0) { return -1; }
    *value = w;
    return 0;
}

static void setup(void)
{
    unsigned int i;
    memset(&ted, 0, sizeof(ted));
    memset(ram, 0, sizeof(ram));
    memset(&context, 0, sizeof(context));
    memset(&draw_alarm, 0, sizeof(draw_alarm));
    memset(&fetch_alarm, 0, sizeof(fetch_alarm));
    memset(&irq_alarm, 0, sizeof(irq_alarm));
    draw_alarm.context = fetch_alarm.context = irq_alarm.context = &context;
    draw_alarm.pending_idx = fetch_alarm.pending_idx = irq_alarm.pending_idx = -1;
    ted.raster_draw_alarm = &draw_alarm;
    ted.raster_fetch_alarm = &fetch_alarm;
    ted.raster_irq_alarm = &irq_alarm;
    ted.raster_irq_clk = CLOCK_MAX;
    ted.draw_clk = 114;
    ted.cycles_per_line = 114;
    ted.tv_height = TED_PAL_SCREEN_HEIGHT;
    ted.screen_leftborderwidth = 32;
    ted.character_fetch_on = 1;
    ted.regs[6] = 0x20;
    ted.regs[7] = 8;
    ted.regs[0x12] = 8;
    ted.regs[0x15] = 0x21;
    ted.regs[0x16] = 0x51;
    ted.regs[0x19] = 0x71;
    memset(ted.vbuf, 0x21, sizeof(ted.vbuf));
    memset(ted.cbuf, 0x43, sizeof(ted.cbuf));
    for (i = 0; i < 1024; i++) {
        ram[0x2000 + i * 8] = 0x80;
    }
    ted_video_reset(0);
}

static void counter_store(CLOCK clk, uint8_t value)
{
    maincpu_clk = clk;
    ted_video_update(clk);
    ted_counter_update(clk);
    ted.video_changed = 1;
    ted_counter_store(value);
}

int main(void)
{
    uint8_t prefix[TED_VIDEO_LINE_SIZE];
    snapshot_module_t saved = {{0}, 0, 0}, expected = {{0}, 0, 0};
    snapshot_module_t restored = {{0}, 0, 0};
    ted_t before;
    unsigned int i;

    setup();
    ram[0x2008] = 0x40;
    ted_video_update(114);
    assert(ted.video_line[31] == 0x71);
    assert(ted.video_line[32] == 0x32 && ted.video_line[33] == 0x41);
    assert(ted.video_line[40] == 0x41 && ted.video_line[41] == 0x32);
    assert(ted.video_line[351] == 0x41 && ted.video_line[352] == 0x71);

    /* Rewinding before the stop extends bitmap fetches; jumping over the
       stop edges keeps the display and fetch window open. Attribute bytes
       stop after 40 cells; the stopped bitmap position keeps the final byte. */
    setup();
    ted.regs[7] |= 0x10;
    for (i = 0; i < 1024; i++) { ram[0x2000 + i * 8] = 0x23; }
    counter_store(89, 0x80);
    memcpy(prefix, ted.video_line, sizeof(prefix));
    counter_store(101, 0x5c);
    ted_video_update(ted.draw_clk);
    assert(memcmp(prefix, ted.video_line, (89 - 16) * 4 + 32) == 0);
    assert(ted.video_display && ted.video_fetching && !ted.video_counting);
    assert(ted.video_line[352] == 0x21 && ted.video_line[354] == 0);
    assert(ted.video_line[358] == 0x51);
    assert(ted.video_line[376] == ted.video_line[384]);

    /* A later RAM write cannot change a byte already sent to the TV. */
    setup();
    ted_video_update(50);
    memcpy(prefix, ted.video_line, sizeof(prefix));
    memset(ram + 0x2000, 0, 0x2000);
    ted_video_update(114);
    assert(memcmp(prefix + 32, ted.video_line + 32, 140) == 0);

    /* Cross-standard output loses chroma and uses the crystal's pixel
       width: a border change after 50 NTSC clocks is at PAL pixel 132. */
    setup();
    ted.regs[7] = 0x48;
    ted.raster.blank_enabled = 1;
    ted.regs[0x19] = 0x7f;
    ted_video_update(50);
    ted.regs[0x19] = 0;
    ted_video_update(100);
    assert(ted.video_line[131] == 0x71 && ted.video_line[132] == 0);
    setup();
    ted.raster.blank_enabled = 1;
    ted.regs[0x19] = 0x7f;
    ted_video_update(50);
    assert(ted.video_line[131] == 0x7f);

    /* Restore halfway through an extended line, then compare its output
       and pending bytes with the uninterrupted continuation. */
    setup();
    counter_store(89, 0x80);
    assert(ted_video_snapshot_write(&saved) == 0);
    before = ted;
    ted_video_update(101);
    assert(ted_video_snapshot_write(&expected) == 0);
    ted = before;
    ted_video_reset(0);
    saved.pos = 0;
    assert(ted_video_snapshot_read(&saved) == 0);
    ted_video_update(101);
    assert(ted_video_snapshot_write(&restored) == 0);
    assert(expected.size == restored.size);
    assert(memcmp(expected.bytes, restored.bytes, expected.size) == 0);
    saved.pos = 0;
    saved.size--;
    assert(ted_video_snapshot_read(&saved) < 0);

    /* All inverted FF1E values, including overflow counts, remain within
       the fixed output buffer in both phases (also run under sanitizers). */
    for (i = 0; i < 512; i++) {
        setup();
        counter_store(50 + (i & 1), i >> 1);
        ted_video_update(ted.draw_clk);
    }
    puts("TED video: counter jumps, held bytes, attribute window, chroma, pixel width, RAM writes and snapshots passed");
    return 0;
}
