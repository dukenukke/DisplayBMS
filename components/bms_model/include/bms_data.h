#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Decoded BMS state. Field meanings and units follow docs/PROTOCOL.md
 * (JK frame types 01 / 02 / 03 = register blocks 0x1000 / 0x1200 / 0x1400).
 * Raw protocol units are kept (mV, mA, 0.1 C); the UI formats them.
 */

#define BMS_MAX_CELLS           32      /* protocol limit; PB1A16S10P uses 16 */
#define BMS_BAT_TEMP_SENSORS    5       /* TempBat1..5 */

/* Plausibility limits applied before display (outside -> shown as "--"). */
#define BMS_CELL_MV_MIN         1000
#define BMS_CELL_MV_MAX         5000
#define BMS_TEMP_DC_MIN         (-400)  /* -40.0 C */
#define BMS_TEMP_DC_MAX         1200    /* 120.0 C */
#define BMS_PACK_MV_MAX         100000
#define BMS_CURRENT_MA_ABS_MAX  500000
#define BMS_PCT_MAX             100

/* Temperature-sensor presence bits (0x12D0: BIT0 MOS, BIT1..5 battery 1..5). */
#define BMS_TEMP_PRESENT_MOS    (1u << 0)
#define BMS_TEMP_PRESENT_BAT(i) (1u << (1 + (i)))

typedef enum {
    BMS_BALANCE_OFF = 0,
    BMS_BALANCE_CHARGE = 1,
    BMS_BALANCE_DISCHARGE = 2,
} bms_balance_state_t;

/* Live data: frame type 02 / block 0x1200 */
typedef struct {
    bool valid;
    int64_t updated_us;                     /* esp_timer time of the last update */

    uint32_t cell_present_mask;             /* CellSta: bit n = cell n exists */
    uint16_t cell_mv[BMS_MAX_CELLS];
    uint16_t cell_avg_mv;
    uint16_t cell_delta_mv;
    uint32_t wire_res_alarm_mask;           /* CellWireResSta */

    int16_t temp_mos_dc;                    /* 0.1 C */
    int16_t temp_bat_dc[BMS_BAT_TEMP_SENSORS];
    uint8_t temp_present_mask;              /* BMS_TEMP_PRESENT_* */

    uint32_t pack_mv;
    int32_t current_ma;                     /* sign convention unverified (OPEN_QUESTIONS B4) */
    uint32_t power_mw;                      /* unsigned in the protocol */
    uint32_t alarms;                        /* bit n = alarm n, see bms_alarms.h */

    int16_t balance_ma;
    uint8_t balance_state;                  /* bms_balance_state_t */
    uint8_t soc_pct;
    uint8_t soh_pct;
    int32_t remain_mah;
    uint32_t full_mah;
    uint32_t cycles;
    uint32_t cycle_cap_mah;

    bool charge_mos;
    bool discharge_mos;
    bool precharge;
    uint32_t runtime_s;
} bms_live_t;

/* Settings, read-only view: frame type 01 / block 0x1000 */
typedef struct {
    bool valid;
    int64_t updated_us;
    uint32_t cell_count;
    uint32_t design_cap_mah;
    uint32_t cell_ovp_mv;
    uint32_t cell_ovpr_mv;
    uint32_t cell_uvp_mv;
    uint32_t cell_uvpr_mv;
    uint32_t soc100_mv;
    uint32_t soc0_mv;
    uint32_t balance_trig_mv;               /* delta that starts balancing */
    uint32_t balance_start_mv;
    uint32_t balance_max_ma;
    uint32_t charge_oc_ma;
    uint32_t discharge_oc_ma;
    int32_t charge_ot_dc;
    int32_t discharge_ot_dc;
    int32_t charge_ut_dc;
    int32_t mos_ot_dc;
    bool charge_enabled;
    bool discharge_enabled;
    bool balance_enabled;
} bms_settings_t;

/*
 * Device info: frame type 03 / block 0x1400. Only these fields are kept; the
 * frame also carries the BLE PIN and password, which are never stored.
 */
typedef struct {
    bool valid;
    int64_t updated_us;
    char model[17];
    char hw_version[9];
    char sw_version[9];
    uint32_t uptime_s;
    uint32_t power_on_count;
} bms_info_t;

typedef struct {
    uint32_t frames_ok;
    uint32_t checksum_errors;
    uint32_t unknown_frames;
    uint32_t resyncs;                       /* bytes skipped while hunting for a header */
    uint32_t bytes_rx;
    int64_t last_frame_us;
} bms_link_stats_t;

typedef struct {
    bms_live_t live;
    bms_settings_t settings;
    bms_info_t info;
    bms_link_stats_t link;
} bms_data_t;

static inline bool bms_cell_mv_ok(uint32_t mv)
{
    return mv >= BMS_CELL_MV_MIN && mv <= BMS_CELL_MV_MAX;
}

static inline bool bms_temp_dc_ok(int32_t dc)
{
    return dc >= BMS_TEMP_DC_MIN && dc <= BMS_TEMP_DC_MAX;
}
