#!/usr/bin/env python3
"""TED freeze bit ($ff07 bit 5), checked against the data sheet."""
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
    source = Path(__file__).with_name("ted-freeze.asm")
    with tempfile.TemporaryDirectory(prefix="ted-freeze-") as directory:
        work = Path(directory)
        prg, labels = work / "test.prg", work / "test.labels"
        data, monitor = work / "result.bin", work / "test.mon"
        subprocess.run(["acme", "-f", "cbm", "-o", str(prg),
                        "--symbollist", str(labels), str(source)], check=True)
        done = re.search(r"\bdone\s*=\s*\$([0-9a-fA-F]+)", labels.read_text()).group(1)
        monitor.write_text(f'bsave "{data}" 0 $1800 $1810\nquit\n')
        result = subprocess.run(
            [emulator, "-default", "-pal", "+sound", "-warp", "-console",
             "-autostartprgmode", "1", "-autostart", str(prg),
             "-initbreak", "0x" + done, "-moncommands", str(monitor),
             "-limitcycles", "10000000"],
            env=dict(os.environ, SDL_VIDEODRIVER="dummy"),
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        if result.returncode != 0 or not data.exists():
            sys.exit(result.stdout.decode(errors="replace"))
        r = data.read_bytes()
        before = (r[0], r[1])
        frozen = (r[2], r[3], r[4] | r[5] << 8)
        later = (r[6], r[7], r[8] | r[9] << 8)
        after = (r[11], r[12], r[13] | r[14] << 8)
        errors = []
        if frozen != later:
            errors.append(f"counters moved while frozen: {frozen} -> {later}")
        if frozen[1] != before[1]:
            errors.append(f"line {before[1]} before the freeze, {frozen[1]} at it")
        if r[10] & 0x02:
            errors.append("raster compare line reached while frozen")
        if after[1] != later[1] or not 0 < (after[0] - later[0]) & 0xff < 32:
            errors.append(f"position {later[:2]} frozen, {after[:2]} after")
        if not 0 < later[2] - after[2] < 16:
            errors.append(f"timer 1 {later[2]:04x} frozen, {after[2]:04x} after")
        if not r[15] & 0x02 or r[16] < 222:
            errors.append(f"raster compare line 222 not reached after the "
                          f"freeze ($ff09 {r[15]:02x}, line {r[16]})")
        if errors:
            sys.exit("\n".join(errors))
        print(f"TED freeze passed (position {frozen[:2]}, timer 1 "
              f"{frozen[2]:04x} kept during the loop)")


if __name__ == "__main__":
    main()
