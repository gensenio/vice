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

## Side and vertical border flip-flops

```sh
sh tests/plus4/run-border-timing-test.sh /path/to/configured/build
python3 tests/plus4/run-border-timing-test.py /path/to/xplus4
```

The previous handlers were copied from the VIC-II and used its cycles (17 and
56) and its first/last border lines. On TED, CSEL is tested at cycle 16 (40
columns) or 18 (38 columns) to start the display and at cycle 94 (38 columns)
or 96 (40 columns) to stop it, and each test only fires for its own width.
A mid-line CSEL write therefore changes the right edge of the same line,
switching 38 to 40 columns during cycles 16–17 leaves the line in the border,
and switching 40 to 38 columns during cycles 94–95 keeps the border open.
The 38 column window is eight pixels narrower on each side (the VIC-II value
was seven and nine). The vertical window opens on line 4 (25 rows) or 8 (24
rows) and closes on line 200 (24 rows) or 204 (25 rows); a RSEL/DEN change
on one of these lines applies that line's test, using the line number that
is incremented at cycle 112. Switching rows on lines 199 or 203 therefore no
longer blanks those lines: 25 to 24 rows on line 203 opens the lower border.

These cycles and lines follow the matching display-window and row-select
logic of YapeSDL (`TED::ted_process`, cases 16/18/94/96, and `newLine`)
and plus4emu (`TED7360::run` columns 0/2/78/80, `write_register_FF06`),
authorized for this analysis, whose horizontal coordinates coincide with
VICE's cycles (YapeSDL) or are offset by 16 (plus4emu). They are consistent
with the preliminary data sheet's 40/38 column and 25/24 row geometry. They
are not new measurements of original hardware.

The unit test includes the production `ted-mem.c` handlers and checks every
cycle of a line. The integration test measures display spans in screenshots
after writes in the middle of lines 100, 199 and 203 (vertical scroll 0
keeps them free of DMA); the previous build fails all three cases. In the open border
VICE still draws the idle background colour; TED's idle graphics there are
not modelled.

## Blink counter line

```sh
sh tests/plus4/run-blink-test.sh /path/to/configured/build
python3 tests/plus4/run-blink-test.py /path/to/xplus4
```

The blink counter in `$ff1f` bits 3–6 (and the flash/cursor state toggled when
it wraps) advanced at VICE's vertical sync, with a FIXME. The data sheet's
horizontal decodes list "Increment Blink" at dot 336 (cycle 100); YapeSDL
(`newLine`, line 205) and plus4emu (`TED7360::run`, column 87 of line 205)
both qualify it with line 205. The counter now advances when line 205 ends;
`$ff1f` reads and writes from cycle 100 of that line account for the pending
increment. Because it follows the line and not vertical sync, raster-counter
writes that skip or repeat line 205 change its rate, as on those emulators.
The line qualifier is their choice, not a statement of the data sheet.

The integration test reads `$ff1f` on lines 204 and 206 and on line 204 of the
following frame; the previous build advanced between line 206 and the next
frame. The unit test covers reads and writes around cycle 100 and a write of
15 after the counter wrapped.

## Mid-line horizontal scroll

```sh
python3 tests/plus4/run-hscroll-test.py /path/to/xplus4
```

A `$ff07` scroll write after the second character was deferred to the next
line. Both YapeSDL (`writeHorizShift`, aligned to the next single clock) and
plus4emu (`setHorizontalScroll` as a delayed event used by the per-cycle
renderers) apply it within the line. The change is now queued on VICE's
character-granular foreground list from the character after the one being
output (character i is output during cycles 16 + 2i and 17 + 2i), so later
characters move: a larger scroll leaves background colour in the gap, as
YapeSDL's `drawEmptyArea` does, and a smaller one overlaps the previous
character. The pixel within a character at which TED reloads its shift
register is not modelled, so the split is exact only to a character.

The test alternates reverse spaces and spaces and writes a scroll of four in
the middle of line 100 (vertical scroll 0 keeps it free of DMA). Line 99 must
be unscrolled, line 101 scrolled, and line 100 unscrolled on the left and
scrolled on the right. The previous build scrolled only from line 101.

## Attribute DMA requested in the middle of a line

```sh
python3 tests/plus4/run-late-dma-test.py /path/to/xplus4
```

A `$ff06` vertical-scroll write that makes the current line an attribute DMA
line after the fetch cycle was ignored (the handling copied from the VIC-II
was disabled). Both YapeSDL (`TED::Write`, `$ff06`, "Delayed DMA") and
plus4emu (`processDelayedEvents`, vertical scroll write and DMA cycles 1–5)
start the DMA within the line: characters already passed keep the previous
request's attributes, three slots receive what the halted CPU keeps on the
bus, and the remaining slots receive the attributes, while the CPU is halted
to the end of the DMA window. The late request now does the same. With the
attribute slot for character i at cycle 12 + 2i (plus4emu column 110 + 2i),
the slots at the store cycle + 4, + 6 and + 8 receive the operand of the
instruction after the store (YapeSDL reads `PC + 1`), and the CPU is halted
until cycle 90, where it resumes after an ordinary bad line. The following
line fetches the characters, as for a request at the fetch cycle. The pixel
phase of the DMA relative to the store follows plus4emu's delayed events and
is not a new measurement of original hardware.

The test changes the attribute base between the ordinary fetch of a row and
a write of vertical scroll 4 in the middle of line 100, followed by
`LDA #$55`. The next row must show the old attributes, three cells of `$55`
and the new attributes, and the second `$ff1e` read must come after cycle 90.
The previous build fetched nothing and did not halt the CPU. Pets Rescue's
register table routine writes `$ff06` without raster synchronisation and now
triggers such requests during its intro, shifting its later animation phase.

## Idle fetch data

```sh
python3 tests/plus4/run-idle-test.py /path/to/xplus4
```

In idle cycles TED reads `$ffff`. VICE used RAM at `$ffff` (the VIC-II's
`$3fff`/`$39ff` distinction had been reduced to it). Both plus4emu
(`idleMemoryRead` through `tedDMAReadMap`, which follows `$ff3e`/`$ff3f`)
and YapeSDL (`Read(0xFFFF)` in the idle branches of its renderers) read it
through the ROM/RAM selection, normally the last byte of the Kernal ROM
(`$fc` in 318004-05, the high byte of the IRQ vector). Idle fetches now use
the selection of the DMA fetches (`$ff13` bit 0). The test opens the lower
border and stores `$81` in RAM at `$ffff`: with ROM selected the idle line
must show `$fc`, with RAM selected (`$ff3f`) `$81`. DRAM refresh addresses
read during the refresh cycles, and the CPU bus seen by YapeSDL in double
clock mode, are not modelled.
