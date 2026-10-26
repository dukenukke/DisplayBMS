#pragma once

#include "lvgl.h"

/*
 * 1-bpp pixel fonts: this panel shows anti-aliased edges as grey speckles.
 * ui_font_unscii_16/32 come from unscii-16.ttf (8x16 design, 1x and 2x).
 * LVGL's built-in lv_font_unscii_8/16 come from unscii-8.ttf (8x8 design, 1x and 2x)
 * and have no degree sign.
 */
LV_FONT_DECLARE(ui_font_unscii_16);
LV_FONT_DECLARE(ui_font_unscii_32);

#define UI_FONT_SMALL   (&lv_font_unscii_8)
#define UI_FONT_TEXT    (&ui_font_unscii_16)
#define UI_FONT_LARGE   (&ui_font_unscii_32)
