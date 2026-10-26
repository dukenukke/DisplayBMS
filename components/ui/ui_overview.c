#include "ui_internal.h"

#include <stdio.h>
#include <stdlib.h>

#define OV_ARC_SIZE         116
#define OV_ARC_WIDTH        10
#define OV_ARC_START_DEG    135
#define OV_ARC_END_DEG      45
#define OV_RIGHT_X          132
#define OV_ROW_Y0           124
#define OV_ROW_STEP         20
#define OV_ROW_GAP          5
#define OV_SOC_LOW_PCT      20
#define OV_SOC_MID_PCT      40

static lv_obj_t *s_arc, *s_soc, *s_voltage, *s_current, *s_direction, *s_power;
static lv_obj_t *s_chg, *s_dsg, *s_bal;
static lv_obj_t *s_temp_mos, *s_temp_bat[BMS_BAT_TEMP_SENSORS];
static lv_obj_t *s_delta, *s_cycles, *s_soh, *s_capacity;

static lv_obj_t *add_pair(lv_obj_t *row, const char *name)
{
    lv_obj_t *label = ui_label(row, NULL, UI_COLOR_HEADING);
    lv_label_set_text(label, name);
    return ui_label(row, NULL, UI_COLOR_TEXT);
}

static lv_obj_t *add_row_at(lv_obj_t *tile, int index)
{
    lv_obj_t *row = ui_row(tile);
    lv_obj_set_pos(row, UI_PAD, OV_ROW_Y0 + index * OV_ROW_STEP);
    /* MOS + T1..T5 is 31 characters (248 px); this gap keeps the row within 312 px. */
    lv_obj_set_style_pad_column(row, OV_ROW_GAP, 0);
    return row;
}

void ui_overview_create(lv_obj_t *tile)
{
    s_arc = lv_arc_create(tile);
    lv_obj_set_size(s_arc, OV_ARC_SIZE, OV_ARC_SIZE);
    lv_obj_set_pos(s_arc, UI_PAD, 2);
    lv_arc_set_bg_angles(s_arc, OV_ARC_START_DEG, OV_ARC_END_DEG);
    lv_arc_set_range(s_arc, 0, BMS_PCT_MAX);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, OV_ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, OV_ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, UI_COLOR_PANEL, LV_PART_MAIN);

    s_soc = ui_label(s_arc, UI_FONT_LARGE, UI_COLOR_TEXT);
    lv_obj_align(s_soc, LV_ALIGN_CENTER, 0, -4);
    lv_obj_t *soc_caption = ui_label(s_arc, UI_FONT_SMALL, UI_COLOR_HEADING);
    lv_label_set_text(soc_caption, "SOC");
    lv_obj_align(soc_caption, LV_ALIGN_CENTER, 0, 22);

    s_voltage = ui_label(tile, UI_FONT_LARGE, UI_COLOR_TEXT);
    lv_obj_set_pos(s_voltage, OV_RIGHT_X, 4);
    s_current = ui_label(tile, UI_FONT_LARGE, UI_COLOR_TEXT);
    lv_obj_set_pos(s_current, OV_RIGHT_X, 40);
    s_direction = ui_label(tile, NULL, UI_COLOR_TEXT);
    lv_obj_set_pos(s_direction, OV_RIGHT_X, 78);
    s_power = ui_label(tile, NULL, UI_COLOR_TEXT);
    lv_obj_set_pos(s_power, OV_RIGHT_X, 96);

    lv_obj_t *row = add_row_at(tile, 0);
    s_chg = add_pair(row, "CHG");
    s_dsg = add_pair(row, "DSG");
    s_bal = add_pair(row, "BAL");

    row = add_row_at(tile, 1);
    s_temp_mos = add_pair(row, "MOS");
    for (int i = 0; i < BMS_BAT_TEMP_SENSORS; ++i) {
        char name[4];
        snprintf(name, sizeof(name), "T%d", i + 1);
        s_temp_bat[i] = add_pair(row, name);
    }

    row = add_row_at(tile, 2);
    s_delta = add_pair(row, "Delta");
    s_cycles = add_pair(row, "Cyc");
    s_soh = add_pair(row, "SOH");

    row = add_row_at(tile, 3);
    s_capacity = add_pair(row, "Cap");
}

static void set_mos(lv_obj_t *label, bool on, bool stale)
{
    ui_label_set_text(label, on ? "ON" : "OFF");
    ui_set_text_color(label, ui_live(on ? UI_COLOR_OK : UI_COLOR_FAULT, stale));
}

