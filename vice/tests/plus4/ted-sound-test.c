/* TED sound: the voices against TLC's periods (Plus/4 World article 500244)
   and FPGATED's counters, clock by clock; the output stage against the
   properties of its band-limited steps and of the board's DC blocking. */
#include "vice.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef TED_SOUND_SOURCE
#define TED_SOUND_SOURCE "../../src/plus4/ted-sound.c"
#endif
#include TED_SOUND_SOURCE

int sidcart_enabled(void)
{
    return 0;
}

void *lib_malloc(size_t size)
{
    void *p = malloc(size);

    assert(p != NULL);
    return p;
}

void lib_free(void *p)
{
    free(p);
}

CLOCK maincpu_clk;
int maincpu_rmw_flag;
static int trace_stores;
static unsigned int store_count;
static CLOCK store_clocks[2];
static uint8_t store_values[2];

int sound_read(uint16_t addr, int chipno)
{
    return ted_sound_machine_read(NULL, addr);
}

void sound_store(uint16_t addr, uint8_t value, int chipno)
{
    if (trace_stores) {
        assert(store_count < 2);
        store_clocks[store_count] = maincpu_clk;
        store_values[store_count++] = value;
    }
    ted_sound_machine_store(NULL, addr, value);
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

/* Doubles as their eight bytes in host order, as snapshot.c writes them. */
int snapshot_module_write_double(snapshot_module_t *m, double value)
{
    uint8_t bytes[sizeof(double)];

    memcpy(bytes, &value, sizeof(bytes));
    return snapshot_module_write_byte_array(m, bytes, sizeof(bytes));
}

int snapshot_module_read_double(snapshot_module_t *m, double *value)
{
    uint8_t bytes[sizeof(double)];

    if (snapshot_module_read_byte_array(m, bytes, sizeof(bytes)) < 0) {
        return -1;
    }
    memcpy(value, bytes, sizeof(bytes));
    return 0;
}

static int16_t test_sample(void)
{
#ifdef SOUND_SYSTEM_FLOAT
    float sample = 0;
    ted_sound_machine_calculate_samples(NULL, &sample, 1, 1, NULL);
    return (int16_t)(sample * 32767.0f + 0.5f);
#else
    int16_t sample = 0;
    ted_sound_machine_calculate_samples(NULL, &sample, 1, 1, 1, NULL);
    return sample;
#endif
}

static int cycle_samples(int16_t *buffer, int capacity, CLOCK *cycles)
{
#ifdef SOUND_SYSTEM_FLOAT
    float samples[4096];
    int i, count = ted_sound_calculate_samples(NULL, samples, capacity, 1, cycles);
    for (i = 0; i < count; i++) {
        buffer[i] = (int16_t)floor(samples[i] * 32767.0f + 0.5f);
    }
    return count;
#else
    return ted_sound_calculate_samples(NULL, buffer, capacity, 1, 1, cycles);
#endif
}

/* Start a chip that has not run yet, as when sound is first opened.
   Later initializations keep the state of the running chip. */
static void power_on(unsigned int rate, unsigned int clock)
{
    memset(plus4_sound_data, 0, sizeof(plus4_sound_data));
    memset(&snd, 0, sizeof(snd));
    ted_sound_machine_init(NULL, rate, clock);
}

/* A time line: with a sample rate of 1 and a sample of 2^30 clocks, a time
   unit is one CPU clock and a counter tick eight; the voices run clock by
   clock and their output level is checked at each clock. */
#define LINE_SAMPLE (1U << 30)

static void line_on(void)
{
    power_on(1, LINE_SAMPLE);
}

static void store(uint16_t addr, uint8_t value)
{
    ted_sound_machine_store(NULL, addr, value);
}

/* TLC's periods: 1023 - frequency ticks of eight clocks for each half
   period, 1024 for $3ff.  After register 17 bit 7 the counter starts at the
   reload value, the state is cleared (high) and it changes when the counter
   reaches $3ff (FPGATED): the first half period is one tick shorter.  Voice
   0 ticks at clocks 8, 16, 24..., voice 1 half a tick earlier, at 4, 12,
   20... (FPGATED clocks it two single clocks after voice 0). */
static void tone(unsigned int freq, unsigned int voice)
{
    unsigned int period = freq == 1023 ? 1024 : 1023 - freq;
    uint16_t low = voice ? 0x0f : 0x0e, high = voice ? 0x10 : 0x12;
    uint8_t control = voice ? 0x28 : 0x18;
    uint32_t t, first = 8 * (period - 1) - 4 * voice, half = 8 * period;
    int on;

    line_on();
    store(low, freq & 255);
    store(high, freq >> 8);
    store(0x11, control | 0x80);
    ted_sound_run(8); /* A tick loads the counter with the frequency. */
    store(0x11, control);
    for (t = 1; t <= first + 6 * half; t++) {
        ted_sound_run(1);
        on = t < first || ((t - first) / half) % 2 == 1;
        assert(snd.level == (on ? volumeTable[control] : 0));
    }
}

/* The same tone on both voices from register 17 bit 7: voice 1 changes
   its state half a tick, four clocks, before voice 0 each time. */
static void voice_phase(void)
{
    uint32_t t, change;
    int on0, on1;

    line_on();
    store(0x0e, 1000 & 255);
    store(0x12, 1000 >> 8);
    store(0x0f, 1000 & 255);
    store(0x10, 1000 >> 8);
    store(0x11, 0xb8);
    ted_sound_run(8);
    store(0x11, 0x38);
    for (t = 1; t <= 2000; t++) {
        ted_sound_run(1);
        change = 8 * 22;
        on0 = t < change || ((t - change) / (8 * 23)) % 2 == 1;
        on1 = t < change - 4 || ((t - (change - 4)) / (8 * 23)) % 2 == 1;
        assert(snd.level == volumeTable[0x08 | (on0 ? 0x10 : 0) | (on1 ? 0x20 : 0)]);
    }
}

/* Reopening the output, or a new clock rate ($FF07 bit 6), keeps the voices
   running: the level changes stay at the same clocks. */
static void reopen_tone(void)
{
    uint32_t t, first = 8 * (252 - 1), half = 8 * 252;
    int on;

    line_on();
    store(0x0e, 771 & 255);
    store(0x12, 771 >> 8);
    store(0x11, 0x98);
    ted_sound_run(8);
    store(0x11, 0x18);
    for (t = 1; t <= 30000; t++) {
        if (t == 7777) {
            ted_sound_machine_init(NULL, 1, LINE_SAMPLE / 2);
            assert(snd.partial_length == 0);
        }
        ted_sound_run(1);
        on = t < first || ((t - first) / half) % 2 == 1;
        assert(snd.level == (on ? volumeTable[0x18] : 0));
    }
}

/* A frequency write changes the reload value, not the running counter. */
static void frequency_write(void)
{
    uint32_t t;
    int on;

    line_on();
    store(0x0e, 923 & 255);
    store(0x12, 923 >> 8);
    store(0x11, 0x98);
    ted_sound_run(8);
    store(0x11, 0x18);
    ted_sound_run(96 * 8);
    assert(snd.voice0_accu == 924 + 96);
    store(0x0e, 1021 & 255);
    /* The counter reaches $3ff at tick 99, then loads $3fe: two ticks for
       each half period. */
    for (t = 96 * 8 + 1; t < 1000; t++) {
        ted_sound_run(1);
        on = t < 99 * 8 || ((t - 99 * 8) / 16) % 2 == 1;
        assert(snd.level == (on ? volumeTable[0x18] : 0));
    }
}

/* While register 17 bit 7 holds the counters, a frequency write loads at
   the next tick; after the release the counters count from there. */
static void held_reload(void)
{
    line_on();
    store(0x11, 0xb8);
    ted_sound_run(8);
    store(0x0e, 0xfd);
    store(0x12, 3);
    store(0x0f, 0xfb);
    store(0x10, 3);
    ted_sound_run(3);
    assert(snd.voice0_accu == 1 && snd.voice1_accu == 1);
    ted_sound_run(1); /* Clock 12: voice 1 loads. */
    assert(snd.voice0_accu == 1 && snd.voice1_accu == 0x3fc);
    ted_sound_run(4); /* Clock 16: voice 0 loads. */
    assert(snd.voice0_accu == 0x3fe && snd.voice1_accu == 0x3fc);
    store(0x11, 0x38);
    assert(snd.level == volumeTable[0x38]);
    ted_sound_run(7);
    assert(snd.level == volumeTable[0x38]);
    ted_sound_run(1); /* Clock 24: voice 0 reaches $3ff. */
    assert(snd.level == volumeTable[0x28]);
    ted_sound_run(11);
    assert(snd.level == volumeTable[0x28]);
    ted_sound_run(1); /* Clock 36: voice 1 reaches $3ff. */
    assert(snd.level == volumeTable[0x08]);
    ted_sound_run(4); /* Clock 40: voice 0 again. */
    assert(snd.level == volumeTable[0x18]);
}

/* With $3fe the counter finishes its count to $3ff, changing the state
   once more, then stays at $3ff: the state holds at the level of its last
   change, low or high, as TLC observed (TED sound experiments, Plus/4 World
   ma/1550).  The noise register holds with voice 1. */
static void holds(void)
{
    int32_t held[2];
    uint8_t noise;
    unsigned int i;
    int level;

    for (level = 0; level < 2; level++) {
        line_on();
        store(0x0e, 0xfd);
        store(0x12, 3);
        store(0x11, 0x98);
        ted_sound_run(8);
        store(0x11, 0x18);
        ted_sound_run(8 * (1 + level));
        store(0x0e, 0xfe);
        ted_sound_run(16);
        assert(snd.voice0_accu == 0x3ff);
        held[level] = snd.level;
        for (i = 0; i < 10000; i++) {
            ted_sound_run(8);
            assert(snd.level == held[level]);
        }
    }
    /* One more state change before the hold gives the other level. */
    assert(held[0] != held[1]);

    line_on();
    store(0x0f, 0xfd);
    store(0x10, 3);
    store(0x11, 0xc8);
    ted_sound_run(8);
    store(0x11, 0x48);
    ted_sound_run(8 * 13);
    store(0x0f, 0xfe);
    ted_sound_run(16);
    noise = snd.noise_shift_register;
    held[0] = snd.level;
    for (i = 0; i < 10000; i++) {
        ted_sound_run(8);
        assert(snd.level == held[0] && snd.noise_shift_register == noise);
    }
    /* A volume/control write must not advance the held noise bit. */
    store(0x11, 0x48);
    assert(snd.level == held[0] && snd.noise_shift_register == noise);
}

/* A held low state clears after 188416 ticks without a change, as in
   plus4emu and FPGATED: the output of either voice goes high. */
static void decay(void)
{
    uint32_t ticks;
    int voice;

    for (voice = 0; voice < 2; voice++) {
        uint16_t lo = voice ? 0x0f : 0x0e, hi = voice ? 0x10 : 0x12;
        uint8_t control = voice ? 0x28 : 0x18;
        uint32_t *hold = voice ? &snd.voice1_hold : &snd.voice0_hold;

        line_on();
        store(lo, 0xfd);
        store(hi, 3);
        store(0x11, control | 0x80);
        ted_sound_run(8);
        store(0x11, control);
        ted_sound_run(8); /* The state changes: low. */
        assert(snd.level == 0);
        store(lo, 0xfe);
        ted_sound_run(4);
        assert(snd.level == 0);
        /* Clock 20: the next tick of voice 0 is at 24, of voice 1 at 28. */
        ticks = TED_SOUND_DECAY_TICKS - *hold;
        ted_sound_run((voice ? 8 : 4) + (ticks - 1) * 8 - 1);
        assert(snd.level == 0);
        ted_sound_run(1);
        assert(snd.level == volumeTable[control]);
    }
}

/* The noise register runs a maximal sequence of 255 steps, and register
   17 bit 7 clears it to a low output.  XNOR feedback never reaches $ff:
   127 ones and 128 zeros. */
static void noise_sequence(void)
{
    unsigned int ones = 0, steps = 0;
    uint8_t start;

    line_on();
    store(0x11, 0xc8);
    assert(snd.noise_shift_register == 0 && snd.noise_output == 0);
    store(0x11, 0x48);
    start = snd.noise_shift_register;
    do {
        clock_shift_register();
        ones += snd.noise_output != 0;
        steps++;
    } while (snd.noise_shift_register != start && steps < 1000);
    assert(steps == 255 && ones == 127);
}

/* Independent acquisition data: TLC, 2000-09-07, DC controls $90..$b8.
   Compare normalized ratios, allowing one output unit for rounding. */
static void measured_levels(void)
{
    static const unsigned int voice0[] = {31, 562, 1528, 2485, 3462, 4454, 5450, 6464, 7334};
    static const unsigned int voice1[] = {31, 542, 1508, 2465, 3443, 4434, 5429, 6443, 7314};
    static const unsigned int both[] = {31, 1102, 3058, 5023, 7075, 9170, 11333, 13614, 15674};
    unsigned int v, voices, index;
    double expected, error;

    line_on();
    for (voices = 1; voices <= 3; voices++) {
        for (v = 0; v < 16; v++) {
            index = v > 8 ? 8 : v;
            expected = voices == 3 ? both[index] : (voice0[index] + voice1[index]) / 2.0;
            expected = (expected - 31) * 19976 / (15674 - 31);
            store(0x11, 0x80 | (voices << 4) | v);
            error = snd.level - expected;
            assert(error >= -0.5 && error <= 0.5);
        }
    }
    /* Saturation and square-over-noise priority are defined by Commodore. */
    store(0x0f, 0xfe);
    store(0x11, 0x28);
    for (v = 8; v < 16; v++) {
        store(0x11, 0xe0 | v);
        assert(snd.noise == 0);
        store(0x11, 0x60 | v);
        assert(snd.level == volumeTable[0x28]);
    }
}

/* Each step of the output is exactly one level change. */
static void step_table_rows(void)
{
    double sum;
    int p, j;

    for (p = 0; p <= TED_SOUND_PHASES; p++) {
        sum = 0.0;
        for (j = 0; j < TED_SOUND_TAPS; j++) {
            sum += step_table[p][j];
        }
        assert(fabs(sum - 1.0) < 1e-12);
    }
}

/* A step `clocks' into sample 100 at 48 kHz: the output is silent
   before it (the minimum phase filter is causal), rises within a few
   samples, then decays with the time constant of the board's coupling.
   Return the centre of the step, in samples. */
static double step_centre(unsigned int clocks)
{
    int16_t out[20000];
    const double level = volumeTable[0x18];
    const double a = exp(-1.0 / (TED_SOUND_DC_TIME * 48000.0));
    double centre, expected, sum = 0.0, moment = 0.0, d;
    CLOCK cycles;
    int k, n = 0;

    power_on(48000, 1773447);
    for (k = 0; k < 100; k++) {
        out[n++] = test_sample();
    }
    cycles = clocks;
    n += cycle_samples(out + n, 1, &cycles);
    assert(n == 100);
    store(0x11, 0x98);
    while (n < 20000) {
        out[n] = test_sample();
        n++;
    }
    for (k = 0; k < 100; k++) {
        assert(out[k] == 0);
    }
    /* 90% four samples after the step. */
    assert(out[104] > 0.9 * level);
    /* The centroid of the differences of the samples, taken before the DC
       blocking stage by undoing its recurrence. */
    for (k = 99; k < 100 + TED_SOUND_TAPS; k++) {
        d = out[k + 1] / a - out[k];
        sum += d;
        moment += (k + 0.5) * d;
    }
    centre = moment / sum;
    /* After the ringing: the level decaying with the coupling. */
    for (k = 100 + TED_SOUND_TAPS; k < 20000; k += 97) {
        expected = level * pow(a, k - centre);
        assert(fabs(out[k] - expected) <= 0.01 * level + 1);
    }
    /* One time constant later, 1 / e of the level. */
    k = (int)(centre + TED_SOUND_DC_TIME * 48000.0);
    assert(fabs(out[k] - level / M_E) <= 0.01 * level);
    return centre;
}

/* The step follows its position within the sample, and is centred less
   than three samples after it. */
static void output_steps(void)
{
    /* Writes 9 and 27 clocks into sample 100, of 1773447 / 48000.  The
       first sample output after a step shows the value at the end of its
       sample. */
    double f0 = 9 * 48000.0 / 1773447.0, f1 = 27 * 48000.0 / 1773447.0;
    double c0 = step_centre(9), c1 = step_centre(27);

    assert(c0 - (100 + f0) > 0.0 && c0 - (100 + f0) < 3.0);
    assert(fabs((c1 - c0) - (f1 - f0)) < 0.01);
}

/* Power of `samples' at `freq' with a Hann window.  */
static double tone_power(const int16_t *samples, int count, double freq, double rate)
{
    double re = 0.0, im = 0.0, w;
    int k;

    for (k = 0; k < count; k++) {
        w = 0.5 - 0.5 * cos(2.0 * M_PI * k / count);
        re += samples[k] * w * cos(2.0 * M_PI * freq * k / rate);
        im -= samples[k] * w * sin(2.0 * M_PI * freq * k / rate);
    }
    return re * re + im * im;
}

/* Harmonics above half the sample rate do not fold into the audio band: a
   4819 Hz square wave's 7th harmonic (33733 Hz) would appear at 14267 Hz,
   a 55420 Hz tone at 7420 Hz.  Averaging the output over each sample, as
   before, left them about 20 dB down. */
static void aliasing(void)
{
    static int16_t out[48000];
    const double single = 1773447.0 / 2.0;
    double f0, fundamental, alias;
    int k;

    power_on(48000, 1773447);
    store(0x0e, 1000 & 255);
    store(0x12, 1000 >> 8);
    store(0x11, 0x18);
    for (k = 0; k < 48000; k++) {
        out[k] = test_sample();
    }
    f0 = single / (8 * 23);
    fundamental = tone_power(out + 4800, 32768, f0, 48000.0);
    alias = tone_power(out + 4800, 32768, 48000.0 - 7 * f0, 48000.0);
    assert(10.0 * log10(alias / fundamental) < -60.0);

    power_on(48000, 1773447);
    store(0x0e, 1021 & 255);
    store(0x12, 1021 >> 8);
    store(0x11, 0x18);
    for (k = 0; k < 48000; k++) {
        out[k] = test_sample();
    }
    f0 = single / (8 * 2);
    alias = tone_power(out + 4800, 32768, f0 - 48000.0, 48000.0);
    /* Against the fundamental of an audible square wave of the same level:
       amplitude 2 / pi of the level, a quarter of the samples with Hann. */
    fundamental = pow(volumeTable[0x18] * 2.0 / M_PI * 32768 / 4.0, 2.0);
    assert(10.0 * log10(alias / fundamental) < -60.0);
}

/* The batched run must match one tick of one voice at a time.  */
static void reference_run(uint32_t length)
{
    uint32_t next, remaining1;

    while (length) {
        remaining1 = ted_voice1_remaining();
        next = snd.tick_remaining < remaining1 ? snd.tick_remaining : remaining1;
        if (length < next) {
            snd.tick_remaining -= length;
            snd.partial_length += length;
            return;
        }
        length -= next;
        snd.partial_length += next;
        if (next == snd.tick_remaining) {
            ted_voice0_clock();
            snd.tick_remaining = snd.tick_length;
        } else {
            ted_voice1_clock();
            snd.tick_remaining -= next;
        }
        ted_sound_level_changed();
    }
}

static int reference_samples(int16_t *buffer, int capacity, CLOCK *cycles)
{
    uint64_t available = (uint64_t)*cycles * snd.sample_rate + snd.cycle_pending;
    uint32_t length;
    int count = 0;

    while (available && count < capacity) {
        length = snd.sample_length - snd.partial_length;
        if (available < length) {
            length = (uint32_t)available;
        }
        reference_run(length);
        available -= length;
        if (snd.partial_length == snd.sample_length) {
            buffer[count++] = ted_sound_emit();
        }
    }
    *cycles = (CLOCK)(available / snd.sample_rate);
    snd.cycle_pending = (uint32_t)(available % snd.sample_rate);
    return count;
}

static void batching(void)
{
    static const unsigned int rates[] = {8000, 44100, 48000, 96000};
    struct plus4_sound_s before, expected;
    int16_t actual[64], reference[64];
    uint32_t random = 17;
    unsigned int r, i, step;
    CLOCK cycles, left;
    int count;

    for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
        power_on(rates[r], 1773447);
        for (i = 0; i < 20000; i++) {
            random = random * 1664525u + 1013904223u;
            if (!(i % 5)) {
                /* Frequencies near $3ff for many changes, all controls. */
                step = (random >> 16) % 5;
                store((uint16_t)(0x0e + step),
                      (uint8_t)(step == 3 ? random >> 24
                                : (step == 0 || step == 1) ? 0xf0 | (random >> 28)
                                : 3));
            }
            cycles = (random >> 8) % 300;
            before = snd;
            left = cycles;
            count = reference_samples(reference, 64, &left);
            expected = snd;
            snd = before;
            assert(cycle_samples(actual, 64, &cycles) == count && cycles == left);
            assert(!memcmp(actual, reference, count * sizeof(*actual)));
            assert(!memcmp(&snd, &expected, sizeof(snd)));
        }
    }
}

