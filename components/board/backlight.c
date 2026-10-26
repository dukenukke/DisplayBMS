#include "backlight.h"
#include "ambient.h"
#include "board.h"

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs.h"
#include "sdkconfig.h"

#define BL_LEDC_MODE            LEDC_LOW_SPEED_MODE
#define BL_LEDC_TIMER           LEDC_TIMER_0
#define BL_LEDC_CHANNEL         LEDC_CHANNEL_0
#define BL_LEDC_RESOLUTION      LEDC_TIMER_10_BIT
#define BL_DUTY_MAX             ((1u << 10) - 1)

#define BL_SERVICE_PERIOD_MS    100
#define BL_FADE_STEP_PCT        4       /* per service tick */
#define BL_AMBIENT_EVERY_TICKS  5       /* LDR sampled every 500 ms */
#define BL_AMBIENT_EMA_SHIFT    2       /* smoothing: new = old + (raw - old) / 4 */
#define BL_DARK_SCALE_PCT       30      /* brightness in full darkness, % of the user value */

#define BL_NVS_NAMESPACE        "dbms"
#define BL_NVS_KEY              "bl_pct"

static const char *TAG = "backlight";

static int s_user_pct = CONFIG_DBMS_BL_DEFAULT_PCT;
static int s_level_pct;
static int s_ambient_raw = -1;
static int s_ambient_pct = 100;
static int64_t s_last_activity_us;
static bool s_dimmed;

static int clamp_pct(int pct, int lo)
{
    return pct < lo ? lo : pct > 100 ? 100 : pct;
}

static void apply_level(int pct)
{
    s_level_pct = pct;
    uint32_t duty = (uint32_t)pct * BL_DUTY_MAX / 100;
    if (ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty) == ESP_OK) {
        ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
    }
}

esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = BL_LEDC_MODE,
        .duty_resolution = BL_LEDC_RESOLUTION,
        .timer_num = BL_LEDC_TIMER,
        .freq_hz = CONFIG_DBMS_BL_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;

    const ledc_channel_config_t channel = {
        .gpio_num = BOARD_LCD_PIN_BL,
        .speed_mode = BL_LEDC_MODE,
        .channel = BL_LEDC_CHANNEL,
        .timer_sel = BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) return err;

    nvs_handle_t h;
    if (nvs_open(BL_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        uint8_t stored;
        if (nvs_get_u8(h, BL_NVS_KEY, &stored) == ESP_OK) {
            s_user_pct = clamp_pct(stored, CONFIG_DBMS_BL_MIN_PCT);
        }
        nvs_close(h);
    }

    err = ambient_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LDR unavailable: %s", esp_err_to_name(err));
    }
    s_last_activity_us = esp_timer_get_time();
    ESP_LOGI(TAG, "PWM %d Hz, user brightness %d%%", CONFIG_DBMS_BL_PWM_FREQ_HZ, s_user_pct);
    return ESP_OK;
}

static int target_pct(void)
{
    int target = s_user_pct;
#if CONFIG_DBMS_LDR_ENABLE
    if (s_ambient_raw >= 0) {
        int scale = BL_DARK_SCALE_PCT + (100 - BL_DARK_SCALE_PCT) * s_ambient_pct / 100;
        target = clamp_pct(target * scale / 100, CONFIG_DBMS_BL_MIN_PCT);
    }
#endif
#if CONFIG_DBMS_BL_DIM_TIMEOUT_S > 0
    const int64_t timeout_us = (int64_t)CONFIG_DBMS_BL_DIM_TIMEOUT_S * 1000000;
    s_dimmed = esp_timer_get_time() - s_last_activity_us >= timeout_us;
    if (s_dimmed && target > CONFIG_DBMS_BL_DIM_PCT) target = CONFIG_DBMS_BL_DIM_PCT;
#endif
    return target;
}

static void service_cb(lv_timer_t *timer)
{
    static unsigned ticks;
    (void)timer;
    if (ticks++ % BL_AMBIENT_EVERY_TICKS == 0) {
        int raw = ambient_read_raw();
        if (raw >= 0) {
            s_ambient_raw = s_ambient_raw < 0 ? raw
                          : s_ambient_raw + ((raw - s_ambient_raw) >> BL_AMBIENT_EMA_SHIFT);
            s_ambient_pct = ambient_raw_to_pct(s_ambient_raw);
        }
    }
    int target = target_pct();
    if (target == s_level_pct) return;
    int step = target > s_level_pct ? BL_FADE_STEP_PCT : -BL_FADE_STEP_PCT;
    int next = s_level_pct + step;
    if ((step > 0 && next > target) || (step < 0 && next < target)) next = target;
    apply_level(next);
}

void backlight_start_service(void)
{
    if (!lv_timer_create(service_cb, BL_SERVICE_PERIOD_MS, NULL)) {
        /* Without the service keep the screen readable at the user level. */
        ESP_LOGE(TAG, "no LVGL timer, fixed brightness");
        apply_level(s_user_pct);
    }
}

void backlight_set_user_pct(int pct, bool persist)
{
    s_user_pct = clamp_pct(pct, CONFIG_DBMS_BL_MIN_PCT);
    s_last_activity_us = esp_timer_get_time();
    if (!persist) return;
    nvs_handle_t h;
    esp_err_t err = nvs_open(BL_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, BL_NVS_KEY, (uint8_t)s_user_pct);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "brightness not saved: %s", esp_err_to_name(err));
}

int backlight_get_user_pct(void)
{
    return s_user_pct;
}

int backlight_get_level_pct(void)
{
    return s_level_pct;
}

int backlight_get_ambient_raw(void)
{
    return s_ambient_raw;
}

int backlight_get_ambient_pct(void)
{
    return s_ambient_pct;
}

bool backlight_touch_activity(void)
{
    bool was_dimmed = s_dimmed;
    s_last_activity_us = esp_timer_get_time();
    s_dimmed = false;
    return was_dimmed;
}
