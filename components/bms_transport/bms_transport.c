#include "bms_transport.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "bms_uart";

esp_err_t bms_transport_init(void)
{
    /* UART bring-up is added in stage 4. */
    ESP_LOGI(TAG, "UART%d TX=%d RX=%d %d 8N1 (not started)", CONFIG_DBMS_BMS_UART_NUM,
             CONFIG_DBMS_BMS_TX_GPIO, CONFIG_DBMS_BMS_RX_GPIO, CONFIG_DBMS_BMS_BAUD);
    return ESP_ERR_NOT_SUPPORTED;
}
