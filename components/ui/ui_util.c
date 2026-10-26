#include "ui_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"

#define UI_TEXT_MAX 96

void ui_label_set_text(lv_obj_t *label, const char *text)
{
    const char *cur = lv_label_get_text(label);
    if (cur && strcmp(cur, text) == 0) return;
    lv_label_set_text(label, text);
}

void ui_label_printf(lv_obj_t *label, const char *fmt, ...)
{
    char buf[UI_TEXT_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ui_label_set_text(label, buf);
}

void ui_set_text_color(lv_obj_t *obj, lv_color_t color)
{
    if (lv_color_eq(lv_obj_get_style_text_color(obj, LV_PART_MAIN), color)) return;
    lv_obj_set_style_text_color(obj, color, 0);
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "");
    if (font) lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t *ui_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return row;
}

lv_color_t ui_temp_color(int32_t dc, bool stale)
{
    if (stale) return UI_COLOR_STALE;
    if (dc >= CONFIG_DBMS_TH_TEMP_CRIT_C * 10) return UI_COLOR_FAULT;
    if (dc >= CONFIG_DBMS_TH_TEMP_WARN_C * 10) return UI_COLOR_WARN;
    if (dc < CONFIG_DBMS_TH_TEMP_COLD_C * 10) return UI_COLOR_COLD;
    return UI_COLOR_TEXT;
}

lv_color_t ui_delta_color(uint32_t mv, bool stale)
{
    if (stale) return UI_COLOR_STALE;
    if (mv >= CONFIG_DBMS_TH_DELTA_CRIT_MV) return UI_COLOR_FAULT;
    if (mv >= CONFIG_DBMS_TH_DELTA_WARN_MV) return UI_COLOR_WARN;
    return UI_COLOR_OK;
}

void ui_fmt_temp(char *buf, size_t len, int32_t dc, bool present)
{
    if (!present || !bms_temp_dc_ok(dc)) {
        snprintf(buf, len, "--");
        return;
    }
    /* Whole degrees: tenths are noise on these sensors and cost screen width. */
    int32_t rounded = (dc >= 0 ? dc + 5 : dc - 5) / 10;
    snprintf(buf, len, "%ld\xC2\xB0", (long)rounded);
}

void ui_fmt_cell(char *buf, size_t len, uint32_t mv)
{
    if (!bms_cell_mv_ok(mv)) {
        snprintf(buf, len, "--");
        return;
    }
    snprintf(buf, len, "%lu.%03lu", (unsigned long)(mv / 1000), (unsigned long)(mv % 1000));
}
