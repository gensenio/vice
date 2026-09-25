#!/usr/bin/env python3
"""Measure CPU clocks after DEN is enabled before/during raster line zero."""
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
    source = Path(__file__).with_name("ted-display-enable.asm")
    with tempfile.TemporaryDirectory(prefix="ted-display-enable-") as directory:
        work = Path(directory)
        for case in (0, 1):
            prg, labels = work / "test.prg", work / "test.labels"
            data, monitor = work / "result.bin", work / "test.mon"
            subprocess.run(["acme", f"-DCASE={case}", "-f", "cbm", "-o", str(prg),
                            "--symbollist", str(labels), str(source)], check=True)
            done = re.search(r"\bdone\s*=\s*\$([0-9a-fA-F]+)", labels.read_text()).group(1)
            monitor.write_text(f'bsave "{data}" 0 $1800 $1801\nquit\n')
            result = subprocess.run(
                [emulator, "-default", "-pal", "+sound", "-warp", "-console",
                 "-autostartprgmode", "1", "-autostart", str(prg),
                 "-initbreak", "0x" + done, "-moncommands", str(monitor),
                 "-limitcycles", "5000000"],
                env=dict(os.environ, SDL_VIDEODRIVER="dummy"),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            if result.returncode != 0 or not data.exists():
                sys.exit(result.stdout.decode(errors="replace"))
            first, last = data.read_bytes()
            # STA abs + eight NOPs + LDA abs: 24 CPU cycles. With the
            # display clock active, these span 48 TED double clocks.
            # FF1E advances by two per double clock and wraps at 228.
            delta = (last - first) % 228
            if delta != 96:
                sys.exit(f"DEN case {case}: FF1E delta {delta}, expected 96")
            print(f"TED DEN case {case}: 24 CPU cycles span 48 TED clocks")


if __name__ == "__main__":
    main()
