/* TED tone periods from TLC, Plus/4 World article 500244.
   Check against the analytic integral of a square wave, not another emulator. */
#include "vice.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifndef TED_SOUND_SOURCE
#define TED_SOUND_SOURCE "../../src/plus4/ted-sound.c"
#endif
#include TED_SOUND_SOURCE

int sidcart_enabled(void)
{
    return 0;
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
    uint8_t bytes[256];
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

/* Start a chip that has not run yet, as when sound is first opened.
   Later initializations keep the state of the running chip. */
static void power_on(unsigned int rate, unsigned int clock)
{
    memset(plus4_sound_data, 0, sizeof(plus4_sound_data));
    memset(&snd, 0, sizeof(snd));
    ted_sound_machine_init(NULL, rate, clock);
}

static uint64_t high_time(uint64_t time, uint64_t half_period)
{
    uint64_t remainder = time % (2 * half_period);
    return (time / (2 * half_period)) * half_period +
           (remainder < half_period ? remainder : half_period);
}

static void tone(unsigned int rate, unsigned int clock, unsigned int freq,
                 unsigned int voice)
{
    unsigned int i;
    uint64_t half_period, previous = 0, current, expected;
    unsigned int period = freq == 1023 ? 1024 : 1023 - freq;
    uint16_t low = voice ? 0x0f : 0x0e;
    uint16_t high = voice ? 0x10 : 0x12;
    uint8_t control = voice ? 0x28 : 0x18;

    power_on(rate, clock);
    ted_sound_machine_store(NULL, low, freq & 255);
    ted_sound_machine_store(NULL, high, freq >> 8);
    ted_sound_machine_store(NULL, 0x11, control | 0x80);
    ted_sound_clock(); /* Hold reload through a counter clock. */
    ted_sound_machine_store(NULL, 0x11, control);
    half_period = (uint64_t)period * 8 * rate;
    for (i = 1; i <= rate; i++) {
        current = high_time((uint64_t)i * clock, half_period);
        expected = (current - previous) * volumeTable[0x18] / clock;
        assert(test_sample() == (int16_t)expected);
        previous = current;
    }
}

static void reopen_tone(void)
{
    unsigned int i;
    uint64_t time, current, previous = 0;
    uint64_t tick = 8 * 48000;
    uint64_t half_period = (uint64_t)(1023 - 771) * tick;

    /* A tone started at power on has its active counter at zero: the first
       half-cycle takes 1024 ticks, the following ones use the frequency. */
    power_on(48000, 1773447);
    ted_sound_machine_store(NULL, 0x0e, 771 & 255);
    ted_sound_machine_store(NULL, 0x12, 771 >> 8);
    ted_sound_machine_store(NULL, 0x11, 0x18);
    for (i = 1; i <= 24000; i++) {
        time = (uint64_t)i * 1773447;
        current = time <= 1024 * tick ? 0 :
                  high_time(time - 1024 * tick, half_period);
        assert(test_sample() == (int16_t)((current - previous) *
                                         volumeTable[0x18] / 1773447));
        previous = current;
    }
    /* Reopening the output at the clock rate of NTSC mode on a PAL crystal
       ($FF07 bit 6) continues the tone.  The counters run in CPU clocks;
       a sample now lasts 2216809 / 48000 of them, so the tone is higher. */
    ted_sound_machine_init(NULL, 48000, 2216809);
    time = (uint64_t)24000 * 1773447 - 1024 * tick;
    previous = high_time(time, half_period);
    for (i = 1; i <= 24000; i++) {
        current = high_time(time + (uint64_t)i * 2216809, half_period);
        assert(test_sample() == (int16_t)((current - previous) *
                                         volumeTable[0x18] / 2216809));
        previous = current;
    }
}

static void frequency_write(void)
{
    unsigned int i;

    /* Use exactly 16 counter ticks per sample to check a mid-period write.
       A frequency write changes the reload, not the running counter. */
    power_on(8000, 1024000);
    ted_sound_machine_store(NULL, 0x0e, 923 & 255);
    ted_sound_machine_store(NULL, 0x12, 923 >> 8);
    ted_sound_machine_store(NULL, 0x11, 0x98);
    ted_sound_clock();
    ted_sound_machine_store(NULL, 0x11, 0x18);
    for (i = 0; i < 3; i++) {
        assert(test_sample() == volumeTable[0x18]);
    }
    ted_sound_machine_store(NULL, 0x0e, 1021 & 255);
    for (i = 0; i < 3; i++) {
        assert(test_sample() == volumeTable[0x18]);
    }
    /* Ticks 96..112: four high ticks before expiry, then six high ticks
       from three new two-tick pulses. */
    assert(test_sample() == volumeTable[0x18] * 10 / 16);
    assert(test_sample() == volumeTable[0x18] / 2);
}

/* A sample lasts three CPU clocks; the sound divider takes eight.  A
   frequency change under held reload must take effect on the next tick,
   without restarting that divider or requiring another control write. */
static void held_reload(void)
{
    power_on(48000, 144000);
    ted_sound_machine_store(NULL, 0x11, 0xb8);
    test_sample();
    ted_sound_machine_store(NULL, 0x0e, 0xfd);
    ted_sound_machine_store(NULL, 0x12, 3);
    ted_sound_machine_store(NULL, 0x0f, 0xfb);
    ted_sound_machine_store(NULL, 0x10, 3);
    test_sample();
    assert(snd.voice0_accu == 0 && snd.voice1_accu == 0);
    test_sample(); /* Tick at clock 8 reloads both voices. */
    assert(snd.voice0_accu == 1022 && snd.voice1_accu == 1020);
    ted_sound_machine_store(NULL, 0x11, 0x18);
    assert(test_sample() == volumeTable[0x18]); /* Clocks 9..12 */
    assert(test_sample() == volumeTable[0x18]); /* 12..15 */
    assert(test_sample() == volumeTable[0x18]); /* 15..18 */
    assert(test_sample() == volumeTable[0x18]); /* 18..21 */
    assert(test_sample() == volumeTable[0x18]); /* 21..24, toggle at 24 */
    assert(test_sample() == 0);
}

static void held_noise(void)
{
    unsigned int i;
    int16_t level;
    int seen_low = 0, seen_high = 0;

    for (i = 1; i < 32; i++) {
        power_on(48000, 768000); /* Two ticks/sample. */
        ted_sound_machine_store(NULL, 0x0f, 0xfd);
        ted_sound_machine_store(NULL, 0x10, 3);
        ted_sound_machine_store(NULL, 0x11, 0xc8);
        test_sample();
        ted_sound_machine_store(NULL, 0x11, 0x48);
        {
            unsigned int j;
            for (j = 0; j < i; j++) {
                test_sample();
            }
        }
        level = volumeTable[snd.voice1_cached_output];
        seen_low |= level == 0;
        seen_high |= level != 0;
        ted_sound_machine_store(NULL, 0x0f, 0xfe);
        assert(test_sample() == level);
        /* A volume/control write must not advance the held noise bit. */
        ted_sound_machine_store(NULL, 0x11, 0x48);
        assert(test_sample() == level);
    }
    assert(seen_low && seen_high);
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

    for (voices = 1; voices <= 3; voices++) {
        for (v = 0; v < 16; v++) {
            index = v > 8 ? 8 : v;
            expected = voices == 3 ? both[index] : (voice0[index] + voice1[index]) / 2.0;
            expected = (expected - 31) * 19976 / (15674 - 31);
            ted_sound_machine_store(NULL, 0x11, 0x80 | (voices << 4) | v);
            error = test_sample() - expected;
            assert(error >= -0.5 && error <= 0.5);
        }
    }
}

/* Deliberately visit each counter tick: this reference does not use the
   sample quotient/remainder or the constant-output shortcut. */
static uint64_t reference_integrate(uint32_t remaining)
{
    uint32_t step;
    uint64_t sum = 0;

    while (remaining) {
        step = remaining < snd.tick_remaining ? remaining : snd.tick_remaining;
        sum += (uint64_t)(snd.digital ? snd.digital_cached_output :
                         volumeTable[snd.voice0_cached_output |
                                     snd.voice1_cached_output]) * step;
        remaining -= step;
        snd.tick_remaining -= step;
        if (!snd.tick_remaining) {
            ted_sound_clock();
            snd.tick_remaining = snd.tick_length;
        }
    }
    return sum;
}

static int16_t reference_sample(void)
{
    return (int16_t)(reference_integrate(snd.sample_length) / snd.sample_length);
}

static void sampling_equivalence(void)
{
    static const unsigned int rates[] = {8000, 44100, 48000, 96000, 384000};
    static const unsigned int clocks[] = {1773447, 1789773};
    struct plus4_sound_s before, expected;
    uint32_t random = 1;
    unsigned int r, c, i;
    int16_t sample;

    for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
        for (c = 0; c < sizeof(clocks) / sizeof(clocks[0]); c++) {
            power_on(rates[r], clocks[c]);
            for (i = 0; i < 100000; i++) {
                random = random * 1664525u + 1013904223u;
                if (!(i % 17)) {
                    ted_sound_machine_store(NULL, 0x0e + (random >> 16) % 5,
                                            random >> 24);
                }
                before = snd;
                sample = reference_sample();
                expected = snd;
                snd = before;
                assert(test_sample() == sample);
                assert(!memcmp(&snd, &expected, sizeof(snd)));
            }
        }
    }
}

