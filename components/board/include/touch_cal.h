#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef void (*touch_cal_done_cb_t)(void);

/** Load calibration from NVS. Returns ESP_OK when a valid one was found. */
esp_err_t touch_cal_load(void);

bool touch_cal_is_valid(void);

/** Remove the stored calibration; the next boot starts the calibration screen. */
esp_err_t touch_cal_erase(void);

/**
 * Show the four-point calibration screen. When it finishes, done() is called
 * (it must load a new screen); the calibration screen is deleted afterwards.
 * LVGL task / port lock only.
 */
void touch_cal_start(touch_cal_done_cb_t done);

/**
 * Touch-driver hook: maps raw XPT2046 ADC values to screen pixels, or feeds
 * the calibration capture while calibration is running. LVGL task only.
 */
void touch_cal_process(uint16_t *x, uint16_t *y, uint8_t point_num, uint8_t max_point_num);
