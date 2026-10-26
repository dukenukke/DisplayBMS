#pragma once

#include "esp_err.h"

/** Create the model mutex. Call once before any producer or consumer. */
esp_err_t bms_model_init(void);
