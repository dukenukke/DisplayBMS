#include "ui.h"

#include <stdio.h>
#include "backlight.h"
#include "board.h"
#include "bms_model.h"
#include "esp_log.h"
#include "esp_system.h"
#include "touch_cal.h"
#include "ui_fonts.h"

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
#define UI_GREY_STEPS       8
#define UI_GREY_W           36
#define UI_GREY_H           18

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

static int s_gamma_curve;   /* panel init selects curve 1 (index 0) */

static void gamma_cb(lv_event_t *e)
{
    lv_obj_t *label = lv_event_get_user_data(e);
    int next = (s_gamma_curve + 1) % BOARD_LCD_GAMMA_CURVES;
    esp_err_t err = board_lcd_set_gamma_curve(next);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gamma: %s", esp_err_to_name(err));
        return;
    }
    s_gamma_curve = next;
    lv_label_set_text_fmt(label, "Gamma %d", s_gamma_curve + 1);
    /* The panel changes in place; force a full redraw anyway so any tearing clears. */
    lv_obj_invalidate(lv_screen_active());
}

static lv_obj_t *add_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

/* Eight grey steps from black to white: shows how the panel renders intermediate levels. */
static void add_grey_ramp(lv_obj_t *parent)
{
    lv_obj_t *row = add_row(parent);
    for (int i = 0; i < UI_GREY_STEPS; ++i) {
        uint8_t v = (uint8_t)(i * 255 / (UI_GREY_STEPS - 1));
        lv_obj_t *box = lv_obj_create(row);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, UI_GREY_W, UI_GREY_H);
        lv_obj_set_style_bg_color(box, lv_color_make(v, v, v), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    }
}

static lv_obj_t *add_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, bool label_as_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, UI_BUTTON_H);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, label_as_data ? label : NULL);
    return btn;
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

    /* Dark default theme with the 8x16 pixel font inherited by every widget. */
    lv_theme_t *theme = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_TEAL),
                                              lv_palette_main(LV_PALETTE_AMBER), true, UI_FONT_TEXT);
    lv_display_set_theme(disp, theme);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_pad_all(scr, UI_PAD, 0);
    lv_obj_set_style_pad_row(scr, UI_ROW_GAP, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    /* More content than one screen: scroll vertically. */
    lv_obj_set_scroll_dir(scr, LV_DIR_VER);
    lv_obj_add_event_cb(scr, screen_pressing_cb, LV_EVENT_PRESSING, NULL);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "DisplayBMS hardware test");
    lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_TEAL), 0);

    lv_obj_t *swatches = add_row(scr);
    add_swatch(swatches, lv_color_hex(0xFF0000), "RED");
    add_swatch(swatches, lv_color_hex(0x00FF00), "GREEN");
    add_swatch(swatches, lv_color_hex(0x0000FF), "BLUE");
    add_swatch(swatches, lv_color_white(), "WHITE");

    add_grey_ramp(scr);

    lv_obj_t *small = lv_label_create(scr);
    lv_label_set_text(small, "Unscii 8: cell 16  3.325 V  delta 12 mV");
    lv_obj_set_style_text_font(small, UI_FONT_SMALL, 0);

    /* Two candidates for body text: compare and choose. */
    lv_obj_t *text_tall = lv_label_create(scr);
    lv_label_set_text(text_tall, "A 8x16: T1 24.5\xC2\xB0" "C  Cell 3.325V  OK");
    lv_obj_set_style_text_font(text_tall, &ui_font_unscii_16, 0);

    lv_obj_t *text_wide = lv_label_create(scr);
    lv_label_set_text(text_wide, "B 16x16: Cell 3.325V");
    lv_obj_set_style_text_font(text_wide, &lv_font_unscii_16, 0);

    lv_obj_t *big = lv_label_create(scr);
    lv_label_set_text(big, "52.34V -12.5A");
    lv_obj_set_style_text_font(big, UI_FONT_LARGE, 0);

    lv_obj_t *big_pct = lv_label_create(scr);
    lv_label_set_text(big_pct, "SOC 87% 24.5\xC2\xB0" "C");
    lv_obj_set_style_text_font(big_pct, UI_FONT_LARGE, 0);

    lv_obj_t *buttons = add_row(scr);
    add_button(buttons, "Gamma 1", gamma_cb, true);
    add_button(buttons, "Recalibrate touch", recalibrate_cb, false);

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

    status_timer_cb(NULL);
    lv_timer_create(status_timer_cb, UI_STATUS_PERIOD_MS, NULL);
    lv_screen_load(scr);
    return ESP_OK;
}
