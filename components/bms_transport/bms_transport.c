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
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "jk_tracker.h"
#include "nvs.h"
#include "sdkconfig.h"

#define BMS_UART            CONFIG_DBMS_BMS_UART_NUM
#define RX_RING_SIZE        4096    /* ~13 frames: covers the sniffer log blocking the task */
#define EVENT_QUEUE_LEN     16
#define RX_CHUNK            128
#define RX_TASK_STACK       4096
#define RX_TASK_PRIORITY    5
#define RX_TASK_CORE        0
#define SNIFF_IDLE_MS       20      /* gap that ends a burst; 300 bytes take 26 ms at 115200 */
#define NVS_NAMESPACE       "dbms"
#define NVS_KEY_BAUD        "bms_baud"

#if CONFIG_DBMS_SNIFFER
#define SNIFFER_NOTE        ", sniffer on"
#else
#define SNIFFER_NOTE        ""
#endif

static const char *TAG = "bms_uart";

const uint32_t bms_baud_rates[BMS_BAUD_RATE_COUNT] = {9600, 14400, 19200, 28800, 38400, 57600, 76800, 115200};

static QueueHandle_t s_events;
static bms_link_stats_t s_link;     /* owned by the RX task */
static bool s_running;

/* Raw view: tracker, capture ring and per-baud counters, shared with the UI. */
static SemaphoreHandle_t s_raw_lock;
static jk_tracker_t s_tracker;
static bms_raw_view_t s_raw;        /* data[] is used as a ring, s_raw_head = next write */
static uint16_t s_raw_head;

static bool baud_supported(uint32_t baud)
{
    for (int i = 0; i < BMS_BAUD_RATE_COUNT; ++i) {
        if (bms_baud_rates[i] == baud) return true;
    }
    return false;
}

static uint32_t load_baud(void)
{
    uint32_t baud = CONFIG_DBMS_BMS_BAUD;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        uint32_t stored;
        if (nvs_get_u32(h, NVS_KEY_BAUD, &stored) == ESP_OK && baud_supported(stored)) baud = stored;
        nvs_close(h);
    }
    return baud;
}

static void save_baud(uint32_t baud)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u32(h, NVS_KEY_BAUD, baud);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "save baud: %s", esp_err_to_name(err));
}

/* Caller holds s_raw_lock (or no other task runs yet). */
static void raw_reset(uint32_t baud)
{
    s_raw = (bms_raw_view_t){.baud = baud};
    s_raw_head = 0;
    jk_tracker_reset(&s_tracker);
}

/* Follows JK frames, zeroes sensitive bytes in place and flags them in masked[]. */
static void raw_capture(uint8_t *data, bool *masked, int n)
{
    xSemaphoreTake(s_raw_lock, portMAX_DELAY);
    for (int i = 0; i < n; ++i) {
        jk_track_event_t ev = jk_tracker_push(&s_tracker, data[i], &masked[i]);
        if (masked[i]) data[i] = 0;
        if (ev == JK_TRACK_HEADER) ++s_raw.headers;
        if (ev == JK_TRACK_FRAME_OK || ev == JK_TRACK_FRAME_BAD) {
            if (ev == JK_TRACK_FRAME_OK) ++s_raw.frames_ok;
            else ++s_raw.frames_bad;
#if CONFIG_DBMS_SNIFFER
            bms_sniffer_frame(s_tracker.type, s_tracker.counter, ev == JK_TRACK_FRAME_OK);
#endif
        }
        s_raw.data[s_raw_head] = data[i];
        s_raw.masked[s_raw_head] = masked[i];
        s_raw_head = (s_raw_head + 1) % BMS_RAW_CAPTURE;
        ++s_raw.total;
    }
    xSemaphoreGive(s_raw_lock);
}

static void read_available(size_t len)
{
    uint8_t chunk[RX_CHUNK];
    bool masked[RX_CHUNK];
    while (len > 0) {
        int n = uart_read_bytes(BMS_UART, chunk, len < sizeof(chunk) ? len : sizeof(chunk), pdMS_TO_TICKS(20));
        if (n <= 0) break;
        len -= n;
        int64_t now = esp_timer_get_time();
        s_link.bytes_rx += n;
        s_link.last_rx_us = now;
        raw_capture(chunk, masked, n);
#if CONFIG_DBMS_SNIFFER
        bms_sniffer_feed(chunk, masked, n, now);
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
            xSemaphoreTake(s_raw_lock, portMAX_DELAY);
            ++s_raw.line_errors;
            xSemaphoreGive(s_raw_lock);
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

    /* DI is wired but reserved: a plain GPIO at the UART idle level, no UART signal. */
    const gpio_num_t tx = CONFIG_DBMS_BMS_TX_GPIO;
    gpio_set_level(tx, 1);
    const gpio_config_t tx_cfg = {.pin_bit_mask = 1ULL << tx, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&tx_cfg), TAG, "TX gpio");
    ESP_RETURN_ON_ERROR(gpio_set_level(tx, 1), TAG, "TX level");

    s_raw_lock = xSemaphoreCreateMutex();
    if (!s_raw_lock) return ESP_ERR_NO_MEM;
    const uint32_t baud = load_baud();
    raw_reset(baud);

    const uart_config_t cfg = {
        .baud_rate = (int)baud,
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
    s_running = true;
    ESP_LOGI(TAG, "UART%d RX GPIO%d, %lu 8N1, listen only (DE and /RE on GPIO%d held low)" SNIFFER_NOTE,
             BMS_UART, CONFIG_DBMS_BMS_RX_GPIO, (unsigned long)baud, rts);
    return ESP_OK;
}

bool bms_transport_running(void)
{
    return s_running;
}

esp_err_t bms_transport_set_baud(uint32_t baud)
{
    if (!s_running) return ESP_ERR_INVALID_STATE;
    if (!baud_supported(baud)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_raw_lock, portMAX_DELAY);
    esp_err_t err = uart_set_baudrate(BMS_UART, baud);
    if (err == ESP_OK) {
        uart_flush_input(BMS_UART);
        raw_reset(baud);
    }
    xSemaphoreGive(s_raw_lock);
    ESP_RETURN_ON_ERROR(err, TAG, "set baud");
    save_baud(baud);
    ESP_LOGI(TAG, "baud rate %lu", (unsigned long)baud);
    return ESP_OK;
}

void bms_transport_raw_view(bms_raw_view_t *out)
{
    if (!s_running) {
        *out = (bms_raw_view_t){0};
        return;
    }
    xSemaphoreTake(s_raw_lock, portMAX_DELAY);
    *out = s_raw;
    /* Unroll the ring so that out->data[0] is the oldest captured byte. */
    uint16_t len = s_raw.total < BMS_RAW_CAPTURE ? (uint16_t)s_raw.total : BMS_RAW_CAPTURE;
    uint16_t start = (uint16_t)((s_raw_head + BMS_RAW_CAPTURE - len) % BMS_RAW_CAPTURE);
    for (uint16_t i = 0; i < len; ++i) {
        uint16_t j = (start + i) % BMS_RAW_CAPTURE;
        out->data[i] = s_raw.data[j];
        out->masked[i] = s_raw.masked[j];
    }
    out->len = len;
    xSemaphoreGive(s_raw_lock);
}