static int cycle_samples(int16_t *buffer, int capacity, CLOCK *cycles)
{
#ifdef SOUND_SYSTEM_FLOAT
    float samples[64];
    int i, count = ted_sound_calculate_samples(NULL, samples, capacity, 1, cycles);
    for (i = 0; i < count; i++) {
        buffer[i] = (int16_t)(samples[i] * 32767.0f + 0.5f);
    }
    return count;
#else
    return ted_sound_calculate_samples(NULL, buffer, capacity, 1, 1, cycles);
#endif
}

static void sub_sample_writes(void)
{
    int16_t buffer[4];
    CLOCK cycles;

    power_on(48000, 768000); /* 16 CPU clocks/sample. */
    ted_sound_machine_store(NULL, 0x11, 0x98);
    cycles = 4;
    assert(cycle_samples(buffer, 4, &cycles) == 0 && cycles == 0);
    ted_sound_machine_store(NULL, 0x11, 0x90);
    cycles = 8;
    assert(cycle_samples(buffer, 4, &cycles) == 0 && cycles == 0);
    ted_sound_machine_store(NULL, 0x11, 0x98);
    cycles = 4;
    assert(cycle_samples(buffer, 4, &cycles) == 1 && cycles == 0);
    assert(buffer[0] == volumeTable[0x18] / 2);
}

