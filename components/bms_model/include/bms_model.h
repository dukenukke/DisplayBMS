#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "bms_data.h"

/** Create the model mutex. Call once before any producer or consumer. */
esp_err_t bms_model_init(void);

/* Producers (protocol reader or mock): copy in and stamp updated_us. */
void bms_model_publish_live(const bms_live_t *live);
void bms_model_publish_settings(const bms_settings_t *settings);
void bms_model_publish_info(const bms_info_t *info);
void bms_model_publish_link(const bms_link_stats_t *link);

/** Consumer: copy of the whole model, taken under the mutex. */
void bms_model_snapshot(bms_data_t *out);

/** True when live data exist but are older than CONFIG_DBMS_BMS_STALE_AFTER_S (or absent). */
bool bms_model_live_stale(const bms_data_t *data, int64_t now_us);
