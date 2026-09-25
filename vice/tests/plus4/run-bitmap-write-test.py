#!/usr/bin/env python3
"""TED bitmap fetch/CPU-write ordering, without another emulator as oracle."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops


def main():
    if len(sys.argv) != 2:
        sys.exit(f"Usage: {sys.argv[0]} /path/to/xplus4 (requires ACME and Pillow)")
    emulator = str(Path(sys.argv[1]).resolve())
    source = Path(__file__).with_name("ted-bitmap-write.asm")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy")
    with tempfile.TemporaryDirectory(prefix="ted-bitmap-write-") as directory:
        work = Path(directory)
        for hires in (False, True):
            screens = []
            for case in range(4):
                prg, png = work / "test.prg", work / "screen.png"
                command = ["acme", f"-DCASE={case}", "-f", "cbm", "-o", str(prg)]
                if hires:
                    command.append("-DHIRES=1")
                subprocess.run(command + [str(source)], check=True)
                result = subprocess.run(
                    [emulator, "-default", "-pal", "+sound", "-warp",
                     "-autostartprgmode", "1", "-autostart", str(prg),
                     "-limitcycles", "5000000", "-exitscreenshot", str(png)],
                    env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    timeout=30, check=False)
                if result.returncode != 1 or b"cycle limit reached" not in result.stdout:
                    sys.exit(result.stdout.decode(errors="replace"))
                with Image.open(png) as screen:
                    screens.append(screen.convert("RGB"))
            early = ImageChops.difference(screens[0], screens[1]).getbbox()
            if early != (32, 138, 40, 139):
                sys.exit(f"Unexpected pre-fetch write result: {early}")
            for case in (2, 3):
                if ImageChops.difference(screens[0], screens[case]).getbbox() is not None:
                    sys.exit(f"Late RAM write changed already displayed pixels (case {case})")
            print(f"TED {'hires' if hires else 'multicolour'} bitmap write ordering passed")


if __name__ == "__main__":
    main()