static void sound_snapshot(void)
{
    snapshot_module_t module = {{0}, 0, 0};
    snapshot_module_t state = {{0}, 0, 0};
    struct plus4_sound_s saved;
    int16_t expected[64], actual[64];
    CLOCK cycles;
    unsigned int i, length;
    int count;

    power_on(48000, 1773447);
    store(0x0f, 0xfd);
    store(0x10, 3);
    store(0x11, 0xc8);
    cycles = 16;
    cycle_samples(actual, 64, &cycles);
    store(0x11, 0x48);
    cycles = 333;
    cycle_samples(actual, 64, &cycles);
    saved = snd;
    assert(saved.partial_length != 0);
    assert(ted_sound_snapshot_write(&module) == 0);
    assert(ted_sound_snapshot_write_state(&state) == 0);
    cycles = 1500;
    count = cycle_samples(expected, 64, &cycles);
    module.position = 0;
    assert(ted_sound_snapshot_read(&module) == 0);
    state.position = 0;
    assert(ted_sound_snapshot_read_state(&state) == 0);
    assert(!memcmp(&snd, &saved, sizeof(snd)));
    assert(saved.voice0_hold != 0);
    /* Opening the backend after loading must not erase the restored phase. */
    ted_sound_machine_init(NULL, 48000, 1773447);
    assert(!memcmp(&snd, &saved, sizeof(snd)));
    cycles = 1500;
    assert(cycle_samples(actual, 64, &cycles) == count);
    assert(!memcmp(expected, actual, count * sizeof(*actual)));
    /* Times of the voices must be shorter than the decay. */
    state.position = 0;
    state.bytes[3] = 0x7f;
    assert(ted_sound_snapshot_read_state(&state) == -1);
    length = state.size;
    for (i = 0; i < length; i++) {
        state.position = 0;
        state.size = i;
        assert(ted_sound_snapshot_read_state(&state) == -1);
    }
    /* A different host sample rate keeps hardware phase, but starts a new
       host sample and drops the steps in samples of the old rate. */
    ted_sound_machine_init(NULL, 96000, 1773447);
    module.position = 0;
    assert(ted_sound_snapshot_read(&module) == 0);
    assert(snd.sample_rate == 96000);
    assert(snd.tick_remaining == 2 * saved.tick_remaining);
    assert(snd.voice0_accu == saved.voice0_accu && snd.voice1_accu == saved.voice1_accu);
    assert(snd.partial_length == 0 && snd.output == snd.level);
    length = module.size;
    for (i = 0; i < length; i++) {
        module.position = 0;
        module.size = i;
        assert(ted_sound_snapshot_read(&module) == -1);
    }
    module.size = length;
    module.position = 0;
    module.bytes[5] = 0xff; /* Invalid active counter. */
    module.bytes[6] = 0xff;
    assert(ted_sound_snapshot_read(&module) == -1);
    assert(snapshot_error == SNAPSHOT_MODULE_INCOMPATIBLE);
    /* Legacy snapshots can restore register values but contain no phase. */
    {
        const uint8_t regs[5] = {0xfd, 0xfb, 3, 0xb8, 3};
        ted_sound_snapshot_legacy(regs);
        assert(!memcmp(plus4_sound_data, regs, 5));
        assert(snd.level == volumeTable[0x38]);
    }
}

