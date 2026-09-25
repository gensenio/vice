#!/bin/sh
# Reproduce the raster-change overflow with the publicly available HNY2013.
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
if [ "$#" -ne 3 ]; then
    echo "Usage: $0 /path/to/xplus4 /path/to/vice/data /path/to/hny2013.prg" >&2
    exit 2
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
export SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy}
export SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy}
status=0
timeout 30 "$1" -default -seed 1 -directory "$2" +sound -warp \
    -autostartprgmode 1 -limitcycles 60000000 -autostart "$3" \
    > "$work/run.log" 2>&1 || status=$?
# VICE returns 1 on the requested cycle-limit termination.
if [ "$status" -ne 1 ] || ! grep -q 'Autostart: Done.' "$work/run.log" \
    || ! grep -q 'cycle limit reached' "$work/run.log"; then
    cat "$work/run.log" >&2
    echo "Extended-frame regression failed (exit $status)" >&2
    exit 1
fi
echo "Extended-frame regression passed (60 million clocks, no crash)"
