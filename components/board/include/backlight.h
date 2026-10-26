#pragma once

#include <stdbool.h>
#include "esp_err.h"

/** LEDC PWM on the backlight pin; starts dark. Loads the user brightness from NVS. */
esp_err_t backlight_init(void);

/** Start the LVGL timer that applies ambient light, auto-dim and fading. LVGL lock held. */
void backlight_start_service(void);

/* The functions below run in the LVGL task only. */

/** Brightness chosen by the user, %. persist = also store in NVS. */
void backlight_set_user_pct(int pct, bool persist);
int backlight_get_user_pct(void);

/** Current PWM level, % (after ambient scaling, dimming and fade). */
int backlight_get_level_pct(void);

/** Last LDR reading: raw ADC value (-1 if unavailable) and mapped 0..100 %. */
int backlight_get_ambient_raw(void);
int backlight_get_ambient_pct(void);

/**
 * Report touch activity. Returns true when the screen was dimmed: the caller
 * should swallow this press so that waking the screen does not click anything.
 */
bool backlight_touch_activity(void);
