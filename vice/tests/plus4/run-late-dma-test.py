#!/usr/bin/env python3
"""TED attribute DMA requested in the middle of a line (late bad line)."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from PIL import Image

# Screenshot row of TED raster line 0 and first display pixel, default PAL.
FIRST_ROW = 36
FIRST_PIXEL = 32


def cell_colours(screen, line):
    y = FIRST_ROW + line
    return [screen.getpixel((FIRST_PIXEL + 8 * i + 4, y)) for i in range(40)]


def main():
    if len(sys.argv) != 2:
        sys.exit(f"Usage: {sys.argv[0]} /path/to/xplus4 (requires ACME and Pillow)")
    emulator = str(Path(sys.argv[1]).resolve())
    source = Path(__file__).with_name("ted-late-dma.asm")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    with tempfile.TemporaryDirectory(prefix="ted-late-dma-") as directory:
        work = Path(directory)
        prg, labels = work / "test.prg", work / "test.labels"
        png, data, monitor = work / "screen.png", work / "result.bin", work / "test.mon"
        subprocess.run(["acme", "-f", "cbm", "-o", str(prg), "--symbollist",
                        str(labels), str(source)], check=True)
        done = re.search(r"\bdone\s*=\s*\$([0-9a-fA-F]+)", labels.read_text()).group(1)
        common = [emulator, "-default", "-pal", "+sound", "-warp",
                  "-autostartprgmode", "1", "-autostart", str(prg)]
        result = subprocess.run(common + ["-limitcycles", "5000000", "-exitscreenshot", str(png)],
                                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=30, check=False)
        if result.returncode != 1 or b"cycle limit reached" not in result.stdout:
            sys.exit(result.stdout.decode(errors="replace"))
        monitor.write_text(f'bsave "{data}" 0 $1f00 $1f01\nquit\n')
        result = subprocess.run(common + ["-console", "-initbreak", "0x" + done,
                                          "-moncommands", str(monitor),
                                          "-limitcycles", "5000000"],
                                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=30, check=False)
        if result.returncode != 0 or not data.exists():
            sys.exit(result.stdout.decode(errors="replace"))
        before, after = data.read_bytes()
        with Image.open(png) as screen:
            screen = screen.convert("RGB")
            colour_a = cell_colours(screen, 90)[0]
            row = cell_colours(screen, 102)
    # $ff1e counts half cycles from cycle 16.  The store writes 12 cycles
    # after the first read (LDA #, STA abs, single clock); without a halt
    # the second read follows 12 cycles later, with it after cycle 90.
    store = before // 2 + 16 + 12
    if not 30 <= store <= 70:
        sys.exit(f"Store at cycle {store}, outside the tested range")
    if after // 2 + 16 < 90:
        sys.exit(f"CPU not halted: $ff1e {before:#04x} -> {after:#04x}")
    if row[0] != colour_a:
        sys.exit(f"Line 102 starts with {row[0]}, expected the old attributes {colour_a}")
    split = next((i for i, c in enumerate(row) if c != colour_a), None)
    # Slots at store + 4, + 6 and + 8 receive the bus; slot i is at 12 + 2i.
    # The screenshot comes from a later frame than the measured store, and
    # the polling jitter places the store between cycles 36 and 56.
    if (split is None or not 14 <= split <= 24 or len(set(row[split:split + 3])) != 1
            or row[split + 3] in (colour_a, row[split])
            or any(c != row[split + 3] for c in row[split + 3:])):
        sys.exit(f"Line 102 cells {row}: expected old, three bus, new attributes")
    print(f"TED late attribute DMA passed (bus bytes at characters {split}-{split + 2})")


if __name__ == "__main__":
    main()
