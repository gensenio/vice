#!/usr/bin/env python3
"""TED mid-line side and vertical border switches, without another emulator."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from PIL import Image

# Screenshot row of TED raster line 0 with the default PAL borders.
FIRST_ROW = 36


def display_span(screen, line):
    """First and last pixel of the given raster line that is not border."""
    y = FIRST_ROW + line
    border = screen.getpixel((0, y))
    inside = [x for x in range(screen.width) if screen.getpixel((x, y)) != border]
    return (inside[0], inside[-1]) if inside else None


def main():
    if len(sys.argv) != 2:
        sys.exit(f"Usage: {sys.argv[0]} /path/to/xplus4 (requires ACME and Pillow)")
    emulator = str(Path(sys.argv[1]).resolve())
    source = Path(__file__).with_name("ted-border.asm")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    expected = {
        0: {99: (32, 351), 100: (32, 343), 110: (40, 343), 121: (32, 351)},
        1: {203: (32, 351), 204: (32, 351), 220: (32, 351)},
        2: {198: (32, 351), 199: (32, 351), 201: (32, 351), 204: None},
    }
    with tempfile.TemporaryDirectory(prefix="ted-border-") as directory:
        work = Path(directory)
        for case, lines in expected.items():
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
                screen = screen.convert("RGB")
                for line, span in lines.items():
                    got = display_span(screen, line)
                    if got != span:
                        sys.exit(f"Case {case}, line {line}: display {got}, expected {span}")
            print(f"TED border case {case} passed")


if __name__ == "__main__":
    main()
