#!/usr/bin/env python3
"""TED hires bitmap scroll gap colour, without another emulator as oracle."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from PIL import Image

# Screenshot row of TED raster line 0 and first display pixel, default PAL.
FIRST_ROW = 36
FIRST_PIXEL = 32


def main():
    if len(sys.argv) != 2:
        sys.exit(f"Usage: {sys.argv[0]} /path/to/xplus4 (requires ACME and Pillow)")
    emulator = str(Path(sys.argv[1]).resolve())
    source = Path(__file__).with_name("ted-bitmap-gap.asm")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    with tempfile.TemporaryDirectory(prefix="ted-bitmap-gap-") as directory:
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
            border = screen.getpixel((0, FIRST_ROW + 100))
            display = screen.getpixel((FIRST_PIXEL + 160, FIRST_ROW + 100))
            gaps = {line: [screen.getpixel((FIRST_PIXEL + x, FIRST_ROW + line))
                           for x in range(4)]
                    for line in (99, 100)}
    if border == display:
        sys.exit(f"Border and display have the same colour {display}")
    for line, gap in gaps.items():
        if gap != [display] * 4:
            sys.exit(f"Line {line} scroll gap {gap}: expected the 0 pixel colour {display}")
    print("TED hires bitmap scroll gap passed")


if __name__ == "__main__":
    main()
