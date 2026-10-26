#include "esp_log.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include "sdkconfig.h"

#include "board.h"
#include "bms_model.h"
#include "bms_transport.h"
#include "ui.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "DisplayBMS, ESP-IDF %s, LVGL %d.%d.%d", esp_get_idf_version(),
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    ESP_ERROR_CHECK(bms_model_init());

    lv_display_t *disp = NULL;
    esp_err_t err = board_init(&disp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "board_init: %s", esp_err_to_name(err));
    }

    err = bms_transport_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bms_transport_init: %s", esp_err_to_name(err));
    }
}
