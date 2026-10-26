#pragma once

#include <stdbool.h>
#include "bms_data.h"
#include "lvgl.h"
#include "sdkconfig.h"
#include "ui_fonts.h"

/* Saturated colours read best on this TN panel. */
#define UI_COLOR_TEXT       lv_color_hex(0xF0F0F0)
#define UI_COLOR_STALE      lv_color_hex(0x6A6A6A)
#define UI_COLOR_HEADING    lv_color_hex(0x00C8C8)
#define UI_COLOR_OK         lv_color_hex(0x30E050)
#define UI_COLOR_CHARGE     lv_color_hex(0x30E050)
#define UI_COLOR_DISCHARGE  lv_color_hex(0xFF9020)
#define UI_COLOR_WARN       lv_color_hex(0xFF9020)
#define UI_COLOR_INFO       lv_color_hex(0xFFE020)
#define UI_COLOR_FAULT      lv_color_hex(0xFF3030)
#define UI_COLOR_COLD       lv_color_hex(0x40A0FF)
#define UI_COLOR_MIN_CELL   lv_color_hex(0x40A0FF)
#define UI_COLOR_MAX_CELL   lv_color_hex(0xFF3030)
#define UI_COLOR_PANEL      lv_color_hex(0x1C2228)

/* Layout (320 x 240): status bar, swipeable tiles, page dots. */
#define UI_STATUS_H         18
#define UI_DOTS_H           10
#define UI_TILE_H           (240 - UI_STATUS_H - UI_DOTS_H)
#define UI_PAD              4

#if CONFIG_DBMS_SOURCE_MOCK
#define IS_MOCK_SOURCE      1
#else
#define IS_MOCK_SOURCE      0
#endif

/* Current below this magnitude is shown as idle. */
#define UI_IDLE_CURRENT_MA  300

/* Helpers (ui_util.c). All of them skip the redraw when nothing changed. */
void ui_label_set_text(lv_obj_t *label, const char *text);
void ui_label_printf(lv_obj_t *label, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void ui_set_text_color(lv_obj_t *obj, lv_color_t color);
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color);
lv_obj_t *ui_row(lv_obj_t *parent);

/* Colour by threshold; returns UI_COLOR_STALE when stale. */
lv_color_t ui_temp_color(int32_t dc, bool stale);
lv_color_t ui_delta_color(uint32_t mv, bool stale);
static inline lv_color_t ui_live(lv_color_t color, bool stale)
{
    return stale ? UI_COLOR_STALE : color;
}

/* Formatting with plausibility checks; "--" when out of range. */
void ui_fmt_temp(char *buf, size_t len, int32_t dc, bool present);
void ui_fmt_cell(char *buf, size_t len, uint32_t mv);

/* Screens: create once in a tile, update from a model snapshot. */
void ui_overview_create(lv_obj_t *tile);
void ui_overview_update(const bms_data_t *d, bool stale);
void ui_cells_create(lv_obj_t *tile);
void ui_cells_update(const bms_data_t *d, bool stale);
void ui_alarms_create(lv_obj_t *tile);
void ui_alarms_update(const bms_data_t *d, bool stale);
void ui_info_create(lv_obj_t *tile);
void ui_info_update(const bms_data_t *d, bool stale);
