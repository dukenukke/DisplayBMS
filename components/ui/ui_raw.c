#include "ui_internal.h"

#include <stdio.h>
#include "bms_transport.h"
#include "esp_log.h"

#define RAW_CTRL_H          28
#define RAW_DROPDOWN_W      110
#define RAW_PAUSE_W         80
#define RAW_STATS_Y         (RAW_CTRL_H + 4)
#define RAW_HEX_Y           (RAW_STATS_Y + 20)
#define RAW_PER_LINE        8
#define RAW_LINES           17      /* 8 px font + 1 px spacing: 153 px below the stats */
#define RAW_LINE_CHARS      (5 + RAW_PER_LINE * 3 + 1 + RAW_PER_LINE + 1)
#define RAW_ERR_WARN_PCT    5

static const char *TAG = "ui_raw";

static lv_obj_t *s_dropdown, *s_pause, *s_stats1, *s_stats2, *s_hex;
static bms_raw_view_t s_view;      /* static: too large for the LVGL task stack frame */
static char s_hex_text[RAW_LINES * RAW_LINE_CHARS + 1];

static void baud_cb(lv_event_t *e)
{
    uint32_t sel = lv_dropdown_get_selected(lv_event_get_target_obj(e));
    if (sel >= BMS_BAUD_RATE_COUNT) return;
    esp_err_t err = bms_transport_set_baud(bms_baud_rates[sel]);
    if (err != ESP_OK) ESP_LOGE(TAG, "set baud: %s", esp_err_to_name(err));
}

static void pause_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target_obj(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    lv_label_set_text(label, lv_obj_has_state(btn, LV_STATE_CHECKED) ? "Resume" : "Pause");
}

void ui_raw_create(lv_obj_t *tile)
{
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    char options[BMS_BAUD_RATE_COUNT * 8];
    int n = 0;
    for (int i = 0; i < BMS_BAUD_RATE_COUNT; ++i) {
        n += snprintf(options + n, sizeof(options) - n, i ? "\n%lu" : "%lu", (unsigned long)bms_baud_rates[i]);
    }
    s_dropdown = lv_dropdown_create(tile);
    lv_dropdown_set_options(s_dropdown, options);
    lv_obj_set_size(s_dropdown, RAW_DROPDOWN_W, RAW_CTRL_H);
    lv_obj_set_style_pad_ver(s_dropdown, 5, 0);
    lv_obj_set_pos(s_dropdown, UI_PAD, 2);
    lv_obj_add_event_cb(s_dropdown, baud_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *caption = ui_label(tile, NULL, UI_COLOR_HEADING);
    lv_label_set_text(caption, "baud");
    lv_obj_align_to(caption, s_dropdown, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    s_pause = lv_button_create(tile);
    lv_obj_add_flag(s_pause, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_size(s_pause, RAW_PAUSE_W, RAW_CTRL_H);
    lv_obj_align(s_pause, LV_ALIGN_TOP_RIGHT, -UI_PAD, 2);
    lv_obj_add_event_cb(s_pause, pause_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *pause_label = lv_label_create(s_pause);
    lv_label_set_text(pause_label, "Pause");
    lv_obj_center(pause_label);

    s_stats1 = ui_label(tile, UI_FONT_SMALL, UI_COLOR_TEXT);
    lv_obj_set_pos(s_stats1, UI_PAD, RAW_STATS_Y);
    s_stats2 = ui_label(tile, UI_FONT_SMALL, UI_COLOR_TEXT);
    lv_obj_set_pos(s_stats2, UI_PAD, RAW_STATS_Y + 9);

    s_hex = ui_label(tile, UI_FONT_SMALL, UI_COLOR_TEXT);
    lv_obj_set_style_text_line_space(s_hex, 1, 0);
    lv_obj_set_pos(s_hex, UI_PAD, RAW_HEX_Y);

    if (!bms_transport_running()) {
        lv_obj_add_state(s_dropdown, LV_STATE_DISABLED);
        lv_label_set_text(s_hex, "UART link not running\n(data source is not the BMS)");
        return;
    }
    bms_transport_raw_view(&s_view);
    for (int i = 0; i < BMS_BAUD_RATE_COUNT; ++i) {
        if (bms_baud_rates[i] == s_view.baud) lv_dropdown_set_selected(s_dropdown, i);
    }
}

/* Last RAW_LINES lines, aligned to RAW_PER_LINE on the byte count since the baud change. */
static void render_hex(const bms_raw_view_t *v)
{
    int64_t oldest = (int64_t)v->total - v->len;
    int64_t last_line = v->total ? (int64_t)(v->total - 1) / RAW_PER_LINE * RAW_PER_LINE : 0;
    int64_t first_line = last_line - (RAW_LINES - 1) * RAW_PER_LINE;
    if (first_line < oldest / RAW_PER_LINE * RAW_PER_LINE) first_line = oldest / RAW_PER_LINE * RAW_PER_LINE;

    int n = 0;
    for (int64_t line = first_line; line <= last_line && n < (int)sizeof(s_hex_text) - RAW_LINE_CHARS; line += RAW_PER_LINE) {
        n += snprintf(s_hex_text + n, sizeof(s_hex_text) - n, "%04X ", (unsigned)(line & 0xFFFF));
        char ascii[RAW_PER_LINE + 1];
        for (int k = 0; k < RAW_PER_LINE; ++k) {
            int64_t abs = line + k;
            if (abs < oldest || abs >= (int64_t)v->total) {
                n += snprintf(s_hex_text + n, sizeof(s_hex_text) - n, "   ");
                ascii[k] = ' ';
                continue;
            }
            int i = (int)(abs - oldest);
            uint8_t b = v->data[i];
            if (v->masked[i]) {
                n += snprintf(s_hex_text + n, sizeof(s_hex_text) - n, "** ");
                ascii[k] = '*';
            } else {
                n += snprintf(s_hex_text + n, sizeof(s_hex_text) - n, "%02X ", b);
                ascii[k] = b >= 0x20 && b < 0x7F ? (char)b : '.';
            }
        }
        ascii[RAW_PER_LINE] = '\0';
        n += snprintf(s_hex_text + n, sizeof(s_hex_text) - n, "%s\n", ascii);
    }
    if (n > 0) s_hex_text[n - 1] = '\0';     /* drop the trailing newline */
    else s_hex_text[0] = '\0';
    ui_label_set_text(s_hex, v->total ? s_hex_text : "No bytes received at this baud rate");
}

void ui_raw_update(const bms_data_t *d, bool stale)
{
    (void)d;
    (void)stale;
    if (!bms_transport_running()) return;
    bms_transport_raw_view(&s_view);

    ui_label_printf(s_stats1, "RX %lu B   line errors %lu", (unsigned long)s_view.total,
                    (unsigned long)s_view.line_errors);
    bool noisy = s_view.total && s_view.line_errors * 100 > s_view.total * RAW_ERR_WARN_PCT;
    ui_set_text_color(s_stats1, noisy ? UI_COLOR_FAULT : UI_COLOR_TEXT);

    ui_label_printf(s_stats2, "JK headers %lu   sum OK %lu  bad %lu", (unsigned long)s_view.headers,
                    (unsigned long)s_view.frames_ok, (unsigned long)s_view.frames_bad);
    ui_set_text_color(s_stats2, s_view.frames_ok ? UI_COLOR_OK : (s_view.headers ? UI_COLOR_WARN : UI_COLOR_TEXT));

    if (!lv_obj_has_state(s_pause, LV_STATE_CHECKED)) render_hex(&s_view);
}
