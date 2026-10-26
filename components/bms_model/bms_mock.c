#include "bms_mock.h"
#include "bms_model.h"

#include <math.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_DBMS_SOURCE_MOCK

/* Same core / priority as the real BMS reader will use. */
#define MOCK_TASK_STACK         3072
#define MOCK_TASK_PRIORITY      5
#define MOCK_TASK_CORE          0

#define MOCK_CELLS              16
#define MOCK_CURRENT_PERIOD_S   600     /* charge/discharge cycle */
#define MOCK_CURRENT_PEAK_MA    40000
#define MOCK_SOC_PERIOD_S       1800
#define MOCK_CELL_BASE_MV       3200
#define MOCK_CELL_SPAN_MV       150     /* added from 0 to 100 % SOC */
#define MOCK_CELL_NOISE_MV      3
#define MOCK_LOW_CELL           6       /* index of a consistently low cell */
#define MOCK_LOW_CELL_MV        9
#define MOCK_HIGH_CELL          10
#define MOCK_HIGH_CELL_MV       7
#define MOCK_BALANCE_MA         210
#define MOCK_FULL_MAH           314000
#define MOCK_OCP_EVERY_S        120     /* "charge over-current" for a while */
#define MOCK_OCP_FOR_S          20
#define MOCK_DROP_EVERY_S       240     /* simulated link loss: last 60 s of each window */
#define MOCK_DROP_FOR_S         60

#define ALARM_CHARGE_OCP        (1u << 6)
#define ALARM_CHANGE_PASSWORD   (1u << 19)

static const char *TAG = "bms_mock";

static int noise(int amplitude)
{
    return (int)(esp_random() % (2u * amplitude + 1)) - amplitude;
}

static void publish_static(void)
{
    const bms_settings_t settings = {
        .cell_count = MOCK_CELLS,
        .design_cap_mah = MOCK_FULL_MAH,
        .cell_ovp_mv = 3650, .cell_ovpr_mv = 3400,
        .cell_uvp_mv = 2800, .cell_uvpr_mv = 3000,
        .soc100_mv = 3450, .soc0_mv = 2900,
        .balance_trig_mv = 10, .balance_start_mv = 3000, .balance_max_ma = 2000,
        .charge_oc_ma = 150000, .discharge_oc_ma = 200000,
        .charge_ot_dc = 700, .discharge_ot_dc = 700, .charge_ut_dc = -200, .mos_ot_dc = 1000,
        .charge_enabled = true, .discharge_enabled = true, .balance_enabled = true,
    };
    bms_model_publish_settings(&settings);

    bms_info_t info = {.uptime_s = 1048576, .power_on_count = 9};
    snprintf(info.model, sizeof(info.model), "JK_PB1A16S10P");
    snprintf(info.hw_version, sizeof(info.hw_version), "19A");
    snprintf(info.sw_version, sizeof(info.sw_version), "19.05");
    bms_model_publish_info(&info);
}

static void fill_live(bms_live_t *live, uint32_t t)
{
    const float two_pi = 6.2831853f;
    live->current_ma = (int32_t)(MOCK_CURRENT_PEAK_MA * sinf(two_pi * t / MOCK_CURRENT_PERIOD_S));
    live->soc_pct = (uint8_t)(60 + 30 * sinf(two_pi * t / MOCK_SOC_PERIOD_S));
    live->soh_pct = 100;
    live->full_mah = MOCK_FULL_MAH;
    live->remain_mah = (int32_t)((uint64_t)MOCK_FULL_MAH * live->soc_pct / 100);
    live->cycles = 3;
    live->cycle_cap_mah = 534000;

    uint32_t sum = 0, min = UINT32_MAX, max = 0;
    live->cell_present_mask = (1u << MOCK_CELLS) - 1;
    for (int i = 0; i < MOCK_CELLS; ++i) {
        int mv = MOCK_CELL_BASE_MV + MOCK_CELL_SPAN_MV * live->soc_pct / 100 + noise(MOCK_CELL_NOISE_MV);
        if (i == MOCK_LOW_CELL) mv -= MOCK_LOW_CELL_MV;
        if (i == MOCK_HIGH_CELL) mv += MOCK_HIGH_CELL_MV;
        live->cell_mv[i] = (uint16_t)mv;
        sum += mv;
        if ((uint32_t)mv < min) min = mv;
        if ((uint32_t)mv > max) max = mv;
    }
    live->pack_mv = sum;
    live->cell_avg_mv = (uint16_t)(sum / MOCK_CELLS);
    live->cell_delta_mv = (uint16_t)(max - min);
    uint32_t abs_ma = live->current_ma < 0 ? -live->current_ma : live->current_ma;
    live->power_mw = (uint32_t)((uint64_t)live->pack_mv * abs_ma / 1000);

    live->temp_present_mask = BMS_TEMP_PRESENT_MOS | BMS_TEMP_PRESENT_BAT(0) | BMS_TEMP_PRESENT_BAT(1);
    live->temp_mos_dc = (int16_t)(280 + abs_ma / 500 + noise(3));
    live->temp_bat_dc[0] = (int16_t)(245 + noise(2));
    live->temp_bat_dc[1] = (int16_t)(252 + noise(2));

    bool balancing = live->cell_delta_mv >= 10;
    live->balance_state = balancing ? (live->current_ma >= 0 ? BMS_BALANCE_CHARGE : BMS_BALANCE_DISCHARGE)
                                    : BMS_BALANCE_OFF;
    live->balance_ma = balancing ? MOCK_BALANCE_MA : 0;

    live->alarms = ALARM_CHANGE_PASSWORD;
    bool ocp = live->current_ma > 0 && t % MOCK_OCP_EVERY_S < MOCK_OCP_FOR_S;
    if (ocp) live->alarms |= ALARM_CHARGE_OCP;
    live->charge_mos = !ocp;
    live->discharge_mos = true;
    live->runtime_s = 1048576 + t;
}

static void mock_task(void *arg)
{
    (void)arg;
    const uint32_t period_ms = CONFIG_DBMS_MOCK_PERIOD_MS;
    bms_live_t live = {0};
    bms_link_stats_t link = {0};
    uint32_t elapsed_ms = 0;

    publish_static();
    while (true) {
        uint32_t t = elapsed_ms / 1000;
#if CONFIG_DBMS_MOCK_LINK_DROPS
        bool dropped = t % MOCK_DROP_EVERY_S >= MOCK_DROP_EVERY_S - MOCK_DROP_FOR_S;
#else
        bool dropped = false;
#endif
        if (!dropped) {
            fill_live(&live, t);
            bms_model_publish_live(&live);
            ++link.frames_ok;
            link.bytes_rx += 300;
            link.last_frame_us = esp_timer_get_time();
            bms_model_publish_link(&link);
        }
        vTaskDelay(pdMS_TO_TICKS(period_ms));
        elapsed_ms += period_ms;
    }
}

esp_err_t bms_mock_start(void)
{
    ESP_LOGW(TAG, "MOCK data source: values are simulated");
    BaseType_t ok = xTaskCreatePinnedToCore(mock_task, "bms_mock", MOCK_TASK_STACK, NULL,
                                            MOCK_TASK_PRIORITY, NULL, MOCK_TASK_CORE);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

#else

esp_err_t bms_mock_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
