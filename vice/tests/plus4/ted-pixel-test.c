/* Pixel pipeline contracts and independently stepped batching oracle. */
#include "vice.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../../src/plus4/ted-draw.c"
#include "ted-timing.h"

ted_t ted;
CLOCK maincpu_clk;
static uint8_t memory[8192];
static uint8_t published[1024];

/* Like the raster publisher, read one canvas width of pixels. */
void raster_line_emulate_pixels(struct raster_s *raster, const uint8_t *pixels)
{
    assert(raster->geometry->screen_size.width <= sizeof(published));
    memcpy(published, pixels, raster->geometry->screen_size.width);
}

/* In-memory snapshot transport; exercise the production field serializer. */
struct snapshot_module_s {
    uint8_t bytes[1024];
    unsigned int position, size;
};

static int snapshot_error;
void snapshot_set_error(int error)
{
    snapshot_error = error;
}

int snapshot_module_write_byte_array(snapshot_module_t *m, const uint8_t *data, unsigned int count)
{
    if (m->position + count > sizeof(m->bytes)) {
        return -1;
    }
    memcpy(m->bytes + m->position, data, count);
    m->position += count;
    m->size = m->position;
    return 0;
}

int snapshot_module_read_byte_array(snapshot_module_t *m, uint8_t *data, unsigned int count)
{
    if (m->position + count > m->size) {
        return -1;
    }
    memcpy(data, m->bytes + m->position, count);
    m->position += count;
    return 0;
}

int snapshot_module_write_byte(snapshot_module_t *m, uint8_t value)
{
    return snapshot_module_write_byte_array(m, &value, 1);
}

int snapshot_module_read_byte(snapshot_module_t *m, uint8_t *value)
{
    return snapshot_module_read_byte_array(m, value, 1);
}

int snapshot_module_write_dword(snapshot_module_t *m, uint32_t value)
{
    unsigned int i;
    for (i = 0; i < 4; i++) {
        if (snapshot_module_write_byte(m, value >> (8 * i)) < 0) {
            return -1;
        }
    }
    return 0;
}

int snapshot_module_read_dword(snapshot_module_t *m, uint32_t *value)
{
    uint8_t byte;
    unsigned int i;
    *value = 0;
    for (i = 0; i < 4; i++) {
        if (snapshot_module_read_byte(m, &byte) < 0) {
            return -1;
        }
        *value |= (uint32_t)byte << (8 * i);
    }
    return 0;
}

int snapshot_module_write_qword(snapshot_module_t *m, uint64_t value)
{
    return snapshot_module_write_dword(m, value) < 0 ||
           snapshot_module_write_dword(m, value >> 32) < 0 ? -1 : 0;
}

int snapshot_module_read_qword(snapshot_module_t *m, uint64_t *value)
{
    uint32_t low, high;
    if (snapshot_module_read_dword(m, &low) < 0 ||
        snapshot_module_read_dword(m, &high) < 0) {
        return -1;
    }
    *value = low | ((uint64_t)high << 32);
    return 0;
}


static void setup(unsigned int mode, unsigned int scroll)
{
    memset(&ted, 0, sizeof(ted));
    memset(memory, 0, sizeof(memory));
    ted.screen_leftborderwidth = 32;
    ted.regs[6] = 0x18 | ((mode & 6) << 4);
    ted.regs[7] = 0x88 | ((mode & 1) << 4) | scroll;
    ted.regs[0x15] = 2;
    ted.regs[0x16] = 3;
    ted.regs[0x17] = 4;
    ted.regs[0x18] = 5;
    ted.regs[0x19] = 6;
    ted.character_fetch_on = 1;
    ted.cursor_visible = 1;
    ted.crsrpos = 0x3ff;
    ted.chargen_ptr = ted.bitmap_ptr = memory;
    memset(ted.cbuf, 0x19, sizeof(ted.cbuf));
    maincpu_clk = 0;
    ted_draw_init();
}

static void write_register(unsigned int reg, unsigned int value, CLOCK clk)
{
    maincpu_clk = clk;
    ted_draw_store(reg, (uint8_t)value);
    ted.regs[reg] = value;
}

