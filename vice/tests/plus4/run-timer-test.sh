#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
config_build=${1:-$root}
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
${CC:-cc} -DHAVE_CONFIG_H -I"$config_build/src" -I"$root/src" -I"$root/src/plus4" \
    -I"$root/src/raster" -I"$root/src/video" \
    -I"$root/src/arch/headless" -I"$root/src/arch/shared" \
    -Wall -Wextra -Wno-unused-parameter ${CFLAGS:-} \
    "$root/tests/plus4/ted-timer-test.c" "$root/src/plus4/ted-timer.c" \
    -o "$build/ted-timer-test"
"$build/ted-timer-test"
