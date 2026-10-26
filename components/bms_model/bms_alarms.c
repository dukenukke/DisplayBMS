#include "bms_alarms.h"

#include <stddef.h>

/* Bit meanings: docs/PROTOCOL.md, "Alarms 0xA0" (source S1). Bits 22-23 unverified. */
static const bms_alarm_desc_t s_alarms[BMS_ALARM_BITS] = {
    [0]  = {"Balance wire resistance high", BMS_ALARM_WARN},
    [1]  = {"MOS over-temperature", BMS_ALARM_FAULT},
    [2]  = {"Cell count mismatch", BMS_ALARM_FAULT},
    [3]  = {"Current sensor error", BMS_ALARM_FAULT},
    [4]  = {"Cell over-voltage", BMS_ALARM_FAULT},
    [5]  = {"Battery over-voltage", BMS_ALARM_FAULT},
    [6]  = {"Charge over-current", BMS_ALARM_FAULT},
    [7]  = {"Charge short circuit", BMS_ALARM_FAULT},
    [8]  = {"Charge over-temperature", BMS_ALARM_FAULT},
    [9]  = {"Charge under-temperature", BMS_ALARM_FAULT},
    [10] = {"Internal communication error", BMS_ALARM_FAULT},
    [11] = {"Cell under-voltage", BMS_ALARM_FAULT},
    [12] = {"Battery under-voltage", BMS_ALARM_FAULT},
    [13] = {"Discharge over-current", BMS_ALARM_FAULT},
    [14] = {"Discharge short circuit", BMS_ALARM_FAULT},
    [15] = {"Discharge over-temperature", BMS_ALARM_FAULT},
    [16] = {"Charge MOS fault", BMS_ALARM_FAULT},
    [17] = {"Discharge MOS fault", BMS_ALARM_FAULT},
    [18] = {"GPS disconnected", BMS_ALARM_WARN},
    [19] = {"Change password", BMS_ALARM_INFO},
    [20] = {"Discharge start failed", BMS_ALARM_FAULT},
    [21] = {"Battery over-temperature", BMS_ALARM_WARN},
    [22] = {"Temperature sensor fault?", BMS_ALARM_WARN},
    [23] = {"Parallel module fault?", BMS_ALARM_WARN},
};

static const bms_alarm_desc_t s_unknown = {NULL, BMS_ALARM_WARN};

const bms_alarm_desc_t *bms_alarm_desc(unsigned bit)
{
    if (bit >= BMS_ALARM_BITS || !s_alarms[bit].name) return &s_unknown;
    return &s_alarms[bit];
}
