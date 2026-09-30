#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
config_build=${1:-$root}
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
${CC:-cc} -DHAVE_CONFIG_H -I"$config_build/src" -I"$root/src" -I"$root/src/plus4" \
    -I"$root/src/plus4/cart" -I"$root/src/c64/cart" -I"$root/src/cartio" \
    -I"$root/src/raster" -I"$root/src/video" -I"$root/src/drive" \
    -I"$root/src/joyport" -I"$root/src/core" -I"$root/src/datasette" \
    -I"$root/src/tape" -I"$root/src/tapeport" -I"$root/src/monitor" \
    -I"$root/src/arch/headless" -I"$root/src/arch/shared" \
    -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections \
    ${CFLAGS:-} "$root/tests/plus4/ted-open-space-test.c" \
    "$root/src/plus4/plus4mem-open.c" \
    -Wl,--gc-sections -o "$build/ted-open-space-test"
"$build/ted-open-space-test"
