#!/usr/bin/env python3
"""TED blink counter increment line, without another emulator as oracle."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 2:
        sys.exit(f"Usage: {sys.argv[0]} /path/to/xplus4 (requires ACME)")
    emulator = str(Path(sys.argv[1]).resolve())
    source = Path(__file__).with_name("ted-blink.asm")
    with tempfile.TemporaryDirectory(prefix="ted-blink-") as directory:
        work = Path(directory)
        prg, labels = work / "test.prg", work / "test.labels"
        data, monitor = work / "result.bin", work / "test.mon"
        subprocess.run(["acme", "-f", "cbm", "-o", str(prg),
                        "--symbollist", str(labels), str(source)], check=True)
        done = re.search(r"\bdone\s*=\s*\$([0-9a-fA-F]+)", labels.read_text()).group(1)
        monitor.write_text(f'bsave "{data}" 0 $1800 $1802\nquit\n')
        result = subprocess.run(
            [emulator, "-default", "-pal", "+sound", "-warp", "-console",
             "-autostartprgmode", "1", "-autostart", str(prg),
             "-initbreak", "0x" + done, "-moncommands", str(monitor),
             "-limitcycles", "10000000"],
            env=dict(os.environ, SDL_VIDEODRIVER="dummy"),
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        if result.returncode != 0 or not data.exists():
            sys.exit(result.stdout.decode(errors="replace"))
        before, after, next_frame = ((b >> 3) & 15 for b in data.read_bytes())
        if after != (before + 1) & 15 or next_frame != after:
            sys.exit(f"Blink counter {before} (204), {after} (206), "
                     f"{next_frame} (next 204): expected one step on line 205")
        print("TED blink counter advances on line 205")


if __name__ == "__main__":
    main()