static void rmw_writes(void)
{
    store(0x11, 0x97);
    assert(ted_sound_read(0x11) == 0x97);
    maincpu_clk = 100;
    maincpu_rmw_flag = 1;
    trace_stores = 1;
    store_count = 0;
    ted_sound_store(0x11, 0x98);
    assert(store_count == 2);
    assert(store_clocks[0] == 99 && store_clocks[1] == 100);
    assert(store_values[0] == 0x97 && store_values[1] == 0x98);
    assert(maincpu_clk == 100);
    maincpu_rmw_flag = 0;
    trace_stores = 0;
}

int main(void)
{
    static const unsigned int frequencies[] = {0, 800, 1000, 1021, 1023};
    unsigned int f, v, i;
    struct plus4_sound_s state;
    int16_t whole[128];

    for (f = 0; f < sizeof(frequencies) / sizeof(frequencies[0]); f++) {
        for (v = 0; v < 2; v++) {
            tone(frequencies[f], v);
        }
    }
    voice_phase();
    reopen_tone();
    frequency_write();
    held_reload();
    holds();
    decay();
    noise_sequence();
    measured_levels();
    step_table_rows();
    output_steps();
    aliasing();
    batching();
    sound_snapshot();
    rmw_writes();

    /* A backend reopen restores frequencies and digital output immediately. */
    power_on(48000, 1773447);
    store(0x0f, 0xff);
    store(0x10, 3);
    store(0x11, 0xb8);
    ted_sound_machine_init(NULL, 48000, 1773447);
    assert(snd.level == volumeTable[0x38]);
    assert(snd.voice1_reload == 0); /* $3ff wraps to the lowest tone. */

    /* Splitting buffers must not alter the oscillator or resampler phase. */
    store(0x0f, 0xfd);
    store(0x11, 0x48);
    state = snd;
    for (i = 0; i < 128; i++) {
        whole[i] = test_sample();
    }
    {
#ifdef SOUND_SYSTEM_FLOAT
        float buffer[128];
        snd = state;
        ted_sound_machine_calculate_samples(NULL, buffer, 37, 1, NULL);
        ted_sound_machine_calculate_samples(NULL, buffer + 37, 91, 1, NULL);
        for (i = 0; i < 128; i++) {
            assert(buffer[i] == whole[i] / 32767.0f);
        }
#else
        int16_t buffer[256] = {0};
        snd = state;
        ted_sound_machine_calculate_samples(NULL, buffer, 37, 2, 1, NULL);
        ted_sound_machine_calculate_samples(NULL, buffer + 74, 91, 2, 1, NULL);
        for (i = 0; i < 128; i++) {
            assert(buffer[2 * i] == whole[i]);
            assert(buffer[2 * i + 1] == whole[i]);
        }
#endif
    }
    puts("TED audio: voice timelines and phase, holds, decay, noise, levels, band-limited steps, DC coupling, aliasing, batching, snapshots passed");
    return 0;
}
