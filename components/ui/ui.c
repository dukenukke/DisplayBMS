#include "ui.h"

#include <stdio.h>
#include "backlight.h"
#include "board.h"
#include "bms_model.h"
#include "esp_log.h"
#include "esp_system.h"
#include "touch_cal.h"

/*
 * Stage 2 hardware test screen: colour order, fonts, touch mapping, brightness
 * and the LDR reading. Replaced by the BMS screens in stage 3.
 */

#define UI_PAD              6
#define UI_ROW_GAP          4
#define UI_SWATCH_W         70
#define UI_SWATCH_H         22
#define UI_BUTTON_H         32
#define UI_STATUS_PERIOD_MS 250

static const char *TAG = "ui";

static lv_obj_t *s_status_label;
static lv_obj_t *s_touch_label;

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    lv_label_set_text_fmt(s_status_label, "LDR raw %d  ambient %d%%  backlight %d%%",
                          backlight_get_ambient_raw(), backlight_get_ambient_pct(),
                          backlight_get_level_pct());
}

static void screen_pressing_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    (void)e;
    lv_label_set_text_fmt(s_touch_label, "Touch x=%d y=%d", (int)p.x, (int)p.y);
}

static void slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    bool persist = lv_event_get_code(e) == LV_EVENT_RELEASED;
    backlight_set_user_pct(lv_slider_get_value(slider), persist);
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

static void add_swatch(lv_obj_t *parent, lv_color_t color, const char *name)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, UI_SWATCH_W, UI_SWATCH_H);
    lv_obj_set_style_bg_color(box, color, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *label = lv_label_create(box);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);
}

esp_err_t ui_init(lv_display_t *disp)
{
    if (!disp) return ESP_ERR_INVALID_ARG;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_pad_all(scr, UI_PAD, 0);
    lv_obj_set_style_pad_row(scr, UI_ROW_GAP, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screen_pressing_cb, LV_EVENT_PRESSING, NULL);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "DisplayBMS hardware test");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    lv_obj_t *swatches = lv_obj_create(scr);
    lv_obj_remove_style_all(swatches);
    lv_obj_set_size(swatches, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(swatches, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(swatches, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    add_swatch(swatches, lv_color_hex(0xFF0000), "RED");
    add_swatch(swatches, lv_color_hex(0x00FF00), "GREEN");
    add_swatch(swatches, lv_color_hex(0x0000FF), "BLUE");
    add_swatch(swatches, lv_color_white(), "WHITE");

    lv_obj_t *big = lv_label_create(scr);
    lv_label_set_text(big, "52.34 V  -12.5 A");
    lv_obj_set_style_text_font(big, &lv_font_montserrat_28, 0);

    lv_obj_t *bl_label = lv_label_create(scr);
    lv_label_set_text(bl_label, "Brightness (saved on release)");

    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_width(slider, LV_PCT(94));
    lv_slider_set_range(slider, CONFIG_DBMS_BL_MIN_PCT, 100);
    lv_slider_set_value(slider, backlight_get_user_pct(), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_RELEASED, NULL);

    s_status_label = lv_label_create(scr);
    s_touch_label = lv_label_create(scr);
    lv_label_set_text(s_touch_label, "Touch: press anywhere");

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_height(btn, UI_BUTTON_H);
    lv_obj_add_event_cb(btn, recalibrate_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Recalibrate touch");
    lv_obj_center(btn_label);

    status_timer_cb(NULL);
    lv_timer_create(status_timer_cb, UI_STATUS_PERIOD_MS, NULL);
    lv_screen_load(scr);
    return ESP_OK;
}