static void colors(void)
{
    unsigned int m, reg, x;

    /* The selected global colour alone sparkles, for precisely one dot.
       Cover background 0, both multicolour registers and all ECM colours. */
    for (m = 0; m < 2; m++) {
        for (reg = 0; reg < 4; reg++) {
            if (m == 0 && reg == 3) { continue; }
            setup(m ? TED_EXTENDED_TEXT_MODE : TED_MULTICOLOR_TEXT_MODE, 0);
            if (m) {
                memset(ted.vbuf, reg << 6, sizeof(ted.vbuf));
            } else {
                memset(memory, reg == 0 ? 0 : reg == 1 ? 0x55 : 0xaa, sizeof(memory));
            }
            write_register(0x15 + reg, 0x37, 19); /* dot 12 of display */
            ted_draw_sync(25);
            for (x = 0; x < 32; x++) {
                unsigned int expected = x < 12 ? reg + 2 : x == 12 ? 127 : 0x37;
                assert(beam.line[32 + x] == expected);
            }
        }
    }
    setup(TED_NORMAL_TEXT_MODE, 0);
    write_register(0x19, 0x34, 12); /* left border dot 16 */
    ted_draw_sync(16);
    assert(beam.line[15] == 6 && beam.line[16] == 127 && beam.line[17] == 0x34);
}

static void fetched_memory(void)
{
    setup(TED_HIRES_BITMAP_MODE, 0);
    memset(memory, 255, sizeof(memory));
    memset(ted.vbuf, 0x12, sizeof(ted.vbuf));
    memset(ted.cbuf, 0x34, sizeof(ted.cbuf));
    ted_draw_sync(17); /* first four bits have left the shifter */
    memset(memory, 0, sizeof(memory));
    ted_draw_sync(20);
    for (unsigned int x = 0; x < 16; x++) {
        assert(beam.line[32 + x] == (x < 8 ? 0x41 : 0x32));
    }
}

static void counter_delay(void)
{
    setup(TED_NORMAL_TEXT_MODE, 0);
    write_register(0x1e, 0, 20);
    assert(beam.h == 16 && beam.pending_counter);
    /* The first pixel still uses the old counter; only then are bits 8:3
       replaced.  Bits 2:0 continue counting, including double-clock phase. */
    dot_events();
    emit(1);
    assert(beam.h == 17);
    /* Restart and exercise the production event dispatcher. */
    setup(TED_NORMAL_TEXT_MODE, 0);
    write_register(0x1e, 0, 20);
    ted_draw_sync(21);
    assert(beam.h == 508 && !beam.pending_counter);
    assert(beam.x == 84); /* output position did not jump */
    ted_draw_sync(22);
    assert(beam.h == 0);
}

/* NTSC with debug borders has the widest canvas, 520 dots.  The line and
   the black line must both cover it: the publisher reads the full width. */
static void wide_canvas(void)
{
    geometry_t geometry;
    unsigned int width = TED_SCREEN_XPIX + TED_SCREEN_NTSC_DEBUG_LEFTBORDERWIDTH
                         + TED_SCREEN_NTSC_DEBUG_RIGHTBORDERWIDTH;

    setup(TED_NORMAL_TEXT_MODE, 0);
    memset(&geometry, 0, sizeof(geometry));
    geometry.screen_size.width = width;
    ted.raster.geometry = &geometry;
    ted.screen_leftborderwidth = TED_SCREEN_NTSC_DEBUG_RIGHTBORDERWIDTH;
    assert(sizeof(beam.line) >= width);
    memset(published, 0xff, sizeof(published));
    ted_draw_line(114, 1);
    assert(published[0] == 6 && published[width - 1] == 6);
    ted_draw_black_line();
    assert(published[0] == 0 && published[width - 1] == 0);
    ted.raster.geometry = NULL;
}

