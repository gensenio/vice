#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
config_build=${1:-$root}
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
${CC:-cc} -DHAVE_CONFIG_H -I"$config_build/src" -I"$root/src" -I"$root/src/plus4" \
    -I"$root/src/raster" -I"$root/src/video" \
    -I"$root/src/joyport" -I"$root/src/core" \
    -I"$root/src/arch/headless" -I"$root/src/arch/shared" \
    -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections \
    ${CFLAGS:-} "$root/tests/plus4/ted-reverse-test.c" \
    -Wl,--gc-sections -o "$build/ted-reverse-test"
"$build/ted-reverse-test"
