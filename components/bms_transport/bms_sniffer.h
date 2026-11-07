#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Hex dump of the raw RX stream, grouped into bursts separated by line idle.
 * BLE PIN and password bytes of the device-info frame are masked as "**".
 * Only called from the RX task.
 */
void bms_sniffer_feed(const uint8_t *data, size_t len, int64_t now_us);
bool bms_sniffer_pending(void);
/** The line went idle: dump the buffered burst. */
void bms_sniffer_flush(void);
