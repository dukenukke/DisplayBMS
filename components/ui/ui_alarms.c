#include "ui_internal.h"

#include "bms_alarms.h"

static lv_obj_t *s_header;
static lv_obj_t *s_rows[BMS_ALARM_BITS];

void ui_alarms_create(lv_obj_t *tile)
{
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tile, UI_PAD, 0);
    lv_obj_set_style_pad_row(tile, 4, 0);
    lv_obj_set_scroll_dir(tile, LV_DIR_VER);

    s_header = ui_label(tile, NULL, UI_COLOR_HEADING);
    lv_label_set_text(s_header, "Waiting for data");
    /* One preallocated row per bit: no allocation while data are updating. */
    for (int i = 0; i < BMS_ALARM_BITS; ++i) {
        s_rows[i] = ui_label(tile, NULL, UI_COLOR_TEXT);
        lv_obj_set_width(s_rows[i], LV_PCT(100));
        lv_label_set_long_mode(s_rows[i], LV_LABEL_LONG_MODE_WRAP);
        lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_color_t severity_color(bms_alarm_severity_t s)
{
    switch (s) {
    case BMS_ALARM_INFO: return UI_COLOR_INFO;
    case BMS_ALARM_WARN: return UI_COLOR_WARN;
    default:             return UI_COLOR_FAULT;
    }
}

void ui_alarms_update(const bms_data_t *d, bool stale)
{
    if (!d->live.valid) return;
    uint32_t alarms = d->live.alarms;
    unsigned active = 0;
    for (unsigned bit = 0; bit < BMS_ALARM_BITS; ++bit) {
        lv_obj_t *row = s_rows[bit];
        if (!(alarms & (1u << bit))) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        ++active;
        const bms_alarm_desc_t *desc = bms_alarm_desc(bit);
        if (desc->name) {
            ui_label_printf(row, "* %s", desc->name);
        } else {
            ui_label_printf(row, "* Unknown alarm bit %u", bit);
        }
        ui_set_text_color(row, ui_live(severity_color(desc->severity), stale));
        lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
    }

    if (active == 0) {
        ui_label_set_text(s_header, stale ? "No active alarms (stale)" : "No active alarms");
        ui_set_text_color(s_header, ui_live(UI_COLOR_OK, stale));
    } else {
        ui_label_printf(s_header, "Active alarms: %u%s", active, stale ? " (stale)" : "");
        ui_set_text_color(s_header, ui_live(UI_COLOR_HEADING, stale));
    }
}
