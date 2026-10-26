idf_component_register(
    SRCS "board.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd lvgl__lvgl espressif__esp_lvgl_port
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio esp_driver_ledc esp_adc nvs_flash
                  espressif__esp_lcd_ili9341 espressif__esp_lcd_touch
                  atanisoft__esp_lcd_touch_xpt2046
)
