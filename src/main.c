#include "esp_log.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_lvgl_port.h"
#include "sdkconfig.h"

#include "board.h"
#include "bms_mock.h"
#include "bms_model.h"
#include "bms_transport.h"
#include "touch_cal.h"
#include "ui.h"

static const char *TAG = "main";

static void start_ui(void)
{
    esp_err_t err = ui_init(lv_display_get_default());
    if (err != ESP_OK) ESP_LOGE(TAG, "ui_init: %s", esp_err_to_name(err));
}

void app_main(void)
{
    ESP_LOGI(TAG, "DisplayBMS, ESP-IDF %s, LVGL %d.%d.%d", esp_get_idf_version(),
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    ESP_ERROR_CHECK(bms_model_init());

    /* Without a display the device is useless: treat bring-up failure as fatal. */
    lv_display_t *disp = NULL;
    ESP_ERROR_CHECK(board_init(&disp));

    if (lvgl_port_lock(0)) {
        if (touch_cal_is_valid()) {
            start_ui();
        } else {
            touch_cal_start(start_ui);
        }
        lvgl_port_unlock();
    }

#if CONFIG_DBMS_SOURCE_MOCK
    esp_err_t err = bms_mock_start();
#else
    esp_err_t err = bms_transport_init();
#endif
    if (err != ESP_OK) {
        /* Not fatal: the UI keeps running and shows "NO DATA". */
        ESP_LOGE(TAG, "data source: %s", esp_err_to_name(err));
    }
}
