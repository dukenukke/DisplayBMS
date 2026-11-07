#include "bms_transport.h"

#include "bms_model.h"
#include "bms_sniffer.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define BMS_UART            CONFIG_DBMS_BMS_UART_NUM
#define RX_RING_SIZE        2048    /* ~6 frames: covers the log blocking the task for a while */
#define EVENT_QUEUE_LEN     16
#define RX_CHUNK            128
#define RX_TASK_STACK       4096
#define RX_TASK_PRIORITY    5
#define RX_TASK_CORE        0
#define SNIFF_IDLE_MS       20      /* gap that ends a burst; 300 bytes take 26 ms at 115200 */

#if CONFIG_DBMS_SNIFFER
#define SNIFFER_NOTE        ", sniffer on"
#else
#define SNIFFER_NOTE        ""
#endif

static const char *TAG = "bms_uart";

static QueueHandle_t s_events;
static bms_link_stats_t s_link;     /* owned by the RX task */

static void read_available(size_t len)
{
    uint8_t chunk[RX_CHUNK];
    while (len > 0) {
        int n = uart_read_bytes(BMS_UART, chunk, len < sizeof(chunk) ? len : sizeof(chunk), pdMS_TO_TICKS(20));
        if (n <= 0) break;
        len -= n;
        int64_t now = esp_timer_get_time();
        s_link.bytes_rx += n;
        s_link.last_rx_us = now;
#if CONFIG_DBMS_SNIFFER
        bms_sniffer_feed(chunk, n, now);
#endif
        /* Stage 5: feed the JK frame parser here. */
    }
}

static void rx_task(void *arg)
{
    (void)arg;
    for (;;) {
        TickType_t wait = portMAX_DELAY;
#if CONFIG_DBMS_SNIFFER
        if (bms_sniffer_pending()) wait = pdMS_TO_TICKS(SNIFF_IDLE_MS);
#endif
        uart_event_t ev;
        if (xQueueReceive(s_events, &ev, wait) != pdTRUE) {
#if CONFIG_DBMS_SNIFFER
            bms_sniffer_flush();
#endif
            continue;
        }
        switch (ev.type) {
        case UART_DATA:
            read_available(ev.size);
            break;
        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
            /* The driver stops reading until the backlog is dropped. */
            ++s_link.overruns;
            uart_flush_input(BMS_UART);
            xQueueReset(s_events);
            ESP_LOGW(TAG, "RX overflow, input flushed");
            break;
        case UART_FRAME_ERR:
        case UART_PARITY_ERR:
        case UART_BREAK:
            /* Many of these usually mean swapped A/B lines or a wrong baud rate. */
            ++s_link.line_errors;
            break;
        default:
            continue;
        }
        bms_model_publish_link(&s_link);
    }
}

esp_err_t bms_transport_init(void)
{
    /*
     * Transceiver DE and /RE: low = receiver on, driver off. Set first and never
     * changed, so the transceiver cannot drive the bus while the firmware runs.
     */
    const gpio_num_t rts = CONFIG_DBMS_BMS_RTS_GPIO;
    gpio_set_level(rts, 0);
    const gpio_config_t rts_cfg = {.pin_bit_mask = 1ULL << rts, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&rts_cfg), TAG, "RTS gpio");
    ESP_RETURN_ON_ERROR(gpio_set_level(rts, 0), TAG, "RTS level");

    const uart_config_t cfg = {
        .baud_rate = CONFIG_DBMS_BMS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    /* No TX buffer: nothing is ever written to this port. */
    ESP_RETURN_ON_ERROR(uart_driver_install(BMS_UART, RX_RING_SIZE, 0, EVENT_QUEUE_LEN, &s_events, 0), TAG,
                        "driver");
    ESP_RETURN_ON_ERROR(uart_param_config(BMS_UART, &cfg), TAG, "config");
    /* RX only: the UART TX and RTS signals are not routed to any pin. */
    ESP_RETURN_ON_ERROR(uart_set_pin(BMS_UART, UART_PIN_NO_CHANGE, CONFIG_DBMS_BMS_RX_GPIO, UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG, "pins");

    if (xTaskCreatePinnedToCore(rx_task, "bms_rx", RX_TASK_STACK, NULL, RX_TASK_PRIORITY, NULL, RX_TASK_CORE) !=
        pdPASS) {
        uart_driver_delete(BMS_UART);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "UART%d RX GPIO%d, %d 8N1, listen only (DE and /RE on GPIO%d held low)" SNIFFER_NOTE,
             BMS_UART, CONFIG_DBMS_BMS_RX_GPIO, CONFIG_DBMS_BMS_BAUD, rts);
    return ESP_OK;
}
