#include "ambient.h"
#include "board.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "sdkconfig.h"

/*
 * The CYD LDR divider produces only a few hundred mV, so the lowest attenuation
 * gives the usable resolution. Verify the raw range on hardware (OPEN_QUESTIONS H2).
 */
#define AMBIENT_ADC_ATTEN   ADC_ATTEN_DB_0
#define AMBIENT_ADC_BITWIDTH ADC_BITWIDTH_12

static const char *TAG = "ambient";

#if CONFIG_DBMS_LDR_ENABLE
static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t s_channel;
#endif

esp_err_t ambient_init(void)
{
#if CONFIG_DBMS_LDR_ENABLE
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(BOARD_LDR_PIN, &unit, &s_channel);
    if (err != ESP_OK) return err;

    const adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = unit};
    err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) return err;

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = AMBIENT_ADC_ATTEN,
        .bitwidth = AMBIENT_ADC_BITWIDTH,
    };
    err = adc_oneshot_config_channel(s_adc, s_channel, &chan_cfg);
    if (err != ESP_OK) {
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
    }
    return err;
#else
    return ESP_OK;
#endif
}

int ambient_read_raw(void)
{
#if CONFIG_DBMS_LDR_ENABLE
    int raw;
    if (!s_adc || adc_oneshot_read(s_adc, s_channel, &raw) != ESP_OK) return -1;
    ESP_LOGD(TAG, "raw %d", raw);
    return raw;
#else
    return -1;
#endif
}

int ambient_raw_to_pct(int raw)
{
#if CONFIG_DBMS_LDR_ENABLE
    const int bright = CONFIG_DBMS_LDR_RAW_BRIGHT;
    const int dark = CONFIG_DBMS_LDR_RAW_DARK;
    if (raw < 0 || bright == dark) return 100;
    /* Works for either direction of the divider. */
    int pct = (raw - dark) * 100 / (bright - dark);
    return pct < 0 ? 0 : pct > 100 ? 100 : pct;
#else
    (void)raw;
    return 100;
#endif
}
