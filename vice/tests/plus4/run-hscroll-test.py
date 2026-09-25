#!/usr/bin/env python3
"""TED mid-line horizontal scroll write, without another emulator as oracle."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from PIL import Image

# Screenshot row of TED raster line 0 and first display pixel, default PAL.
FIRST_ROW = 36
FIRST_PIXEL = 32


def block_starts(screen, line):
    """Offsets from the display start of the 16 pixel blocks' left edges."""
    y = FIRST_ROW + line
    background = screen.getpixel((0, y))
    starts = []
    for x in range(FIRST_PIXEL, FIRST_PIXEL + 320):
        if (screen.getpixel((x, y)) != background
                and screen.getpixel((x - 1, y)) == background):
            starts.append(x - FIRST_PIXEL)
    return starts


def main():
    if len(sys.argv) != 2:
        sys.exit(f"Usage: {sys.argv[0]} /path/to/xplus4 (requires ACME and Pillow)")
    emulator = str(Path(sys.argv[1]).resolve())
    source = Path(__file__).with_name("ted-hscroll.asm")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    with tempfile.TemporaryDirectory(prefix="ted-hscroll-") as directory:
        work = Path(directory)
        prg, png = work / "test.prg", work / "screen.png"
        subprocess.run(["acme", "-f", "cbm", "-o", str(prg), str(source)], check=True)
        result = subprocess.run(
            [emulator, "-default", "-pal", "+sound", "-warp",
             "-autostartprgmode", "1", "-autostart", str(prg),
             "-limitcycles", "5000000", "-exitscreenshot", str(png)],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=30, check=False)
        if result.returncode != 1 or b"cycle limit reached" not in result.stdout:
            sys.exit(result.stdout.decode(errors="replace"))
        with Image.open(png) as screen:
            screen = screen.convert("RGB")
            before = block_starts(screen, 99)
            split = block_starts(screen, 100)
            after = block_starts(screen, 101)
    if before != list(range(0, 320, 16)):
        sys.exit(f"Line 99 blocks {before}: expected no scroll")
    if after != list(range(4, 320, 16)):
        sys.exit(f"Line 101 blocks {after}: expected a four pixel scroll")
    # The write lands between cycles 62 and 80, i.e. characters 24-32.
    left = [x for x in split if x % 16 == 0]
    right = [x for x in split if x % 16 == 4]
    if (left + right != split or not left or not right
            or not 24 * 8 <= right[0] <= 34 * 8 or left[-1] >= right[0]):
        sys.exit(f"Line 100 blocks {split}: expected unscrolled, then scrolled")
    print(f"TED mid-line horizontal scroll passed (split at pixel {right[0]})")


if __name__ == "__main__":
    main()
