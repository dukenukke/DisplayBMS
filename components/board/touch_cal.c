/*
 * Interactive four-point touch calibration stored in NVS.
 * Adapted from the fungen project (same author, GPL-3.0).
 */
#include "touch_cal.h"
#include "touch_cal_math.h"
#include "board.h"

#include <stddef.h>
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "nvs.h"
#include "lvgl.h"
#include "sdkconfig.h"

#if CONFIG_XPT2046_CONVERT_ADC_TO_COORDS
#error "Touch calibration requires raw XPT2046 ADC coordinates"
#endif

#define CAL_NVS_NAMESPACE       "dbms"
#define CAL_NVS_KEY             "touch_cal"
#define CAL_VERSION             1
#define CAL_MIN_SAMPLES         3
#define CAL_MAX_SAMPLES         8
#define CAL_SAMPLE_TOLERANCE    120     /* raw ADC units */
#define CAL_MARGIN_PX           40
#define CAL_CROSS_SIZE_PX       31
#define CAL_STEP_DELAY_MS       10
#define CAL_POINTS              4

static const char *TAG = "touch_cal";

typedef struct {
    uint32_t version;
    touch_cal_mapping_t mapping;
    uint32_t crc;
} touch_cal_blob_t;

static touch_cal_blob_t s_cal;
static bool s_valid;

/* Capture state; all of it is touched only from the LVGL task. */
static int s_step = -1;
static bool s_step_pending;
static uint32_t s_sum_x, s_sum_y;
static unsigned s_samples;
static touch_cal_point_t s_captured[CAL_POINTS];
static const touch_cal_point_t s_targets[CAL_POINTS] = {
    {CAL_MARGIN_PX, CAL_MARGIN_PX},
    {BOARD_LCD_H_RES - 1 - CAL_MARGIN_PX, CAL_MARGIN_PX},
    {CAL_MARGIN_PX, BOARD_LCD_V_RES - 1 - CAL_MARGIN_PX},
    {BOARD_LCD_H_RES - 1 - CAL_MARGIN_PX, BOARD_LCD_V_RES - 1 - CAL_MARGIN_PX},
};
static const char *s_prompts[CAL_POINTS] = {
    "Hold the top-left cross, then release",
    "Hold the top-right cross, then release",
    "Hold the bottom-left cross, then release",
    "Hold the bottom-right cross, then release",
};
static lv_obj_t *s_screen, *s_cross, *s_label;
static touch_cal_done_cb_t s_done;

static uint32_t blob_crc(const touch_cal_blob_t *b)
{
    return esp_rom_crc32_le(0, (const uint8_t *)b, offsetof(touch_cal_blob_t, crc));
}

esp_err_t touch_cal_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAL_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    touch_cal_blob_t blob = {0};
    size_t size = sizeof(blob);
    err = nvs_get_blob(h, CAL_NVS_KEY, &blob, &size);
    nvs_close(h);
    if (err != ESP_OK) return err;
    if (size != sizeof(blob) || blob.version != CAL_VERSION || blob.crc != blob_crc(&blob) ||
        !touch_cal_mapping_valid(&blob.mapping)) {
        ESP_LOGW(TAG, "stored calibration rejected");
        return ESP_ERR_INVALID_CRC;
    }
    s_cal = blob;
    s_valid = true;
    return ESP_OK;
}

bool touch_cal_is_valid(void)
{
    return s_valid;
}

esp_err_t touch_cal_erase(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAL_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(h, CAL_NVS_KEY);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    return err;
}

static void save_calibration(void)
{
    s_cal.version = CAL_VERSION;
    s_cal.crc = blob_crc(&s_cal);
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAL_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, CAL_NVS_KEY, &s_cal, sizeof(s_cal));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS save failed (%s); calibration applies until reboot", esp_err_to_name(err));
    }
}

static void reset_samples(void)
{
    s_sum_x = s_sum_y = 0;
    s_samples = 0;
}