static void cycle_equivalence(void)
{
    struct plus4_sound_s before, expected;
    uint32_t random = 17;
    unsigned int i, count, step;
    uint64_t remaining;
    CLOCK cycles;
    int16_t actual[64], reference[64];

    power_on(48000, 1773447);
    for (i = 0; i < 100000; i++) {
        random = random * 1664525u + 1013904223u;
        ted_sound_machine_store(NULL, 0x0e + (random >> 16) % 5, random >> 24);
        cycles = 1 + (random & 63);
        before = snd;
        remaining = cycles * snd.sample_rate;
        count = 0;
        while (remaining) {
            step = snd.sample_length - snd.partial_length;
            if (step > remaining) {
                step = remaining;
            }
            snd.partial_sum += reference_integrate(step);
            snd.partial_length += step;
            remaining -= step;
            if (snd.partial_length == snd.sample_length) {
                reference[count++] = snd.partial_sum / snd.sample_length;
                snd.partial_sum = 0;
                snd.partial_length = 0;
            }
        }
        expected = snd;
        snd = before;
        assert(cycle_samples(actual, 64, &cycles) == (int)count && cycles == 0);
        assert(!memcmp(actual, reference, count * sizeof(*actual)));
        assert(!memcmp(&snd, &expected, sizeof(snd)));
    }
    /* A full destination must return the unconsumed cycles without losing
       the fractional CPU cycle at the sample boundary. */
    before = snd;
    cycles = 1500;
    count = cycle_samples(reference, 64, &cycles);
    assert(cycles == 0);
    expected = snd;
    snd = before;
    cycles = 1500;
    for (i = 0; i < count; i++) {
        assert(cycle_samples(actual + i, 1, &cycles) == 1);
    }
    assert(cycle_samples(actual, 1, &cycles) == 0 && cycles == 0);
    assert(!memcmp(actual, reference, count * sizeof(*actual)));
    assert(!memcmp(&snd, &expected, sizeof(snd)));
}