static void snapshots(void)
{
    snapshot_module_t m = {{0}, 0, 0};
    ted_beam_t saved, expected;
    unsigned int size, i;

    setup(TED_MULTICOLOR_TEXT_MODE, 3);
    memset(memory, 0x6c, sizeof(memory));
    write_register(0x16, 0x17, 21);
    saved = beam;
    assert(ted_draw_snapshot_write(&m) == 0);
    ted_draw_sync(114);
    expected = beam;
    m.position = 0;
    assert(ted_draw_snapshot_read(&m) == 0);
    assert(memcmp(&saved, &beam, sizeof(beam)) == 0);
    ted_draw_sync(114);
    assert(memcmp(&expected, &beam, sizeof(beam)) == 0);
    size = m.size;
    for (i = 0; i < size; i++) {
        m.position = 0;
        m.size = i;
        assert(ted_draw_snapshot_read(&m) < 0);
    }
}

/* Calling the same renderer after every CPU clock must match a whole line,
   for every mode, all scroll phases and arbitrary register writes. */
static void chunking(void)
{
    ted_beam_t before, expected;
    unsigned int m, scroll, clk, narrow;

    for (narrow = 0; narrow < 2; narrow++) {
    for (m = 0; m < 8; m++) {
        for (scroll = 0; scroll < 8; scroll++) {
            setup(m, scroll);
            if (narrow) { ted.regs[7] &= ~8; }
            memset(memory, 0x96, sizeof(memory));
            before = beam;
            ted_draw_sync(114);
            expected = beam;
            beam = before;
            for (clk = 1; clk <= 114; clk++) {
                ted_draw_sync(clk);
            }
            if (memcmp(&expected, &beam, sizeof(beam))) {
                fprintf(stderr, "chunking mode %u scroll %u\n", m, scroll);
                assert(0);
            }
        }
    }
    }
}

static void reference_sync(CLOCK clk)
{
    while (beam.clk < clk) {
        dot_events();
        emit(1);
        if (beam.delay && !--beam.delay) {
            if (beam.pending_color >= 0) {
                beam.palette[beam.pending_color] = beam.color_value;
                beam.pending_color = -1;
            }
            if (beam.pending_counter) {
                beam.h = ((~beam.counter_value & 0xfc) << 1) | (beam.h & 7);
                beam.pending_counter = 0;
            }
        }
    }
}

static uint32_t random_state = 7;
static unsigned int next_random(void)
{
    random_state = random_state * 1664525U + 1013904223U;
    return random_state >> 8;
}

/* A row of spaces takes the path for repeated cells.  The cursor must stay
   in its cell also when the video matrix address wraps at 1024 inside the
   row (character positions near $3ff, e.g. set through $ff1a/$ff1b). */
static void cursor_wrap(void)
{
    static const unsigned int cases[][2] = {
        { 0x000, 21 }, { 0x3f0, 21 }, { 0x3e8, 30 }, { 0x3fe, 1 }
    };
    ted_beam_t before, expected;
    unsigned int i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        setup(TED_NORMAL_TEXT_MODE, 0);
        memset(memory, 0, sizeof(memory));
        memset(ted.vbuf, 0x20, sizeof(ted.vbuf));
        memset(ted.cbuf, 0x71, sizeof(ted.cbuf));
        ted.memptr = cases[i][0];
        ted.crsrpos = (cases[i][0] + cases[i][1]) & 0x3ff;
        before = beam;
        reference_sync(114);
        expected = beam;
        beam = before;
        ted_draw_sync(114);
        assert(memcmp(&expected, &beam, sizeof(beam)) == 0);
        /* The cursor cell is drawn in the character colour.  */
        assert(beam.line[32 + cases[i][1] * 8] == 0x71);
    }
}

/* Lines that the raster does not show are not drawn, but their latches
   must advance as if they were: two runs over the same frames, one with
   hidden lines, latch identical state and publish identical shown lines.
   Between frames a few inputs of random lines change. */
#define HIDDEN_FRAMES 60
#define HIDDEN_LINES  48

typedef struct {
    uint8_t regs6, regs7, palette[5], vbuf[40], cbuf[40];
    uint8_t ycounter, idle, idle_data, fetch_on, blank, cursor;
    uint16_t memptr, crsrpos;
} line_input_t;

static line_input_t inputs[HIDDEN_LINES];
static uint8_t drawn_out[HIDDEN_FRAMES][HIDDEN_LINES][384];
static ted_beam_t drawn_state[HIDDEN_FRAMES][HIDDEN_LINES];

