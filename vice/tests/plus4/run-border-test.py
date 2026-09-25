#!/usr/bin/env python3
"""PAL TED crop-independence regression; requires Pillow and a Pets Rescue disk."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops


def main():
    if len(sys.argv) != 3:
        sys.exit(f"Usage: {sys.argv[0]} /path/to/xplus4 /path/to/petsrescue.d64")
    emulator, disk = map(lambda p: str(Path(p).resolve()), sys.argv[1:])
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    with tempfile.TemporaryDirectory(prefix="ted-borders-") as directory:
        images = []
        for mode in (0, 3):
            image = Path(directory) / f"border-{mode}.png"
            result = subprocess.run(
                [emulator, "-default", "-pal", "-seed", "1", "+sound", "-warp",
                 "-TEDborders", str(mode), "-drive8type", "1541",
                 "-limitcycles", "50000000", "-autostart", disk,
                 "-exitscreenshot", str(image)],
                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                timeout=60, check=False,
            )
            if (result.returncode != 1 or b"Autostart: Done." not in result.stdout
                    or b"cycle limit reached" not in result.stdout):
                sys.exit(result.stdout.decode(errors="replace"))
            with Image.open(image) as screen:
                images.append(screen.convert("RGB"))
        normal, cropped = images
        if normal.size != (384, 288) or cropped.size != (320, 200):
            sys.exit("Unexpected PAL screenshot dimensions")
        # Normal PAL starts at TV line 19, cropped PAL at TV line 59;
        # the normal left border is 32 pixels wide (ted-timing.h).
        common = normal.crop((32, 40, 352, 240))
        if ImageChops.difference(common, cropped).getbbox() is not None:
            sys.exit("TED border mode changed the emulated picture")
        if len(common.getcolors(320 * 200)) < 16:
            sys.exit("Picture is blank or has not reached the test scene")
        print("TED border regression passed: common pixels are identical")


if __name__ == "__main__":
    main()
