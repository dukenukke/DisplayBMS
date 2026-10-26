#pragma once

#include <stdint.h>

#define BMS_ALARM_BITS  32

typedef enum {
    BMS_ALARM_INFO,     /* informational, e.g. default password still set */
    BMS_ALARM_WARN,
    BMS_ALARM_FAULT,    /* protection tripped or hardware fault */
} bms_alarm_severity_t;

typedef struct {
    const char *name;               /* NULL for bits without a documented meaning */
    bms_alarm_severity_t severity;
} bms_alarm_desc_t;

/** Description of alarm bit 0..31 of the 0x12A0 bitmask (docs/PROTOCOL.md). */
const bms_alarm_desc_t *bms_alarm_desc(unsigned bit);