static int hidden_line(unsigned int line)
{
    return line % 11 == 5;
}

static void mutate(line_input_t *in)
{
    switch (next_random() % 12) {
        case 0: in->regs6 = 0x18 | ((next_random() & 3) << 5); break;
        case 1: in->regs7 = (next_random() & 0x9f) | 0x08; break;
        case 2: in->palette[next_random() % 5] = next_random() & 0x7f; break;
        case 3: in->vbuf[next_random() % 40] = next_random(); break;
        case 4: in->cbuf[next_random() % 40] = next_random(); break;
        case 5: in->ycounter = next_random() & 7; break;
        case 6: in->memptr = next_random() & 0x3ff; break;
        case 7: in->crsrpos = (in->memptr + next_random() % 48) & 0x3ff; break;
        case 8: in->cursor ^= 1; break;
        case 9: in->idle = !(next_random() & 7); in->idle_data = next_random(); break;
        case 10: in->blank = !(next_random() & 7); in->fetch_on = !!(next_random() & 15); break;
        default: memory[next_random() % sizeof(memory)] = next_random(); break;
    }
}

/* Without hidden lines (`hide' 0) store the published lines and the
   latches; with them, compare.  */
static void hidden_run(int hide)
{
    geometry_t geometry;
    CLOCK clk = 0;
    unsigned int frame, line, i;

    setup(TED_NORMAL_TEXT_MODE, 0);
    memset(&geometry, 0, sizeof(geometry));
    geometry.screen_size.width = 384;
    ted.raster.geometry = &geometry;
    random_state = 99;
    for (i = 0; i < sizeof(memory); i++) { memory[i] = next_random(); }
    for (line = 0; line < HIDDEN_LINES; line++) {
        line_input_t *in = &inputs[line];
        in->regs6 = 0x18 | ((line % 4) << 5);
        in->regs7 = 0x08 | (line & 0x97);
        for (i = 0; i < 5; i++) { in->palette[i] = next_random() & 0x7f; }
        for (i = 0; i < 40; i++) {
            in->vbuf[i] = line & 1 ? 0x20 : next_random();
            in->cbuf[i] = line & 2 ? 0x71 : next_random();
        }
        in->ycounter = line & 7;
        in->memptr = (line >> 3) * 40 + (line == 40 ? 0x3e0 : 0);
        in->crsrpos = (in->memptr + line % 41) & 0x3ff;
        in->cursor = line & 1;
        in->idle = line == 13;
        in->idle_data = 0x5a;
        in->fetch_on = line != 17;
        in->blank = line >= 44;
    }
    for (frame = 0; frame < HIDDEN_FRAMES; frame++) {
        for (line = 0; line < HIDDEN_LINES; line++) {
            const line_input_t *in = &inputs[line];
            int hidden = hide && hidden_line(line);

            ted.regs[6] = in->regs6;
            ted.regs[7] = in->regs7;
            memcpy(beam.palette, in->palette, 5);
            memcpy(ted.vbuf, in->vbuf, 40);
            memcpy(ted.cbuf, in->cbuf, 40);
            ted.raster.ycounter = in->ycounter;
            ted.memptr = in->memptr;
            ted.crsrpos = in->crsrpos;
            ted.cursor_visible = in->cursor;
            ted.idle_state = in->idle;
            ted.idle_data = in->idle_data;
            ted.character_fetch_on = in->fetch_on;
            ted.raster.blank_enabled = in->blank;
            ted_draw_begin_line(clk, !hidden);
            clk += 114;
            ted_draw_line(clk, 1);
            if (!hide) {
                memcpy(drawn_out[frame][line], published, 384);
                drawn_state[frame][line] = beam;
            } else {
                assert(hidden || memcmp(drawn_out[frame][line], published, 384) == 0);
                assert(memcmp(&drawn_state[frame][line], &beam,
                              offsetof(ted_beam_t, line)) == 0);
            }
        }
        for (i = 0; i < 4; i++) {
            mutate(&inputs[next_random() % HIDDEN_LINES]);
        }
    }
    ted.raster.geometry = NULL;
}

static void hidden_lines(void)
{
    hidden_run(0);
    hidden_run(1);
}