static void set_temp(lv_obj_t *value, int32_t dc, bool present, bool stale)
{
    char buf[12];
    /* Hide absent sensors together with their caption (previous sibling). */
    lv_obj_t *caption = lv_obj_get_sibling(value, -1);
    if (!present) {
        lv_obj_add_flag(value, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(caption, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(value, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(caption, LV_OBJ_FLAG_HIDDEN);
    ui_fmt_temp(buf, sizeof(buf), dc, true);
    ui_label_set_text(value, buf);
    ui_set_text_color(value, ui_temp_color(dc, stale));
}

static lv_color_t soc_color(uint8_t soc)
{
    if (soc < OV_SOC_LOW_PCT) return UI_COLOR_FAULT;
    if (soc < OV_SOC_MID_PCT) return UI_COLOR_WARN;
    return UI_COLOR_OK;
}

void ui_overview_update(const bms_data_t *d, bool stale)
{
    const bms_live_t *l = &d->live;
    if (!l->valid) {
        ui_label_set_text(s_soc, "--");
        ui_label_set_text(s_voltage, "--.--V");
        ui_label_set_text(s_current, "--.-A");
        ui_label_set_text(s_direction, "Waiting for data");
        return;
    }

    if (l->soc_pct <= BMS_PCT_MAX) {
        lv_arc_set_value(s_arc, l->soc_pct);
        ui_label_printf(s_soc, "%u%%", l->soc_pct);
    } else {
        ui_label_set_text(s_soc, "--");
    }
    lv_obj_set_style_arc_color(s_arc, ui_live(soc_color(l->soc_pct), stale), LV_PART_INDICATOR);
    ui_set_text_color(s_soc, ui_live(UI_COLOR_TEXT, stale));

    if (l->pack_mv <= BMS_PACK_MV_MAX) {
        ui_label_printf(s_voltage, "%lu.%02luV", (unsigned long)(l->pack_mv / 1000),
                        (unsigned long)(l->pack_mv % 1000 / 10));
    } else {
        ui_label_set_text(s_voltage, "--.--V");
    }
    ui_set_text_color(s_voltage, ui_live(UI_COLOR_TEXT, stale));

    int32_t ma = l->current_ma;
    long abs_ma = labs(ma);
    lv_color_t flow = UI_COLOR_TEXT;
    const char *direction = "IDLE";
    if (abs_ma > BMS_CURRENT_MA_ABS_MAX) {
        ui_label_set_text(s_current, "--.-A");
        direction = "";
    } else {
        ui_label_printf(s_current, "%s%ld.%ldA", ma < 0 ? "-" : "", abs_ma / 1000, abs_ma % 1000 / 100);
        if (abs_ma >= UI_IDLE_CURRENT_MA) {
            flow = ma > 0 ? UI_COLOR_CHARGE : UI_COLOR_DISCHARGE;
            direction = ma > 0 ? "CHARGE" : "DISCHARGE";
        }
    }
    ui_set_text_color(s_current, ui_live(flow, stale));
    ui_label_set_text(s_direction, direction);
    ui_set_text_color(s_direction, ui_live(flow, stale));

    uint32_t w = l->power_mw / 1000;
    if (w >= 10000) {
        ui_label_printf(s_power, "%lu.%lu kW", (unsigned long)(w / 1000), (unsigned long)(w % 1000 / 100));
    } else {
        ui_label_printf(s_power, "%lu W", (unsigned long)w);
    }
    ui_set_text_color(s_power, ui_live(UI_COLOR_TEXT, stale));

    set_mos(s_chg, l->charge_mos, stale);
    set_mos(s_dsg, l->discharge_mos, stale);
    static const char *const balance_names[] = {"off", "chg", "dsg"};
    ui_label_set_text(s_bal, l->balance_state <= BMS_BALANCE_DISCHARGE ? balance_names[l->balance_state] : "?");
    ui_set_text_color(s_bal, ui_live(l->balance_state ? UI_COLOR_INFO : UI_COLOR_TEXT, stale));

    set_temp(s_temp_mos, l->temp_mos_dc, l->temp_present_mask & BMS_TEMP_PRESENT_MOS, stale);
    for (int i = 0; i < BMS_BAT_TEMP_SENSORS; ++i) {
        set_temp(s_temp_bat[i], l->temp_bat_dc[i], l->temp_present_mask & BMS_TEMP_PRESENT_BAT(i), stale);
    }

    ui_label_printf(s_delta, "%umV", l->cell_delta_mv);
    ui_set_text_color(s_delta, ui_delta_color(l->cell_delta_mv, stale));
    ui_label_printf(s_cycles, "%lu", (unsigned long)l->cycles);
    ui_set_text_color(s_cycles, ui_live(UI_COLOR_TEXT, stale));
    if (l->soh_pct <= BMS_PCT_MAX) {
        ui_label_printf(s_soh, "%u%%", l->soh_pct);
    } else {
        ui_label_set_text(s_soh, "--");
    }
    ui_set_text_color(s_soh, ui_live(UI_COLOR_TEXT, stale));

    long remain = l->remain_mah > 0 ? l->remain_mah : 0;
    ui_label_printf(s_capacity, "%ld.%ld / %lu.%lu Ah", remain / 1000, remain % 1000 / 100,
                    (unsigned long)(l->full_mah / 1000), (unsigned long)(l->full_mah % 1000 / 100));
    ui_set_text_color(s_capacity, ui_live(UI_COLOR_TEXT, stale));
}
