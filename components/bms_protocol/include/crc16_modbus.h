#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * CRC-16/MODBUS (poly 0xA001 reflected, init 0xFFFF).
 * The result goes on the wire low byte first.
 */
uint16_t crc16_modbus(const uint8_t *data, size_t len);
