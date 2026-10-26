#pragma once

#include "esp_err.h"

/** Start the mock producer task (plausible changing data, simulated link drops). */
esp_err_t bms_mock_start(void);
