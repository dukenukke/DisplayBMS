#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define BMS_BAUD_RATE_COUNT 11
#define BMS_RAW_CAPTURE     256     /* last bytes kept for the raw view */

/** Baud rates offered for the link, ascending. */
extern const uint32_t bms_baud_rates[BMS_BAUD_RATE_COUNT];

/** Raw stream view. Counters restart on a baud change. */
typedef struct {
    uint32_t baud;
    uint32_t total;         /* bytes since the baud change */
    uint32_t line_errors;   /* framing/parity/break events since the baud change */
    uint32_t headers;       /* JK headers 55 AA EB 90 seen */
    uint32_t frames_ok;     /* JK frames with a valid checksum */
    uint32_t frames_bad;
    uint16_t len;           /* valid bytes in data[], oldest first */
    uint8_t data[BMS_RAW_CAPTURE];      /* BLE PIN and password bytes are zeroed ... */
    bool masked[BMS_RAW_CAPTURE];       /* ... and flagged here */
} bms_raw_view_t;

/**
 * Receive-only UART link: DE and /RE held low, TX not routed. The baud rate
 * comes from NVS (set from the raw screen), else from Kconfig.
 */
esp_err_t bms_transport_init(void);
bool bms_transport_running(void);

/** Change the baud rate now and store it in NVS. Must be one of bms_baud_rates. */
esp_err_t bms_transport_set_baud(uint32_t baud);

/** Copy of the raw view; all zero when the link is not running. */
void bms_transport_raw_view(bms_raw_view_t *out);
