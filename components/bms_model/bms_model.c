#include "bms_model.h"

#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#define STALE_AFTER_US  ((int64_t)CONFIG_DBMS_BMS_STALE_AFTER_S * 1000000)

static SemaphoreHandle_t s_lock;
static bms_data_t s_data;

esp_err_t bms_model_init(void)
{
    if (s_lock) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

/* Copies are a few hundred bytes, so the lock is held only briefly. */
static void locked_copy(void *dst, const void *src, size_t len)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(dst, src, len);
    xSemaphoreGive(s_lock);
}

void bms_model_publish_live(const bms_live_t *live)
{
    bms_live_t copy = *live;
    copy.valid = true;
    copy.updated_us = esp_timer_get_time();
    locked_copy(&s_data.live, &copy, sizeof(copy));
}

void bms_model_publish_settings(const bms_settings_t *settings)
{
    bms_settings_t copy = *settings;
    copy.valid = true;
    copy.updated_us = esp_timer_get_time();
    locked_copy(&s_data.settings, &copy, sizeof(copy));
}

void bms_model_publish_info(const bms_info_t *info)
{
    bms_info_t copy = *info;
    copy.valid = true;
    copy.updated_us = esp_timer_get_time();
    locked_copy(&s_data.info, &copy, sizeof(copy));
}

void bms_model_publish_link(const bms_link_stats_t *link)
{
    locked_copy(&s_data.link, link, sizeof(*link));
}

void bms_model_snapshot(bms_data_t *out)
{
    locked_copy(out, &s_data, sizeof(*out));
}

bool bms_model_live_stale(const bms_data_t *data, int64_t now_us)
{
    return !data->live.valid || now_us - data->live.updated_us > STALE_AFTER_US;
}
