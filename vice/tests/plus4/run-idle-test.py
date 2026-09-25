#!/usr/bin/env python3
"""TED idle fetch data in an opened lower border, without another emulator."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from PIL import Image

# Screenshot row of TED raster line 0 and first display pixel, default PAL.
FIRST_ROW = 36
FIRST_PIXEL = 32


def line_bytes(screen, line):
    """Bytes drawn on a line: foreground (black) pixels are ones."""
    y = FIRST_ROW + line
    background = screen.getpixel((FIRST_PIXEL - 1, y))  # border is black
    result = []
    for cell in range(40):
        value = 0
        for bit in range(8):
            pixel = screen.getpixel((FIRST_PIXEL + cell * 8 + bit, y))
            value = (value << 1) | (pixel == background)
        result.append(value)
    return result


def main():
    if len(sys.argv) != 2:
        sys.exit(f"Usage: {sys.argv[0]} /path/to/xplus4 (requires ACME and Pillow)")
    emulator = str(Path(sys.argv[1]).resolve())
    source = Path(__file__).with_name("ted-idle.asm")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    with tempfile.TemporaryDirectory(prefix="ted-idle-") as directory:
        work = Path(directory)
        for case, expected in ((0, 0xfc), (1, 0x81)):
            prg, png = work / "test.prg", work / "screen.png"
            subprocess.run(["acme", f"-DCASE={case}", "-f", "cbm", "-o", str(prg),
                            str(source)], check=True)
            result = subprocess.run(
                [emulator, "-default", "-pal", "+sound", "-warp",
                 "-autostartprgmode", "1", "-autostart", str(prg),
                 "-limitcycles", "5000000", "-exitscreenshot", str(png)],
                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                timeout=30, check=False)
            if result.returncode != 1 or b"cycle limit reached" not in result.stdout:
                sys.exit(result.stdout.decode(errors="replace"))
            with Image.open(png) as screen:
                got = set(line_bytes(screen.convert("RGB"), 220))
            if got != {expected}:
                sys.exit(f"Case {case}: idle bytes {sorted(got)}, expected {expected:#04x}")
            print(f"TED idle fetch case {case} passed (${expected:02x})")


if __name__ == "__main__":
    main()
