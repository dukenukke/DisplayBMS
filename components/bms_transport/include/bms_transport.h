#pragma once

#include "esp_err.h"

/** Install the UART driver on the port and pins selected in Kconfig. */
esp_err_t bms_transport_init(void);