static void period_equivalence(void)
{
    ted_beam_t before, expected;
    unsigned int trial, i;

    for (trial = 0; trial < 2000; trial++) {
        setup(next_random() & 7, next_random() & 7);
        ted.regs[7] ^= next_random() & 0x88;
        ted.screen_leftborderwidth = next_random() % 65;
        ted.idle_state = next_random() & 1;
        ted.idle_data = next_random() & 255;
        ted.character_fetch_on = next_random() & 1;
        ted.raster.blank_enabled = next_random() & 1;
        ted.raster.ycounter = next_random() & 7;
        ted.memptr = next_random() & 1023;
        ted.crsrpos = (ted.memptr + next_random() % 40) & 1023;
        ted.cursor_visible = next_random() & 1;
        for (i = 0; i < sizeof(memory); i++) { memory[i] = next_random(); }
        for (i = 0; i < 40; i++) {
            ted.vbuf[i] = next_random();
            ted.cbuf[i] = next_random();
        }
        before = beam;
        reference_sync(114);
        expected = beam;
        beam = before;
        ted_draw_sync(114);
        if (memcmp(&expected, &beam, sizeof(beam))) {
            fprintf(stderr, "period trial %u mode %u scroll %u idle %d blank %d fetch %d\n",
                    trial, mode(), beam.scroll, ted.idle_state, ted.raster.blank_enabled, ted.character_fetch_on);
            for (i = 0; i < sizeof(beam); i++) {
                if (((uint8_t *)&expected)[i] != ((uint8_t *)&beam)[i]) {
                    fprintf(stderr, "offset %u expected %u actual %u\n", i,
                            ((uint8_t *)&expected)[i], ((uint8_t *)&beam)[i]);
                }
            }
            assert(0);
        }
    }
}

static void event_equivalence(void)
{
    ted_beam_t before, expected;
    unsigned int frame, clk, i, reg, value;

    for (frame = 0; frame < 300; frame++) {
        setup(next_random() & 7, next_random() & 7);
        ted.regs[7] ^= next_random() & 0x88;
        ted.idle_state = !(next_random() & 15);
        ted.idle_data = next_random() & 255;
        for (i = 0; i < sizeof(memory); i++) { memory[i] = next_random(); }
        for (i = 0; i < 40; i++) {
            ted.vbuf[i] = next_random();
            ted.cbuf[i] = next_random();
        }
        for (clk = 1; clk <= 114; clk += 1 + next_random() % 17) {
            before = beam;
            reference_sync(clk);
            expected = beam;
            beam = before;
            ted_draw_sync(clk);
            if (memcmp(&expected, &beam, sizeof(beam))) {
                fprintf(stderr, "event equivalence frame %u clk %u h %u mode %u scroll %u\n",
                        frame, clk, before.h, mode(), beam.scroll);
                for (i = 0; i < sizeof(beam); i++) {
                    if (((uint8_t *)&beam)[i] != ((uint8_t *)&expected)[i]) {
                        fprintf(stderr, "offset %u: expected %u actual %u\n", i,
                                ((uint8_t *)&expected)[i], ((uint8_t *)&beam)[i]);
                    }
                }
                fprintf(stderr, "before: bits %02x attr %02x char %02x pair %u ctrl %02x/%02x; after %02x %02x pair %u\n",
                        before.bits,before.attr,before.character,before.pair,before.control1,before.control2,beam.bits,beam.attr,beam.pair);
                assert(0);
            }
            if (!(next_random() & 3)) {
                reg = next_random() % 8;
                reg = reg < 2 ? reg + 6 : reg < 7 ? reg + 0x13 : 0x1e;
                value = next_random() & 255;
                write_register(reg, value, clk);
            }
        }
    }
}

int main(void)
{
    colors();
    fetched_memory();
    counter_delay();
    wide_canvas();
    chunking();
    snapshots();
    event_equivalence();
    period_equivalence();
    cursor_wrap();
    hidden_lines();
    puts("TED pixel pipeline: colour dots, RAM latch, counter delay, wide canvas, chunking, snapshots, cursor wrap and hidden lines passed");
    return 0;
}
