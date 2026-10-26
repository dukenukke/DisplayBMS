#include "board.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_touch_xpt2046.h"

static const char *TAG = "board";

esp_err_t board_init(lv_display_t **out_disp)
{
    /* Hardware bring-up is added in stage 2; the skeleton only links the drivers. */
    *out_disp = NULL;
    ESP_LOGW(TAG, "display not initialised yet");
    return ESP_ERR_NOT_SUPPORTED;
}
