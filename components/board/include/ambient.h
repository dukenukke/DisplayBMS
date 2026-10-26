#pragma once

#include "esp_err.h"

/** Configure the photoresistor ADC (GPIO34, ADC1). No-op when the LDR is disabled. */
esp_err_t ambient_init(void);

/** Take one ADC reading. Returns -1 when the LDR is disabled or the read failed. */
int ambient_read_raw(void);

/** Map a raw reading to 0 (dark) .. 100 (bright) using the Kconfig range. */
int ambient_raw_to_pct(int raw);
