#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Hex dump of the raw RX stream, grouped into bursts separated by line idle.
 * Bytes flagged as masked (BLE PIN, password) are printed as "**".
 * Only called from the RX task.
 */
void bms_sniffer_feed(const uint8_t *data, const bool *masked, size_t len, int64_t now_us);
/** A JK frame ended in the current burst; summarised after its dump. */
void bms_sniffer_frame(uint8_t type, uint8_t counter, bool sum_ok);
bool bms_sniffer_pending(void);
/** The line went idle: dump the buffered burst. */
void bms_sniffer_flush(void);
