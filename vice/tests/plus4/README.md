# TED regressions

Configure an SDL2 VICE build, then run from the source directory:

```sh
tests/plus4/run-timer-test.sh /path/to/configured/build
CFLAGS='-g -fsanitize=address,undefined' tests/plus4/run-timer-test.sh /path/to/configured/build
```

The build argument defaults to the source directory for in-tree builds. The
runner creates its executable in a temporary directory and removes it on exit.
It compiles the actual `ted-timer.c`, uses VICE's inline alarm scheduling and
dispatch, and substitutes only alarm allocation/removal and the IRQ output.
It does not require ROMs, a display, or downloaded programs.

Coverage includes all three timers: low-byte stop/high-byte start, delayed
alarm dispatch, interrupt timestamps, zero-count wraparound, repeated reads,
and reset. Timers 2 and 3 additionally exercise live-byte preservation, with
and without preceding reads, and must roll over to a full 65536-clock period
rather than repeat the initial count. Timer 1 exercises the documented
low/high programming sequence and multiple overdue reloads. The preliminary
data sheet does not fully specify isolated-byte writes to timer 1's live
counter; those semantics are unchanged and are not asserted by these tests.
The original implementation fails timer 2's live-high-byte preservation test.

The timer clock is measured in two VICE main-CPU clock units. These tests verify
the counter/alarm contract, not the exact TED oscillator phase or CPU IRQ-entry
latency. Passing them is not a claim of cycle-exact display or demo compatibility.

References:

