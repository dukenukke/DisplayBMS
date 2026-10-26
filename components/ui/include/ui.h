#pragma once

#include "esp_err.h"
#include "lvgl.h"

/** Build all screens. Must be called with the LVGL port lock held. */
esp_err_t ui_init(lv_display_t *disp);