static void sound_snapshot(void)
{
    snapshot_module_t module = {{0}, 0, 0};
    struct plus4_sound_s saved;
    int16_t expected[64], actual[64];
    CLOCK cycles;
    unsigned int i, length;
    int count;

    power_on(48000, 1773447);
    ted_sound_machine_store(NULL, 0x0f, 0xfd);
    ted_sound_machine_store(NULL, 0x10, 3);
    ted_sound_machine_store(NULL, 0x11, 0xc8);
    cycles = 16;
    cycle_samples(actual, 64, &cycles);
    ted_sound_machine_store(NULL, 0x11, 0x48);
    cycles = 333;
    cycle_samples(actual, 64, &cycles);
    saved = snd;
    assert(saved.partial_length != 0);
    assert(ted_sound_snapshot_write(&module) == 0);
    cycles = 1500;
    count = cycle_samples(expected, 64, &cycles);
    module.position = 0;
    assert(ted_sound_snapshot_read(&module) == 0);
    assert(!memcmp(&snd, &saved, sizeof(snd)));
    /* Opening the backend after loading must not erase the restored phase. */
    ted_sound_machine_init(NULL, 48000, 1773447);
    assert(!memcmp(&snd, &saved, sizeof(snd)));
    cycles = 1500;
    assert(cycle_samples(actual, 64, &cycles) == count);
    assert(!memcmp(expected, actual, count * sizeof(*actual)));
    /* A different host sample rate keeps hardware phase, but starts a new
       host sample rather than treating old-rate integrals as new-rate data. */
    ted_sound_machine_init(NULL, 96000, 1773447);
    module.position = 0;
    assert(ted_sound_snapshot_read(&module) == 0);
    assert(snd.sample_rate == 96000);
    assert(snd.tick_remaining == 2 * saved.tick_remaining);
    assert(snd.voice0_accu == saved.voice0_accu && snd.voice1_accu == saved.voice1_accu);
    assert(snd.partial_length == 0 && snd.partial_sum == 0);
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
        assert(test_sample() == volumeTable[0x38]);
    }
}

static void rmw_writes(void)
{
    ted_sound_machine_store(NULL, 0x11, 0x97);
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
    static const unsigned int rates[] = {8000, 44100, 48000, 96000, 384000};
    static const unsigned int clocks[] = {1773447, 1789773};
    static const unsigned int frequencies[] = {0, 800, 1000, 1021, 1023};
    unsigned int r, c, f, v, i;
    struct plus4_sound_s state;
    int16_t whole[128];

    for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
        for (c = 0; c < sizeof(clocks) / sizeof(clocks[0]); c++) {
            for (f = 0; f < sizeof(frequencies) / sizeof(frequencies[0]); f++) {
                for (v = 0; v < 2; v++) {
                    tone(rates[r], clocks[c], frequencies[f], v);
                }
            }
        }
    }

    reopen_tone();
    frequency_write();
    held_reload();
    held_noise();
    measured_levels();
    sampling_equivalence();
    sub_sample_writes();
    cycle_equivalence();
    sound_snapshot();
    rmw_writes();
    /* A backend reopen restores frequencies and digital output immediately. */
    ted_sound_machine_store(NULL, 0x0f, 0xff);
    ted_sound_machine_store(NULL, 0x10, 3);
    ted_sound_machine_store(NULL, 0x11, 0xb8);
    ted_sound_machine_init(NULL, 48000, 1773447);
    assert(test_sample() == volumeTable[0x38]);
    assert(snd.voice1_reload == 0); /* $3ff wraps to the lowest tone. */
    ted_sound_machine_store(NULL, 0x0f, 0xfe);
    ted_sound_machine_store(NULL, 0x11, 0x28);
    for (i = 0; i < 100; i++) {
        assert(test_sample() == volumeTable[0x28]);
    }
    /* Saturation and square-over-noise priority are defined by Commodore. */
    for (i = 8; i < 16; i++) {
        ted_sound_machine_store(NULL, 0x11, 0xe0 | i);
        assert(snd.noise == 0);
        assert(test_sample() == volumeTable[0x28]);
    }

    /* Splitting buffers must not alter the oscillator or resampler phase. */
    ted_sound_machine_store(NULL, 0x0f, 0xfd);
    ted_sound_machine_store(NULL, 0x11, 0x48);
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
    puts("TED audio: analytic PAL/NTSC tone integrals, reopen, controls and chunking passed");
    return 0;
}
