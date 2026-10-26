#include "crc16_modbus.h"

#define CRC16_MODBUS_INIT   0xFFFFu
#define CRC16_MODBUS_POLY   0xA001u

uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = CRC16_MODBUS_INIT;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) ? (crc >> 1) ^ CRC16_MODBUS_POLY : crc >> 1;
        }
    }
    return crc;
}
