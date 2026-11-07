#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * JK protocol 015 frame (docs/PROTOCOL.md): 300 bytes, header 55 AA EB 90,
 * byte 4 = type, byte 5 = counter, bytes 6..298 = block payload (byte k is
 * offset k of the block, little-endian), byte 299 = sum of bytes 0..298.
 */
#define JK_FRAME_LEN            300
#define JK_FRAME_HEADER         0x55AAEB90u     /* bytes 0..3 read as a big-endian word */
#define JK_FRAME_TYPE_POS       4
#define JK_FRAME_COUNTER_POS    5
#define JK_FRAME_PAYLOAD_POS    6
#define JK_FRAME_SUM_POS        (JK_FRAME_LEN - 1)

#define JK_FRAME_SETTINGS       0x01    /* block 0x1000 */
#define JK_FRAME_LIVE           0x02    /* block 0x1200 */
#define JK_FRAME_INFO           0x03    /* block 0x1400 */

/*
 * Device-info frame: BLE PIN at 0x38 and password at 0x70 (unofficial map).
 * Field lengths are unknown, so the masked ranges run up to the next known
 * field (serial at 0x50) and 16 bytes for the password.
 */
#define JK_INFO_PIN_OFFSET      0x38
#define JK_INFO_PIN_END         0x50
#define JK_INFO_PASSWORD_OFFSET 0x70
#define JK_INFO_PASSWORD_END    0x80

/** True if byte `pos` of a frame of type `type` holds the BLE PIN or password. */
static inline bool jk_frame_byte_sensitive(uint8_t type, int pos)
{
    if (type != JK_FRAME_INFO || pos < JK_FRAME_PAYLOAD_POS) return false;
    int offset = pos - JK_FRAME_PAYLOAD_POS;
    return (offset >= JK_INFO_PIN_OFFSET && offset < JK_INFO_PIN_END) ||
           (offset >= JK_INFO_PASSWORD_OFFSET && offset < JK_INFO_PASSWORD_END);
}