- [Commodore TED preliminary data sheet](https://www.pagetable.com/docs/ted/TED%207360R0%20Preliminary%20Data%20Sheet.pdf), register descriptions 0–5 (printed page 13): stop/start behavior, live reads, timer 1 reload, timers 2/3 free-running counters.

## Extended-frame crash regression

HNY2013 writes raster counters and border colors while generating nonstandard
video timing. Before the fix, TED skipped lines beyond the canvas without
applying their queued changes. Border changes accumulated past
`RASTER_CHANGES_MAX` and corrupted the adjacent change list, causing a crash in
`raster_changes_apply_all` when drawing resumed. The fix drains all five change
lists on skipped lines, without drawing outside the canvas. It does not change
the common raster renderer used by other machines.

Obtain the PRG from the [author's release page](https://plus4world.powweb.com/software/HNY2013)
and pass it to the integration test (the third-party binary is not included):

```sh
tests/plus4/run-extended-frame-test.sh /path/to/xplus4 /path/to/vice/data /path/to/hny2013.prg
```

This requires GNU `timeout` and works with headless or SDL2 builds (SDL uses the
dummy video driver by default). It verifies that direct-RAM autostart completes
and the emulator reaches 60 million clocks without crashing or hanging. VICE
returns status 1 on the deliberately requested cycle limit; other errors are
not accepted. This is a memory-safety regression, **not** a test that HNY2013's
picture is correctly emulated. The original checkout crashed in this test.

## Character-position reload regression

```sh
tests/plus4/run-position-test.sh /path/to/configured/build
CFLAGS='-g -fsanitize=address,undefined' tests/plus4/run-position-test.sh /path/to/configured/build
```

This compiles the production register handlers from `ted-mem.c`; unused
emulator functions are discarded by the linker. It verifies byte preservation,
the 10-bit mask, readback of the live reload register, and independence from
both the matrix DMA counter and the current bitmap row. The specification is
the Commodore TED preliminary data sheet, registers 26/27 (printed pages
17–18). It does not test the horizontal phase at which the reload is latched.

TED snapshot version 1.7 added preservation of the character-position
reload, current bitmap counter and horizontal-event state separately. Older snapshots initialize both
from their saved bitmap pointer, since they do not contain distinct values.

## Border-mode independence

```sh
python3 tests/plus4/run-border-test.py /path/to/xplus4 /path/to/petsrescue.d64
```

This requires Pillow and the Pets Rescue freeware disk. It captures the
pre-intro picture with normal borders and with borders hidden at the same
emulated clock, then compares their common pixels exactly. The old code
incorrectly gated internal counter advancement on the visible viewport; this
test failed before removing that gate and passes afterwards. It checks display
cropping independence, not whether the picture itself is correctly rendered.


## Horizontal-counter events

```sh
tests/plus4/run-counter-test.sh /path/to/configured/build
CFLAGS='-g -fsanitize=address,undefined' tests/plus4/run-counter-test.sh /path/to/configured/build
```

This compiles the production `ted-counter.c` with alarm allocation and CPU
phase notification replaced by test stubs. It checks separate live/reload
positions, 10-bit wraparound, horizontal jumps that skip increments, the stop
latch, idle bitmap behavior, inverted FF1E writes, preserved clock phase,
overflow above dot 455, and rescheduled deadlines. All 256 write values are
exercised in both clock phases. These tests establish the event contract at
VICE's four-dot clock resolution, not individual-dot silicon timing.

The timing basis is the preliminary data sheet's Internal Operation table
(printed page 11), and its register 30 description (printed page 18).
[FPGATED technical measurements](https://hackaday.io/project/11460-fpgated/details)
corroborate inverted horizontal writes and overflow at 512 for counts above
455. The row-counter integration follows the attribute-before-character DMA
sequence described in [Degauss's FLI notes](https://plus4world.powweb.com/plus4encyclopedia/500027).

With the Pets Rescue disk SHA256
`744c48f7bd44a1c0c765b6f615e8df0c6715510679fea926c7ac41009cf8d528`,
the border test's 50-million-clock run now displays the coherent pre-intro
illustration. A 250-million-clock run reaches the start menu. This is a visual
regression check, not a comparison against a captured original-machine frame.
Partial DMA, individual-dot write latency and all demo effects remain outside
this test's guarantees.

The optimized updater batches increments between reload, latch and horizontal
wrap. An independent per-clock reference retained in the test checks exact
state equivalence over 50,000 deterministic combinations of row, idle/fetch
flags, address values, interval length and overflow, plus all FF1E writes in
both phases. This guards the batching optimization without changing the
hardware assumptions above or adding serialized state.

## Masked raster compare events

```sh
tests/plus4/run-irq-test.sh /path/to/configured/build
CFLAGS='-g -fsanitize=address,undefined' tests/plus4/run-irq-test.sh /path/to/configured/build
```

The Commodore preliminary data sheet separates the event status (register 9)
from the interrupt mask (register 10). The
[Plus/4 World register documentation](https://plus4world.powweb.com/plus4encyclopedia/500024)
explicitly states that source flags latch regardless of their enable bits.
Changing the raster comparison to the current line previously discarded the
event when masked. The comparator now latches it and lets the existing IRQ
output logic apply the mask, as scheduled raster events already do.

The test compiles production `ted-irq.c` and the register handlers. It checks
both compare bytes with IRQ enabled/disabled, enabling an already pending
source, acknowledgement, repeated equal comparisons, nonmatching comparisons,
and preservation of a pending timer interrupt when acknowledging raster.
Only alarm removal and the CPU IRQ sink are substituted. The masked comparison
assertion fails with the preceding implementation. Existing comparator phase
and read-modify-write timing are retained, not independently validated against
silicon by this test.

## TED audio counters and sampling

```sh
tests/plus4/run-sound-test.sh /path/to/configured/build
CFLAGS='-O1 -g -fsanitize=address,undefined' tests/plus4/run-sound-test.sh /path/to/configured/build
CFLAGS='-O1 -g -fsanitize=address,undefined -DSOUND_SYSTEM_FLOAT' tests/plus4/run-sound-test.sh /path/to/configured/build
```

The test compiles production `ted-sound.c` and compares both oscillators with
analytic square-wave integrals for PAL/NTSC clocks at 8, 44.1, 48, 96 and
384 kHz. This covers low tones, audible tones, multiple transitions per
sample and the `$3ff` wraparound. It also checks deferred frequency reloads,
audio reopen with a saved tone/digital level, volume saturation, square-wave
priority over noise, and split-buffer/stereo/float consistency. The preceding
implementation fails the analytic waveform test. No other emulator is used
as an oracle.

[Commodore's preliminary data sheet](https://www.pagetable.com/docs/ted/TED%207360R0%20Preliminary%20Data%20Sheet.pdf)
defines controls in its Sound section and registers 14–18. Its preliminary
frequency formula is superseded here by the counter description in
[TLC's TED Sound Generation Internals](https://plus4world.powweb.com/plus4encyclopedia/500244):
quarter-single-clock ticks, `1023-r` half-periods and the special `$3ff` case.
The FPGA reproduction also uses a ten-bit counter and frequency-plus-one
reload; it is corroboration rather than an original-chip measurement.

The generator integrates its output over each sample and batches counter ticks
when no transition occurs. This avoids losing ultrasonic transitions and
rounded-step drift without look-ahead buffering. Box integration is not ideal
band-limited resampling. Held reload is checked with a frequency write between
counter clocks, including divider phase on release. Noise held at `$3fe` must
retain either output level and must not advance on a control write.

The level test uses the DC measurements in
[TLC's 2000 hardware report](https://plus4world.powweb.com/ma/1550), subtracting
idle level 31, averaging the two single voices, and normalizing the measured
dual-voice peak 15674 to 19976. This is an empirical mean-level model; it does
not reproduce PWM edge timing or characterize every board revision.
The noise feedback/clock and immediate-high tone approximation at `$3fe`
remain unverified. The original sample-path tests do not cover sub-sample writes or analogue
decay. Native cycle-timed writes are covered by the tests below.

The audio sampling optimization is checked against a deliberately simple
per-tick integrator over one million samples, with deterministic register
changes across PAL/NTSC and five sample rates. Every sample and the complete
sound state must agree, including held reload, noise and divider phase.
The production path precomputes the sample quotient/remainder and integrates
between counter overflows; it must not skip muted oscillators or noise events.

## Cycle-timed TED audio and snapshots

With the SID cartridge disabled, the first mixer slot delegates timing to
TED instead of running a disabled SID engine for its sample count. TED keeps
the integral of a partial sample across calls: two volume writes within one
sample are no longer collapsed to its final level. The enabled SID cartridge
keeps its existing path; no reSID engine code is modified. Float mixing copies
the native samples into the TED slot so its normal mixing metadata applies.

Tests cover a four-cycle pulse at each end of one 16-cycle sample, randomized
cycle chunks with register writes against a per-tick integrator, and output
capacity exhaustion including the fractional CPU cycle. RMW stores check the
unmodified value at the preceding cycle and the final write at the CPU clock.

TED snapshot version 1.8 appends the five sound registers, both active
counters and signs, noise register/output, sample clock/rate, divider phase,
partial integral/length and fractional cycle credit. Derived caches are
rebuilt. Truncated and invalid states are rejected; loading 1.7 still reads
the counter-event fields before falling back to register-only sound restore.
Old snapshots cannot recover sound state that they never saved. Loading at a
different output rate preserves oscillator/divider phase but discards the old
host-rate partial sample. Tests compare continued samples and state after a
round trip and a lazy audio-device open, and reject every truncation point.

These changes do not establish the analogue PWM transfer function, exact
noise clock/phase, relative voice clock phase, or the decay time at `$3fe`.
The existing approximation for those unresolved hardware details is retained.

## HSP DMA position reload

Re-entering the horizontal reload event with the DMA incrementer still active
now retains the incremented live DMA position. An ordinary scanline, which
crosses the increment stop first, keeps its existing reload. Tests exercise
the re-entry through `$FF1E`, ten-bit wraparound, and independence from bitmap
idle and bitmap reload. The previous implementation loses this displacement.

The circuit contract follows FPGATED 1.3's DMA address generator
(`videocounter_reload`), corroborated at the technique level by
[Alpharay's authors](https://plus4world.powweb.com/forum/38581) and the
[description of horizontal colour positioning](https://plus4world.powweb.com/forum/55993).
This is an FPGA-informed correction within VICE's existing event coordinates,
not a new measurement of sub-dot timing on an original TED.
Alpharay's sprite colours improve; the status bar and complete gameplay/audio
compatibility are not established by this unit test.

## Mid-line row writes and consecutive DMA requests

```sh
python3 tests/plus4/run-row-write-test.py /path/to/xplus4
sh tests/plus4/run-dma-follow-test.sh /path/to/configured/build
```

The row test requires ACME and Pillow. It checks hires and multicolour output:
a late `$ff1f` write must not change a cell already displayed, while an earlier
write must affect its row selection. This is a causality regression, not a
measurement of the exact pixel-fetch phase. The implementation uses VICE's
existing character-granularity foreground change queue.

The DMA test compiles the production fetch code. It checks that the second
DMA request survives a vertical-scroll change, that a new scroll match cannot
invent a previous request, and that overlapping requests stall the CPU only
once. Both buffers receive attribute data when the requests overlap. It also
checks the 10-bit address wrap.

The second-request latch, shared DMA FSM and address selection are based on
FPGATED 1.3 (`badline2`, `dma_state`, `tedaddress`, attribute/character buffers),
not a new original-hardware measurement. Exact intra-line changes to the DMA
request remain outside the aggregated fetch model. Snapshot version 1.10 adds
the CPU/render row values and second-request latch; existing snapshot limits
on pending raster changes are not resolved by this change.

Queued row changes also exercise the uncached raster background path. The
Pets Rescue border regression covers an open right border with zero space
remaining after horizontal scroll: a negative fill length must not be passed
to `memset`. The shared raster renderer now applies its existing positive-length
guard in both border-capability cases.

### Display enable and CPU clocks

```
python3 tests/plus4/run-display-enable-test.py /path/to/xplus4
sh tests/plus4/run-clock-test.sh /path/to/configured/build
```

The display-enable probe turns DEN on before the frame or during raster line
zero, then measures the same instruction sequence on a normal display line.
In both cases 24 CPU cycles must occupy 48 TED double clocks. This catches the
late-enable path that activated DMA without activating the display CPU clock
window, causing Alpharay to calibrate its timer too early and show a flickering
line above the HUD. FPGATED's EnableDisplay and clock-controller circuits
independently specify that line-zero DEN enables the display clock window.

The clock test compares the production conversion in `ted-timing.c` with
individual clock slots for all 114 phases and 0–8 pending CPU cycles. It covers
forced single clock and automatic clock with/without the display window,
including line crossings and even-phase alignment. Run with
`CFLAGS='-fsanitize=address,undefined -g'` for sanitizer checks.
