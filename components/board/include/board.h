#pragma once

#include "esp_err.h"
#include "lvgl.h"

/* CYD (ESP32-2432S028R) display bus: HSPI / SPI2 */
#define BOARD_LCD_SPI_HOST      SPI2_HOST
#define BOARD_LCD_PIN_SCLK      14
#define BOARD_LCD_PIN_MOSI      13
#define BOARD_LCD_PIN_MISO      12
#define BOARD_LCD_PIN_CS        15
#define BOARD_LCD_PIN_DC        2
#define BOARD_LCD_PIN_RST       (-1)
#define BOARD_LCD_PIN_BL        21

/* Touch bus: VSPI / SPI3, separate from the display */
#define BOARD_TOUCH_SPI_HOST    SPI3_HOST
#define BOARD_TOUCH_PIN_SCLK    25
#define BOARD_TOUCH_PIN_MOSI    32
#define BOARD_TOUCH_PIN_MISO    39
#define BOARD_TOUCH_PIN_CS      33
#define BOARD_TOUCH_PIN_IRQ     36

/* Photoresistor: GPIO34 = ADC1 channel 6 */
#define BOARD_LDR_PIN           34

#define BOARD_LCD_H_RES         320
#define BOARD_LCD_V_RES         240

/* Gamma curves selectable with the Gamma Set command (ILI9341 and ST7789: 0x01, 0x02, 0x04, 0x08) */
#define BOARD_LCD_GAMMA_CURVES  4

/**
 * Bring up SPI buses, LCD panel, touch, backlight and the LVGL port.
 * On success *out_disp is the LVGL display.
 */
esp_err_t board_init(lv_display_t **out_disp);

/** Select gamma curve 0..BOARD_LCD_GAMMA_CURVES-1. LVGL task only (shares the LCD bus). */
esp_err_t board_lcd_set_gamma_curve(int curve);
