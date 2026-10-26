#!/usr/bin/env sh
# Regenerates components/ui/fonts/ui_font_unscii_{16,32}.c (1 bpp, no anti-aliasing).
# Source: unscii-16.ttf (8x16 design) from http://viznut.fi/unscii/ (Public Domain;
# the GPL "unscii-16-full" variant is NOT used). 16 px = 1x, 32 px = exact 2x.
# Usage: tools/gen_fonts.sh path/to/unscii-16.ttf
set -eu
TTF="${1:?path to unscii-16.ttf}"
DIR="$(dirname "$0")/../components/ui/fonts"
for SIZE in 16 32; do
    OUT="$DIR/ui_font_unscii_$SIZE.c"
    npx -y lv_font_conv@1.5.3 --font "$TTF" --size "$SIZE" --bpp 1 --no-compress --format lvgl \
        -r 0x20-0x7E -r 0xB0 --lv-font-name "ui_font_unscii_$SIZE" -o "$OUT"
    sed -i '4s|.*| * Opts: see tools/gen_fonts.sh (unscii-16.ttf, Public Domain, http://viznut.fi/unscii/)|' "$OUT"
done