void touch_cal_process(uint16_t *x, uint16_t *y, uint8_t point_num, uint8_t max_point_num)
{
    for (uint8_t i = 0; i < point_num && i < max_point_num; ++i) {
        if (s_valid && s_step < 0) {
            touch_cal_point_t p = touch_cal_map(&s_cal.mapping, (touch_cal_point_t){x[i], y[i]});
            x[i] = (uint16_t)lroundf(fminf(BOARD_LCD_H_RES - 1, fmaxf(0, p.x)));
            y[i] = (uint16_t)lroundf(fminf(BOARD_LCD_V_RES - 1, fmaxf(0, p.y)));
            continue;
        }
        if (i == 0 && s_step >= 0 && s_step < CAL_POINTS && !s_step_pending &&
            s_samples < CAL_MAX_SAMPLES) {
            /* Restart an unstable hold; stop after CAL_MAX_SAMPLES to ignore lift-off drift. */
            if (s_samples && (fabsf(x[i] - (float)s_sum_x / s_samples) > CAL_SAMPLE_TOLERANCE ||
                              fabsf(y[i] - (float)s_sum_y / s_samples) > CAL_SAMPLE_TOLERANCE)) {
                reset_samples();
            }
            s_sum_x += x[i];
            s_sum_y += y[i];
            ++s_samples;
        }
        /* Raw values must never reach LVGL hit-testing: route presses to the screen centre. */
        x[i] = BOARD_LCD_H_RES / 2;
        y[i] = BOARD_LCD_V_RES / 2;
    }
}

static void show_target(void)
{
    lv_obj_set_pos(s_cross, (int32_t)s_targets[s_step].x - CAL_CROSS_SIZE_PX / 2,
                   (int32_t)s_targets[s_step].y - CAL_CROSS_SIZE_PX / 2);
    lv_label_set_text(s_label, s_prompts[s_step]);
}

static void step_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    reset_samples();
    s_step_pending = false;
    if (++s_step < CAL_POINTS) {
        show_target();
        return;
    }
    if (!touch_cal_fit(s_captured, s_targets, &s_cal.mapping)) {
        ESP_LOGW(TAG, "inconsistent points, restarting");
        s_step = 0;
        show_target();
        lv_label_set_text(s_label, "Calibration failed.\nHold the top-left cross, then release");
        return;
    }
    ESP_LOGI(TAG, "X = %.5f*rx + %.5f*ry + %.1f; Y = %.5f*rx + %.5f*ry + %.1f",
             s_cal.mapping.xx, s_cal.mapping.xy, s_cal.mapping.x_offset,
             s_cal.mapping.yx, s_cal.mapping.yy, s_cal.mapping.y_offset);
    save_calibration();
    s_valid = true;
    s_step = -1;
    /* done() loads the next screen before the active one is deleted. */
    if (s_done) s_done();
    lv_obj_delete(s_screen);
    s_screen = s_cross = s_label = NULL;
}

static void release_cb(lv_event_t *event)
{
    (void)event;
    if (s_step_pending || s_step < 0 || s_step >= CAL_POINTS) return;
    if (s_samples < CAL_MIN_SAMPLES) {
        reset_samples();
        lv_label_set_text(s_label, "Hold the cross a little longer,\nthen release");
        return;
    }
    s_captured[s_step] = (touch_cal_point_t){(float)s_sum_x / s_samples, (float)s_sum_y / s_samples};
    ESP_LOGI(TAG, "point %d: raw %.0f, %.0f (%u samples)", s_step + 1,
             s_captured[s_step].x, s_captured[s_step].y, s_samples);
    s_step_pending = true;
    /* Defer the screen change until LVGL has finished dispatching this release. */
    lv_timer_t *t = lv_timer_create(step_timer_cb, CAL_STEP_DELAY_MS, NULL);
    if (t) {
        lv_timer_set_repeat_count(t, 1);
    } else {
        s_step_pending = false;
        reset_samples();
    }
}

static lv_obj_t *make_line(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, w, h);
    lv_obj_center(line);
    lv_obj_set_style_bg_color(line, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return line;
}

void touch_cal_start(touch_cal_done_cb_t done)
{
    ESP_LOGI(TAG, "starting four-point calibration");
    s_done = done;
    s_step = 0;
    s_step_pending = false;
    reset_samples();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_screen, release_cb, LV_EVENT_RELEASED, NULL);

    s_label = lv_label_create(s_screen);
    lv_obj_set_width(s_label, BOARD_LCD_H_RES - 20);
    lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_label, lv_color_white(), 0);
    lv_obj_align(s_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(s_label, LV_OBJ_FLAG_CLICKABLE);

    s_cross = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_cross);
    lv_obj_set_size(s_cross, CAL_CROSS_SIZE_PX, CAL_CROSS_SIZE_PX);
    lv_obj_remove_flag(s_cross, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    make_line(s_cross, CAL_CROSS_SIZE_PX, 1);
    make_line(s_cross, 1, CAL_CROSS_SIZE_PX);

    show_target();
    lv_screen_load(s_screen);
}
