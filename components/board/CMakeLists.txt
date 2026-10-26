idf_component_register(
    SRCS "board.c" "backlight.c" "ambient.c" "touch_cal.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd lvgl__lvgl espressif__esp_lvgl_port
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio esp_driver_ledc esp_adc esp_timer nvs_flash
                  esp_rom espressif__esp_lcd_ili9341 espressif__esp_lcd_touch
                  atanisoft__esp_lcd_touch_xpt2046
)
