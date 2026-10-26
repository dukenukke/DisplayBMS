#include "board.h"
#include "backlight.h"
#include "touch_cal.h"

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#if CONFIG_DBMS_LCD_ILI9341
#include "esp_lcd_ili9341.h"
#else
#include "esp_lcd_panel_st7789.h"
#endif

/* LCD: values verified on this board in the reference project (fungen). */
#define LCD_PIXEL_CLOCK_HZ      (CONFIG_DBMS_LCD_SPI_CLOCK_MHZ * 1000 * 1000)
#define LCD_CMD_BITS            8
#define LCD_PARAM_BITS          8
#define LCD_SPI_MODE            0
#define LCD_TRANS_QUEUE_DEPTH   10
#define LCD_BITS_PER_PIXEL      16
#define LCD_INVERT_COLORS       true
#define LCD_MIRROR_X            false
#define LCD_MIRROR_Y            true
#define LCD_SWAP_XY             false

/* LVGL draw buffers: 2 x 40 lines of RGB565 in internal DMA RAM (2 x 25.6 KB, no PSRAM). */
#define LCD_BUF_LINES           40
#define LCD_BUF_PIXELS          (BOARD_LCD_H_RES * LCD_BUF_LINES)

/* Touch */
#define TOUCH_PIXEL_CLOCK_HZ    (2 * 1000 * 1000)
#define TOUCH_TRANS_QUEUE_DEPTH 3
#define TOUCH_IRQ_ACTIVE_LEVEL  0       /* XPT2046 PENIRQ is active low */
/* LVGL polls the touch every ~33 ms; a longer gap between samples means a new press. */
#define TOUCH_NEW_PRESS_GAP_US  (150 * 1000)

/* LVGL task: core 1, so the BMS reader on core 0 never competes with rendering. */
#define LVGL_TASK_PRIORITY      4
#define LVGL_TASK_STACK         8192
#define LVGL_TASK_CORE          1
#define LVGL_TASK_MAX_SLEEP_MS  500
#define LVGL_TIMER_PERIOD_MS    5

static const char *TAG = "board";

static int64_t s_last_touch_us;
static bool s_swallow_press;

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase");
        err = nvs_flash_init();
    }
    return err;
}

/* Runs in the LVGL task on every touch sample while the panel is pressed. */
static void touch_process(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength,
                          uint8_t *point_num, uint8_t max_point_num)
{
    (void)tp;
    (void)strength;
    if (*point_num == 0) return;

    int64_t now = esp_timer_get_time();
    bool new_press = now - s_last_touch_us > TOUCH_NEW_PRESS_GAP_US;
    s_last_touch_us = now;
    bool was_dimmed = backlight_touch_activity();
    if (new_press) s_swallow_press = was_dimmed;
    if (s_swallow_press) {
        /* The press that wakes the screen is not delivered to LVGL. */
        *point_num = 0;
        return;
    }
    touch_cal_process(x, y, *point_num, max_point_num);
}

static esp_err_t init_lcd(esp_lcd_panel_io_handle_t *out_io, esp_lcd_panel_handle_t *out_panel)
{
    const spi_bus_config_t bus = {
        .sclk_io_num = BOARD_LCD_PIN_SCLK,
        .mosi_io_num = BOARD_LCD_PIN_MOSI,
        .miso_io_num = BOARD_LCD_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_BUF_PIXELS * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "LCD bus");

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = BOARD_LCD_PIN_DC,
        .cs_gpio_num = BOARD_LCD_PIN_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = LCD_SPI_MODE,
        .trans_queue_depth = LCD_TRANS_QUEUE_DEPTH,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST,
                                                 &io_cfg, out_io), TAG, "LCD io");

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
    };
#if CONFIG_DBMS_LCD_ILI9341
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(*out_io, &panel_cfg, out_panel), TAG, "ILI9341");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(*out_io, &panel_cfg, out_panel), TAG, "ST7789");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*out_panel), TAG, "LCD reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*out_panel), TAG, "LCD init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(*out_panel, LCD_INVERT_COLORS), TAG, "LCD invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*out_panel, true), TAG, "LCD on");
    return ESP_OK;
}

static esp_err_t init_touch(esp_lcd_touch_handle_t *out)
{
    const spi_bus_config_t bus = {
        .sclk_io_num = BOARD_TOUCH_PIN_SCLK,
        .mosi_io_num = BOARD_TOUCH_PIN_MOSI,
        .miso_io_num = BOARD_TOUCH_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_TOUCH_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "touch bus");

    esp_lcd_panel_io_handle_t io;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = -1,
        .cs_gpio_num = BOARD_TOUCH_PIN_CS,
        .pclk_hz = TOUCH_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = TOUCH_TRANS_QUEUE_DEPTH,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_TOUCH_SPI_HOST,
                                                 &io_cfg, &io), TAG, "touch io");

    const esp_lcd_touch_config_t cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = BOARD_TOUCH_PIN_IRQ,
        .levels = {.interrupt = TOUCH_IRQ_ACTIVE_LEVEL},
        /* The affine calibration covers swap and mirroring, so no driver flags. */
        .process_coordinates = touch_process,
    };
    return esp_lcd_touch_new_spi_xpt2046(io, &cfg, out);
}

esp_err_t board_init(lv_display_t **out_disp)
{
    *out_disp = NULL;
    ESP_RETURN_ON_ERROR(init_nvs(), TAG, "NVS");
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");

    esp_lcd_panel_io_handle_t lcd_io;
    esp_lcd_panel_handle_t panel;
    ESP_RETURN_ON_ERROR(init_lcd(&lcd_io, &panel), TAG, "LCD");

    const lvgl_port_cfg_t port_cfg = {
        .task_priority = LVGL_TASK_PRIORITY,
        .task_stack = LVGL_TASK_STACK,
        .task_affinity = LVGL_TASK_CORE,
        .task_max_sleep_ms = LVGL_TASK_MAX_SLEEP_MS,
        .task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT,
        .timer_period_ms = LVGL_TIMER_PERIOD_MS,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "LVGL port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = panel,
        .buffer_size = LCD_BUF_PIXELS,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = LCD_SWAP_XY,
            .mirror_x = LCD_MIRROR_X,
            .mirror_y = LCD_MIRROR_Y,
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(disp, ESP_FAIL, TAG, "LVGL display");

    esp_lcd_touch_handle_t touch;
    ESP_RETURN_ON_ERROR(init_touch(&touch), TAG, "touch");
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = touch,
    };
    ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&touch_cfg), ESP_FAIL, TAG, "LVGL touch");

    esp_err_t err = touch_cal_load();
    if (err != ESP_OK) ESP_LOGW(TAG, "no touch calibration (%s)", esp_err_to_name(err));

    if (lvgl_port_lock(0)) {
        backlight_start_service();
        lvgl_port_unlock();
    }
    *out_disp = disp;
    ESP_LOGI(TAG, "display %dx%d ready", BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return ESP_OK;
}
