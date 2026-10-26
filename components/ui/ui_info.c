#include "ui_internal.h"

#include <stdio.h>
#include "backlight.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "touch_cal.h"

#define INFO_SLIDER_W_PCT   92

static const char *TAG = "ui_info";

enum {
    L_MODEL, L_VERSION, L_UPTIME,
    L_CAPACITY, L_OVP, L_UVP, L_BALANCE, L_CURRENT, L_TEMP_HIGH, L_TEMP_LOW, L_SWITCHES,
    L_SOURCE, L_FRAMES, L_ERRORS, L_LAST_FRAME,
    L_LIGHT,
    L_COUNT
};
static lv_obj_t *s_lines[L_COUNT];

static void heading(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = ui_label(parent, NULL, UI_COLOR_HEADING);
    lv_label_set_text(label, text);
}

static void lines(lv_obj_t *parent, int first, int last)
{
    for (int i = first; i <= last; ++i) {
        s_lines[i] = ui_label(parent, NULL, UI_COLOR_TEXT);
        lv_label_set_text(s_lines[i], "--");
    }
}

static void slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    backlight_set_user_pct(lv_slider_get_value(slider), lv_event_get_code(e) == LV_EVENT_RELEASED);
}

static void recalibrate_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = touch_cal_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase calibration: %s", esp_err_to_name(err));
        return;
    }
    esp_restart();
}

void ui_info_create(lv_obj_t *tile)
{
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tile, UI_PAD, 0);
    lv_obj_set_style_pad_row(tile, 2, 0);
    lv_obj_set_scroll_dir(tile, LV_DIR_VER);

    heading(tile, "Device");
    lines(tile, L_MODEL, L_UPTIME);
    heading(tile, "Settings (read-only)");
    lines(tile, L_CAPACITY, L_SWITCHES);
    heading(tile, "Link");
    lines(tile, L_SOURCE, L_LAST_FRAME);
    heading(tile, "Display");

    lv_obj_t *slider = lv_slider_create(tile);
    lv_obj_set_width(slider, LV_PCT(INFO_SLIDER_W_PCT));
    lv_obj_set_style_margin_ver(slider, 8, 0);
    lv_slider_set_range(slider, CONFIG_DBMS_BL_MIN_PCT, 100);
    lv_slider_set_value(slider, backlight_get_user_pct(), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_RELEASED, NULL);
    lines(tile, L_LIGHT, L_LIGHT);

    lv_obj_t *btn = lv_button_create(tile);
    lv_obj_add_event_cb(btn, recalibrate_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Recalibrate touch");

#if CONFIG_DBMS_SOURCE_MOCK
    ui_label_set_text(s_lines[L_SOURCE], "Source: MOCK (simulated)");
    ui_set_text_color(s_lines[L_SOURCE], UI_COLOR_INFO);
#else
    ui_label_printf(s_lines[L_SOURCE], "Source: UART%d RX GPIO%d, listen only",
                    CONFIG_DBMS_BMS_UART_NUM, CONFIG_DBMS_BMS_RX_GPIO);
#endif
}

/* Millivolts as V with 3 decimals. */
static void fmt_v(char *buf, size_t len, uint32_t mv)
{
    snprintf(buf, len, "%lu.%03lu", (unsigned long)(mv / 1000), (unsigned long)(mv % 1000));
}

static void update_settings(const bms_settings_t *s)
{
    if (!s->valid) return;
    char a[12], b[12];
    ui_label_printf(s_lines[L_CAPACITY], "Cells %lu  Capacity %lu Ah", (unsigned long)s->cell_count,
                    (unsigned long)(s->design_cap_mah / 1000));
    fmt_v(a, sizeof(a), s->cell_ovp_mv);
    fmt_v(b, sizeof(b), s->cell_ovpr_mv);
    ui_label_printf(s_lines[L_OVP], "Cell OVP %sV  rec %sV", a, b);
    fmt_v(a, sizeof(a), s->cell_uvp_mv);
    fmt_v(b, sizeof(b), s->cell_uvpr_mv);
    ui_label_printf(s_lines[L_UVP], "Cell UVP %sV  rec %sV", a, b);
    fmt_v(a, sizeof(a), s->balance_start_mv);
    ui_label_printf(s_lines[L_BALANCE], "Balance from %sV  dV %lumV  %lu.%luA", a,
                    (unsigned long)s->balance_trig_mv, (unsigned long)(s->balance_max_ma / 1000),
                    (unsigned long)(s->balance_max_ma % 1000 / 100));
    ui_label_printf(s_lines[L_CURRENT], "OCP charge %luA  discharge %luA",
                    (unsigned long)(s->charge_oc_ma / 1000), (unsigned long)(s->discharge_oc_ma / 1000));
    ui_label_printf(s_lines[L_TEMP_HIGH], "OTP chg %ld\xC2\xB0  dsg %ld\xC2\xB0  MOS %ld\xC2\xB0",
                    (long)(s->charge_ot_dc / 10), (long)(s->discharge_ot_dc / 10), (long)(s->mos_ot_dc / 10));
    ui_label_printf(s_lines[L_TEMP_LOW], "UTP chg %ld\xC2\xB0", (long)(s->charge_ut_dc / 10));
    ui_label_printf(s_lines[L_SWITCHES], "Switch CHG %s  DSG %s  BAL %s", s->charge_enabled ? "on" : "off",
                    s->discharge_enabled ? "on" : "off", s->balance_enabled ? "on" : "off");
}

void ui_info_update(const bms_data_t *d, bool stale)
{
    (void)stale;
    const bms_info_t *info = &d->info;
    if (info->valid) {
        ui_label_printf(s_lines[L_MODEL], "Model %s", info->model);
        ui_label_printf(s_lines[L_VERSION], "HW %s  SW %s", info->hw_version, info->sw_version);
        uint32_t days = info->uptime_s / 86400, hours = info->uptime_s % 86400 / 3600;
        ui_label_printf(s_lines[L_UPTIME], "Uptime %lud %02luh  Power-on %lu", (unsigned long)days,
                        (unsigned long)hours, (unsigned long)info->power_on_count);
    }
    update_settings(&d->settings);

    const bms_link_stats_t *link = &d->link;
    ui_label_printf(s_lines[L_FRAMES], "Frames OK %lu  RX %lu B", (unsigned long)link->frames_ok,
                    (unsigned long)link->bytes_rx);
    ui_label_printf(s_lines[L_ERRORS], "Sum err %lu  Unknown %lu  Resync %lu",
                    (unsigned long)link->checksum_errors, (unsigned long)link->unknown_frames,
                    (unsigned long)link->resyncs);
    ui_set_text_color(s_lines[L_ERRORS], link->checksum_errors ? UI_COLOR_WARN : UI_COLOR_TEXT);
    if (link->frames_ok) {
        int64_t age_s = (esp_timer_get_time() - link->last_frame_us) / 1000000;
        ui_label_printf(s_lines[L_LAST_FRAME], "Last frame %llds ago", (long long)age_s);
    } else {
        ui_label_set_text(s_lines[L_LAST_FRAME], "Last frame: none yet");
    }

    ui_label_printf(s_lines[L_LIGHT], "LDR raw %d  ambient %d%%  level %d%%", backlight_get_ambient_raw(),
                    backlight_get_ambient_pct(), backlight_get_level_pct());
}
